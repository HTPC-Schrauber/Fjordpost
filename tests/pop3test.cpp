#include "core/accountmanager.h"
#include "mail/pop3/pop3account.h"
#include "storage/database.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace Fjordpost;

namespace {

// Kleiner POP3-Server: beantwortet die Befehle aus einem festen Postfach.
class FakePop3Server : public QObject
{
public:
    FakePop3Server()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_socket = m_server.nextPendingConnection();
            connect(m_socket, &QTcpSocket::readyRead, this, &FakePop3Server::onRead);
            m_socket->write("+OK ready\r\n");
        });
    }

    quint16 port() const { return m_server.serverPort(); }

    QMap<int, QPair<QByteArray, QByteArray>> mailbox; // Nummer -> UIDL, Inhalt
    QList<QByteArray> commands;
    bool rejectPassword = false;
    bool dropBeforeRetr = false;

private:
    void onRead()
    {
        m_buffer += m_socket->readAll();
        qsizetype nl;
        while ((nl = m_buffer.indexOf("\r\n")) >= 0) {
            const QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 2);
            commands << line;
            handle(line);
        }
    }

    void handle(const QByteArray &line)
    {
        const QByteArray command = line.split(' ').first();
        const int number = line.split(' ').value(1).toInt();
        if (command == "USER") {
            m_socket->write("+OK\r\n");
        } else if (command == "PASS") {
            m_socket->write(rejectPassword ? "-ERR invalid password\r\n" : "+OK logged in\r\n");
        } else if (command == "UIDL") {
            QByteArray out = "+OK\r\n";
            for (auto it = mailbox.cbegin(); it != mailbox.cend(); ++it)
                out += QByteArray::number(it.key()) + ' ' + it.value().first + "\r\n";
            m_socket->write(out + ".\r\n");
        } else if (command == "RETR") {
            if (dropBeforeRetr) {
                m_socket->disconnectFromHost();
                return;
            }
            // In kleinen Stücken senden, Zeilen können über TCP-Pakete verteilt ankommen.
            const QByteArray out = "+OK\r\n" + mailbox.value(number).second + ".\r\n";
            for (qsizetype i = 0; i < out.size(); i += 7) {
                const QByteArray chunk = out.mid(i, 7);
                QTimer::singleShot(int(i / 7), m_socket, [socket = m_socket, chunk] {
                    socket->write(chunk);
                });
            }
        } else if (command == "DELE") {
            mailbox.remove(number);
            m_socket->write("+OK\r\n");
        } else if (command == "QUIT") {
            m_socket->write("+OK bye\r\n");
            m_socket->disconnectFromHost();
        } else {
            m_socket->write("-ERR unknown\r\n");
        }
    }

    QTcpServer m_server;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
};

QByteArray mail(const QByteArray &id, const QByteArray &subject, const QByteArray &bodyLine = "Hallo")
{
    return "From: Anna <anna@example.com>\r\nTo: max@example.com\r\nSubject: " + subject + "\r\nMessage-ID: <" + id
        + "@example.com>\r\nDate: Thu, 29 Oct 2026 10:00:00 +0100\r\n\r\n" + bodyLine + "\r\n";
}

} // namespace

class Pop3Test : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("fjordpost");
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
        m_accounts.load();
    }

    void leavesMessagesOnServerAndFetchesOnlyNewOnes()
    {
        FakePop3Server server;
        server.mailbox[1] = {"uid-a", mail("a", "Erste")};
        server.mailbox[2] = {"uid-b", mail("b", "Zweite", "..Punkt am Anfang")};

        const Account account = makeAccount(server.port(), true);
        Pop3Account pop3(account);
        pop3.setCredentials(account, QStringLiteral("geheim"));
        pop3.ensureFolders();
        QSignalSpy arrived(&pop3, &Pop3Account::newMessages);

        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(arrived.size(), 1, 5000);
        QCOMPARE(arrived.first().at(1).toBool(), false); // erster Abruf: keine Benachrichtigung
        const auto ids = arrived.first().first().value<QList<qint64>>();
        QCOMPARE(ids.size(), 2);
        QTRY_VERIFY(!pop3.isSyncing());
        QVERIFY(!server.commands.contains("DELE 1"));

        const MessageLocation loc = MailStore::location(ids.at(1));
        QCOMPARE(loc.uid, 0);
        QVERIFY(loc.bodyFetched);
        QCOMPARE(loc.folderRole, QStringLiteral("inbox"));
        QVERIFY(!(loc.flags & MessageFlag::Seen));
        // Punkt-Verdopplung aufgehoben
        QVERIFY(bodyOf(ids.at(1)).startsWith(QLatin1String(".Punkt")));

        // Zweiter Abruf holt nur die neue Nachricht und meldet sie.
        server.commands.clear();
        server.mailbox[3] = {"uid-c", mail("c", "Dritte")};
        arrived.clear();
        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(arrived.size(), 1, 5000);
        QCOMPARE(arrived.first().at(1).toBool(), true);
        QCOMPARE(arrived.first().first().value<QList<qint64>>().size(), 1);
        QVERIFY(server.commands.contains("RETR 3"));
        QVERIFY(!server.commands.contains("RETR 1"));
    }

    void deletesFromServerWhenConfigured()
    {
        FakePop3Server server;
        server.mailbox[1] = {"uid-x", mail("x", "Weg damit")};
        const Account account = makeAccount(server.port(), false);
        Pop3Account pop3(account);
        pop3.setCredentials(account, QStringLiteral("geheim"));
        pop3.ensureFolders();
        QSignalSpy arrived(&pop3, &Pop3Account::newMessages);
        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(arrived.size(), 1, 5000);
        QTRY_VERIFY(server.mailbox.isEmpty());
        QVERIFY(server.commands.indexOf("DELE 1") > server.commands.indexOf("RETR 1"));
        QVERIFY(server.commands.contains("QUIT"));
    }

    void reportsLoginFailure()
    {
        FakePop3Server server;
        server.rejectPassword = true;
        const Account account = makeAccount(server.port(), true);
        Pop3Account pop3(account);
        pop3.setCredentials(account, QStringLiteral("falsch"));
        pop3.ensureFolders();
        QSignalSpy errors(&pop3, &Pop3Account::errorOccurred);
        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QVERIFY(errors.first().first().toString().contains(QLatin1String("invalid password")));
        QTRY_VERIFY(!pop3.isSyncing());
    }

    void interruptedFetchIsRetried()
    {
        FakePop3Server server;
        server.mailbox[1] = {"uid-r", mail("r", "Abbruch")};
        server.dropBeforeRetr = true;
        const Account account = makeAccount(server.port(), false);
        Pop3Account pop3(account);
        pop3.setCredentials(account, QStringLiteral("geheim"));
        pop3.ensureFolders();
        QSignalSpy errors(&pop3, &Pop3Account::errorOccurred);
        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 5000);
        QTRY_VERIFY(!pop3.isSyncing());
        QVERIFY(MailStore::pop3KnownUids(account.id).isEmpty());
        QCOMPARE(server.mailbox.size(), 1); // nichts verloren

        server.dropBeforeRetr = false;
        QSignalSpy arrived(&pop3, &Pop3Account::newMessages);
        pop3.sync();
        QTRY_COMPARE_WITH_TIMEOUT(arrived.size(), 1, 5000);
        QTRY_VERIFY(server.mailbox.isEmpty());
    }

private:
    Account makeAccount(quint16 port, bool leaveOnServer)
    {
        Account a;
        a.name = QStringLiteral("POP %1").arg(port);
        a.email = QStringLiteral("max@example.com");
        a.type = AccountType::Pop3;
        a.incomingHost = QStringLiteral("127.0.0.1");
        a.incomingPort = port;
        a.incomingSecurity = Security::None;
        a.leaveOnServer = leaveOnServer;
        return m_accounts.save(a);
    }

    QString bodyOf(qint64 id)
    {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT body_text FROM messages WHERE id = ?"));
        q.addBindValue(id);
        return q.exec() && q.next() ? q.value(0).toString() : QString();
    }

    QTemporaryDir m_dir;
    AccountManager m_accounts;
};

QTEST_GUILESS_MAIN(Pop3Test)
#include "pop3test.moc"

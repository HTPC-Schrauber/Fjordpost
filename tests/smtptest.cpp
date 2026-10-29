#include "mail/smtp/smtpsender.h"

#include <KLocalizedString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

using namespace MopMail;

namespace {

// Minimaler SMTP-Server nach Drehbuch: jede Zeile "C: ..." wird erwartet,
// jede Zeile "S: ..." gesendet. Antworten werden absichtlich in kleine
// TCP-Stücke zerlegt, wie es manche Server tun.
class ScriptedSmtpServer : public QObject
{
public:
    explicit ScriptedSmtpServer(const QStringList &script)
        : m_script(script)
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            m_socket = m_server.nextPendingConnection();
            connect(m_socket, &QTcpSocket::readyRead, this, &ScriptedSmtpServer::onRead);
            sendResponses();
        });
    }

    quint16 port() const { return m_server.serverPort(); }
    bool done() const { return m_pos >= m_script.size(); }
    QString mismatch() const { return m_mismatch; }
    QByteArray data() const { return m_data; }

private:
    void onRead()
    {
        m_buffer += m_socket->readAll();
        while (true) {
            if (m_inData) {
                const qsizetype end = m_buffer.indexOf("\r\n.\r\n");
                if (end < 0)
                    return;
                m_data = m_buffer.left(end + 2);
                m_buffer.remove(0, end + 5);
                m_inData = false;
                expect(QStringLiteral("<DATA>"));
                continue;
            }
            const qsizetype nl = m_buffer.indexOf("\r\n");
            if (nl < 0)
                return;
            const QString line = QString::fromUtf8(m_buffer.left(nl));
            m_buffer.remove(0, nl + 2);
            expect(line);
        }
    }

    void expect(const QString &line)
    {
        const QString expected = m_script.value(m_pos);
        if (!expected.startsWith(QLatin1String("C: ")) || expected.mid(3) != line) {
            if (m_mismatch.isEmpty())
                m_mismatch = QStringLiteral("erwartet '%1', bekommen '%2'").arg(expected, line);
            return;
        }
        ++m_pos;
        if (line == QLatin1String("DATA"))
            m_inData = true;
        sendResponses();
    }

    void sendResponses()
    {
        QByteArray out;
        while (m_pos < m_script.size() && m_script.at(m_pos).startsWith(QLatin1String("S: ")))
            out += m_script.at(m_pos++).mid(3).toUtf8() + "\r\n";
        // In Stücken senden: erst ein Byte, dann den Rest mit Verzögerung.
        for (qsizetype i = 0; i < out.size(); i += 5) {
            const QByteArray chunk = out.mid(i, 5);
            QTimer::singleShot(int(i / 5) * 2, m_socket, [socket = m_socket, chunk] {
                socket->write(chunk);
                socket->flush();
            });
        }
    }

    QStringList m_script;
    QTcpServer m_server;
    QTcpSocket *m_socket = nullptr;
    QByteArray m_buffer;
    QByteArray m_data;
    bool m_inData = false;
    qsizetype m_pos = 0;
    QString m_mismatch;
};

Account account(quint16 port)
{
    Account a;
    a.email = QStringLiteral("max@example.org");
    a.username = QStringLiteral("max");
    a.outgoingHost = QStringLiteral("127.0.0.1");
    a.outgoingPort = port;
    a.outgoingSecurity = Security::None;
    return a;
}

struct Result {
    bool done = false;
    bool ok = false;
    QString error;
};

} // namespace

class SmtpTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("mopmail");
    }

    void sendWithPlainAuth()
    {
        const QByteArray plain = QByteArray("\0max\0geheim", 11).toBase64();
        ScriptedSmtpServer server({
            QStringLiteral("S: 220 mx.example.org ESMTP ready"),
            QStringLiteral("C: EHLO [127.0.0.1]"),
            QStringLiteral("S: 250-mx.example.org"),
            QStringLiteral("S: 250-SIZE 10240000"),
            QStringLiteral("S: 250-AUTH LOGIN PLAIN"),
            QStringLiteral("S: 250 8BITMIME"),
            QStringLiteral("C: AUTH PLAIN ") + QString::fromLatin1(plain),
            QStringLiteral("S: 235 2.7.0 Authentication successful"),
            QStringLiteral("C: MAIL FROM:<max@example.org> SIZE=43"),
            QStringLiteral("S: 250 OK"),
            QStringLiteral("C: RCPT TO:<anna@example.org>"),
            QStringLiteral("S: 250 OK"),
            QStringLiteral("C: RCPT TO:<bob@example.org>"),
            QStringLiteral("S: 250 OK"),
            QStringLiteral("C: DATA"),
            QStringLiteral("S: 354 go ahead"),
            QStringLiteral("C: <DATA>"),
            QStringLiteral("S: 250 queued"),
            QStringLiteral("C: QUIT"),
            QStringLiteral("S: 221 bye"),
        });

        // LF-Zeilenenden und eine Zeile mit führendem Punkt
        const QByteArray message = "Subject: Test\n\nHallo\n.punkt am Anfang\nEnde\n";
        QCOMPARE(message.size(), 43);
        SmtpSender smtp;
        Result r;
        smtp.send(account(server.port()), QStringLiteral("geheim"), QStringLiteral("max@example.org"),
                  {QStringLiteral("anna@example.org"), QStringLiteral("bob@example.org")}, message, [&](bool ok, const QString &error) {
                      r = {true, ok, error};
                  });
        QTRY_VERIFY_WITH_TIMEOUT(r.done, 5000);
        QVERIFY2(r.ok, qPrintable(r.error));
        QTRY_VERIFY2_WITH_TIMEOUT(server.done(), qPrintable(server.mismatch()), 5000);
        QCOMPARE(server.data(), QByteArray("Subject: Test\r\n\r\nHallo\r\n..punkt am Anfang\r\nEnde\r\n"));
    }

    void authLoginFallback()
    {
        ScriptedSmtpServer server({
            QStringLiteral("S: 220 old.example.org"),
            QStringLiteral("C: EHLO [127.0.0.1]"),
            QStringLiteral("S: 250-old.example.org"),
            QStringLiteral("S: 250 AUTH=LOGIN"),
            QStringLiteral("C: AUTH LOGIN"),
            QStringLiteral("S: 334 VXNlcm5hbWU6"),
            QStringLiteral("C: ") + QString::fromLatin1(QByteArray("max").toBase64()),
            QStringLiteral("S: 334 UGFzc3dvcmQ6"),
            QStringLiteral("C: ") + QString::fromLatin1(QByteArray("geheim").toBase64()),
            QStringLiteral("S: 235 ok"),
            QStringLiteral("C: QUIT"),
            QStringLiteral("S: 221 bye"),
        });
        SmtpSender smtp;
        Result r;
        smtp.testLogin(account(server.port()), QStringLiteral("geheim"), [&](bool ok, const QString &error) {
            r = {true, ok, error};
        });
        QTRY_VERIFY_WITH_TIMEOUT(r.done, 5000);
        QVERIFY2(r.ok, qPrintable(r.error));
        QTRY_VERIFY2_WITH_TIMEOUT(server.done(), qPrintable(server.mismatch()), 5000);
    }

    void wrongPassword()
    {
        ScriptedSmtpServer server({
            QStringLiteral("S: 220 mx.example.org"),
            QStringLiteral("C: EHLO [127.0.0.1]"),
            QStringLiteral("S: 250-mx.example.org"),
            QStringLiteral("S: 250 AUTH PLAIN"),
            QStringLiteral("C: AUTH PLAIN ") + QString::fromLatin1(QByteArray("\0max\0falsch", 11).toBase64()),
            QStringLiteral("S: 535 5.7.8 Authentication credentials invalid"),
        });
        SmtpSender smtp;
        Result r;
        smtp.testLogin(account(server.port()), QStringLiteral("falsch"), [&](bool ok, const QString &error) {
            r = {true, ok, error};
        });
        QTRY_VERIFY_WITH_TIMEOUT(r.done, 5000);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("credentials invalid")), qPrintable(r.error));
    }

    void rejectedRecipient()
    {
        ScriptedSmtpServer server({
            QStringLiteral("S: 220 mx.example.org"),
            QStringLiteral("C: EHLO [127.0.0.1]"),
            QStringLiteral("S: 250 mx.example.org"),
            QStringLiteral("C: MAIL FROM:<max@example.org>"),
            QStringLiteral("S: 250 OK"),
            QStringLiteral("C: RCPT TO:<nobody@example.org>"),
            QStringLiteral("S: 550 5.1.1 User unknown"),
        });
        SmtpSender smtp;
        Result r;
        // Ohne AUTH-Angebot des Servers wird ohne Anmeldung gesendet.
        smtp.send(account(server.port()), QStringLiteral("geheim"), QStringLiteral("max@example.org"), {QStringLiteral("nobody@example.org")}, "x\n",
                  [&](bool ok, const QString &error) {
                      r = {true, ok, error};
                  });
        QTRY_VERIFY_WITH_TIMEOUT(r.done, 5000);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("nobody@example.org")) && r.error.contains(QStringLiteral("User unknown")), qPrintable(r.error));
    }

    void startTlsRequiredButMissing()
    {
        ScriptedSmtpServer server({
            QStringLiteral("S: 220 mx.example.org"),
            QStringLiteral("C: EHLO [127.0.0.1]"),
            QStringLiteral("S: 250-mx.example.org"),
            QStringLiteral("S: 250 AUTH PLAIN"),
        });
        Account a = account(server.port());
        a.outgoingSecurity = Security::StartTls;
        SmtpSender smtp;
        Result r;
        smtp.testLogin(a, QStringLiteral("geheim"), [&](bool ok, const QString &error) {
            r = {true, ok, error};
        });
        QTRY_VERIFY_WITH_TIMEOUT(r.done, 5000);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("STARTTLS")));
    }
};

QTEST_GUILESS_MAIN(SmtpTest)
#include "smtptest.moc"

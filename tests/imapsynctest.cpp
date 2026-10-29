#include "core/accountmanager.h"
#include "mail/imap/imapaccount.h"
#include "storage/database.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <kimaptest/fakeserver.h>

#include <KLocalizedString>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

namespace {

QByteArray rawMessage(const QByteArray &subject, const QByteArray &from, const QByteArray &body)
{
    return "From: " + from + "\r\nTo: user@example.org\r\nSubject: " + subject
        + "\r\nDate: Wed, 28 Oct 2026 10:00:00 +0100\r\nMessage-ID: <" + subject.toLower().replace(' ', '-')
        + "@example.org>\r\nContent-Type: text/plain; charset=utf-8\r\n\r\n" + body + "\r\n";
}

QByteArray literal(const QByteArray &data)
{
    return "{" + QByteArray::number(data.size()) + "}\r\n" + data;
}

QStringList subjects(qint64 folderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT subject FROM messages WHERE folder_id = ? ORDER BY uid"));
    q.addBindValue(folderId);
    q.exec();
    QStringList result;
    while (q.next())
        result << q.value(0).toString();
    return result;
}

} // namespace

class ImapSyncTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("fjordpost");
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
    }

    void fullSyncAndIncrementalUpdate()
    {
        AccountManager accounts;
        Account a;
        a.name = QStringLiteral("Test");
        a.email = QStringLiteral("user@example.org");
        a.username = QStringLiteral("user");
        a.incomingHost = QStringLiteral("127.0.0.1");
        a.incomingPort = 5989;
        a.incomingSecurity = Security::None;
        a = accounts.save(a);

        const QByteArray m1 = rawMessage("Erste Nachricht", "Anna <anna@example.org>", "Hallo eins");
        const QByteArray m2 = rawMessage("Zweite Nachricht", "Bob <bob@example.org>", "Hallo zwei");
        const QByteArray m3 = rawMessage("Dritte Nachricht", "Carl <carl@example.org>", "Hallo drei");

        QList<QByteArray> scenario;
        scenario << FakeServer::greeting()
                 // Anmeldung
                 << "C: A000001 LOGIN \"user\" \"secret\""
                 << "S: A000001 OK logged in"
                 << "C: A000002 CAPABILITY"
                 << "S: * CAPABILITY IMAP4rev1 IDLE MOVE"
                 << "S: A000002 OK done"
                 // Erster Abgleich
                 << "C: A000003 LIST \"\" *"
                 << "S: * LIST (\\HasNoChildren) \"/\" INBOX"
                 << "S: * LIST (\\HasNoChildren \\Sent) \"/\" \"Gesendete Objekte\""
                 << "S: * LIST (\\Noselect \\HasChildren) \"/\" Archiv"
                 << "S: A000003 OK done"
                 << "C: A000004 SELECT \"INBOX\""
                 << "S: * 2 EXISTS"
                 << "S: * OK [UIDVALIDITY 42] ok"
                 << "S: * OK [UIDNEXT 3] ok"
                 << "S: A000004 OK [READ-WRITE] done"
                 << "C: A000005 FETCH 1:* (RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER] FLAGS UID)"
                 << "S: * 1 FETCH (UID 1 RFC822.SIZE " + QByteArray::number(m1.size()) + " FLAGS (\\Seen) BODY[HEADER] " + literal(m1) + ")"
                 << "S: * 2 FETCH (UID 2 RFC822.SIZE " + QByteArray::number(m2.size()) + " FLAGS () BODY[HEADER] " + literal(m2) + ")"
                 << "S: A000005 OK done"
                 << "C: A000006 UID FETCH 1:2 (RFC822.SIZE INTERNALDATE BODY.PEEK[] FLAGS UID)"
                 << "S: * 1 FETCH (UID 1 FLAGS (\\Seen) BODY[] " + literal(m1) + ")"
                 << "S: * 2 FETCH (UID 2 FLAGS () BODY[] " + literal(m2) + ")"
                 << "S: A000006 OK done"
                 << "C: A000007 SELECT \"Gesendete Objekte\""
                 << "S: * 0 EXISTS"
                 << "S: * OK [UIDVALIDITY 7] ok"
                 << "S: * OK [UIDNEXT 1] ok"
                 << "S: A000007 OK [READ-WRITE] done"
                 // Zweiter Abgleich: UID 1 gelöscht, UID 2 jetzt gelesen + markiert, UID 3 neu
                 << "C: A000008 LIST \"\" *"
                 << "S: * LIST (\\HasNoChildren) \"/\" INBOX"
                 << "S: * LIST (\\HasNoChildren \\Sent) \"/\" \"Gesendete Objekte\""
                 << "S: A000008 OK done"
                 << "C: A000009 SELECT \"INBOX\""
                 << "S: * 2 EXISTS"
                 << "S: * OK [UIDVALIDITY 42] ok"
                 << "S: * OK [UIDNEXT 4] ok"
                 << "S: A000009 OK [READ-WRITE] done"
                 << "C: A000010 UID FETCH 3:* (RFC822.SIZE INTERNALDATE BODY.PEEK[HEADER] FLAGS UID)"
                 << "S: * 2 FETCH (UID 3 RFC822.SIZE " + QByteArray::number(m3.size()) + " FLAGS () BODY[HEADER] " + literal(m3) + ")"
                 << "S: A000010 OK done"
                 << "C: A000011 UID FETCH 1:* (FLAGS UID)"
                 << "S: * 1 FETCH (UID 2 FLAGS (\\Seen \\Flagged))"
                 << "S: * 2 FETCH (UID 3 FLAGS ())"
                 << "S: A000011 OK done"
                 << "C: A000012 UID FETCH 3 (RFC822.SIZE INTERNALDATE BODY.PEEK[] FLAGS UID)"
                 << "S: * 2 FETCH (UID 3 FLAGS () BODY[] " + literal(m3) + ")"
                 << "S: A000012 OK done"
                 << "C: A000013 SELECT \"Gesendete Objekte\""
                 << "S: * 0 EXISTS"
                 << "S: * OK [UIDVALIDITY 7] ok"
                 << "S: * OK [UIDNEXT 1] ok"
                 << "S: A000013 OK [READ-WRITE] done"
                 // Abmelden beim Beenden
                 << "C: A000014 LOGOUT"
                 << "S: A000014 OK bye";

        FakeServer server;
        server.setScenario(scenario);
        server.startAndWait();

        ImapAccount imap(a);
        imap.setCredentials(a, QStringLiteral("secret"));
        QSignalSpy syncing(&imap, &ImapAccount::syncingChanged);
        QSignalSpy newMessages(&imap, &ImapAccount::newMessages);
        QSignalSpy errors(&imap, &ImapAccount::errorOccurred);

        // --- erster Abgleich
        imap.sync();
        QTRY_COMPARE_WITH_TIMEOUT(syncing.size(), 2, 10000);
        QVERIFY(errors.isEmpty());

        const QList<FolderState> folders = MailStore::folders(a.id);
        QCOMPARE(folders.size(), 2);
        QCOMPARE(folders.at(0).role, QStringLiteral("inbox"));
        QCOMPARE(folders.at(1).role, QStringLiteral("sent"));
        QCOMPARE(folders.at(0).uidValidity, 42);
        QCOMPARE(folders.at(0).uidNext, 3);

        const qint64 inbox = folders.at(0).id;
        QCOMPARE(subjects(inbox), QStringList({QStringLiteral("Erste Nachricht"), QStringLiteral("Zweite Nachricht")}));
        auto local = MailStore::localMessages(inbox);
        QCOMPARE(local.value(1).flags, int(MessageFlag::Seen));
        QCOMPARE(local.value(2).flags, 0);
        const MessageLocation loc2 = MailStore::location(local.value(2).id);
        QVERIFY(loc2.bodyFetched);
        QVERIFY(!loc2.blobPath.isEmpty());
        QCOMPARE(newMessages.size(), 0); // Erstabgleich meldet nichts

        // --- zweiter Abgleich
        imap.sync();
        QTRY_COMPARE_WITH_TIMEOUT(syncing.size(), 4, 10000);
        QVERIFY(errors.isEmpty());

        QCOMPARE(subjects(inbox), QStringList({QStringLiteral("Zweite Nachricht"), QStringLiteral("Dritte Nachricht")}));
        local = MailStore::localMessages(inbox);
        QCOMPARE(local.value(2).flags, int(MessageFlag::Seen | MessageFlag::Flagged));
        QCOMPARE(MailStore::folder(inbox).uidNext, 4);
        QCOMPARE(newMessages.size(), 1);
        QCOMPARE(newMessages.first().first().value<QList<qint64>>(), QList<qint64>{local.value(3).id});
        QVERIFY(MailStore::location(local.value(3).id).bodyFetched);

        imap.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(server.isAllScenarioDone(), 5000);
        server.quit();
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ImapSyncTest)
#include "imapsynctest.moc"

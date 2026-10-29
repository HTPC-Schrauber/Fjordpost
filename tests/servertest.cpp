// Integrationstest gegen einen echten Mailserver (GreenMail aus dev/docker-compose.yml).
// GreenMail akzeptiert jedes Passwort; Anmeldefehler testen smtptest/imapsynctest.
// Läuft nur, wenn FJORDPOST_TEST_SERVER gesetzt ist, z. B.:
//   FJORDPOST_TEST_SERVER=127.0.0.1 ctest --test-dir build -R servertest --output-on-failure

#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "mail/mailservice.h"
#include "mail/imap/imapaccount.h"
#include "mail/mime/messagebuilder.h"
#include "mail/mime/messageparser.h"
#include "mail/smtp/smtpsender.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/labelstore.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QFile>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace Fjordpost;

namespace {

const QString password = QStringLiteral("geheim");

// Nachrichten-ID (lokal) im Ordner mit diesem Betreff, 0 wenn nicht vorhanden.
qint64 messageWithSubject(qint64 folderId, const QString &subject)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id FROM messages WHERE folder_id = ? AND subject = ?"));
    q.addBindValue(folderId);
    q.addBindValue(subject);
    q.exec();
    return q.next() ? q.value(0).toLongLong() : 0;
}

// GreenMail akzeptiert jedes Passwort; der echte Schlüsselbund wird nicht gebraucht.
class FakeSecretStore : public SecretStore
{
public:
    void read(const QString &, ReadCallback callback) override
    {
        callback(true, password);
    }
};

qint64 outboxCount()
{
    QSqlQuery q(Database::connection());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM outbox"));
    return q.next() ? q.value(0).toLongLong() : -1;
}

} // namespace

class ServerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        m_host = qEnvironmentVariable("FJORDPOST_TEST_SERVER");
        if (m_host.isEmpty())
            QSKIP("FJORDPOST_TEST_SERVER nicht gesetzt (Testserver: dev/docker-compose.yml)");

        KLocalizedString::setApplicationDomain("fjordpost");
        QVERIFY(m_dir.isValid());
        qputenv("XDG_DATA_HOME", m_dir.filePath(QStringLiteral("data")).toUtf8());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));

        m_max = makeAccount(QStringLiteral("Max"), QStringLiteral("max@fjordpost.test"));
        m_anna = makeAccount(QStringLiteral("Anna"), QStringLiteral("anna@fjordpost.test"));
        // Zweite, unabhängige Sicht auf Annas Postfach zum Prüfen des Serverzustands.
        m_annaCheck = makeAccount(QStringLiteral("Anna (Kontrolle)"), QStringLiteral("anna@fjordpost.test"));
        m_tag = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    }

    void sendAndReceive()
    {
        const QString subject = QStringLiteral("Grüße %1").arg(m_tag);
        QVERIFY(send(subject, QStringLiteral("Hallo Anna, schöne Grüße!\nZweite Zeile.")));

        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));

        const qint64 inbox = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Inbox));
        QVERIFY(inbox > 0);
        // Zustellung bei GreenMail ist asynchron: notfalls erneut abgleichen.
        qint64 id = messageWithSubject(inbox, subject);
        for (int i = 0; i < 5 && id == 0; ++i) {
            QTest::qWait(500);
            QVERIFY(syncAndWait(imap));
            id = messageWithSubject(inbox, subject);
        }
        QVERIFY2(id > 0, "Nachricht nicht im Posteingang angekommen");

        // Vorabgeladener Inhalt (Prefetch des Posteingangs)
        const MessageLocation loc = MailStore::location(id);
        QVERIFY(loc.bodyFetched);
        QFile blob(BlobStore::absolutePath(loc.blobPath));
        QVERIFY(blob.open(QIODevice::ReadOnly));
        const ParsedMessage parsed = MessageParser::parse(blob.readAll());
        QCOMPARE(parsed.subject, subject);
        QCOMPARE(parsed.fromEmail, QStringLiteral("max@fjordpost.test"));
        QVERIFY2(parsed.plainText.contains(QStringLiteral("schöne Grüße")), qPrintable(parsed.plainText));
        QVERIFY(!(loc.flags & MessageFlag::Seen));
        imap.shutdown();
    }

    void idleReportsNewMail()
    {
        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));
        QSignalSpy newMessages(&imap, &ImapAccount::newMessages);
        imap.startIdle();
        QTest::qWait(1500); // IDLE-Verbindung aufbauen lassen

        const QString subject = QStringLiteral("IDLE %1").arg(m_tag);
        QVERIFY(send(subject, QStringLiteral("Push!")));
        QTRY_VERIFY_WITH_TIMEOUT(!newMessages.isEmpty(), 20000);

        const qint64 inbox = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Inbox));
        const qint64 id = messageWithSubject(inbox, subject);
        QVERIFY(id > 0);
        QVERIFY(newMessages.first().first().value<QList<qint64>>().contains(id));
        imap.shutdown();
    }

    void flagsReachServer()
    {
        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));
        const qint64 inbox = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Inbox));
        const qint64 id = messageWithSubject(inbox, QStringLiteral("Grüße %1").arg(m_tag));
        QVERIFY(id > 0);

        const MessageLocation loc = MailStore::location(id);
        MailStore::updateFlags(id, loc.flags | MessageFlag::Seen | MessageFlag::Flagged);
        MailStore::enqueueOp(m_anna.id, QStringLiteral("flags"), loc.folderPath, loc.uid, QStringLiteral("+\\Seen"));
        MailStore::enqueueOp(m_anna.id, QStringLiteral("flags"), loc.folderPath, loc.uid, QStringLiteral("+\\Flagged"));
        imap.flushPendingOps();
        QTRY_VERIFY_WITH_TIMEOUT(MailStore::pendingOps(m_anna.id).isEmpty(), 15000);
        imap.shutdown();

        const qint64 checkId = checkMessage(FolderRole::Inbox, QStringLiteral("Grüße %1").arg(m_tag));
        QVERIFY(checkId > 0);
        const int flags = MailStore::location(checkId).flags;
        QVERIFY(flags & MessageFlag::Seen);
        QVERIFY(flags & MessageFlag::Flagged);
    }

    void moveCreatesMissingFolder()
    {
        // GreenMail kennt anfangs nur INBOX: der Archivordner muss angelegt werden.
        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));
        const QString subject = QStringLiteral("Grüße %1").arg(m_tag);
        const qint64 inbox = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Inbox));
        const qint64 id = messageWithSubject(inbox, subject);
        QVERIFY(id > 0);

        const MessageLocation loc = MailStore::location(id);
        const qint64 archive = MailStore::ensureFolderForRole(m_anna.id, QString::fromLatin1(FolderRole::Archive));
        QVERIFY(archive > 0);
        MailStore::moveLocally(id, archive);
        MailStore::enqueueOp(m_anna.id, QStringLiteral("move"), loc.folderPath, loc.uid, MailStore::folder(archive).path);
        imap.flushPendingOps();
        QTRY_VERIFY_WITH_TIMEOUT(MailStore::pendingOps(m_anna.id).isEmpty(), 15000);

        // Lokaler Zustand nach erneutem Abgleich: Nachricht mit Server-UID im Archiv
        QVERIFY(syncAndWait(imap));
        QCOMPARE(messageWithSubject(inbox, subject), 0);
        const qint64 archived = messageWithSubject(archive, subject);
        QVERIFY(archived > 0);
        QVERIFY(MailStore::location(archived).uid > 0);
        QVERIFY(MailStore::location(archived).flags & MessageFlag::Flagged);
        imap.shutdown();

        QCOMPARE(checkMessage(FolderRole::Inbox, subject), 0);
        QVERIFY(checkMessage(FolderRole::Archive, subject) > 0);
    }

    void expungeDeletesOnServer()
    {
        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));
        const QString subject = QStringLiteral("Grüße %1").arg(m_tag);
        const qint64 archive = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Archive));
        const qint64 id = messageWithSubject(archive, subject);
        QVERIFY(id > 0);

        const MessageLocation loc = MailStore::location(id);
        MailStore::enqueueOp(m_anna.id, QStringLiteral("expunge"), loc.folderPath, loc.uid);
        MailStore::removeMessages({id});
        imap.flushPendingOps();
        QTRY_VERIFY_WITH_TIMEOUT(MailStore::pendingOps(m_anna.id).isEmpty(), 15000);
        imap.shutdown();

        QCOMPARE(checkMessage(FolderRole::Archive, subject), 0);
    }

    void appendCreatesSentFolder()
    {
        const QString subject = QStringLiteral("Gesendet %1").arg(m_tag);
        OutgoingMessage m;
        m.fromName = QStringLiteral("Anna");
        m.fromEmail = m_anna.email;
        m.to = {QStringLiteral("max@fjordpost.test")};
        m.subject = subject;
        m.plainText = QStringLiteral("Kopie für Gesendet");
        const QByteArray raw = MessageBuilder::build(m);
        QVERIFY(!raw.isEmpty());

        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QSignalSpy errors(&imap, &ImapAccount::errorOccurred);
        QSignalSpy updated(&imap, &ImapAccount::folderUpdated);
        QVERIFY(syncAndWait(imap));
        imap.append(QString::fromLatin1(FolderRole::Sent), raw, {"\\Seen"});
        const qint64 sent = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Sent));
        QVERIFY(sent > 0);
        QTRY_VERIFY_WITH_TIMEOUT(messageWithSubject(sent, subject) > 0, 15000);
        QVERIFY(errors.isEmpty());
        imap.shutdown();

        const qint64 checkId = checkMessage(FolderRole::Sent, subject);
        QVERIFY(checkId > 0);
        QVERIFY(MailStore::location(checkId).flags & MessageFlag::Seen);
    }

    // Label als IMAP-Schlüsselwort: setzen, beim zweiten Konto ankommen lassen, entfernen
    void labelsTravelAsKeywords()
    {
        const QString subject = QStringLiteral("Label %1").arg(m_tag);
        QVERIFY(send(subject, QStringLiteral("Mit Label")));
        const qint64 labelId = LabelStore::create(QStringLiteral("Projekt %1").arg(m_tag), QStringLiteral("#6366f1"));
        QVERIFY(labelId > 0);
        const QString keyword = LabelStore::label(labelId).keyword;

        ImapAccount imap(m_anna);
        imap.setCredentials(m_anna, password);
        QVERIFY(syncAndWait(imap));
        const qint64 inbox = MailStore::folderIdForRole(m_anna.id, QString::fromLatin1(FolderRole::Inbox));
        // Die Zustellung kann einen Moment dauern: bis dahin erneut abgleichen
        const auto arrived = [&] {
            return syncAndWait(imap) && messageWithSubject(inbox, subject) > 0;
        };
        QTRY_VERIFY_WITH_TIMEOUT(arrived(), 15000);
        if (!MailStore::folder(inbox).keywords)
            QSKIP("Server speichert keine eigenen Schlüsselwörter (PERMANENTFLAGS ohne \\*)");

        const qint64 id = messageWithSubject(inbox, subject);
        const MessageLocation loc = MailStore::location(id);
        QVERIFY(LabelStore::setLabel(id, labelId, true));
        MailStore::enqueueOp(m_anna.id, QStringLiteral("flags"), loc.folderPath, loc.uid, QLatin1Char('+') + keyword);
        imap.flushPendingOps();
        QTRY_VERIFY_WITH_TIMEOUT(MailStore::pendingOps(m_anna.id).isEmpty(), 15000);

        // Das Kontrollkonto sieht das Label nach seinem eigenen Abgleich
        const qint64 checkId = checkMessage(FolderRole::Inbox, subject);
        QVERIFY(checkId > 0);
        QVERIFY(LabelStore::labelsOf(checkId).contains(labelId));

        // Entfernen kommt ebenfalls an
        QVERIFY(LabelStore::setLabel(id, labelId, false));
        MailStore::enqueueOp(m_anna.id, QStringLiteral("flags"), loc.folderPath, loc.uid, QLatin1Char('-') + keyword);
        imap.flushPendingOps();
        QTRY_VERIFY_WITH_TIMEOUT(MailStore::pendingOps(m_anna.id).isEmpty(), 15000);
        imap.shutdown();
        QVERIFY(!LabelStore::labelsOf(checkMessage(FolderRole::Inbox, subject)).contains(labelId));
    }

    // "Senden rückgängig": Rücknahme vor Ablauf und sofortiger Versand beim Beenden
    void delayedSendCancelAndFlush()
    {
        const QString cancelled = QStringLiteral("Zurückgenommen %1").arg(m_tag);
        const QString flushed = QStringLiteral("Beim Beenden %1").arg(m_tag);
        {
            AccountManager accounts;
            accounts.load();
            FakeSecretStore secrets;
            MailService mail(&accounts, &secrets);
            mail.start();

            qint64 first = 0, second = 0;
            QCOMPARE(mail.queueMessage(m_max.id, outgoing(cancelled), 0, {}, 60, &first), QString());
            QCOMPARE(mail.queueMessage(m_max.id, outgoing(flushed), 0, {}, 60, &second), QString());
            QVERIFY(first > 0 && second > 0);
            QCOMPARE(outboxCount(), 2);

            QVERIFY(mail.cancelQueued(first));
            QVERIFY(!mail.cancelQueued(first));
            QCOMPARE(outboxCount(), 1);

            QVERIFY(mail.sendDelayedNow());
            QTRY_VERIFY_WITH_TIMEOUT(!mail.hasPendingWork(), 30000);
            QCOMPARE(outboxCount(), 0);
            QVERIFY(!mail.cancelQueued(second));
        }

        QVERIFY(checkMessage(FolderRole::Inbox, flushed) > 0);
        QCOMPARE(checkMessage(FolderRole::Inbox, cancelled), 0);

        // Kopie in Max' "Gesendet" auf dem Server
        ImapAccount max(m_max);
        max.setCredentials(m_max, password);
        QVERIFY(syncAndWait(max));
        max.shutdown();
        const qint64 sent = MailStore::folderIdForRole(m_max.id, QString::fromLatin1(FolderRole::Sent));
        QVERIFY(sent > 0);
        QVERIFY(messageWithSubject(sent, flushed) > 0);
    }

private:
    OutgoingMessage outgoing(const QString &subject) const
    {
        OutgoingMessage m;
        m.fromName = QStringLiteral("Max Mustermann");
        m.fromEmail = m_max.email;
        m.to = {QStringLiteral("Anna <anna@fjordpost.test>")};
        m.subject = subject;
        m.plainText = QStringLiteral("Verzögert gesendet");
        return m;
    }

    Account makeAccount(const QString &name, const QString &email)
    {
        AccountManager accounts;
        Account a;
        a.name = name;
        a.displayName = name;
        a.email = email;
        a.username = email;
        a.incomingHost = m_host;
        a.incomingPort = 3143;
        a.incomingSecurity = Security::None;
        a.outgoingHost = m_host;
        a.outgoingPort = 3025;
        a.outgoingSecurity = Security::None;
        return accounts.save(a);
    }

    bool send(const QString &subject, const QString &text)
    {
        OutgoingMessage m;
        m.fromName = QStringLiteral("Max Mustermann");
        m.fromEmail = m_max.email;
        m.to = {QStringLiteral("Anna <anna@fjordpost.test>")};
        m.subject = subject;
        m.plainText = text;
        const QByteArray raw = MessageBuilder::build(m);

        SmtpSender smtp;
        bool done = false, ok = false;
        QString error;
        smtp.send(m_max, password, m_max.email, MessageBuilder::envelopeRecipients(m), raw, [&](bool success, const QString &e) {
            ok = success;
            error = e;
            done = true;
        });
        if (!QTest::qWaitFor([&] { return done; }, 15000))
            qWarning() << "SMTP: Zeitüberschreitung";
        if (!ok)
            qWarning() << "SMTP-Fehler:" << error;
        return ok;
    }

    static bool syncAndWait(ImapAccount &imap)
    {
        QSignalSpy errors(&imap, &ImapAccount::errorOccurred);
        imap.sync();
        const bool finished = QTest::qWaitFor([&] { return !imap.isSyncing(); }, 20000);
        if (!errors.isEmpty())
            qWarning() << "IMAP-Fehler:" << errors.first().first().toString();
        return finished && errors.isEmpty();
    }

    // Gleicht das Kontrollkonto frisch ab und sucht die Nachricht im Ordner der Rolle.
    qint64 checkMessage(const char *role, const QString &subject)
    {
        ImapAccount check(m_annaCheck);
        check.setCredentials(m_annaCheck, password);
        if (!syncAndWait(check))
            return -1;
        check.shutdown();
        const qint64 folder = MailStore::folderIdForRole(m_annaCheck.id, QString::fromLatin1(role));
        return folder > 0 ? messageWithSubject(folder, subject) : 0;
    }

    QTemporaryDir m_dir;
    QString m_host;
    QString m_tag;
    Account m_max;
    Account m_anna;
    Account m_annaCheck;
};

QTEST_GUILESS_MAIN(ServerTest)
#include "servertest.moc"

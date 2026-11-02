#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "integration/accountsetup.h"
#include "integration/appcontroller.h"
#include "mail/filterengine.h"
#include "mail/mailservice.h"
#include "mail/mailtransfer.h"
#include "mail/mime/mbox.h"
#include "models/messagelistmodel.h"
#include "storage/database.h"
#include "storage/filterstore.h"
#include "storage/labelstore.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QBuffer>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace Fjordpost;

namespace {

QByteArray rawMail(const QByteArray &id, const QByteArray &subject, const QByteArray &from = "Anna <anna@example.com>", const QByteArray &body = "Hallo\n")
{
    return "From: " + from + "\nTo: max@example.com\nSubject: " + subject + "\nMessage-ID: <" + id + "@example.com>\nDate: Thu, 29 Oct 2026 10:00:00 +0100\n\n" + body;
}

} // namespace

// Snooze, Filterregeln sowie Import und Export
class M2FeaturesTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("fjordpost");
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
        m_accounts.load();
        Account a;
        a.name = QStringLiteral("Lokal");
        a.email = QStringLiteral("max@example.com");
        a.type = AccountType::Pop3;
        m_account = m_accounts.save(a).id;
        m_inbox = MailStore::upsertFolder(m_account, QStringLiteral("INBOX"), QStringLiteral("Inbox"), QStringLiteral("inbox"), QStringLiteral("/"), 0);
        m_archive = MailStore::upsertFolder(m_account, QStringLiteral("Archive"), QStringLiteral("Archive"), QStringLiteral("archive"), QStringLiteral("/"), 3);
        m_custom = MailStore::upsertFolder(m_account, QStringLiteral("Rechnungen"), QStringLiteral("Rechnungen"), QString(), QStringLiteral("/"), 10);
    }

    void init()
    {
        QSqlQuery q(Database::connection());
        q.exec(QStringLiteral("DELETE FROM messages"));
        q.exec(QStringLiteral("DELETE FROM filter_rules"));
    }

    // ---------------------------------------------------------------- Snooze

    void snoozedMessagesLeaveTheInbox()
    {
        const qint64 keep = add("keep", "Bleibt");
        const qint64 snoozed = add("snooze", "Später");
        MailStore::setSnooze({snoozed}, QDateTime::currentSecsSinceEpoch() + 3600);

        MessageListModel inbox;
        inbox.setFilter({});
        QCOMPARE(inbox.rowCount(), 1);
        QCOMPARE(inbox.idAt(0), keep);

        MessageListModel::Filter f;
        f.role = QString::fromLatin1(FolderRole::Snoozed);
        MessageListModel list;
        list.setFilter(f);
        QCOMPARE(list.rowCount(), 1);
        QCOMPARE(list.idAt(0), snoozed);
        QVERIFY(!list.index(0).data(MessageListModel::SnoozedUntilRole).toString().isEmpty());

        // Über die Suche bleibt sie auffindbar.
        MessageListModel::Filter s;
        s.search = QStringLiteral("Später");
        MessageListModel search;
        search.setFilter(s);
        QCOMPARE(search.rowCount(), 1);
        QCOMPARE(MailStore::snoozedCount(m_account), 1);
    }

    void wakingPutsMessageOnTop()
    {
        const qint64 old = add("old", "Alt");
        add("newer", "Neuer", QDateTime::currentSecsSinceEpoch() + 100);
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        MailStore::setSnooze({old}, now + 1000);

        QVERIFY(MailStore::wakeSnoozed(now + 999).isEmpty());
        QCOMPARE(MailStore::wakeSnoozed(now + 1000), QList<qint64>{old});
        QCOMPARE(MailStore::snoozeUntil(old), 0);
        // Woken-Zeitpunkt ist "jetzt"; das Datum der Mail bleibt unverändert.
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("UPDATE messages SET woken_at = ? WHERE id = ?"));
        q.addBindValue(now + 5000);
        q.addBindValue(old);
        QVERIFY(q.exec());

        MessageListModel inbox;
        inbox.setFilter({});
        QCOMPARE(inbox.idAt(0), old);
        MessageListModel::Filter threaded;
        threaded.threaded = true;
        MessageListModel conversations;
        conversations.setFilter(threaded);
        QCOMPARE(conversations.idAt(0), old);
    }

    void replyWakesSnoozedConversation()
    {
        const qint64 snoozed = add("thread", "Frage");
        const qint64 unrelated = add("unrelated", "Anderes");
        MailStore::setSnooze({snoozed, unrelated}, QDateTime::currentSecsSinceEpoch() + 3600);

        const qint64 own = addReply("own", "thread", "Max <MAX@example.com>", m_inbox);
        QVERIFY(MailStore::wakeRepliedThreads({own}, QStringLiteral("max@example.com")).isEmpty());

        const qint64 spam = addReply("spam", "thread", "Anna <anna@example.com>", MailStore::ensureFolderForRole(m_account, QString::fromLatin1(FolderRole::Junk)));
        QVERIFY(MailStore::wakeRepliedThreads({spam}, QStringLiteral("max@example.com")).isEmpty());
        QVERIFY(MailStore::snoozeUntil(snoozed) > 0);

        const qint64 reply = addReply("reply", "thread", "Anna <anna@example.com>", m_inbox);
        QCOMPARE(MailStore::wakeRepliedThreads({reply}, QStringLiteral("max@example.com")), QList<qint64>{snoozed});
        QCOMPARE(MailStore::snoozeUntil(snoozed), 0);
        QVERIFY(MailStore::snoozeUntil(unrelated) > 0);
    }

    void junkFolderIsCreatedOnDemand()
    {
        Account a;
        a.name = QStringLiteral("Ohne Spam");
        a.email = QStringLiteral("spam@example.com");
        const qint64 account = m_accounts.save(a).id;
        MailStore::upsertFolder(account, QStringLiteral("INBOX"), QStringLiteral("Inbox"), QStringLiteral("inbox"), QStringLiteral("/"), 0);

        const QString junk = QString::fromLatin1(FolderRole::Junk);
        const qint64 folder = MailStore::ensureFolderForRole(account, junk);
        QVERIFY(folder > 0);
        QCOMPARE(MailStore::folder(folder).path, QStringLiteral("Junk"));
        QCOMPARE(MailStore::ensureFolderForRole(account, junk), folder);
        QCOMPARE(MailStore::pendingFolderCreations(account), QStringList{QStringLiteral("Junk")});
    }

    void movingEndsSnooze()
    {
        const qint64 id = add("move", "Verschoben");
        MailStore::setSnooze({id}, QDateTime::currentSecsSinceEpoch() + 3600);
        MailStore::moveLocally(id, m_archive);
        QCOMPARE(MailStore::snoozeUntil(id), 0);
    }

    void snoozeTimesAreInTheFuture_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<int>("minimum");
        const QDate wednesday(2026, 10, 28);
        QTest::newRow("Mittwoch früh") << QDateTime(wednesday, QTime(9, 0)) << 5;
        QTest::newRow("Mittwoch spät") << QDateTime(wednesday, QTime(23, 0)) << 3;
        QTest::newRow("Freitag") << QDateTime(wednesday.addDays(2), QTime(12, 0)) << 3;
        QTest::newRow("Sonntag") << QDateTime(wednesday.addDays(4), QTime(12, 0)) << 3;
    }

    void snoozeTimesAreInTheFuture()
    {
        QFETCH(QDateTime, now);
        QFETCH(int, minimum);
        const auto times = AppController::snoozeTimes(now);
        QVERIFY(times.size() >= minimum);
        for (const auto &t : times)
            QVERIFY2(t.when > now, qPrintable(t.label));
        QCOMPARE(times.last().when.date().dayOfWeek(), int(Qt::Monday));
        QVERIFY(now.date().daysTo(times.last().when.date()) >= 1);
    }

    // ---------------------------------------------------------------- Filter

    void conditionsMatch()
    {
        const FilterFields fields{QStringLiteral("Anna Beispiel"), QStringLiteral("anna@example.com"), QStringLiteral("max@example.com"),
                                  QStringLiteral("team@example.com"), QStringLiteral("Rechnung Oktober")};
        FilterRule rule;
        rule.conditions = {{QStringLiteral("from"), QStringLiteral("is"), QStringLiteral("ANNA@example.com")}};
        QVERIFY(FilterEngine::matches(rule, fields));

        rule.conditions = {{QStringLiteral("to"), QStringLiteral("contains"), QStringLiteral("team@")}};
        QVERIFY(FilterEngine::matches(rule, fields));

        rule.conditions = {{QStringLiteral("subject"), QStringLiteral("startsWith"), QStringLiteral("rechnung")},
                           {QStringLiteral("subject"), QStringLiteral("endsWith"), QStringLiteral("november")}};
        QVERIFY(!FilterEngine::matches(rule, fields));
        rule.matchAll = false;
        QVERIFY(FilterEngine::matches(rule, fields));

        // Verneinung über mehrere Felder: nirgends enthalten
        rule.matchAll = true;
        rule.conditions = {{QStringLiteral("any"), QStringLiteral("notContains"), QStringLiteral("anna")}};
        QVERIFY(!FilterEngine::matches(rule, fields));
        rule.conditions = {{QStringLiteral("any"), QStringLiteral("notContains"), QStringLiteral("spam")}};
        QVERIFY(FilterEngine::matches(rule, fields));

        // Regeln ohne Inhalt treffen nie, sonst würde eine leere Regel alles verschieben.
        rule.conditions = {{QStringLiteral("subject"), QStringLiteral("contains"), QString()}};
        QVERIFY(!FilterEngine::matches(rule, fields));
        rule.conditions.clear();
        QVERIFY(!FilterEngine::matches(rule, fields));
    }

    void actionsAreApplied()
    {
        const qint64 label = LabelStore::create(QStringLiteral("Finanzen %1").arg(QDateTime::currentMSecsSinceEpoch()), QString());
        const qint64 hit = add("hit", "Rechnung 42");
        const qint64 other = add("other", "Hallo");

        FilterRule rule;
        rule.name = QStringLiteral("Rechnungen");
        rule.accountId = m_account;
        rule.conditions = {{QStringLiteral("subject"), QStringLiteral("contains"), QStringLiteral("rechnung")}};
        rule.actions = {{QStringLiteral("read"), {}},
                        {QStringLiteral("flag"), {}},
                        {QStringLiteral("label"), QString::number(label)},
                        {QStringLiteral("move"), QStringLiteral("Rechnungen")}};
        QVERIFY(FilterStore::save(rule) > 0);

        const FilterResult result = FilterEngine::apply({hit, other}, FilterStore::rules());
        QCOMPARE(result.matched, 1);
        QCOMPARE(result.untouched, QList<qint64>{other});
        QVERIFY(result.folders.contains(m_inbox) && result.folders.contains(m_custom));

        const MessageLocation loc = MailStore::location(hit);
        QCOMPARE(loc.folderId, m_custom);
        QVERIFY(loc.flags & MessageFlag::Seen);
        QVERIFY(loc.flags & MessageFlag::Flagged);
        QVERIFY(LabelStore::labelsOf(hit).contains(label));
        QCOMPARE(MailStore::location(other).folderId, m_inbox);
    }

    void stopAndOrderAreRespected()
    {
        const qint64 id = add("stop", "Newsletter");
        FilterRule first;
        first.name = QStringLiteral("Erste");
        first.conditions = {{QStringLiteral("subject"), QStringLiteral("contains"), QStringLiteral("news")}};
        first.actions = {{QStringLiteral("archive"), {}}};
        first.stop = true;
        const qint64 firstId = FilterStore::save(first);
        FilterRule second = first;
        second.name = QStringLiteral("Zweite");
        second.actions = {{QStringLiteral("flag"), {}}};
        second.stop = false;
        const qint64 secondId = FilterStore::save(second);

        QCOMPARE(FilterStore::rules().first().id, firstId);
        FilterEngine::apply({id}, FilterStore::rules());
        QCOMPARE(MailStore::location(id).folderId, m_archive);
        QVERIFY(!(MailStore::location(id).flags & MessageFlag::Flagged));

        FilterStore::move(secondId, -1);
        QCOMPARE(FilterStore::rules().first().id, secondId);
        FilterStore::move(secondId, -1); // schon oben: nichts passiert
        QCOMPARE(FilterStore::rules().first().id, secondId);
    }

    void disabledAndForeignRulesAreSkipped()
    {
        const qint64 id = add("skip", "Alles");
        FilterRule off;
        off.name = QStringLiteral("Aus");
        off.enabled = false;
        off.conditions = {{QStringLiteral("subject"), QStringLiteral("contains"), QStringLiteral("alles")}};
        off.actions = {{QStringLiteral("flag"), {}}};
        FilterStore::save(off);
        FilterRule foreign = off;
        foreign.enabled = true;
        foreign.accountId = m_account + 999;
        FilterStore::save(foreign);

        const FilterResult result = FilterEngine::apply({id}, FilterStore::rules());
        QCOMPARE(result.matched, 0);
        QCOMPARE(result.untouched, QList<qint64>{id});
    }

    void imapChangesBecomePendingOperations()
    {
        Account a;
        a.name = QStringLiteral("IMAP");
        a.email = QStringLiteral("imap@example.com");
        const qint64 account = m_accounts.save(a).id;
        const qint64 inbox = MailStore::upsertFolder(account, QStringLiteral("INBOX"), QStringLiteral("Inbox"), QStringLiteral("inbox"), QStringLiteral("/"), 0);
        const qint64 archive = MailStore::upsertFolder(account, QStringLiteral("Archiv"), QStringLiteral("Archiv"), QStringLiteral("archive"), QStringLiteral("/"), 3);
        const qint64 id = MailStore::insertMessage(account, inbox, 7, 0, 10, MessageParser::parse(rawMail("imap", "Regel")));

        FilterRule rule;
        rule.conditions = {{QStringLiteral("subject"), QStringLiteral("contains"), QStringLiteral("regel")}};
        rule.actions = {{QStringLiteral("archive"), {}}, {QStringLiteral("read"), {}}};
        const FilterResult result = FilterEngine::apply({id}, {rule});
        QVERIFY(result.accounts.contains(account));
        QCOMPARE(MailStore::location(id).folderId, archive);

        // Erst das Flag am alten Ort, dann das Verschieben: danach gilt die UID dort nicht mehr.
        const auto ops = MailStore::pendingOps(account);
        QCOMPARE(ops.size(), 2);
        QCOMPARE(ops.at(0).op, QStringLiteral("flags"));
        QCOMPARE(ops.at(0).argument, QStringLiteral("+\\Seen"));
        QCOMPARE(ops.at(1).op, QStringLiteral("move"));
        QCOMPARE(ops.at(1).uid, 7);
        QCOMPARE(ops.at(1).argument, QStringLiteral("Archiv"));
    }

    // ------------------------------------------------------------------ mbox

    void mboxRoundTrip()
    {
        const QByteArray tricky = "Hallo\nFrom der Zeile\n>From schon maskiert\nFrom a@b Mon Jan  1 12:00:00 2024\nEnde\n";
        QByteArray file;
        file += Mbox::entry(rawMail("one", "Eins", "Anna <anna@example.com>", tricky), QStringLiteral("anna@example.com"), QDateTime(QDate(2026, 10, 29), QTime(10, 0), QTimeZone::UTC), MessageFlag::Seen);
        file += Mbox::entry(rawMail("two", "Zwei"), QString(), QDateTime(QDate(2026, 1, 3), QTime(1, 5), QTimeZone::UTC), MessageFlag::Flagged);

        QVERIFY(file.startsWith("From anna@example.com Thu Oct 29 10:00:00 2026\n"));
        QVERIFY(file.contains("\n>From der Zeile\n"));
        QVERIFY(file.contains("\n>>From schon maskiert\n"));
        QVERIFY(file.contains("\n>From a@b Mon Jan  1 12:00:00 2024\n"));

        QBuffer buffer(&file);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        Mbox::Reader reader(&buffer);
        const QByteArray first = reader.next();
        const QByteArray second = reader.next();
        QVERIFY(reader.next().isEmpty());

        const ParsedMessage one = MessageParser::parse(first);
        QCOMPARE(one.subject, QStringLiteral("Eins"));
        QCOMPARE(one.plainText.trimmed(), QString::fromUtf8(tricky).trimmed());
        QCOMPARE(MessageParser::parse(second).subject, QStringLiteral("Zwei"));

        QVERIFY(Mbox::flagsFromHeaders(first) & MessageFlag::Seen);
        QCOMPARE(Mbox::flagsFromHeaders(second), int(MessageFlag::Flagged) | 0);
    }

    void mboxReaderKeepsUnquotedFromLines()
    {
        // Thunderbird-Dateien (mboxo): "From " im Text ohne Datum ist keine Trennzeile.
        QByteArray file = "From - Thu Oct 29 10:00:00 2026\nX-Mozilla-Status: 0005\nSubject: A\n\nFrom here on\nmore\n\n"
                          "From - Thu Oct 29 11:00:00 2026\nX-Mozilla-Status: 0000\nSubject: B\n\nText\n";
        QBuffer buffer(&file);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        Mbox::Reader reader(&buffer);
        const QByteArray a = reader.next();
        const QByteArray b = reader.next();
        QVERIFY(a.contains("From here on"));
        QCOMPARE(Mbox::flagsFromHeaders(a), MessageFlag::Seen | MessageFlag::Flagged);
        QCOMPARE(MessageParser::parse(b).subject, QStringLiteral("B"));
        QCOMPARE(Mbox::flagsFromHeaders(b), 0);
    }

    void statusDefaultsToRead()
    {
        QCOMPARE(Mbox::flagsFromHeaders(rawMail("x", "Ohne Status")), int(MessageFlag::Seen));
    }

    // --------------------------------------------------------- Import/Export

    void importExportRoundTrip()
    {
        SecretStore secrets;
        MailService mail(&m_accounts, &secrets);
        MailTransfer transfer(&mail, &m_accounts);
        QSignalSpy finished(&transfer, &MailTransfer::finished);

        const QString mbox = m_dir.filePath(QStringLiteral("import.mbox"));
        const QString eml = m_dir.filePath(QStringLiteral("single.eml"));
        QFile f(mbox);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(Mbox::entry(rawMail("m1", "Mbox eins"), QStringLiteral("a@b"), QDateTime::currentDateTimeUtc(), MessageFlag::Seen));
        f.write(Mbox::entry(rawMail("m2", "Mbox zwei"), QStringLiteral("a@b"), QDateTime::currentDateTimeUtc(), MessageFlag::Flagged));
        f.close();
        QFile e(eml);
        QVERIFY(e.open(QIODevice::WriteOnly));
        // CRLF wie aus Windows-Programmen
        e.write(rawMail("e1", "Eml eins").replace('\n', "\r\n"));
        e.close();

        QCOMPARE(transfer.startImport(m_custom, {mbox, eml, m_dir.filePath(QStringLiteral("fehlt.mbox"))}), QString());
        QVERIFY(transfer.isBusy());
        QVERIFY(!transfer.startImport(m_custom, {mbox}).isEmpty()); // nur ein Vorgang gleichzeitig
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.first().first().toString().contains(QLatin1String("fehlt.mbox")));
        QCOMPARE(countIn(m_custom), 3);
        QVERIFY(MailStore::hasMessageId(m_custom, "m1@example.com"));
        QVERIFY(MailStore::hasMessageId(m_custom, "e1@example.com"));

        // Erneuter Import legt nichts doppelt an.
        finished.clear();
        QCOMPARE(transfer.startImport(m_custom, {mbox}), QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(countIn(m_custom), 3);

        // Export schreibt alle Nachrichten; die Datei lässt sich wieder einlesen.
        QList<qint64> ids;
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT id FROM messages WHERE folder_id = ?"));
        q.addBindValue(m_custom);
        q.exec();
        while (q.next())
            ids << q.value(0).toLongLong();
        const QString out = m_dir.filePath(QStringLiteral("export.mbox"));
        finished.clear();
        QCOMPARE(transfer.startExport(ids, out, false), QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);

        QFile exported(out);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        Mbox::Reader reader(&exported);
        int count = 0;
        while (!reader.next().isEmpty())
            ++count;
        QCOMPARE(count, 3);

        // Eine einzelne Nachricht unverändert als .eml
        const QString single = m_dir.filePath(QStringLiteral("out.eml"));
        finished.clear();
        QCOMPARE(transfer.startExport({ids.first()}, single, true), QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QFile result(single);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QVERIFY(!result.readAll().startsWith("From "));
    }

    void exportReportsMissingBodies()
    {
        SecretStore secrets;
        MailService mail(&m_accounts, &secrets);
        MailTransfer transfer(&mail, &m_accounts);
        QSignalSpy finished(&transfer, &MailTransfer::finished);

        // Kein Inhalt und keine Verbindung: nichts vortäuschen, leere Datei vermeiden.
        const qint64 id = MailStore::insertMessage(m_account, m_inbox, 0, 0, 10, MessageParser::parse(rawMail("nobody", "Ohne Inhalt")));
        const QString out = m_dir.filePath(QStringLiteral("leer.mbox"));
        QCOMPARE(transfer.startExport({id}, out, false), QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!QFile::exists(out));
    }

private:
    qint64 add(const QByteArray &id, const QByteArray &subject, qint64 date = 0)
    {
        ParsedMessage p = MessageParser::parse(rawMail(id, subject));
        if (date > 0)
            p.date = QDateTime::fromSecsSinceEpoch(date);
        return MailStore::insertMessage(m_account, m_inbox, 0, 0, 10, p);
    }

    qint64 addReply(const QByteArray &id, const QByteArray &parent, const QByteArray &from, qint64 folder)
    {
        const ParsedMessage p = MessageParser::parse("In-Reply-To: <" + parent + "@example.com>\n" + rawMail(id, "Re: Frage", from));
        return MailStore::insertMessage(m_account, folder, 0, 0, 10, p);
    }

    int countIn(qint64 folder)
    {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT COUNT(*) FROM messages WHERE folder_id = ?"));
        q.addBindValue(folder);
        return q.exec() && q.next() ? q.value(0).toInt() : -1;
    }

    QTemporaryDir m_dir;
    AccountManager m_accounts;
    qint64 m_account = 0;
    qint64 m_inbox = 0;
    qint64 m_archive = 0;
    qint64 m_custom = 0;
};

QTEST_GUILESS_MAIN(M2FeaturesTest)
#include "m2featurestest.moc"

#include "core/accountmanager.h"
#include "models/messagelistmodel.h"
#include "storage/database.h"
#include "storage/demodata.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"
#include "mail/mime/messageparser.h"

#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

class MessageListModelTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
        m_accounts.load();
        seedDemoData(m_accounts);
        QCOMPARE(m_accounts.accounts().size(), 3);
    }

    void unifiedInboxContainsAllAccounts()
    {
        MessageListModel model;
        model.setFilter({});
        // 8 Mails je Konto, dazu zwei bzw. vier aus den Demo-Konversationen
        QCOMPARE(model.rowCount(), 3 * 8 + 2 + 4);

        // Absteigend nach Datum sortiert
        for (int i = 1; i < model.rowCount(); ++i) {
            QVERIFY(model.index(i - 1).data(MessageListModel::DateRole).toDateTime()
                    >= model.index(i).data(MessageListModel::DateRole).toDateTime());
        }
    }

    void singleAccountFilter()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.accountId = m_accounts.accounts().first().id;
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 10);
    }

    void conversationViewGroupsThreads()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.accountId = m_accounts.accounts().first().id;
        f.threaded = true;
        model.setFilter(f);
        // Die beiden Posteingangs-Mails der Konversation werden eine Zeile
        QCOMPARE(model.rowCount(), 9);

        int row = -1;
        for (int i = 0; i < model.rowCount(); ++i) {
            if (model.index(i).data(MessageListModel::SubjectRole).toString() == QLatin1String("Re: Planung Sommerfest"))
                row = i;
        }
        QVERIFY(row >= 0);
        const QModelIndex idx = model.index(row);
        // zählt auch die Antwort in "Gesendet"
        QCOMPARE(idx.data(MessageListModel::ThreadCountRole).toInt(), 3);
        QVERIFY(idx.data(MessageListModel::UnreadRole).toBool());
        QCOMPARE(MailStore::threadMessages(idx.data(MessageListModel::MessageIdRole).toLongLong()).size(), 3);
        QCOMPARE(MailStore::threadMessages(idx.data(MessageListModel::MessageIdRole).toLongLong(), true).size(), 2);

        // Aufklappen: die beiden älteren Nachrichten (eine aus "Gesendet") erscheinen darunter
        const qint64 latest = idx.data(MessageListModel::MessageIdRole).toLongLong();
        model.toggleThread(latest);
        QCOMPARE(model.rowCount(), 11);
        QVERIFY(model.index(row).data(MessageListModel::ExpandedRole).toBool());
        QVERIFY(model.index(row + 1).data(MessageListModel::IsChildRole).toBool());
        QVERIFY(model.index(row + 2).data(MessageListModel::IsChildRole).toBool());
        QVERIFY(model.index(row + 1).data(MessageListModel::SentRole).toBool());
        QCOMPARE(model.index(row + 1).data(MessageListModel::DateGroupRole), idx.data(MessageListModel::DateGroupRole));
        model.toggleThread(latest);
        QCOMPARE(model.rowCount(), 9);

        // Ohne Konversationsansicht wieder einzelne Nachrichten
        f.threaded = false;
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 10);
    }

    void threadsFollowReferences()
    {
        const qint64 account = m_accounts.accounts().first().id;
        const qint64 inbox = MailStore::folderIdForRole(account, QString::fromLatin1(FolderRole::Inbox));
        const qint64 archive = MailStore::folderIdForRole(account, QString::fromLatin1(FolderRole::Archive));

        auto message = [](const char *id, const char *inReplyTo, QList<QByteArray> refs = {}) {
            ParsedMessage m;
            m.subject = QStringLiteral("Thread-Test");
            m.messageId = id;
            m.inReplyTo = inReplyTo;
            m.references = refs;
            return m;
        };
        auto threadOf = [](qint64 id) {
            QSqlQuery q(Database::connection());
            q.prepare(QStringLiteral("SELECT thread_id FROM messages WHERE id = ?"));
            q.addBindValue(id);
            return q.exec() && q.next() ? q.value(0).toLongLong() : -1;
        };

        // Antwort kommt vor der Bezugsnachricht an (z. B. anderer Ordner zuerst abgeglichen)
        const qint64 reply = MailStore::insertMessage(account, inbox, 101, 0, 0, message("<b@test>", "<a@test>", {"<a@test>"}));
        const qint64 deeper = MailStore::insertMessage(account, inbox, 102, 0, 0, message("<c@test>", "", {"<a@test>", "<b@test>"}));
        QCOMPARE(threadOf(deeper), threadOf(reply));
        const qint64 parent = MailStore::insertMessage(account, archive, 201, 0, 0, message("<a@test>", ""));
        QCOMPARE(threadOf(reply), threadOf(parent));
        QCOMPARE(threadOf(deeper), threadOf(parent));

        // Kopie derselben Nachricht in einem anderen Ordner gehört dazu
        const qint64 copy = MailStore::insertMessage(account, inbox, 103, 0, 0, message("<a@test>", ""));
        QCOMPARE(threadOf(copy), threadOf(parent));

        // Unabhängige Nachricht bleibt für sich
        const qint64 other = MailStore::insertMessage(account, inbox, 104, 0, 0, message("<z@test>", ""));
        QCOMPARE(threadOf(other), other);
    }

    void flaggedVirtualFolder()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.role = QString::fromLatin1(FolderRole::Flagged);
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);
        for (int i = 0; i < model.rowCount(); ++i)
            QVERIFY(model.index(i).data(MessageListModel::FlaggedRole).toBool());
    }

    void searchIgnoresSyntaxCharacters()
    {
        MessageListModel model;
        MessageListModel::Filter f;
        f.search = QStringLiteral("Buchung\" *");
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);

        f.search = QStringLiteral("münch"); // Präfix + ohne Umlaut-Unterschied
        model.setFilter(f);
        QCOMPARE(model.rowCount(), 3);
    }

    void rebuildGroupsExistingMessages()
    {
        const qint64 account = m_accounts.accounts().first().id;
        const qint64 inbox = MailStore::folderIdForRole(account, QString::fromLatin1(FolderRole::Inbox));
        // Zwei Antworten auf eine Nachricht, die lokal nicht vorliegt
        ParsedMessage a;
        a.messageId = "<sib-1@test>";
        a.inReplyTo = "<missing@test>";
        ParsedMessage b;
        b.messageId = "<sib-2@test>";
        b.inReplyTo = "<missing@test>";
        const qint64 first = MailStore::insertMessage(account, inbox, 301, 0, 0, a);
        const qint64 second = MailStore::insertMessage(account, inbox, 302, 0, 0, b);

        // Stand einer älteren Version: jede Nachricht ist ihr eigener Thread
        QSqlQuery q(Database::connection());
        QVERIFY(q.exec(QStringLiteral("UPDATE messages SET thread_id = id")));
        QVERIFY(q.exec(QStringLiteral("DELETE FROM sync_state WHERE key = 'thread_version'")));

        MessageListModel model;
        MessageListModel::Filter f;
        f.accountId = account;
        f.threaded = true;
        model.setFilter(f);
        const int ungrouped = model.rowCount();

        QVERIFY(MailStore::rebuildThreads() > 0);
        QCOMPARE(MailStore::rebuildThreads(), 0); // nur einmal
        model.reload();
        QVERIFY(model.rowCount() < ungrouped);
        QCOMPARE(MailStore::threadMessages(first), (QList<qint64>{first, second}));
    }

private:
    QTemporaryDir m_dir;
    AccountManager m_accounts;
};

QTEST_GUILESS_MAIN(MessageListModelTest)
#include "messagelistmodeltest.moc"

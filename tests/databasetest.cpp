#include "core/accountmanager.h"
#include "storage/database.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

class DatabaseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("test.db"))));
    }

    void migratesToLatest()
    {
        QCOMPARE(Database::schemaVersion(), Database::latestSchemaVersion());
        // Erneutes Initialisieren darf nichts kaputt machen
        QVERIFY(Database::initialize(Database::path()));
        QCOMPARE(Database::schemaVersion(), Database::latestSchemaVersion());
    }

    void accountRoundTrip()
    {
        AccountManager manager;
        QSignalSpy added(&manager, &AccountManager::accountAdded);

        Account a;
        a.name = QStringLiteral("Test");
        a.email = QStringLiteral("test@example.org");
        a.type = AccountType::Pop3;
        a.incomingSecurity = Security::StartTls;
        a.incomingPort = 110;
        a = manager.save(a);
        QVERIFY(a.isValid());
        QCOMPARE(added.count(), 1);

        AccountManager reloaded;
        reloaded.load();
        const Account b = reloaded.account(a.id);
        QCOMPARE(b.email, a.email);
        QCOMPARE(b.type, AccountType::Pop3);
        QCOMPARE(b.incomingSecurity, Security::StartTls);
        QCOMPARE(b.incomingPort, 110);

        QVERIFY(reloaded.remove(a.id));
        QVERIFY(!reloaded.account(a.id).isValid());
    }

    void fullTextSearchFollowsUpdates()
    {
        QSqlQuery q(Database::connection());
        QVERIFY(q.exec(QStringLiteral("INSERT INTO accounts (name, email) VALUES ('x', 'x@example.org')")));
        const qint64 accountId = q.lastInsertId().toLongLong();
        q.prepare(QStringLiteral("INSERT INTO folders (account_id, path, name, role) VALUES (?, 'INBOX', 'Inbox', 'inbox')"));
        q.addBindValue(accountId);
        QVERIFY(q.exec());
        const qint64 folderId = q.lastInsertId().toLongLong();

        q.prepare(QStringLiteral("INSERT INTO messages (account_id, folder_id, subject, body_text) VALUES (?, ?, 'Grüße aus München', 'Hallo Welt')"));
        q.addBindValue(accountId);
        q.addBindValue(folderId);
        QVERIFY(q.exec());
        const qint64 msgId = q.lastInsertId().toLongLong();

        auto matches = [](const QString &term) {
            QSqlQuery s(Database::connection());
            s.prepare(QStringLiteral("SELECT rowid FROM messages_fts WHERE messages_fts MATCH ?"));
            s.addBindValue(term);
            s.exec();
            return s.next();
        };
        QVERIFY(matches(QStringLiteral("munchen"))); // remove_diacritics
        QVERIFY(matches(QStringLiteral("welt")));

        q.prepare(QStringLiteral("UPDATE messages SET body_text = 'Neuer Inhalt' WHERE id = ?"));
        q.addBindValue(msgId);
        QVERIFY(q.exec());
        QVERIFY(!matches(QStringLiteral("welt")));
        QVERIFY(matches(QStringLiteral("inhalt")));

        q.prepare(QStringLiteral("DELETE FROM accounts WHERE id = ?"));
        q.addBindValue(accountId);
        QVERIFY(q.exec());
        QVERIFY(!matches(QStringLiteral("inhalt")));
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "databasetest.moc"

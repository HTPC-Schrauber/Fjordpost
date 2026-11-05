#include "core/accountmanager.h"
#include "core/secretmigration.h"
#include "core/secretstore.h"
#include "storage/database.h"
#include "storage/davstore.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

namespace {

// SecretStore im Speicher; Rückrufe sofort
class MemorySecrets : public SecretStore
{
public:
    void read(const QString &key, ReadCallback callback) override { callback(entries.contains(key), entries.value(key)); }
    void write(const QString &key, const QString &secret, WriteCallback callback) override
    {
        entries.insert(key, secret);
        if (callback)
            callback(true);
    }
    void remove(const QString &key, WriteCallback callback) override
    {
        entries.remove(key);
        if (callback)
            callback(true);
    }
    QHash<QString, QString> entries;
};

} // namespace

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
        QCOMPARE(b.signature, QString());

        // Signatur ändern, ohne dass die Verbindungen neu aufgebaut werden
        QSignalSpy changed(&reloaded, &AccountManager::accountChanged);
        QVERIFY(reloaded.setSignature(a.id, QStringLiteral("Gruß\nTest")));
        QCOMPARE(reloaded.account(a.id).signature, QStringLiteral("Gruß\nTest"));
        QCOMPARE(changed.count(), 0);
        AccountManager third;
        third.load();
        QCOMPARE(third.account(a.id).signature, QStringLiteral("Gruß\nTest"));

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

    void secretNames()
    {
        const QString profile = Database::profileId();
        QCOMPARE(profile.size(), 4);

        AccountManager manager;
        Account a;
        a.name = QStringLiteral("Privat");
        a.email = QStringLiteral("max@example.de");
        a = manager.save(a);
        QCOMPARE(a.secretKey(), QStringLiteral("Mail: max@example.de [%1\u00b7%2]").arg(profile).arg(a.id));
        AccountManager reloaded;
        reloaded.load();
        QCOMPARE(reloaded.account(a.id).secretKey(), a.secretKey());

        DavAccount dav;
        dav.name = QStringLiteral("Google (max@gmail.com)");
        dav.url = QStringLiteral("https://example.org/");
        QVERIFY(DavStore::saveAccount(dav) > 0);
        QCOMPARE(dav.secretKey(), QStringLiteral("Kalender: Google (max@gmail.com) [%1\u00b7%2]").arg(profile).arg(dav.id));
        QCOMPARE(DavStore::account(dav.id).secretKey(), dav.secretKey());

        // Beim Entfernen erfährt der Empfänger den Namen, um den Eintrag zu löschen
        QSignalSpy removed(&reloaded, &AccountManager::accountRemoved);
        QVERIFY(reloaded.remove(a.id));
        QCOMPARE(removed.constFirst().at(1).toString(), a.secretKey());
        DavStore::removeAccount(dav.id);
    }

    void legacySecretsMove()
    {
        // Stand vor v9: keine Namen in der DB, Einträge heißen account-N / dav-N
        AccountManager manager;
        Account a;
        a.email = QStringLiteral("alt@example.de");
        a = manager.save(a);
        DavAccount dav;
        dav.name = QStringLiteral("Nextcloud");
        dav.url = QStringLiteral("https://cloud.example.org/");
        DavStore::saveAccount(dav);
        QSqlQuery q(Database::connection());
        QVERIFY(q.exec(QStringLiteral("UPDATE accounts SET secret_key = ''")));
        QVERIFY(q.exec(QStringLiteral("UPDATE dav_accounts SET secret_key = ''")));

        MemorySecrets secrets;
        secrets.entries.insert(QStringLiteral("account-%1").arg(a.id), QStringLiteral("passwort"));
        secrets.entries.insert(QStringLiteral("dav-%1").arg(dav.id), QStringLiteral("dav-passwort"));

        // Anderes Datenverzeichnis: alte Einträge nicht anfassen, nur Namen vergeben
        {
            AccountManager other;
            other.load();
            MemorySecrets copy;
            copy.entries = secrets.entries;
            bool done = false;
            migrateSecretNames(&copy, &other, false, [&done] {
                done = true;
            });
            QVERIFY(done);
            QCOMPARE(copy.entries, secrets.entries);
            QCOMPARE(other.account(a.id).secretKey(), a.secretKey());
            QVERIFY(q.exec(QStringLiteral("UPDATE accounts SET secret_key = ''")));
            QVERIFY(q.exec(QStringLiteral("UPDATE dav_accounts SET secret_key = ''")));
        }

        AccountManager loaded;
        loaded.load();
        QVERIFY(loaded.account(a.id).secretKey().isEmpty());
        bool done = false;
        migrateSecretNames(&secrets, &loaded, true, [&done] {
            done = true;
        });
        QVERIFY(done);
        QCOMPARE(loaded.account(a.id).secretKey(), a.secretKey());
        QCOMPARE(secrets.entries.value(a.secretKey()), QStringLiteral("passwort"));
        QCOMPARE(secrets.entries.value(DavStore::account(dav.id).secretKey()), QStringLiteral("dav-passwort"));
        QVERIFY(!secrets.entries.contains(QStringLiteral("account-%1").arg(a.id)));
        QVERIFY(!secrets.entries.contains(QStringLiteral("dav-%1").arg(dav.id)));
        QCOMPARE(secrets.entries.size(), 2);

        // Zweiter Lauf: nichts mehr zu tun
        migrateSecretNames(&secrets, &loaded, true, [] {});
        QCOMPARE(secrets.entries.size(), 2);
    }

private:
    QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "databasetest.moc"

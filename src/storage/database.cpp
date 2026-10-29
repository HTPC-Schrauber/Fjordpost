#include "database.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>

Q_LOGGING_CATEGORY(lcDb, "fjordpost.db")

namespace Fjordpost {

namespace {

QString s_path;
QString s_dataDir;

// Jede Migration bringt das Schema genau eine Version weiter.
// Bestehende Einträge niemals ändern, nur neue anhängen.
const char *const migrations[] = {
    // v1: Grundschema
    R"SQL(
    CREATE TABLE accounts (
        id                 INTEGER PRIMARY KEY AUTOINCREMENT,
        name               TEXT NOT NULL,
        display_name       TEXT NOT NULL DEFAULT '',
        email              TEXT NOT NULL,
        color              TEXT NOT NULL DEFAULT '',
        type               TEXT NOT NULL DEFAULT 'imap',
        incoming_host      TEXT NOT NULL DEFAULT '',
        incoming_port      INTEGER NOT NULL DEFAULT 993,
        incoming_security  TEXT NOT NULL DEFAULT 'tls',
        outgoing_host      TEXT NOT NULL DEFAULT '',
        outgoing_port      INTEGER NOT NULL DEFAULT 465,
        outgoing_security  TEXT NOT NULL DEFAULT 'tls',
        username           TEXT NOT NULL DEFAULT '',
        auth_method        TEXT NOT NULL DEFAULT 'password',
        enabled            INTEGER NOT NULL DEFAULT 1,
        sort_order         INTEGER NOT NULL DEFAULT 0
    );

    CREATE TABLE folders (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        parent_id       INTEGER REFERENCES folders(id) ON DELETE CASCADE,
        path            TEXT NOT NULL,
        name            TEXT NOT NULL,
        role            TEXT NOT NULL DEFAULT '',
        delimiter       TEXT NOT NULL DEFAULT '/',
        uidvalidity     INTEGER NOT NULL DEFAULT 0,
        uidnext         INTEGER NOT NULL DEFAULT 0,
        highestmodseq   INTEGER NOT NULL DEFAULT 0,
        subscribed      INTEGER NOT NULL DEFAULT 1,
        sort_order      INTEGER NOT NULL DEFAULT 0,
        UNIQUE(account_id, path)
    );

    CREATE TABLE messages (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        account_id       INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        folder_id        INTEGER NOT NULL REFERENCES folders(id) ON DELETE CASCADE,
        uid              INTEGER NOT NULL DEFAULT 0,
        message_id       TEXT NOT NULL DEFAULT '',
        in_reply_to      TEXT NOT NULL DEFAULT '',
        refs             TEXT NOT NULL DEFAULT '',
        thread_id        INTEGER NOT NULL DEFAULT 0,
        subject          TEXT NOT NULL DEFAULT '',
        from_name        TEXT NOT NULL DEFAULT '',
        from_email       TEXT NOT NULL DEFAULT '',
        to_list          TEXT NOT NULL DEFAULT '',
        cc_list          TEXT NOT NULL DEFAULT '',
        date             INTEGER NOT NULL DEFAULT 0,
        size             INTEGER NOT NULL DEFAULT 0,
        flags            INTEGER NOT NULL DEFAULT 0,
        snippet          TEXT NOT NULL DEFAULT '',
        has_attachments  INTEGER NOT NULL DEFAULT 0,
        blob_path        TEXT NOT NULL DEFAULT '',
        body_text        TEXT NOT NULL DEFAULT '',
        snooze_until     INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX idx_messages_folder_date ON messages(folder_id, date DESC);
    CREATE INDEX idx_messages_folder_uid ON messages(folder_id, uid);
    CREATE INDEX idx_messages_thread ON messages(thread_id);
    CREATE INDEX idx_messages_msgid ON messages(message_id);

    CREATE VIRTUAL TABLE messages_fts USING fts5(
        subject, from_name, from_email, body_text,
        content='messages', content_rowid='id', tokenize='unicode61 remove_diacritics 2'
    );
    CREATE TRIGGER messages_ai AFTER INSERT ON messages BEGIN
        INSERT INTO messages_fts(rowid, subject, from_name, from_email, body_text)
        VALUES (new.id, new.subject, new.from_name, new.from_email, new.body_text);
    END;
    CREATE TRIGGER messages_ad AFTER DELETE ON messages BEGIN
        INSERT INTO messages_fts(messages_fts, rowid, subject, from_name, from_email, body_text)
        VALUES ('delete', old.id, old.subject, old.from_name, old.from_email, old.body_text);
    END;
    CREATE TRIGGER messages_au AFTER UPDATE OF subject, from_name, from_email, body_text ON messages BEGIN
        INSERT INTO messages_fts(messages_fts, rowid, subject, from_name, from_email, body_text)
        VALUES ('delete', old.id, old.subject, old.from_name, old.from_email, old.body_text);
        INSERT INTO messages_fts(rowid, subject, from_name, from_email, body_text)
        VALUES (new.id, new.subject, new.from_name, new.from_email, new.body_text);
    END;

    CREATE TABLE sync_state (
        key   TEXT PRIMARY KEY,
        value TEXT NOT NULL
    );
    )SQL",

    // v2: Synchronisation, Offline-Operationen und Postausgang
    R"SQL(
    ALTER TABLE messages ADD COLUMN body_fetched INTEGER NOT NULL DEFAULT 0;
    ALTER TABLE messages ADD COLUMN bcc_list TEXT NOT NULL DEFAULT '';
    ALTER TABLE messages ADD COLUMN reply_to TEXT NOT NULL DEFAULT '';

    -- Lokale Änderungen, die noch zum Server müssen (Flags, Verschieben, Löschen).
    CREATE TABLE pending_ops (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        op          TEXT NOT NULL,
        folder_path TEXT NOT NULL,
        uid         INTEGER NOT NULL,
        argument    TEXT NOT NULL DEFAULT '',
        created     INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX idx_pending_ops_account ON pending_ops(account_id, id);

    -- Zu sendende Nachrichten (RFC 822 im BlobStore).
    CREATE TABLE outbox (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
        blob_path   TEXT NOT NULL,
        sender      TEXT NOT NULL,
        recipients  TEXT NOT NULL,
        subject     TEXT NOT NULL DEFAULT '',
        send_after  INTEGER NOT NULL DEFAULT 0,
        attempts    INTEGER NOT NULL DEFAULT 0,
        last_error  TEXT NOT NULL DEFAULT '',
        -- Bezugsnachricht für Antwort/Weiterleitung (Flag nach dem Senden setzen)
        reference_message INTEGER NOT NULL DEFAULT 0,
        reference_kind    TEXT NOT NULL DEFAULT ''
    );
    )SQL",

    // v3: Signaturen pro Konto (Klartext)
    R"SQL(
    ALTER TABLE accounts ADD COLUMN signature TEXT NOT NULL DEFAULT '';
    )SQL",

    // v4: Konversationen. Jede Nachricht gehört zu einem Thread (sonst ihr eigener);
    // Index für das nachträgliche Zusammenführen über In-Reply-To.
    R"SQL(
    UPDATE messages SET thread_id = id WHERE thread_id = 0;
    CREATE INDEX idx_messages_inreplyto ON messages(account_id, in_reply_to);
    )SQL",

    // v5: Labels (kontenübergreifend), auf dem Server als IMAP-Schlüsselwort
    R"SQL(
    CREATE TABLE labels (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        name        TEXT NOT NULL UNIQUE COLLATE NOCASE,
        color       TEXT NOT NULL DEFAULT '',
        keyword     TEXT NOT NULL UNIQUE,
        sort_order  INTEGER NOT NULL DEFAULT 0
    );
    CREATE TABLE message_labels (
        message_id  INTEGER NOT NULL REFERENCES messages(id) ON DELETE CASCADE,
        label_id    INTEGER NOT NULL REFERENCES labels(id) ON DELETE CASCADE,
        PRIMARY KEY (message_id, label_id)
    );
    CREATE INDEX idx_message_labels_label ON message_labels(label_id);
    -- Erlaubt der Server eigene Schlüsselwörter (PERMANENTFLAGS \*)? Bis zum ersten SELECT angenommen.
    ALTER TABLE folders ADD COLUMN keywords INTEGER NOT NULL DEFAULT 1;
    )SQL",
};

constexpr int migrationCount = int(sizeof(migrations) / sizeof(migrations[0]));

QString connectionName()
{
    return QStringLiteral("fjordpost-%1").arg(quintptr(QThread::currentThreadId()));
}

bool execScript(QSqlDatabase &db, const QString &script)
{
    // QSqlQuery führt nur ein Statement pro exec() aus; Trigger-Bodies
    // enthalten selbst Semikolons, daher an "END;" bzw. ";" auf Zeilenende trennen.
    QStringList statements;
    QString current;
    bool inTrigger = false;
    const auto lines = script.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        current += line + QLatin1Char('\n');
        if (trimmed.startsWith(QLatin1String("CREATE TRIGGER")))
            inTrigger = true;
        if (inTrigger) {
            if (trimmed == QLatin1String("END;")) {
                statements << current;
                current.clear();
                inTrigger = false;
            }
        } else if (trimmed.endsWith(QLatin1Char(';'))) {
            statements << current;
            current.clear();
        }
    }
    if (!current.trimmed().isEmpty())
        statements << current;

    QSqlQuery q(db);
    for (const QString &stmt : std::as_const(statements)) {
        if (!q.exec(stmt)) {
            qCWarning(lcDb) << "Migration fehlgeschlagen:" << q.lastError().text() << stmt;
            return false;
        }
    }
    return true;
}

void applyPragmas(QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    q.exec(QStringLiteral("PRAGMA journal_mode = WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous = NORMAL"));
    q.exec(QStringLiteral("PRAGMA busy_timeout = 5000"));
}

} // namespace

bool Database::initialize(const QString &path)
{
    s_path = path;
    s_dataDir = QFileInfo(path).absolutePath();
    QDir().mkpath(s_dataDir);

    QSqlDatabase db = connection();
    if (!db.isOpen()) {
        qCCritical(lcDb) << "Datenbank konnte nicht geöffnet werden:" << path << db.lastError().text();
        return false;
    }
    return migrate(db);
}

QSqlDatabase Database::connection()
{
    const QString name = connectionName();
    if (QSqlDatabase::contains(name))
        return QSqlDatabase::database(name);

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(s_path);
    if (db.open())
        applyPragmas(db);
    return db;
}

QString Database::path()
{
    return s_path;
}

QString Database::dataDir()
{
    return s_dataDir;
}

int Database::schemaVersion()
{
    QSqlQuery q(connection());
    q.exec(QStringLiteral("PRAGMA user_version"));
    return q.next() ? q.value(0).toInt() : 0;
}

int Database::latestSchemaVersion()
{
    return migrationCount;
}

bool Database::migrate(QSqlDatabase &db)
{
    const int current = schemaVersion();
    for (int v = current; v < migrationCount; ++v) {
        qCInfo(lcDb) << "Migriere Datenbankschema auf Version" << v + 1;
        if (!db.transaction())
            return false;
        if (!execScript(db, QString::fromUtf8(migrations[v]))) {
            db.rollback();
            return false;
        }
        QSqlQuery q(db);
        q.exec(QStringLiteral("PRAGMA user_version = %1").arg(v + 1));
        if (!db.commit())
            return false;
    }
    return true;
}

} // namespace Fjordpost

#include "mailstore.h"

#include "database.h"
#include "messageflags.h"

#include <algorithm>

#include <QDateTime>
#include <QHash>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcStore, "fjordpost.store")

namespace Fjordpost::MailStore {

namespace {

bool exec(QSqlQuery &q)
{
    if (!q.exec()) {
        qCWarning(lcStore) << q.lastError().text() << q.lastQuery();
        return false;
    }
    return true;
}

QString nonNull(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

QString utf8(const QByteArray &b)
{
    return nonNull(QString::fromUtf8(b));
}

// Thread-Zuordnung: gleiche Konversation, wenn dieselbe Nachricht (Kopie in
// einem anderen Ordner) oder ein referenzierter Vorgänger im selben Konto
// bereits bekannt ist.
qint64 findThread(qint64 accountId, qint64 ownId, const ParsedMessage &m)
{
    QList<QByteArray> candidates;
    if (!m.messageId.isEmpty())
        candidates << m.messageId;
    if (!m.inReplyTo.isEmpty())
        candidates << m.inReplyTo;
    // Neueste Referenz zuerst: der direkte Vorgänger ist am wahrscheinlichsten bekannt.
    for (auto it = m.references.crbegin(); it != m.references.crend(); ++it)
        candidates << *it;
    if (candidates.isEmpty())
        return 0;

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT thread_id FROM messages WHERE account_id = ? AND message_id = ? AND id != ? AND thread_id > 0 LIMIT 1"));
    for (const QByteArray &id : std::as_const(candidates)) {
        q.addBindValue(accountId);
        q.addBindValue(QString::fromUtf8(id));
        q.addBindValue(ownId);
        if (exec(q) && q.next())
            return q.value(0).toLongLong();
    }
    // Bezugsnachricht fehlt lokal: eine andere Antwort auf dieselbe Nachricht genügt.
    QSqlQuery s(Database::connection());
    s.prepare(QStringLiteral("SELECT thread_id FROM messages WHERE account_id = ? AND in_reply_to = ? AND id != ? AND thread_id > 0 LIMIT 1"));
    for (const QByteArray &id : std::as_const(candidates)) {
        if (id == m.messageId)
            continue;
        s.addBindValue(accountId);
        s.addBindValue(QString::fromUtf8(id));
        s.addBindValue(ownId);
        if (exec(s) && s.next())
            return s.value(0).toLongLong();
    }
    return 0;
}

// Antworten, die vor ihrer Bezugsnachricht angekommen sind, haben einen eigenen
// Thread bekommen; diese Threads jetzt in den der Bezugsnachricht übernehmen.
void adoptReplies(qint64 accountId, const QByteArray &messageId, qint64 thread)
{
    if (messageId.isEmpty())
        return;
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "UPDATE messages SET thread_id = ? WHERE account_id = ? AND thread_id IN "
        "(SELECT thread_id FROM messages WHERE account_id = ? AND in_reply_to = ? AND thread_id != ?)"));
    q.addBindValue(thread);
    q.addBindValue(accountId);
    q.addBindValue(accountId);
    q.addBindValue(QString::fromUtf8(messageId));
    q.addBindValue(thread);
    exec(q);
}

} // namespace

qint64 upsertFolder(qint64 accountId, const QString &path, const QString &name, const QString &role, const QString &delimiter, int sortOrder)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "INSERT INTO folders (account_id, path, name, role, delimiter, sort_order) VALUES (?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(account_id, path) DO UPDATE SET name = excluded.name, role = excluded.role, "
        "delimiter = excluded.delimiter, sort_order = excluded.sort_order"));
    q.addBindValue(accountId);
    q.addBindValue(path);
    q.addBindValue(name);
    q.addBindValue(nonNull(role));
    q.addBindValue(nonNull(delimiter));
    q.addBindValue(sortOrder);
    exec(q);

    q.prepare(QStringLiteral("SELECT id FROM folders WHERE account_id = ? AND path = ?"));
    q.addBindValue(accountId);
    q.addBindValue(path);
    return exec(q) && q.next() ? q.value(0).toLongLong() : 0;
}

void removeFoldersExcept(qint64 accountId, const QStringList &paths)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, path FROM folders WHERE account_id = ?"));
    q.addBindValue(accountId);
    exec(q);
    QList<qint64> stale;
    while (q.next()) {
        if (!paths.contains(q.value(1).toString()))
            stale << q.value(0).toLongLong();
    }
    QSqlQuery del(Database::connection());
    del.prepare(QStringLiteral("DELETE FROM folders WHERE id = ?"));
    for (qint64 id : std::as_const(stale)) {
        del.addBindValue(id);
        exec(del);
    }
}

static FolderState folderFromQuery(const QSqlQuery &q)
{
    FolderState f;
    f.id = q.value(0).toLongLong();
    f.path = q.value(1).toString();
    f.role = q.value(2).toString();
    f.uidValidity = q.value(3).toLongLong();
    f.uidNext = q.value(4).toLongLong();
    f.highestModSeq = q.value(5).toULongLong();
    return f;
}

QList<FolderState> folders(qint64 accountId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, path, role, uidvalidity, uidnext, highestmodseq FROM folders WHERE account_id = ? "
                             "ORDER BY CASE role WHEN 'inbox' THEN 0 ELSE 1 END, sort_order, path"));
    q.addBindValue(accountId);
    exec(q);
    QList<FolderState> result;
    while (q.next())
        result << folderFromQuery(q);
    return result;
}

FolderState folder(qint64 folderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, path, role, uidvalidity, uidnext, highestmodseq FROM folders WHERE id = ?"));
    q.addBindValue(folderId);
    return exec(q) && q.next() ? folderFromQuery(q) : FolderState{};
}

qint64 folderIdForRole(qint64 accountId, const QString &role)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id FROM folders WHERE account_id = ? AND role = ? ORDER BY id LIMIT 1"));
    q.addBindValue(accountId);
    q.addBindValue(role);
    return exec(q) && q.next() ? q.value(0).toLongLong() : 0;
}

qint64 folderIdForPath(qint64 accountId, const QString &path)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id FROM folders WHERE account_id = ? AND path = ?"));
    q.addBindValue(accountId);
    q.addBindValue(path);
    return exec(q) && q.next() ? q.value(0).toLongLong() : 0;
}

int roleSortOrder(const QString &role)
{
    static const QStringList order = {
        QString::fromLatin1(FolderRole::Inbox),
        QString::fromLatin1(FolderRole::Drafts),
        QString::fromLatin1(FolderRole::Sent),
        QString::fromLatin1(FolderRole::Archive),
        QString::fromLatin1(FolderRole::Junk),
        QString::fromLatin1(FolderRole::Trash),
    };
    const int idx = order.indexOf(role);
    return idx >= 0 ? idx : 10;
}

qint64 ensureFolderForRole(qint64 accountId, const QString &role)
{
    if (const qint64 id = folderIdForRole(accountId, role))
        return id;

    static const QHash<QString, QString> names = {
        {QString::fromLatin1(FolderRole::Drafts), QStringLiteral("Drafts")},
        {QString::fromLatin1(FolderRole::Sent), QStringLiteral("Sent")},
        {QString::fromLatin1(FolderRole::Trash), QStringLiteral("Trash")},
        {QString::fromLatin1(FolderRole::Archive), QStringLiteral("Archive")},
    };
    const QString name = names.value(role);
    if (name.isEmpty())
        return 0;

    // Trennzeichen und Namensraum vom Posteingang übernehmen: Server wie Courier
    // erlauben Ordner nur unterhalb von "INBOX.".
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT path, delimiter FROM folders WHERE account_id = ?"));
    q.addBindValue(accountId);
    exec(q);
    QString delimiter = QStringLiteral("/");
    QStringList others;
    while (q.next()) {
        const QString path = q.value(0).toString();
        if (path.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0) {
            if (!q.value(1).toString().isEmpty())
                delimiter = q.value(1).toString();
        } else {
            others << path;
        }
    }
    const QString prefix = QStringLiteral("INBOX") + delimiter;
    const bool belowInbox = !others.isEmpty() && std::all_of(others.cbegin(), others.cend(), [&prefix](const QString &p) {
        return p.startsWith(prefix, Qt::CaseInsensitive);
    });
    const QString path = belowInbox ? prefix + name : name;

    const qint64 id = upsertFolder(accountId, path, name, role, delimiter, roleSortOrder(role));
    if (id > 0)
        enqueueOp(accountId, QStringLiteral("create"), path, 0);
    return id;
}

QStringList pendingFolderCreations(qint64 accountId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT folder_path FROM pending_ops WHERE account_id = ? AND op = 'create'"));
    q.addBindValue(accountId);
    exec(q);
    QStringList paths;
    while (q.next())
        paths << q.value(0).toString();
    return paths;
}

void saveFolderState(const FolderState &state)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE folders SET uidvalidity = ?, uidnext = ?, highestmodseq = ? WHERE id = ?"));
    q.addBindValue(state.uidValidity);
    q.addBindValue(state.uidNext);
    q.addBindValue(qint64(state.highestModSeq));
    q.addBindValue(state.id);
    exec(q);
}

void clearFolder(qint64 folderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM messages WHERE folder_id = ?"));
    q.addBindValue(folderId);
    exec(q);
}

QHash<qint64, LocalMessage> localMessages(qint64 folderId, qint64 minUid)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT uid, id, flags FROM messages WHERE folder_id = ? AND uid >= ? AND uid > 0"));
    q.addBindValue(folderId);
    q.addBindValue(minUid);
    exec(q);
    QHash<qint64, LocalMessage> result;
    while (q.next())
        result.insert(q.value(0).toLongLong(), {q.value(1).toLongLong(), q.value(2).toInt()});
    return result;
}

qint64 minUid(qint64 folderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT MIN(uid) FROM messages WHERE folder_id = ? AND uid > 0"));
    q.addBindValue(folderId);
    return exec(q) && q.next() ? q.value(0).toLongLong() : 0;
}

qint64 insertMessage(qint64 accountId, qint64 folderId, qint64 uid, int flags, qint64 size, const ParsedMessage &h)
{
    QSqlQuery q(Database::connection());

    // Lokal verschobene Nachricht (uid 0) mit gleicher Message-ID übernehmen,
    // statt sie doppelt anzulegen.
    if (!h.messageId.isEmpty()) {
        q.prepare(QStringLiteral("SELECT id FROM messages WHERE folder_id = ? AND uid = 0 AND message_id = ? LIMIT 1"));
        q.addBindValue(folderId);
        q.addBindValue(QString::fromUtf8(h.messageId));
        if (exec(q) && q.next()) {
            const qint64 id = q.value(0).toLongLong();
            QSqlQuery u(Database::connection());
            u.prepare(QStringLiteral("UPDATE messages SET uid = ?, flags = ? WHERE id = ?"));
            u.addBindValue(uid);
            u.addBindValue(flags);
            u.addBindValue(id);
            exec(u);
            return id;
        }
    }

    q.prepare(QStringLiteral(
        "INSERT INTO messages (account_id, folder_id, uid, message_id, in_reply_to, refs, subject, from_name, from_email, "
        "to_list, cc_list, bcc_list, reply_to, date, size, flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(accountId);
    q.addBindValue(folderId);
    q.addBindValue(uid);
    q.addBindValue(utf8(h.messageId));
    q.addBindValue(utf8(h.inReplyTo));
    QStringList refs;
    for (const QByteArray &r : h.references)
        refs << QString::fromUtf8(r);
    q.addBindValue(nonNull(refs.join(QLatin1Char(' '))));
    q.addBindValue(nonNull(h.subject));
    q.addBindValue(nonNull(h.fromName));
    q.addBindValue(nonNull(h.fromEmail));
    q.addBindValue(nonNull(h.to));
    q.addBindValue(nonNull(h.cc));
    q.addBindValue(nonNull(h.bcc));
    q.addBindValue(nonNull(h.replyTo));
    q.addBindValue(h.date.isValid() ? h.date.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch());
    q.addBindValue(size);
    q.addBindValue(flags);
    if (!exec(q))
        return 0;
    const qint64 id = q.lastInsertId().toLongLong();

    qint64 thread = findThread(accountId, id, h);
    if (thread <= 0)
        thread = id;
    QSqlQuery t(Database::connection());
    t.prepare(QStringLiteral("UPDATE messages SET thread_id = ? WHERE id = ?"));
    t.addBindValue(thread);
    t.addBindValue(id);
    exec(t);
    adoptReplies(accountId, h.messageId, thread);
    return id;
}

void updateFlags(qint64 messageId, int flags)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE messages SET flags = ? WHERE id = ?"));
    q.addBindValue(flags);
    q.addBindValue(messageId);
    exec(q);
}

void removeMessages(const QList<qint64> &messageIds)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM messages WHERE id = ?"));
    for (qint64 id : messageIds) {
        q.addBindValue(id);
        exec(q);
    }
}

void storeBody(qint64 messageId, const QString &blobPath, const ParsedMessage &parsed)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE messages SET blob_path = ?, body_fetched = 1, body_text = ?, snippet = ?, has_attachments = ? WHERE id = ?"));
    q.addBindValue(blobPath);
    q.addBindValue(nonNull(parsed.plainText.isEmpty() ? parsed.snippet : parsed.plainText));
    q.addBindValue(nonNull(parsed.snippet));
    q.addBindValue(!parsed.attachments.isEmpty());
    q.addBindValue(messageId);
    exec(q);
}

MessageLocation location(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "SELECT m.id, m.account_id, m.folder_id, f.path, f.role, m.uid, m.flags, m.blob_path, m.body_fetched, m.message_id "
        "FROM messages m JOIN folders f ON f.id = m.folder_id WHERE m.id = ?"));
    q.addBindValue(messageId);
    if (!exec(q) || !q.next())
        return {};
    MessageLocation l;
    l.id = q.value(0).toLongLong();
    l.accountId = q.value(1).toLongLong();
    l.folderId = q.value(2).toLongLong();
    l.folderPath = q.value(3).toString();
    l.folderRole = q.value(4).toString();
    l.uid = q.value(5).toLongLong();
    l.flags = q.value(6).toInt();
    l.blobPath = q.value(7).toString();
    l.bodyFetched = q.value(8).toBool();
    l.messageId = q.value(9).toString().toUtf8();
    return l;
}

QList<qint64> messagesWithoutBody(qint64 folderId, int limit, qint64 maxSize)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id FROM messages WHERE folder_id = ? AND body_fetched = 0 AND uid > 0 AND size <= ? "
                             "ORDER BY date DESC LIMIT ?"));
    q.addBindValue(folderId);
    q.addBindValue(maxSize);
    q.addBindValue(limit);
    exec(q);
    QList<qint64> ids;
    while (q.next())
        ids << q.value(0).toLongLong();
    return ids;
}

void moveLocally(qint64 messageId, qint64 targetFolderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE messages SET folder_id = ?, uid = 0 WHERE id = ?"));
    q.addBindValue(targetFolderId);
    q.addBindValue(messageId);
    exec(q);
}

int rebuildThreads()
{
    // Einmalig je Version des Zuordnungsverfahrens
    constexpr auto versionKey = "thread_version";
    constexpr auto currentVersion = "1";
    {
        QSqlQuery v(Database::connection());
        v.prepare(QStringLiteral("SELECT value FROM sync_state WHERE key = ?"));
        v.addBindValue(QString::fromLatin1(versionKey));
        if (exec(v) && v.next() && v.value(0).toString() == QLatin1String(currentVersion))
            return 0;
    }

    QSqlDatabase db = Database::connection();
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT id, account_id, message_id, in_reply_to, refs, thread_id FROM messages ORDER BY id")))
        return 0;

    // Union-Find über Nachrichten und Message-IDs je Konto: Nachrichten, die dieselbe
    // ID tragen oder sich (auch über eine fehlende Nachricht) referenzieren, gehören zusammen.
    QHash<QString, QString> parent;
    auto find = [&](const QString &key) {
        QString root = key;
        while (parent.value(root, root) != root)
            root = parent.value(root);
        // Pfad verkürzen
        QString node = key;
        while (node != root) {
            const QString next = parent.value(node);
            parent[node] = root;
            node = next;
        }
        return root;
    };
    auto unite = [&](const QString &a, const QString &b) {
        const QString ra = find(a);
        const QString rb = find(b);
        if (ra != rb)
            parent[rb] = ra;
    };

    struct Entry {
        qint64 id;
        qint64 thread;
        QString key;
    };
    QList<Entry> entries;
    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        const QString account = q.value(1).toString();
        const QString key = QStringLiteral("m:") + QString::number(id);
        parent.insert(key, key);
        QStringList ids = q.value(4).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        ids << q.value(2).toString() << q.value(3).toString();
        for (const QString &mid : std::as_const(ids)) {
            if (!mid.isEmpty())
                unite(key, account + QLatin1Char('|') + mid);
        }
        entries << Entry{id, q.value(5).toLongLong(), key};
    }

    // Thread-ID = kleinste Nachrichten-ID der Gruppe (stabil gegenüber dem alten Stand)
    QHash<QString, qint64> threadOfRoot;
    for (const Entry &e : std::as_const(entries)) {
        const QString root = find(e.key);
        if (!threadOfRoot.contains(root))
            threadOfRoot.insert(root, e.id); // entries sind nach id sortiert
    }

    int changed = 0;
    db.transaction();
    QSqlQuery u(db);
    u.prepare(QStringLiteral("UPDATE messages SET thread_id = ? WHERE id = ?"));
    for (const Entry &e : std::as_const(entries)) {
        const qint64 thread = threadOfRoot.value(find(e.key));
        if (thread == e.thread)
            continue;
        u.addBindValue(thread);
        u.addBindValue(e.id);
        if (exec(u))
            ++changed;
    }
    QSqlQuery v(db);
    v.prepare(QStringLiteral("INSERT INTO sync_state (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    v.addBindValue(QString::fromLatin1(versionKey));
    v.addBindValue(QString::fromLatin1(currentVersion));
    exec(v);
    db.commit();
    if (changed > 0)
        qCInfo(lcStore) << "Konversationen neu zugeordnet:" << changed << "Nachrichten";
    return changed;
}

QList<qint64> threadMessages(qint64 messageId, bool sameFolder)
{
    // Papierkorb und Spam gehören nicht zur Konversation, außer man schaut gerade dort hinein.
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "SELECT t.id FROM messages m JOIN folders mf ON mf.id = m.folder_id "
        "JOIN messages t ON t.account_id = m.account_id AND t.thread_id = m.thread_id "
        "JOIN folders tf ON tf.id = t.folder_id "
        "WHERE m.id = ? AND (t.flags & %1) = 0 "
        "AND (? = 0 OR t.folder_id = m.folder_id) "
        "AND (tf.role NOT IN ('trash', 'junk') OR tf.role = mf.role) "
        "ORDER BY t.date, t.id").arg(MessageFlag::Deleted));
    q.addBindValue(messageId);
    q.addBindValue(sameFolder ? 1 : 0);
    QList<qint64> ids;
    if (exec(q)) {
        while (q.next())
            ids << q.value(0).toLongLong();
    }
    if (ids.isEmpty())
        ids << messageId;
    return ids;
}

void enqueueOp(qint64 accountId, const QString &op, const QString &folderPath, qint64 uid, const QString &argument)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("INSERT INTO pending_ops (account_id, op, folder_path, uid, argument, created) VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(accountId);
    q.addBindValue(op);
    q.addBindValue(folderPath);
    q.addBindValue(uid);
    q.addBindValue(nonNull(argument));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    exec(q);
}

QList<PendingOp> pendingOps(qint64 accountId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, op, folder_path, uid, argument FROM pending_ops WHERE account_id = ? ORDER BY id"));
    q.addBindValue(accountId);
    exec(q);
    QList<PendingOp> ops;
    while (q.next()) {
        PendingOp op;
        op.id = q.value(0).toLongLong();
        op.accountId = accountId;
        op.op = q.value(1).toString();
        op.folderPath = q.value(2).toString();
        op.uid = q.value(3).toLongLong();
        op.argument = q.value(4).toString();
        ops << op;
    }
    return ops;
}

void removeOp(qint64 opId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM pending_ops WHERE id = ?"));
    q.addBindValue(opId);
    exec(q);
}

QSet<qint64> uidsWithPendingOps(qint64 accountId, const QString &folderPath)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT uid FROM pending_ops WHERE account_id = ? AND folder_path = ?"));
    q.addBindValue(accountId);
    q.addBindValue(folderPath);
    exec(q);
    QSet<qint64> uids;
    while (q.next())
        uids.insert(q.value(0).toLongLong());
    return uids;
}

int flagsFromImap(const QList<QByteArray> &flags)
{
    int mask = 0;
    for (const QByteArray &f : flags) {
        const QByteArray l = f.toLower();
        if (l == "\\seen")
            mask |= MessageFlag::Seen;
        else if (l == "\\flagged")
            mask |= MessageFlag::Flagged;
        else if (l == "\\answered")
            mask |= MessageFlag::Answered;
        else if (l == "\\draft")
            mask |= MessageFlag::Draft;
        else if (l == "\\deleted")
            mask |= MessageFlag::Deleted;
        else if (l == "$forwarded")
            mask |= MessageFlag::Forwarded;
    }
    return mask;
}

QList<QByteArray> imapFlagsFor(int mask)
{
    QList<QByteArray> flags;
    if (mask & MessageFlag::Seen)
        flags << "\\Seen";
    if (mask & MessageFlag::Flagged)
        flags << "\\Flagged";
    if (mask & MessageFlag::Answered)
        flags << "\\Answered";
    if (mask & MessageFlag::Draft)
        flags << "\\Draft";
    if (mask & MessageFlag::Deleted)
        flags << "\\Deleted";
    if (mask & MessageFlag::Forwarded)
        flags << "$Forwarded";
    return flags;
}

QString roleFor(const QString &path, const QList<QByteArray> &attributes)
{
    if (path.compare(QLatin1String("INBOX"), Qt::CaseInsensitive) == 0)
        return QString::fromLatin1(FolderRole::Inbox);

    for (const QByteArray &a : attributes) {
        const QByteArray l = a.toLower();
        if (l == "\\sent")
            return QString::fromLatin1(FolderRole::Sent);
        if (l == "\\drafts")
            return QString::fromLatin1(FolderRole::Drafts);
        if (l == "\\trash")
            return QString::fromLatin1(FolderRole::Trash);
        if (l == "\\junk")
            return QString::fromLatin1(FolderRole::Junk);
        if (l == "\\archive")
            return QString::fromLatin1(FolderRole::Archive);
    }

    // Fallback über verbreitete Namen (nur oberste Ebene bzw. unter INBOX)
    const QString name = path.section(QLatin1Char('/'), -1).section(QLatin1Char('.'), -1).toLower();
    static const QHash<QString, const char *> names = {
        {QStringLiteral("sent"), FolderRole::Sent},
        {QStringLiteral("sent items"), FolderRole::Sent},
        {QStringLiteral("sent messages"), FolderRole::Sent},
        {QStringLiteral("gesendet"), FolderRole::Sent},
        {QStringLiteral("gesendete objekte"), FolderRole::Sent},
        {QStringLiteral("gesendete elemente"), FolderRole::Sent},
        {QStringLiteral("drafts"), FolderRole::Drafts},
        {QStringLiteral("entwürfe"), FolderRole::Drafts},
        {QStringLiteral("trash"), FolderRole::Trash},
        {QStringLiteral("deleted items"), FolderRole::Trash},
        {QStringLiteral("deleted messages"), FolderRole::Trash},
        {QStringLiteral("papierkorb"), FolderRole::Trash},
        {QStringLiteral("gelöschte objekte"), FolderRole::Trash},
        {QStringLiteral("gelöschte elemente"), FolderRole::Trash},
        {QStringLiteral("junk"), FolderRole::Junk},
        {QStringLiteral("spam"), FolderRole::Junk},
        {QStringLiteral("junk e-mail"), FolderRole::Junk},
        {QStringLiteral("archive"), FolderRole::Archive},
        {QStringLiteral("archiv"), FolderRole::Archive},
    };
    const auto it = names.constFind(name);
    return it != names.constEnd() ? QString::fromLatin1(it.value()) : QString();
}

} // namespace Fjordpost::MailStore

#include "davstore.h"

#include "storage/database.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

Q_LOGGING_CATEGORY(lcDavStore, "fjordpost.davstore")

namespace Fjordpost::DavStore {

namespace {

// Null-QStrings würden als SQL-NULL gebunden und NOT NULL verletzen.
QString nonNull(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

QString likeEscape(const QString &text)
{
    QString s = text;
    s.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('%'), QLatin1String("\\%")).replace(QLatin1Char('_'), QLatin1String("\\_"));
    return s;
}

const QString accountColumns = QStringLiteral("id, name, url, username, mail_account_id, calendar_home, addressbook_home, auto_schedule, oauth_provider, secret_key");

DavAccount accountFrom(const QSqlQuery &q)
{
    DavAccount a;
    a.id = q.value(0).toLongLong();
    a.name = q.value(1).toString();
    a.url = q.value(2).toString();
    a.username = q.value(3).toString();
    a.mailAccountId = q.value(4).toLongLong();
    a.calendarHome = q.value(5).toString();
    a.addressbookHome = q.value(6).toString();
    a.autoSchedule = q.value(7).toBool();
    a.oauthProvider = q.value(8).toString();
    a.secretName = q.value(9).toString();
    return a;
}

const QString collectionColumns = QStringLiteral("c.id, c.account_id, c.kind, c.href, c.name, c.color, c.ctag, c.has_events, c.has_todos, c.read_only, c.visible");

DavCollection collectionFrom(const QSqlQuery &q)
{
    DavCollection c;
    c.id = q.value(0).toLongLong();
    c.accountId = q.value(1).toLongLong();
    c.kind = q.value(2).toString();
    c.href = q.value(3).toString();
    c.name = q.value(4).toString();
    c.color = q.value(5).toString();
    c.ctag = q.value(6).toString();
    c.hasEvents = q.value(7).toBool();
    c.hasTodos = q.value(8).toBool();
    c.readOnly = q.value(9).toBool();
    c.visible = q.value(10).toBool();
    return c;
}

const QString itemColumns = QStringLiteral("i.id, i.collection_id, i.kind, i.href, i.etag, i.uid, i.raw, i.pending, i.title, i.subtitle, "
                                           "i.range_start, i.range_end, i.has_alarm, i.due, i.completed, i.priority");

DavItem itemFrom(const QSqlQuery &q)
{
    DavItem i;
    i.id = q.value(0).toLongLong();
    i.collectionId = q.value(1).toLongLong();
    i.kind = q.value(2).toString();
    i.href = q.value(3).toString();
    i.etag = q.value(4).toString();
    i.uid = q.value(5).toString();
    i.raw = q.value(6).toString();
    i.pending = q.value(7).toInt();
    i.title = q.value(8).toString();
    i.subtitle = q.value(9).toString();
    i.rangeStart = q.value(10).toLongLong();
    i.rangeEnd = q.value(11).toLongLong();
    i.hasAlarm = q.value(12).toBool();
    i.due = q.value(13).toLongLong();
    i.completed = q.value(14).toBool();
    i.priority = q.value(15).toInt();
    return i;
}

QList<DavItem> itemList(QSqlQuery &q)
{
    QList<DavItem> result;
    if (!q.exec()) {
        qCWarning(lcDavStore) << q.lastError().text();
        return result;
    }
    while (q.next())
        result << itemFrom(q);
    return result;
}

} // namespace

QList<DavAccount> accounts()
{
    QList<DavAccount> result;
    QSqlQuery q(Database::connection());
    q.exec(QStringLiteral("SELECT %1 FROM dav_accounts ORDER BY id").arg(accountColumns));
    while (q.next())
        result << accountFrom(q);
    return result;
}

DavAccount account(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_accounts WHERE id = ?").arg(accountColumns));
    q.addBindValue(id);
    q.exec();
    return q.next() ? accountFrom(q) : DavAccount();
}

qint64 saveAccount(DavAccount &a)
{
    QSqlQuery q(Database::connection());
    if (a.id > 0) {
        q.prepare(QStringLiteral("UPDATE dav_accounts SET name = ?, url = ?, username = ?, mail_account_id = ?, calendar_home = ?, "
                                 "addressbook_home = ?, auto_schedule = ?, oauth_provider = ? WHERE id = ?"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO dav_accounts (name, url, username, mail_account_id, calendar_home, addressbook_home, auto_schedule, oauth_provider) "
                                 "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    }
    q.addBindValue(nonNull(a.name));
    q.addBindValue(nonNull(a.url));
    q.addBindValue(nonNull(a.username));
    q.addBindValue(a.mailAccountId > 0 ? QVariant(a.mailAccountId) : QVariant(QMetaType(QMetaType::LongLong)));
    q.addBindValue(nonNull(a.calendarHome));
    q.addBindValue(nonNull(a.addressbookHome));
    q.addBindValue(a.autoSchedule);
    q.addBindValue(nonNull(a.oauthProvider));
    if (a.id > 0)
        q.addBindValue(a.id);
    if (!q.exec()) {
        qCWarning(lcDavStore) << q.lastError().text();
        return 0;
    }
    if (a.id > 0)
        return a.id;
    a.id = q.lastInsertId().toLongLong();
    a.secretName = Database::secretEntryName(QStringLiteral("Kalender"), a.name, a.id);
    if (!setSecretName(a.id, a.secretName))
        return 0;
    return a.id;
}

DavAccount accountWithSecret(const QString &secretName)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_accounts WHERE secret_key = ? AND secret_key <> ''").arg(accountColumns));
    q.addBindValue(secretName);
    q.exec();
    return q.next() ? accountFrom(q) : DavAccount();
}

bool setSecretName(qint64 accountId, const QString &name)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE dav_accounts SET secret_key = ? WHERE id = ?"));
    q.addBindValue(nonNull(name));
    q.addBindValue(accountId);
    return q.exec();
}

bool removeAccount(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM dav_accounts WHERE id = ?"));
    q.addBindValue(id);
    return q.exec();
}

QList<DavCollection> collections(qint64 accountId, const QString &kind)
{
    QList<DavCollection> result;
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_collections c WHERE (? = 0 OR c.account_id = ?) AND (? = '' OR c.kind = ?) ORDER BY c.account_id, c.name")
                  .arg(collectionColumns));
    q.addBindValue(accountId);
    q.addBindValue(accountId);
    q.addBindValue(nonNull(kind));
    q.addBindValue(nonNull(kind));
    q.exec();
    while (q.next())
        result << collectionFrom(q);
    return result;
}

DavCollection collection(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_collections c WHERE c.id = ?").arg(collectionColumns));
    q.addBindValue(id);
    q.exec();
    return q.next() ? collectionFrom(q) : DavCollection();
}

qint64 saveCollection(const DavCollection &c)
{
    QSqlQuery q(Database::connection());
    // ctag und visible gehören dem Abgleich bzw. dem Benutzer und bleiben unberührt.
    q.prepare(QStringLiteral("INSERT INTO dav_collections (account_id, kind, href, name, color, has_events, has_todos, read_only) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT (account_id, href) DO UPDATE SET "
                             "kind = excluded.kind, name = excluded.name, color = excluded.color, has_events = excluded.has_events, "
                             "has_todos = excluded.has_todos, read_only = excluded.read_only"));
    q.addBindValue(c.accountId);
    q.addBindValue(c.kind);
    q.addBindValue(c.href);
    q.addBindValue(nonNull(c.name));
    q.addBindValue(nonNull(c.color));
    q.addBindValue(c.hasEvents);
    q.addBindValue(c.hasTodos);
    q.addBindValue(c.readOnly);
    if (!q.exec()) {
        qCWarning(lcDavStore) << q.lastError().text();
        return 0;
    }
    QSqlQuery id(Database::connection());
    id.prepare(QStringLiteral("SELECT id FROM dav_collections WHERE account_id = ? AND href = ?"));
    id.addBindValue(c.accountId);
    id.addBindValue(c.href);
    id.exec();
    return id.next() ? id.value(0).toLongLong() : 0;
}

void removeCollection(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM dav_collections WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
}

QString displayColor(const DavCollection &c)
{
    static const char *const fallback[] = {"#6366f1", "#10b981", "#f43f5e", "#f59e0b", "#06b6d4", "#a855f7"};
    return c.color.isEmpty() ? QString::fromLatin1(fallback[c.id % 6]) : c.color;
}

void setCollectionVisible(qint64 id, bool visible)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE dav_collections SET visible = ? WHERE id = ?"));
    q.addBindValue(visible);
    q.addBindValue(id);
    q.exec();
}

void setCollectionCtag(qint64 id, const QString &ctag)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE dav_collections SET ctag = ? WHERE id = ?"));
    q.addBindValue(nonNull(ctag));
    q.addBindValue(id);
    q.exec();
}

DavItem item(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i WHERE i.id = ?").arg(itemColumns));
    q.addBindValue(id);
    q.exec();
    return q.next() ? itemFrom(q) : DavItem();
}

DavItem itemByHref(qint64 collectionId, const QString &href)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i WHERE i.collection_id = ? AND i.href = ?").arg(itemColumns));
    q.addBindValue(collectionId);
    q.addBindValue(href);
    q.exec();
    return q.next() ? itemFrom(q) : DavItem();
}

DavItem itemByUid(const QString &uid)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i WHERE i.uid = ? AND i.kind <> 'contact' AND i.pending <> 3 LIMIT 1").arg(itemColumns));
    q.addBindValue(uid);
    q.exec();
    return q.next() ? itemFrom(q) : DavItem();
}

bool saveItem(DavItem &i)
{
    QSqlDatabase db = Database::connection();
    db.transaction();
    QSqlQuery q(db);
    if (i.id > 0) {
        q.prepare(QStringLiteral("UPDATE dav_items SET collection_id = ?, kind = ?, href = ?, etag = ?, uid = ?, raw = ?, pending = ?, title = ?, "
                                 "subtitle = ?, range_start = ?, range_end = ?, has_alarm = ?, due = ?, completed = ?, priority = ? WHERE id = ?"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO dav_items (collection_id, kind, href, etag, uid, raw, pending, title, subtitle, range_start, "
                                 "range_end, has_alarm, due, completed, priority) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    }
    q.addBindValue(i.collectionId);
    q.addBindValue(i.kind);
    q.addBindValue(nonNull(i.href));
    q.addBindValue(nonNull(i.etag));
    q.addBindValue(nonNull(i.uid));
    q.addBindValue(nonNull(i.raw));
    q.addBindValue(i.pending);
    q.addBindValue(nonNull(i.title));
    q.addBindValue(nonNull(i.subtitle));
    q.addBindValue(i.rangeStart);
    q.addBindValue(i.rangeEnd > 0 ? QVariant(i.rangeEnd) : QVariant(QMetaType(QMetaType::LongLong)));
    q.addBindValue(i.hasAlarm);
    q.addBindValue(i.due);
    q.addBindValue(i.completed);
    q.addBindValue(i.priority);
    if (i.id > 0)
        q.addBindValue(i.id);
    if (!q.exec()) {
        qCWarning(lcDavStore) << q.lastError().text();
        db.rollback();
        return false;
    }
    if (i.id == 0)
        i.id = q.lastInsertId().toLongLong();

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM dav_item_emails WHERE item_id = ?"));
    del.addBindValue(i.id);
    del.exec();
    QSqlQuery ins(db);
    ins.prepare(QStringLiteral("INSERT INTO dav_item_emails (item_id, email) VALUES (?, ?)"));
    for (const QString &email : std::as_const(i.emails)) {
        ins.addBindValue(i.id);
        ins.addBindValue(email.toLower());
        ins.exec();
    }
    return db.commit();
}

void removeItem(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM dav_items WHERE id = ?"));
    q.addBindValue(id);
    q.exec();
}

QList<ItemState> itemStates(qint64 collectionId)
{
    QList<ItemState> result;
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT href, etag, pending, id FROM dav_items WHERE collection_id = ?"));
    q.addBindValue(collectionId);
    q.exec();
    while (q.next())
        result << ItemState{q.value(0).toString(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toLongLong()};
    return result;
}

QList<DavItem> pendingItems(qint64 accountId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_collections c ON c.id = i.collection_id "
                             "WHERE c.account_id = ? AND i.pending <> 0 ORDER BY i.id").arg(itemColumns));
    q.addBindValue(accountId);
    return itemList(q);
}

QList<DavItem> eventsInRange(qint64 from, qint64 to)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_collections c ON c.id = i.collection_id "
                             "WHERE i.kind = 'event' AND i.pending <> 3 AND c.visible = 1 AND i.range_start < ? "
                             "AND (i.range_end IS NULL OR i.range_end >= ?)").arg(itemColumns));
    q.addBindValue(to);
    q.addBindValue(from);
    return itemList(q);
}

QList<DavItem> alarmItems(qint64 from, qint64 to)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_collections c ON c.id = i.collection_id "
                             "WHERE i.kind IN ('event', 'todo') AND i.has_alarm = 1 AND i.pending <> 3 AND c.visible = 1 AND i.range_start < ? "
                             "AND (i.range_end IS NULL OR i.range_end >= ?)").arg(itemColumns));
    q.addBindValue(to);
    q.addBindValue(from);
    return itemList(q);
}

QList<DavItem> todos(qint64 collectionId, bool includeCompleted)
{
    QSqlQuery q(Database::connection());
    // Fällige zuerst, Aufgaben ohne Termin danach; Erledigte ans Ende.
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_collections c ON c.id = i.collection_id "
                             "WHERE i.kind = 'todo' AND i.pending <> 3 AND c.visible = 1 AND (? = 0 OR i.collection_id = ?) AND (? OR i.completed = 0) "
                             "ORDER BY i.completed, i.due = 0, i.due, CASE i.priority WHEN 0 THEN 10 ELSE i.priority END, i.title COLLATE NOCASE")
                  .arg(itemColumns));
    q.addBindValue(collectionId);
    q.addBindValue(collectionId);
    q.addBindValue(includeCompleted);
    return itemList(q);
}

QList<DavItem> contacts(const QString &search, qint64 collectionId)
{
    QSqlQuery q(Database::connection());
    const QString like = QLatin1Char('%') + likeEscape(search) + QLatin1Char('%');
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_collections c ON c.id = i.collection_id "
                             "WHERE i.kind = 'contact' AND i.pending <> 3 AND c.visible = 1 AND (? = 0 OR i.collection_id = ?) AND "
                             "(? = '' OR i.title LIKE ? ESCAPE '\\' OR i.subtitle LIKE ? ESCAPE '\\' OR EXISTS "
                             "(SELECT 1 FROM dav_item_emails e WHERE e.item_id = i.id AND e.email LIKE ? ESCAPE '\\')) "
                             "ORDER BY i.title COLLATE NOCASE").arg(itemColumns + QLatin1String(", (SELECT group_concat(email, ' ') FROM dav_item_emails e WHERE e.item_id = i.id)")));
    q.addBindValue(collectionId);
    q.addBindValue(collectionId);
    q.addBindValue(nonNull(search));
    q.addBindValue(like);
    q.addBindValue(like);
    q.addBindValue(like.toLower());
    QList<DavItem> result;
    if (!q.exec())
        return result;
    while (q.next()) {
        DavItem i = itemFrom(q);
        i.emails = q.value(16).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        result << i;
    }
    return result;
}

DavItem contactByEmail(const QString &email)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM dav_items i JOIN dav_item_emails e ON e.item_id = i.id "
                             "WHERE e.email = ? AND i.kind = 'contact' AND i.pending <> 3 LIMIT 1").arg(itemColumns));
    q.addBindValue(email.toLower());
    q.exec();
    return q.next() ? itemFrom(q) : DavItem();
}

QStringList completeAddress(const QString &prefix, int limit)
{
    QStringList result;
    if (prefix.size() < 2)
        return result;
    QSqlQuery q(Database::connection());
    const QString start = likeEscape(prefix) + QLatin1Char('%');
    // Treffer am Wortanfang des Namens zählen ebenso wie am Anfang der Adresse.
    q.prepare(QStringLiteral("SELECT DISTINCT i.title, e.email FROM dav_item_emails e JOIN dav_items i ON i.id = e.item_id "
                             "WHERE i.kind = 'contact' AND i.pending <> 3 AND (e.email LIKE ? ESCAPE '\\' OR i.title LIKE ? ESCAPE '\\' "
                             "OR i.title LIKE ? ESCAPE '\\') ORDER BY i.title COLLATE NOCASE LIMIT ?"));
    q.addBindValue(start.toLower());
    q.addBindValue(start);
    q.addBindValue(QString(QLatin1String("% ") + start));
    q.addBindValue(limit);
    q.exec();
    while (q.next()) {
        const QString name = q.value(0).toString();
        const QString email = q.value(1).toString();
        result << (name.isEmpty() ? email : QStringLiteral("%1 <%2>").arg(name, email));
    }
    return result;
}

} // namespace Fjordpost::DavStore

#include "messagelistmodel.h"

#include "storage/database.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcMessageList, "fjordpost.messagelist")

namespace Fjordpost {

namespace {
constexpr int pageSize = 200;
}

MessageListModel::MessageListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void MessageListModel::setFilter(const Filter &filter)
{
    m_filter = filter;
    m_expandedThreads.clear();
    reload();
}

void MessageListModel::reload()
{
    beginResetModel();
    const QList<Row> top = fetchPage(0, pageSize);
    m_topLevelCount = int(top.size());
    m_hasMore = top.size() == pageSize;
    m_rows = withChildren(top);
    endResetModel();
    Q_EMIT countChanged();
}

void MessageListModel::refresh()
{
    const int limit = std::max(m_topLevelCount, pageSize);
    const QList<Row> top = fetchPage(0, limit);
    m_topLevelCount = int(top.size());
    const QList<Row> fresh = withChildren(top);

    // Zeilen, die verschwunden sind, entfernen (von hinten nach vorn).
    QSet<qint64> freshIds;
    for (const Row &r : fresh)
        freshIds.insert(r.id);
    for (int i = int(m_rows.size()) - 1; i >= 0; --i) {
        if (!freshIds.contains(m_rows.at(i).id)) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }

    // Neue Zeilen an der richtigen Position einfügen, vorhandene aktualisieren.
    for (int i = 0; i < fresh.size(); ++i) {
        const Row &r = fresh.at(i);
        if (i < m_rows.size() && m_rows.at(i).id == r.id) {
            Row &old = m_rows[i];
            if (!(old == r)) {
                old = r;
                Q_EMIT dataChanged(index(i), index(i));
            }
            continue;
        }
        const int existing = rowOf(r.id);
        if (existing >= 0) {
            // Reihenfolge geändert (z. B. Datum): verschieben
            beginMoveRows({}, existing, existing, {}, existing < i ? i + 1 : i);
            m_rows.move(existing, i);
            endMoveRows();
            m_rows[i] = r;
            Q_EMIT dataChanged(index(i), index(i));
        } else {
            beginInsertRows({}, i, i);
            m_rows.insert(i, r);
            endInsertRows();
        }
    }
    m_hasMore = top.size() == limit;
    Q_EMIT countChanged();
}

void MessageListModel::toggleThread(qint64 messageId)
{
    const int row = rowOf(messageId);
    if (row < 0 || m_rows.at(row).threadCount < 2)
        return;
    const qint64 thread = m_rows.at(row).threadId;
    if (!m_expandedThreads.remove(thread))
        m_expandedThreads.insert(thread);
    refresh();
    // Die Zeile selbst ändert nur ihr Symbol
    const int now = rowOf(messageId);
    if (now >= 0)
        Q_EMIT dataChanged(index(now), index(now), {ExpandedRole});
}

MessageListModel::Row MessageListModel::rowFromQuery(const QSqlQuery &q)
{
    Row r;
    r.id = q.value(0).toLongLong();
    r.subject = q.value(1).toString();
    r.fromName = q.value(2).toString();
    r.fromEmail = q.value(3).toString();
    r.date = QDateTime::fromSecsSinceEpoch(q.value(4).toLongLong());
    r.groupDate = r.date;
    r.snippet = q.value(5).toString();
    r.flags = q.value(6).toInt();
    r.hasAttachments = q.value(7).toBool();
    r.accountColor = QColor(q.value(8).toString());
    r.folderName = q.value(9).toString();
    r.threadId = q.value(10).toLongLong();
    r.sent = q.value(11).toString() == QLatin1String(FolderRole::Sent);
    r.threadCount = std::max(1, q.value(12).toInt());
    r.threadUnread = q.value(13).toInt();
    return r;
}

QList<MessageListModel::Row> MessageListModel::withChildren(const QList<Row> &topLevel) const
{
    if (m_expandedThreads.isEmpty() || !m_filter.threaded)
        return topLevel;

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT m.id, m.subject, m.from_name, m.from_email, m.date, m.snippet, m.flags, m.has_attachments, a.color, "
                             "f.name, m.thread_id, f.role, 1, (m.flags & %1) = 0 FROM messages m JOIN folders f ON f.id = m.folder_id "
                             "JOIN accounts a ON a.id = m.account_id WHERE m.id = ?")
                  .arg(MessageFlag::Seen));
    QList<Row> result;
    for (const Row &top : topLevel) {
        result << top;
        if (top.threadCount < 2 || !m_expandedThreads.contains(top.threadId))
            continue;
        // Übrige Nachrichten der Konversation, neueste zuerst
        QList<qint64> ids = MailStore::threadMessages(top.id);
        std::reverse(ids.begin(), ids.end());
        for (qint64 id : std::as_const(ids)) {
            if (id == top.id)
                continue;
            q.addBindValue(id);
            if (!q.exec() || !q.next())
                continue;
            Row child = rowFromQuery(q);
            child.child = true;
            child.groupDate = top.groupDate;
            result << child;
        }
    }
    return result;
}

QString MessageListModel::ftsQuery(const QString &text)
{
    // Jedes Wort als Präfixsuche, Sonderzeichen entfernen, damit die
    // FTS5-Syntax nicht durch Benutzereingaben gebrochen wird.
    static const QRegularExpression nonWord(QStringLiteral("[^\\w@.\\-]+"), QRegularExpression::UseUnicodePropertiesOption);
    QStringList terms;
    const auto words = text.split(nonWord, Qt::SkipEmptyParts);
    for (const QString &w : words)
        terms << QLatin1Char('"') + QString(w).remove(QLatin1Char('"')) + QStringLiteral("\"*");
    return terms.join(QLatin1Char(' '));
}

QList<MessageListModel::Row> MessageListModel::fetchPage(int offset, int limit) const
{
    QStringList where;
    QVariantList binds;

    const QString search = ftsQuery(m_filter.search);
    if (!search.isEmpty()) {
        where << QStringLiteral("m.id IN (SELECT rowid FROM messages_fts WHERE messages_fts MATCH ?)");
        binds << search;
        // Papierkorb und Spam aus der Suche heraushalten
        where << QStringLiteral("f.role NOT IN ('trash', 'junk')");
    } else if (m_filter.folderId > 0) {
        where << QStringLiteral("m.folder_id = ?");
        binds << m_filter.folderId;
    } else if (m_filter.role == QLatin1String(FolderRole::Flagged)) {
        where << QStringLiteral("(m.flags & %1) != 0").arg(MessageFlag::Flagged);
        where << QStringLiteral("f.role NOT IN ('trash', 'junk')");
    } else {
        where << QStringLiteral("f.role = ?");
        binds << (m_filter.role.isEmpty() ? QString::fromLatin1(FolderRole::Inbox) : m_filter.role);
    }
    if (m_filter.accountId > 0) {
        where << QStringLiteral("m.account_id = ?");
        binds << m_filter.accountId;
    }
    where << QStringLiteral("(m.flags & %1) = 0").arg(MessageFlag::Deleted);
    if (m_filter.quick == QLatin1String("unread"))
        where << QStringLiteral("(m.flags & %1) = 0").arg(MessageFlag::Seen);
    else if (m_filter.quick == QLatin1String("flagged"))
        where << QStringLiteral("(m.flags & %1) != 0").arg(MessageFlag::Flagged);
    else if (m_filter.quick == QLatin1String("attachments"))
        where << QStringLiteral("m.has_attachments = 1");
    const QString condition = where.join(QStringLiteral(" AND "));

    const QString columns = QStringLiteral("m.id, m.subject, m.from_name, m.from_email, m.date, m.snippet, m.flags, m.has_attachments, a.color, f.name, "
                                           "m.thread_id, f.role");
    QString sql;
    if (!m_filter.threaded) {
        sql = QStringLiteral("SELECT %1, 1, (m.flags & %2) = 0 FROM messages m JOIN folders f ON f.id = m.folder_id "
                             "JOIN accounts a ON a.id = m.account_id WHERE %3 ORDER BY m.date DESC")
                  .arg(columns)
                  .arg(MessageFlag::Seen)
                  .arg(condition);
    } else {
        // Pro Konversation die neueste passende Nachricht; Anzahl und Ungelesene zählen
        // die ganze Konversation (ohne Papierkorb/Spam, außer man ist dort).
        const QString threadScope = QStringLiteral(
                                        "FROM messages t JOIN folders tf ON tf.id = t.folder_id WHERE t.thread_id = m.thread_id "
                                        "AND t.account_id = m.account_id AND (t.flags & %1) = 0 AND (tf.role NOT IN ('trash', 'junk') OR tf.role = f.role)")
                                        .arg(MessageFlag::Deleted);
        sql = QStringLiteral(
                  "WITH matching AS (SELECT m.id, m.thread_id, m.date FROM messages m JOIN folders f ON f.id = m.folder_id WHERE %1), "
                  "latest AS (SELECT id FROM (SELECT id, ROW_NUMBER() OVER (PARTITION BY thread_id ORDER BY date DESC, id DESC) AS rn "
                  "FROM matching) WHERE rn = 1) "
                  "SELECT %2, (SELECT COUNT(*) %3), (SELECT COUNT(*) %3 AND (t.flags & %4) = 0) "
                  "FROM latest l JOIN messages m ON m.id = l.id JOIN folders f ON f.id = m.folder_id "
                  "JOIN accounts a ON a.id = m.account_id ORDER BY m.date DESC")
                  .arg(condition, columns, threadScope)
                  .arg(MessageFlag::Seen);
    }
    sql += QStringLiteral(" LIMIT %1 OFFSET %2").arg(limit).arg(offset);

    QSqlQuery q(Database::connection());
    q.prepare(sql);
    for (const QVariant &v : std::as_const(binds))
        q.addBindValue(v);
    if (!q.exec()) {
        qCWarning(lcMessageList) << q.lastError().text() << sql;
        return {};
    }

    QList<Row> rows;
    while (q.next())
        rows << rowFromQuery(q);
    return rows;
}

bool MessageListModel::canFetchMore(const QModelIndex &parent) const
{
    return !parent.isValid() && m_hasMore;
}

void MessageListModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid() || !m_hasMore)
        return;
    const QList<Row> top = fetchPage(m_topLevelCount, pageSize);
    m_hasMore = top.size() == pageSize;
    if (top.isEmpty())
        return;
    m_topLevelCount += int(top.size());
    const QList<Row> more = withChildren(top);
    beginInsertRows({}, int(m_rows.size()), int(m_rows.size() + more.size() - 1));
    m_rows << more;
    endInsertRows();
    Q_EMIT countChanged();
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QString MessageListModel::formatDate(const QDateTime &date)
{
    const QDate today = QDate::currentDate();
    const QDate d = date.date();
    const QLocale locale;
    if (d == today)
        return locale.toString(date.time(), QLocale::ShortFormat);
    if (d == today.addDays(-1))
        return i18n("Yesterday");
    if (d.daysTo(today) < 7)
        return locale.dayName(d.dayOfWeek(), QLocale::ShortFormat);
    if (d.year() == today.year())
        return locale.toString(d, QStringLiteral("d. MMM"));
    return locale.toString(d, QLocale::ShortFormat);
}

QString MessageListModel::dateGroup(const QDateTime &date)
{
    const QDate today = QDate::currentDate();
    const qint64 days = date.date().daysTo(today);
    if (days <= 0)
        return i18n("Today");
    if (days == 1)
        return i18n("Yesterday");
    if (days < 7)
        return i18n("This Week");
    if (date.date().year() == today.year() && date.date().month() == today.month())
        return i18n("This Month");
    return i18n("Older");
}

qint64 MessageListModel::idAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : 0;
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case MessageIdRole:
        return r.id;
    case Qt::DisplayRole:
    case SubjectRole:
        return r.subject.isEmpty() ? i18n("(No subject)") : r.subject;
    case FromNameRole:
        return r.fromName.isEmpty() ? r.fromEmail : r.fromName;
    case FromEmailRole:
        return r.fromEmail;
    case DateRole:
        return r.date;
    case DateTextRole:
        return formatDate(r.date);
    case SnippetRole:
        return r.snippet;
    case UnreadRole:
        return r.threadUnread > 0;
    case FlaggedRole:
        return (r.flags & MessageFlag::Flagged) != 0;
    case AnsweredRole:
        return (r.flags & MessageFlag::Answered) != 0;
    case HasAttachmentsRole:
        return r.hasAttachments;
    case AccountColorRole:
        return r.accountColor;
    case DateGroupRole:
        return dateGroup(r.groupDate);
    case InitialsRole: {
        const QString name = r.fromName.isEmpty() ? r.fromEmail : r.fromName;
        static const QRegularExpression separators(QStringLiteral("[\\s.@_-]+"));
        const QStringList parts = name.split(separators, Qt::SkipEmptyParts);
        QString initials;
        for (const QString &p : parts) {
            if (p.at(0).isLetter())
                initials += p.at(0).toUpper();
            if (initials.size() == 2)
                break;
        }
        return initials.isEmpty() ? QStringLiteral("?") : initials;
    }
    case FolderNameRole:
        return r.folderName;
    case ThreadCountRole:
        return r.threadCount;
    case ExpandedRole:
        return m_expandedThreads.contains(r.threadId);
    case IsChildRole:
        return r.child;
    case SentRole:
        return r.sent;
    }
    return {};
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        {MessageIdRole, "messageId"},
        {SubjectRole, "subject"},
        {FromNameRole, "fromName"},
        {FromEmailRole, "fromEmail"},
        {DateRole, "date"},
        {DateTextRole, "dateText"},
        {SnippetRole, "snippet"},
        {UnreadRole, "unread"},
        {FlaggedRole, "flagged"},
        {AnsweredRole, "answered"},
        {HasAttachmentsRole, "hasAttachments"},
        {AccountColorRole, "accountColor"},
        {DateGroupRole, "dateGroup"},
        {InitialsRole, "initials"},
        {FolderNameRole, "folderName"},
        {ThreadCountRole, "threadCount"},
        {ExpandedRole, "expanded"},
        {IsChildRole, "isChild"},
        {SentRole, "sent"},
    };
}

int MessageListModel::rowOf(qint64 messageId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == messageId)
            return i;
    }
    return -1;
}

void MessageListModel::updateFlags(qint64 messageId, int flags)
{
    if (m_filter.threaded) {
        refresh();
        return;
    }
    const int row = rowOf(messageId);
    if (row < 0)
        return;
    m_rows[row].flags = flags;
    m_rows[row].threadUnread = (flags & MessageFlag::Seen) ? 0 : 1;
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {UnreadRole, FlaggedRole, AnsweredRole});
}

void MessageListModel::removeMessage(qint64 messageId)
{
    const int row = rowOf(messageId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    if (!m_rows.at(row).child)
        --m_topLevelCount;
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

} // namespace Fjordpost

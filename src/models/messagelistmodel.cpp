#include "messagelistmodel.h"

#include "storage/database.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcMessageList, "mopmail.messagelist")

namespace MopMail {

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
    reload();
}

void MessageListModel::reload()
{
    beginResetModel();
    m_rows = fetchPage(0, pageSize);
    m_hasMore = m_rows.size() == pageSize;
    endResetModel();
    Q_EMIT countChanged();
}

void MessageListModel::refresh()
{
    const int limit = std::max(int(m_rows.size()), pageSize);
    const QList<Row> fresh = fetchPage(0, limit);

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
            if (old.flags != r.flags || old.snippet != r.snippet || old.subject != r.subject || old.hasAttachments != r.hasAttachments) {
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
    m_hasMore = fresh.size() == limit;
    Q_EMIT countChanged();
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
    QString sql = QStringLiteral(
        "SELECT m.id, m.subject, m.from_name, m.from_email, m.date, m.snippet, m.flags, m.has_attachments, a.color, f.name "
        "FROM messages m JOIN folders f ON f.id = m.folder_id JOIN accounts a ON a.id = m.account_id ");
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
    sql += QStringLiteral("WHERE ") + where.join(QStringLiteral(" AND "));
    sql += QStringLiteral(" ORDER BY m.date DESC LIMIT %1 OFFSET %2").arg(limit).arg(offset);

    QSqlQuery q(Database::connection());
    q.prepare(sql);
    for (const QVariant &v : std::as_const(binds))
        q.addBindValue(v);
    if (!q.exec()) {
        qCWarning(lcMessageList) << q.lastError().text() << sql;
        return {};
    }

    QList<Row> rows;
    while (q.next()) {
        Row r;
        r.id = q.value(0).toLongLong();
        r.subject = q.value(1).toString();
        r.fromName = q.value(2).toString();
        r.fromEmail = q.value(3).toString();
        r.date = QDateTime::fromSecsSinceEpoch(q.value(4).toLongLong());
        r.snippet = q.value(5).toString();
        r.flags = q.value(6).toInt();
        r.hasAttachments = q.value(7).toBool();
        r.accountColor = QColor(q.value(8).toString());
        r.folderName = q.value(9).toString();
        rows << r;
    }
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
    const QList<Row> more = fetchPage(int(m_rows.size()), pageSize);
    m_hasMore = more.size() == pageSize;
    if (more.isEmpty())
        return;
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
        return (r.flags & MessageFlag::Seen) == 0;
    case FlaggedRole:
        return (r.flags & MessageFlag::Flagged) != 0;
    case AnsweredRole:
        return (r.flags & MessageFlag::Answered) != 0;
    case HasAttachmentsRole:
        return r.hasAttachments;
    case AccountColorRole:
        return r.accountColor;
    case DateGroupRole:
        return dateGroup(r.date);
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
    const int row = rowOf(messageId);
    if (row < 0)
        return;
    m_rows[row].flags = flags;
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx, {UnreadRole, FlaggedRole, AnsweredRole});
}

void MessageListModel::removeMessage(qint64 messageId)
{
    const int row = rowOf(messageId);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();
}

} // namespace MopMail

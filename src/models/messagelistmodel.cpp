#include "messagelistmodel.h"

#include "storage/database.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
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
        "SELECT m.id, m.subject, m.from_name, m.from_email, m.date, m.snippet, m.flags, m.has_attachments, a.color "
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

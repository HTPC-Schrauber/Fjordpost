#include "foldermodel.h"

#include "storage/database.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QSqlQuery>

namespace MopMail {

QString iconForFolderRole(const QString &role)
{
    if (role == QLatin1String(FolderRole::Inbox))
        return QStringLiteral("mail-folder-inbox");
    if (role == QLatin1String(FolderRole::Sent))
        return QStringLiteral("mail-folder-sent");
    if (role == QLatin1String(FolderRole::Drafts))
        return QStringLiteral("document-edit");
    if (role == QLatin1String(FolderRole::Trash))
        return QStringLiteral("user-trash");
    if (role == QLatin1String(FolderRole::Junk))
        return QStringLiteral("mail-mark-junk");
    if (role == QLatin1String(FolderRole::Archive))
        return QStringLiteral("archive-insert");
    if (role == QLatin1String(FolderRole::Flagged))
        return QStringLiteral("flag");
    return QStringLiteral("folder-mail");
}

FolderModel::FolderModel(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

void FolderModel::setAccountId(qint64 id)
{
    if (m_accountId == id)
        return;
    m_accountId = id;
    reload();
    Q_EMIT accountIdChanged();
}

void FolderModel::reload()
{
    beginResetModel();
    m_entries.clear();
    QSqlQuery q(Database::connection());

    if (m_accountId == 0) {
        // Sammelordner über alle Konten
        const QList<QPair<QString, QString>> virtualFolders = {
            {QLatin1String(FolderRole::Inbox), i18n("All Inboxes")},
            {QLatin1String(FolderRole::Flagged), i18n("Flagged")},
            {QLatin1String(FolderRole::Drafts), i18n("Drafts")},
            {QLatin1String(FolderRole::Sent), i18n("Sent")},
            {QLatin1String(FolderRole::Archive), i18n("Archive")},
            {QLatin1String(FolderRole::Junk), i18n("Spam")},
            {QLatin1String(FolderRole::Trash), i18n("Trash")},
        };
        for (const auto &[role, name] : virtualFolders) {
            Entry e;
            e.role = role;
            e.name = name;
            if (role == QLatin1String(FolderRole::Inbox)) {
                q.prepare(QStringLiteral("SELECT COUNT(*) FROM messages m JOIN folders f ON f.id = m.folder_id "
                                         "WHERE f.role = 'inbox' AND (m.flags & ?) = 0"));
                q.addBindValue(int(MessageFlag::Seen));
                q.exec();
                e.unread = q.next() ? q.value(0).toInt() : 0;
            }
            m_entries << e;
        }
    } else {
        q.prepare(QStringLiteral(
            "SELECT f.id, f.name, f.role, "
            "(SELECT COUNT(*) FROM messages m WHERE m.folder_id = f.id AND (m.flags & ?) = 0) "
            "FROM folders f WHERE f.account_id = ? ORDER BY f.sort_order, f.name"));
        q.addBindValue(int(MessageFlag::Seen));
        q.addBindValue(m_accountId);
        q.exec();
        while (q.next()) {
            Entry e;
            e.id = q.value(0).toLongLong();
            e.name = q.value(1).toString();
            e.role = q.value(2).toString();
            e.unread = q.value(3).toInt();
            m_entries << e;
        }
    }
    endResetModel();
}

int FolderModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_entries.size());
}

QVariant FolderModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case FolderIdRole:
        return e.id;
    case FolderRoleRole:
        return e.role;
    case IconNameRole:
        return iconForFolderRole(e.role);
    case UnreadCountRole:
        return e.unread;
    }
    return {};
}

QHash<int, QByteArray> FolderModel::roleNames() const
{
    return {
        {FolderIdRole, "folderId"},
        {NameRole, "name"},
        {FolderRoleRole, "folderRole"},
        {IconNameRole, "iconName"},
        {UnreadCountRole, "unreadCount"},
    };
}

} // namespace MopMail

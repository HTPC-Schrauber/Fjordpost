#include "accountmodel.h"

#include "core/accountmanager.h"
#include "storage/database.h"
#include "storage/messageflags.h"

#include <QSqlQuery>

namespace Fjordpost {

AccountModel::AccountModel(AccountManager *manager, QObject *parent)
    : QAbstractListModel(parent)
    , m_manager(manager)
{
    connect(manager, &AccountManager::accountAdded, this, &AccountModel::reload);
    connect(manager, &AccountManager::accountChanged, this, &AccountModel::reload);
    connect(manager, &AccountManager::accountRemoved, this, &AccountModel::reload);
    reload();
}

void AccountModel::reload()
{
    beginResetModel();
    m_accounts = m_manager->accounts();
    m_unread.clear();

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "SELECT m.account_id, COUNT(*) FROM messages m JOIN folders f ON f.id = m.folder_id "
        "WHERE f.role = 'inbox' AND (m.flags & ?) = 0 GROUP BY m.account_id"));
    q.addBindValue(int(MessageFlag::Seen));
    q.exec();
    while (q.next())
        m_unread.insert(q.value(0).toLongLong(), q.value(1).toInt());
    endResetModel();
}

int AccountModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_accounts.size());
}

QVariant AccountModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid))
        return {};
    const Account &a = m_accounts.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return a.name;
    case AccountIdRole:
        return a.id;
    case EmailRole:
        return a.email;
    case ColorRole:
        return a.color;
    case InitialsRole:
        return a.name.left(1).toUpper();
    case UnreadCountRole:
        return m_unread.value(a.id);
    }
    return {};
}

QHash<int, QByteArray> AccountModel::roleNames() const
{
    return {
        {AccountIdRole, "accountId"},
        {NameRole, "name"},
        {EmailRole, "email"},
        {ColorRole, "accountColor"},
        {InitialsRole, "initials"},
        {UnreadCountRole, "unreadCount"},
    };
}

} // namespace Fjordpost

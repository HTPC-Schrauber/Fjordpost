#pragma once

#include "core/account.h"

#include <QAbstractListModel>

namespace Fjordpost {

class AccountManager;

// Konten für die App-Leiste (Avatare links).
class AccountModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        AccountIdRole = Qt::UserRole + 1,
        NameRole,
        EmailRole,
        ColorRole,
        InitialsRole,
        UnreadCountRole,
    };
    Q_ENUM(Roles)

    explicit AccountModel(AccountManager *manager, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void reload();

private:
    AccountManager *m_manager;
    QList<Account> m_accounts;
    QHash<qint64, int> m_unread;
};

} // namespace Fjordpost

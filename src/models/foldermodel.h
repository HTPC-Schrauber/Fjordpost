#pragma once

#include <QAbstractListModel>

namespace MopMail {

// Ordnerliste für das gewählte Konto. Bei accountId == 0 (Unified Inbox)
// werden virtuelle Sammelordner nach Rolle angezeigt (folderId == 0, role gesetzt).
class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(qint64 accountId READ accountId WRITE setAccountId NOTIFY accountIdChanged)
public:
    enum Roles {
        FolderIdRole = Qt::UserRole + 1,
        NameRole,
        FolderRoleRole,
        IconNameRole,
        UnreadCountRole,
    };

    explicit FolderModel(QObject *parent = nullptr);

    qint64 accountId() const { return m_accountId; }
    void setAccountId(qint64 id);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void reload();

Q_SIGNALS:
    void accountIdChanged();

private:
    struct Entry {
        qint64 id = 0;
        QString name;
        QString role;
        int unread = 0;
    };
    qint64 m_accountId = 0;
    QList<Entry> m_entries;
};

QString iconForFolderRole(const QString &role);

} // namespace MopMail

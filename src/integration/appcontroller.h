#pragma once

#include <QObject>
#include <QVariantMap>

namespace MopMail {

class AccountManager;
class AccountModel;
class FolderModel;
class MessageListModel;

// Zentrale Fassade für die QML-Oberfläche: hält die Auswahl
// (Konto/Ordner/Nachricht) und stellt Aktionen bereit.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *accounts READ accountsObject CONSTANT)
    Q_PROPERTY(QObject *folders READ foldersObject CONSTANT)
    Q_PROPERTY(QObject *messages READ messagesObject CONSTANT)
    Q_PROPERTY(qint64 selectedAccountId READ selectedAccountId NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedFolderId READ selectedFolderId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedRole READ selectedRole NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionTitle READ selectionTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QVariantMap currentMessage READ currentMessage NOTIFY currentMessageChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalUnreadChanged)

public:
    explicit AppController(AccountManager *accounts, QObject *parent = nullptr);

    QObject *accountsObject() const;
    QObject *foldersObject() const;
    QObject *messagesObject() const;

    qint64 selectedAccountId() const { return m_accountId; }
    qint64 selectedFolderId() const { return m_folderId; }
    QString selectedRole() const { return m_role; }
    QString selectionTitle() const { return m_title; }
    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);
    QVariantMap currentMessage() const { return m_current; }
    int totalUnread() const { return m_totalUnread; }

    // 0 = Unified Inbox (alle Konten)
    Q_INVOKABLE void selectAccount(qint64 accountId);
    Q_INVOKABLE void selectFolder(qint64 folderId, const QString &role, const QString &name);
    Q_INVOKABLE void openMessage(qint64 messageId);

    Q_INVOKABLE void setRead(qint64 messageId, bool read);
    Q_INVOKABLE void toggleFlagged(qint64 messageId);
    Q_INVOKABLE void moveToRole(qint64 messageId, const QString &role);
    Q_INVOKABLE void deleteMessage(qint64 messageId);
    Q_INVOKABLE void archiveMessage(qint64 messageId);

Q_SIGNALS:
    void selectionChanged();
    void searchTextChanged();
    void currentMessageChanged();
    void totalUnreadChanged();

private:
    void applyFilter();
    void setFlags(qint64 messageId, int flags);
    int flagsOf(qint64 messageId) const;
    void refreshCounts();

    AccountManager *m_accountManager;
    AccountModel *m_accountModel;
    FolderModel *m_folderModel;
    MessageListModel *m_messageModel;

    qint64 m_accountId = 0;
    qint64 m_folderId = 0;
    QString m_role;
    QString m_title;
    QString m_search;
    QVariantMap m_current;
    int m_totalUnread = 0;
};

} // namespace MopMail

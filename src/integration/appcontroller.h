#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

namespace Fjordpost {

class AccountManager;
class AccountModel;
class AccountSetup;
class FolderModel;
class MailService;
class MessageListModel;
struct OutgoingMessage;

// Zentrale Fassade für die QML-Oberfläche: hält die Auswahl
// (Konto/Ordner/Nachricht) und stellt Aktionen bereit.
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *accounts READ accountsObject CONSTANT)
    Q_PROPERTY(QObject *folders READ foldersObject CONSTANT)
    Q_PROPERTY(QObject *messages READ messagesObject CONSTANT)
    Q_PROPERTY(QObject *accountSetup READ accountSetupObject CONSTANT)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY accountsChanged)
    Q_PROPERTY(qint64 selectedAccountId READ selectedAccountId NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectedFolderId READ selectedFolderId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedRole READ selectedRole NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionTitle READ selectionTitle NOTIFY selectionChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QVariantMap currentMessage READ currentMessage NOTIFY currentMessageChanged)
    Q_PROPERTY(int totalUnread READ totalUnread NOTIFY totalUnreadChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool remoteContentAllowed READ remoteContentAllowed NOTIFY remoteContentAllowedChanged)
    Q_PROPERTY(QString quickFilter READ quickFilter WRITE setQuickFilter NOTIFY quickFilterChanged)
    Q_PROPERTY(QString selectionSubtitle READ selectionSubtitle NOTIFY selectionChanged)
    // Sekunden, die eine Nachricht nach "Senden" im Postausgang wartet (0 = sofort)
    Q_PROPERTY(int undoSendSeconds READ undoSendSeconds WRITE setUndoSendSeconds NOTIFY undoSendSecondsChanged)

public:
    AppController(AccountManager *accounts, MailService *mail, AccountSetup *setup, QObject *parent = nullptr);

    QObject *accountsObject() const;
    QObject *foldersObject() const;
    QObject *messagesObject() const;
    QObject *accountSetupObject() const;

    bool hasAccounts() const;
    qint64 selectedAccountId() const { return m_accountId; }
    qint64 selectedFolderId() const { return m_folderId; }
    QString selectedRole() const { return m_role; }
    QString selectionTitle() const { return m_title; }
    QString searchText() const { return m_search; }
    void setSearchText(const QString &text);
    QVariantMap currentMessage() const { return m_current; }
    int totalUnread() const { return m_totalUnread; }
    bool syncing() const;
    QString lastError() const { return m_lastError; }
    bool remoteContentAllowed() const { return m_remoteAllowed; }
    QString quickFilter() const { return m_quickFilter; }
    void setQuickFilter(const QString &filter);
    QString selectionSubtitle() const;
    int undoSendSeconds() const { return m_undoSendSeconds; }
    void setUndoSendSeconds(int seconds);

    // 0 = Unified Inbox (alle Konten)
    Q_INVOKABLE void selectAccount(qint64 accountId);
    Q_INVOKABLE void selectFolder(qint64 folderId, const QString &role, const QString &name);
    Q_INVOKABLE void openMessage(qint64 messageId);
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void dismissError();

    Q_INVOKABLE void setRead(qint64 messageId, bool read);
    Q_INVOKABLE void toggleFlagged(qint64 messageId);
    Q_INVOKABLE void moveToRole(qint64 messageId, const QString &role);
    Q_INVOKABLE void deleteMessage(qint64 messageId);
    Q_INVOKABLE void archiveMessage(qint64 messageId);
    Q_INVOKABLE void allowRemoteContent();
    Q_INVOKABLE void markAllRead();
    // Kontext-Seitenleiste
    Q_INVOKABLE int messageCountFrom(const QString &email) const;
    Q_INVOKABLE void showMessagesFrom(const QString &email);
    Q_INVOKABLE QString cacheSize() const;
    // Farben für die Darstellung von Klartext-Mails (aus dem QML-Theme).
    Q_INVOKABLE void setReaderColors(const QString &background, const QString &text, const QString &link, const QString &muted);
    // Anwendungsweites Farbschema aus den Theme-Farben; leere Map = Systemfarben.
    Q_INVOKABLE void applyColorScheme(const QVariantMap &colors);

    // Composer
    Q_INVOKABLE QVariantList identities() const;
    // mode: "new", "reply", "replyAll", "forward"
    Q_INVOKABLE QVariantMap composeData(qint64 messageId, const QString &mode) const;
    // Liefert eine Fehlermeldung oder einen leeren String bei Erfolg.
    Q_INVOKABLE QString sendMessage(const QVariantMap &data);
    Q_INVOKABLE QString saveDraft(const QVariantMap &data);
    // Schnellantwort aus dem Lesebereich: Text über dem Zitat der aktuellen Nachricht.
    Q_INVOKABLE QString sendQuickReply(const QString &text);
    // "Senden rückgängig": true, wenn die Nachricht noch nicht unterwegs war.
    Q_INVOKABLE bool cancelQueued(qint64 outboxId);
    Q_INVOKABLE bool setSignature(qint64 accountId, const QString &signature);

    // Anhänge
    Q_INVOKABLE void openAttachment(qint64 messageId, int index);
    Q_INVOKABLE bool saveAttachment(qint64 messageId, int index, const QUrl &target);

Q_SIGNALS:
    void accountsChanged();
    void selectionChanged();
    void searchTextChanged();
    void currentMessageChanged();
    void totalUnreadChanged();
    void syncingChanged();
    void lastErrorChanged();
    void remoteContentAllowedChanged();
    void quickFilterChanged();
    void notify(const QString &message);
    void undoSendSecondsChanged();
    // draft enthält die Composer-Daten, um die Nachricht nach dem Rückgängigmachen
    // wieder zu öffnen.
    void messageQueued(qint64 outboxId, int delaySeconds, const QVariantMap &draft);

private:
    void applyFilter();
    void setFlags(qint64 messageId, int flags, const QString &imapChange);
    void refreshCounts();
    void loadCurrent(qint64 messageId);
    void setError(const QString &message);
    OutgoingMessage outgoingFromMap(const QVariantMap &data) const;
    QString attachmentTempPath(qint64 messageId, int index, const QString &fileName) const;

    AccountManager *m_accountManager;
    MailService *m_mail;
    AccountSetup *m_setup;
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
    QString m_lastError;
    bool m_remoteAllowed = false;
    QString m_quickFilter;
    QStringList m_readerColors;
    int m_undoSendSeconds = 0;
};

} // namespace Fjordpost

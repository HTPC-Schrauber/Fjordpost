#pragma once

#include "core/account.h"
#include "storage/mailstore.h"

#include <QObject>
#include <QPointer>
#include <QTimer>

namespace KIMAP {
class Session;
class IdleJob;
}

namespace Fjordpost {

class ImapConnection;

// Synchronisiert ein IMAP-Konto mit der lokalen Datenbank.
class ImapAccount : public QObject
{
    Q_OBJECT
public:
    explicit ImapAccount(const Account &account, QObject *parent = nullptr);
    ~ImapAccount() override;

    qint64 accountId() const { return m_account.id; }
    void setCredentials(const Account &account, const QString &secret);

    // Kompletter Abgleich: Offline-Operationen, Ordnerliste, Nachrichten.
    void sync();
    // Nur einen Ordner abgleichen (z. B. nach IDLE-Meldung).
    void syncFolder(qint64 folderId);
    // Offline-Operationen sofort zum Server schicken.
    void flushPendingOps();
    // Volltext einer Nachricht laden (für die Anzeige).
    void fetchBody(qint64 messageId);
    // Nachricht in einen Ordner legen (Gesendet, Entwürfe).
    void append(const QString &role, const QByteArray &raw, const QList<QByteArray> &flags);

    void startIdle();
    void shutdown();
    // Laufende oder wartende Aufgaben auf der interaktiven Verbindung (z. B. APPEND)
    bool isInteractiveBusy() const;

    bool isSyncing() const { return m_syncing; }

    // Grenzwerte, öffentlich für Tests.
    static constexpr int initialHeaderLimit = 1000;
    static constexpr int prefetchCount = 30;
    static constexpr qint64 prefetchMaxSize = 512 * 1024;

Q_SIGNALS:
    void syncingChanged(bool syncing);
    void errorOccurred(const QString &message);
    void foldersChanged();
    void folderUpdated(qint64 folderId);
    void newMessages(const QList<qint64> &messageIds);
    void bodyFetched(qint64 messageId, bool ok);

private:
    using Done = std::function<void()>;

    void setSyncing(bool syncing);
    void listFolders(KIMAP::Session *session, Done done);
    void syncFolders(KIMAP::Session *session, QList<FolderState> folders, Done done);
    void syncOneFolder(KIMAP::Session *session, const FolderState &folder, Done done);
    void fetchNewHeaders(KIMAP::Session *session, FolderState folder, int exists, bool firstSync, Done done);
    void syncFlags(KIMAP::Session *session, const FolderState &folder, Done done);
    void prefetchBodies(KIMAP::Session *session, const FolderState &folder, Done done);
    void fetchBodies(KIMAP::Session *session, const QString &mailbox, const QHash<qint64, qint64> &uidToMessage, Done done);
    void runPendingOps(KIMAP::Session *session, Done done);
    void runOp(KIMAP::Session *session, const PendingOp &op, std::function<void(bool keep)> done);
    void selectMailbox(ImapConnection *connection, KIMAP::Session *session, const QString &mailbox, std::function<void(bool)> done);

    Account m_account;
    ImapConnection *m_sync;
    ImapConnection *m_interactive;
    QPointer<KIMAP::Session> m_idleSession;
    QPointer<KIMAP::IdleJob> m_idleJob;
    QString m_secret;
    QTimer m_idleRetry;
    bool m_syncing = false;
    bool m_syncQueued = false;
    bool m_opsQueued = false;
    bool m_shuttingDown = false;
};

} // namespace Fjordpost

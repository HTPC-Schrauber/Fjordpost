#pragma once

#include "core/account.h"
#include "pop3client.h"

#include <QObject>
#include <QPointer>

namespace Fjordpost {

// Ruft ein POP3-Konto ab. Ordner gibt es nur lokal; Nachrichten haben uid 0 und
// liegen vollständig im BlobStore, Offline-Operationen entfallen dadurch.
class Pop3Account : public QObject
{
    Q_OBJECT
public:
    explicit Pop3Account(const Account &account, QObject *parent = nullptr);
    ~Pop3Account() override;

    qint64 accountId() const { return m_account.id; }
    void setCredentials(const Account &account, const QString &secret);
    // Legt die lokalen Ordner an (auch vor dem ersten Abruf nutzbar).
    void ensureFolders();
    void sync();
    void shutdown();
    bool isSyncing() const { return m_client != nullptr; }

Q_SIGNALS:
    void syncingChanged(bool syncing);
    void errorOccurred(const QString &message);
    void foldersChanged();
    void folderUpdated(qint64 folderId);
    // notify = false beim ersten Abruf: ein volles Postfach ist keine neue Post.
    void newMessages(const QList<qint64> &messageIds, bool notify);

private:
    void onListed(const QList<Pop3Entry> &entries);
    void retrieveNext();
    void onRetrieved(int number, const QByteArray &raw);
    void finish();
    void reportNew();

    Account m_account;
    QString m_secret;
    QPointer<Pop3Client> m_client;
    QList<Pop3Entry> m_queue;
    QByteArray m_currentUidl;
    QList<qint64> m_new;
    qint64 m_inboxId = 0;
    bool m_firstSync = false;
};

} // namespace Fjordpost

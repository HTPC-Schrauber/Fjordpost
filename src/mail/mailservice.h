#pragma once

#include "core/account.h"
#include "mail/mime/messagebuilder.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

namespace Fjordpost {

class AccountManager;
class ImapAccount;
class OAuthTokens;
class Pop3Account;
class SecretStore;
class SmtpSender;

// Koordiniert Abgleich und Versand für alle Konten. Einziger Ansprechpartner
// der Oberfläche für alles, was mit Servern zu tun hat.
class MailService : public QObject
{
    Q_OBJECT
public:
    MailService(AccountManager *accounts, SecretStore *secrets, QObject *parent = nullptr);
    ~MailService() override;

    // Ohne Tokens (Tests) können OAuth-Konten keine Verbindung aufbauen.
    void setTokens(OAuthTokens *tokens) { m_tokens = tokens; }

    // Lädt die Zugangsdaten aller Konten und startet Abgleich + IDLE.
    void start();

    void syncAll();
    void syncAccount(qint64 accountId);
    void syncFolder(qint64 folderId);
    void fetchBody(qint64 messageId);
    // Nach lokalen Änderungen (Flags, Verschieben) aufrufen.
    void flushPendingOps(qint64 accountId);

    // Legt die Nachricht in den Postausgang; Versand nach delaySeconds
    // (für "Senden rückgängig"). Liefert bei Fehlern eine Meldung.
    QString queueMessage(qint64 accountId, const OutgoingMessage &message, qint64 referenceMessage = 0,
                         const QString &referenceKind = {}, int delaySeconds = 0, qint64 *outboxId = nullptr);
    // Nimmt eine noch nicht versendete Nachricht aus dem Postausgang.
    // false, wenn sie schon gesendet wird oder wurde.
    bool cancelQueued(qint64 outboxId);
    // Beim Beenden: Nachrichten, die nur noch für "Senden rückgängig" warten,
    // sofort senden. Liefert true, solange noch Versand oder Ablage läuft.
    bool sendDelayedNow();
    bool hasPendingWork() const;
    // Entwurf auf dem Server ablegen (IMAP) bzw. lokal (POP3).
    QString saveDraft(qint64 accountId, const OutgoingMessage &message);
    // Für den Import: IMAP lädt auf den Server hoch, POP3 speichert lokal. done läuft
    // auch bei fehlgeschlagener Anmeldung, solange context lebt.
    bool canStoreMessages(qint64 accountId) const;
    void storeMessage(qint64 accountId, qint64 folderId, const QByteArray &raw, int flags, QObject *context, std::function<void(bool ok)> done);

    bool isSyncing() const { return !m_syncing.isEmpty(); }

    static constexpr int syncIntervalMinutes = 10;

Q_SIGNALS:
    void syncingChanged();
    void errorOccurred(qint64 accountId, const QString &message);
    void foldersChanged(qint64 accountId);
    void folderUpdated(qint64 folderId);
    void newMessages(const QList<qint64> &messageIds);
    void bodyFetched(qint64 messageId, bool ok);
    void messageSent(qint64 accountId, const QString &subject);
    void outboxChanged();

private:
    void addAccount(const Account &account);
    void addImapAccount(const Account &account);
    void addPop3Account(const Account &account);
    template<typename T>
    void connectAccount(T *mailbox, qint64 id);
    void dropAccount(qint64 accountId);
    // Filterregeln anwenden, dann für das Übrige benachrichtigen.
    void handleArrived(qint64 accountId, const QList<qint64> &messageIds, bool notify);
    void wakeSnoozed();
    void markWoken(const QList<qint64> &messageIds);
    void removeAccount(qint64 accountId, const QString &secretKey);
    void updateAccount(const Account &account);
    void processOutbox();
    void afterSent(qint64 outboxId, qint64 accountId, const QByteArray &raw, qint64 referenceMessage, const QString &referenceKind);

    AccountManager *m_accounts;
    SecretStore *m_secrets;
    OAuthTokens *m_tokens = nullptr;
    SmtpSender *m_smtp;
    QHash<qint64, ImapAccount *> m_imap;
    QHash<qint64, Pop3Account *> m_pop3;
    QHash<qint64, QString> m_secretCache;
    QSet<qint64> m_syncing;
    QSet<qint64> m_sendingOutbox;
    QTimer m_syncTimer;
    QTimer m_outboxTimer;
    QTimer m_snoozeTimer;
};

} // namespace Fjordpost

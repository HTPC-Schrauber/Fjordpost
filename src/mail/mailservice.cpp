#include "mailservice.h"

#include "auth/oauthtokens.h"
#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "mail/filterengine.h"
#include "mail/imap/imapaccount.h"
#include "mail/pop3/pop3account.h"
#include "mail/smtp/smtpsender.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/filterstore.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <KMime/Util>
#include <QDateTime>

#include <algorithm>
#include <QLoggingCategory>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcService, "fjordpost.service")

namespace Fjordpost {

MailService::MailService(AccountManager *accounts, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_accounts(accounts)
    , m_secrets(secrets)
    , m_smtp(new SmtpSender(this))
{
    connect(accounts, &AccountManager::accountAdded, this, &MailService::addAccount);
    connect(accounts, &AccountManager::accountChanged, this, &MailService::updateAccount);
    connect(accounts, &AccountManager::accountRemoved, this, &MailService::removeAccount);

    m_syncTimer.setInterval(syncIntervalMinutes * 60 * 1000);
    connect(&m_syncTimer, &QTimer::timeout, this, &MailService::syncAll);

    // Postausgang regelmäßig prüfen (verzögertes Senden, Wiederholungen).
    m_outboxTimer.setInterval(15 * 1000);
    connect(&m_outboxTimer, &QTimer::timeout, this, &MailService::processOutbox);

    // Snooze ist rein lokal und läuft auch ohne Serververbindung.
    m_snoozeTimer.setInterval(30 * 1000);
    connect(&m_snoozeTimer, &QTimer::timeout, this, &MailService::wakeSnoozed);
    m_snoozeTimer.start();
    QTimer::singleShot(0, this, &MailService::wakeSnoozed);
}

MailService::~MailService()
{
    for (ImapAccount *imap : std::as_const(m_imap))
        imap->shutdown();
    for (Pop3Account *pop3 : std::as_const(m_pop3))
        pop3->shutdown();
}

void MailService::start()
{
    const auto accounts = m_accounts->accounts();
    for (const Account &a : accounts)
        addAccount(a);
    m_syncTimer.start();
    m_outboxTimer.start();
}

void MailService::addAccount(const Account &account)
{
    if (!account.enabled || m_imap.contains(account.id) || m_pop3.contains(account.id))
        return;
    if (account.type == AccountType::Pop3)
        addPop3Account(account);
    else
        addImapAccount(account);
}

template<typename T>
void MailService::connectAccount(T *mailbox, qint64 id)
{
    connect(mailbox, &T::syncingChanged, this, [this, id](bool syncing) {
        if (syncing)
            m_syncing.insert(id);
        else
            m_syncing.remove(id);
        Q_EMIT syncingChanged();
    });
    connect(mailbox, &T::errorOccurred, this, [this, id](const QString &message) {
        Q_EMIT errorOccurred(id, message);
    });
    connect(mailbox, &T::foldersChanged, this, [this, id] {
        Q_EMIT foldersChanged(id);
    });
    connect(mailbox, &T::folderUpdated, this, &MailService::folderUpdated);
}

void MailService::addImapAccount(const Account &account)
{
    auto *imap = new ImapAccount(account, this);
    m_imap.insert(account.id, imap);
    const qint64 id = account.id;

    connectAccount(imap, id);
    connect(imap, &ImapAccount::newMessages, this, [this, id](const QList<qint64> &ids) {
        handleArrived(id, ids, true);
    });
    connect(imap, &ImapAccount::bodyFetched, this, &MailService::bodyFetched);

    if (account.authMethod == AuthMethod::OAuth2) {
        if (!m_tokens) {
            Q_EMIT errorOccurred(id, i18n("Sign-in through the browser is not available."));
            return;
        }
        const QString key = account.secretKey();
        // Ein abgelehntes Token nicht weiterverwenden; das nächste wird frisch geholt.
        connect(imap, &ImapAccount::errorOccurred, this, [this, key] {
            m_tokens->invalidate(key);
        });
        imap->setTokenSource(m_tokens->fetcher(key, account.oauthProvider));
        // Platzhalter: Das eigentliche Token kommt vor jeder Anmeldung aus der Token-Quelle.
        imap->setCredentials(account, QStringLiteral("oauth2"));
        imap->sync();
        imap->startIdle();
        return;
    }

    m_secrets->read(account.secretKey(), [this, id](bool ok, const QString &secret) {
        ImapAccount *imap = m_imap.value(id);
        if (!imap)
            return;
        if (!ok || secret.isEmpty()) {
            Q_EMIT errorOccurred(id, i18n("No password stored for this account."));
            return;
        }
        m_secretCache.insert(id, secret);
        imap->setCredentials(m_accounts->account(id), secret);
        imap->sync();
        imap->startIdle();
    });
}

void MailService::addPop3Account(const Account &account)
{
    auto *pop3 = new Pop3Account(account, this);
    m_pop3.insert(account.id, pop3);
    const qint64 id = account.id;

    connectAccount(pop3, id);
    connect(pop3, &Pop3Account::newMessages, this, [this, id](const QList<qint64> &ids, bool notify) {
        handleArrived(id, ids, notify);
    });
    pop3->ensureFolders();

    m_secrets->read(account.secretKey(), [this, id](bool ok, const QString &secret) {
        Pop3Account *pop3 = m_pop3.value(id);
        if (!pop3)
            return;
        if (!ok || secret.isEmpty()) {
            Q_EMIT errorOccurred(id, i18n("No password stored for this account."));
            return;
        }
        m_secretCache.insert(id, secret);
        pop3->setCredentials(m_accounts->account(id), secret);
        pop3->sync();
    });
}

void MailService::dropAccount(qint64 accountId)
{
    if (ImapAccount *imap = m_imap.take(accountId)) {
        imap->shutdown();
        imap->deleteLater();
    }
    if (Pop3Account *pop3 = m_pop3.take(accountId)) {
        pop3->shutdown();
        pop3->deleteLater();
    }
    m_secretCache.remove(accountId);
}

void MailService::removeAccount(qint64 accountId, const QString &secretKey)
{
    dropAccount(accountId);
    FilterStore::removeForAccount(accountId);
    m_syncing.remove(accountId);
    m_secrets->remove(secretKey);
    if (m_tokens)
        m_tokens->forget(secretKey);
    Q_EMIT syncingChanged();
}

void MailService::updateAccount(const Account &account)
{
    // Einfach neu aufbauen: Serverdaten oder Zugangsdaten können sich geändert haben.
    dropAccount(account.id);
    addAccount(account);
}

void MailService::handleArrived(qint64 accountId, const QList<qint64> &messageIds, bool notify)
{
    const FilterResult result = FilterEngine::apply(messageIds, FilterStore::rules());
    flushPendingOps(accountId);
    for (qint64 folder : result.folders)
        Q_EMIT folderUpdated(folder);
    // Beim ersten Abruf kommt alte Post, das ist keine neue Antwort.
    if (notify)
        markWoken(MailStore::wakeRepliedThreads(messageIds, m_accounts->account(accountId).email));
    if (notify && !result.untouched.isEmpty())
        Q_EMIT newMessages(result.untouched);
}

void MailService::wakeSnoozed()
{
    const QList<qint64> ids = MailStore::wakeSnoozed(QDateTime::currentSecsSinceEpoch());
    if (ids.isEmpty())
        return;
    markWoken(ids);
    Q_EMIT newMessages(ids);
}

void MailService::markWoken(const QList<qint64> &messageIds)
{
    // Wie neu angekommen: wieder ungelesen. Das gilt auch auf dem Server.
    QSet<qint64> folders;
    QSet<qint64> accounts;
    for (qint64 id : messageIds) {
        const MessageLocation loc = MailStore::location(id);
        if (loc.id == 0)
            continue;
        folders.insert(loc.folderId);
        if (!(loc.flags & MessageFlag::Seen))
            continue;
        MailStore::updateFlags(id, loc.flags & ~MessageFlag::Seen);
        if (loc.uid > 0) {
            MailStore::enqueueOp(loc.accountId, QStringLiteral("flags"), loc.folderPath, loc.uid, QStringLiteral("-\\Seen"));
            accounts.insert(loc.accountId);
        }
    }
    for (qint64 account : std::as_const(accounts))
        flushPendingOps(account);
    for (qint64 folder : std::as_const(folders))
        Q_EMIT folderUpdated(folder);
}

void MailService::syncAll()
{
    for (ImapAccount *imap : std::as_const(m_imap))
        imap->sync();
    for (Pop3Account *pop3 : std::as_const(m_pop3))
        pop3->sync();
    processOutbox();
}

void MailService::syncAccount(qint64 accountId)
{
    if (ImapAccount *imap = m_imap.value(accountId))
        imap->sync();
    else if (Pop3Account *pop3 = m_pop3.value(accountId))
        pop3->sync();
}

void MailService::syncFolder(qint64 folderId)
{
    const FolderState folder = MailStore::folder(folderId);
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT account_id FROM folders WHERE id = ?"));
    q.addBindValue(folderId);
    if (q.exec() && q.next()) {
        if (ImapAccount *imap = m_imap.value(q.value(0).toLongLong()))
            imap->syncFolder(folder.id);
    }
}

void MailService::fetchBody(qint64 messageId)
{
    const MessageLocation loc = MailStore::location(messageId);
    ImapAccount *imap = m_imap.value(loc.accountId);
    if (!imap) {
        Q_EMIT bodyFetched(messageId, false);
        return;
    }
    imap->fetchBody(messageId);
}

void MailService::flushPendingOps(qint64 accountId)
{
    if (ImapAccount *imap = m_imap.value(accountId))
        imap->flushPendingOps();
}

// ------------------------------------------------------------------ Versand

QString MailService::queueMessage(qint64 accountId, const OutgoingMessage &message, qint64 referenceMessage, const QString &referenceKind, int delaySeconds, qint64 *outboxId)
{
    const Account account = m_accounts->account(accountId);
    if (!account.isValid())
        return i18n("Unknown account.");

    const QStringList recipients = MessageBuilder::envelopeRecipients(message);
    if (recipients.isEmpty())
        return i18n("Please enter at least one recipient.");

    QString error;
    const QByteArray raw = MessageBuilder::build(message, nullptr, &error);
    if (raw.isEmpty())
        return error.isEmpty() ? i18n("The message could not be created.") : error;

    const QString blob = BlobStore::store(accountId, QStringLiteral("outbox"), raw);
    if (blob.isEmpty())
        return i18n("The message could not be saved.");

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("INSERT INTO outbox (account_id, blob_path, sender, recipients, subject, send_after, reference_message, reference_kind) "
                             "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(accountId);
    q.addBindValue(blob);
    q.addBindValue(message.fromEmail);
    q.addBindValue(recipients.join(QLatin1Char('\n')));
    q.addBindValue(message.subject.isNull() ? QStringLiteral("") : message.subject);
    q.addBindValue(QDateTime::currentSecsSinceEpoch() + delaySeconds);
    q.addBindValue(referenceMessage);
    q.addBindValue(referenceKind.isNull() ? QStringLiteral("") : referenceKind);
    if (!q.exec())
        return i18n("The message could not be saved.");
    if (outboxId)
        *outboxId = q.lastInsertId().toLongLong();

    Q_EMIT outboxChanged();
    if (delaySeconds <= 0)
        processOutbox();
    else // nicht erst auf den nächsten Durchlauf des Postausgang-Timers warten
        QTimer::singleShot(delaySeconds * 1000 + 200, this, &MailService::processOutbox);
    return {};
}

bool MailService::cancelQueued(qint64 outboxId)
{
    if (m_sendingOutbox.contains(outboxId))
        return false;

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT blob_path FROM outbox WHERE id = ?"));
    q.addBindValue(outboxId);
    if (!q.exec() || !q.next())
        return false;
    const QString blob = q.value(0).toString();

    QSqlQuery d(Database::connection());
    d.prepare(QStringLiteral("DELETE FROM outbox WHERE id = ?"));
    d.addBindValue(outboxId);
    if (!d.exec() || d.numRowsAffected() == 0)
        return false;
    BlobStore::remove(blob);
    Q_EMIT outboxChanged();
    return true;
}

QString MailService::saveDraft(qint64 accountId, const OutgoingMessage &message)
{
    QString error;
    const QByteArray raw = MessageBuilder::build(message, nullptr, &error);
    if (raw.isEmpty())
        return error;
    ImapAccount *imap = m_imap.value(accountId);
    if (!imap) {
        // POP3 kennt keine Serverordner.
        const qint64 folder = m_accounts->account(accountId).type == AccountType::Pop3 ? MailStore::ensureFolderForRole(accountId, QString::fromLatin1(FolderRole::Drafts)) : 0;
        if (folder == 0 || MailStore::insertRaw(accountId, folder, raw, MessageFlag::Seen | MessageFlag::Draft, QStringLiteral("local")) == 0)
            return i18n("The draft could not be saved.");
        Q_EMIT folderUpdated(folder);
        return {};
    }
    imap->append(QString::fromLatin1(FolderRole::Drafts), raw, {"\\Seen", "\\Draft"});
    return {};
}

bool MailService::canStoreMessages(qint64 accountId) const
{
    return m_imap.contains(accountId) || m_accounts->account(accountId).type == AccountType::Pop3;
}

void MailService::storeMessage(qint64 accountId, qint64 folderId, const QByteArray &raw, int flags, QObject *context, std::function<void(bool)> done)
{
    ImapAccount *imap = m_imap.value(accountId);
    if (!imap) {
        done(MailStore::insertRaw(accountId, folderId, raw, flags, QStringLiteral("local")) > 0);
        return;
    }
    // Ohne Anmeldung leert ImapConnection die Warteschlange, der Rückruf bliebe dann aus.
    auto answered = std::make_shared<bool>(false);
    auto once = [answered, done](bool ok) {
        if (*answered)
            return;
        *answered = true;
        done(ok);
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(imap, &ImapAccount::errorOccurred, context, [once, connection] {
        QObject::disconnect(*connection);
        once(false);
    });
    imap->appendToFolder(MailStore::folder(folderId).path, KMime::LFtoCRLF(raw), MailStore::imapFlagsFor(flags), [once, connection](bool ok) {
        QObject::disconnect(*connection);
        once(ok);
    });
}

bool MailService::sendDelayedNow()
{
    // attempts = 0: nur Wartezeiten von "Senden rückgängig", keine
    // Wiederholungen nach Fehlschlägen (die würden das Beenden aufhalten)
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE outbox SET send_after = ? WHERE send_after > ? AND attempts = 0"));
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    q.addBindValue(now);
    q.addBindValue(now);
    q.exec();
    processOutbox();
    return hasPendingWork();
}

bool MailService::hasPendingWork() const
{
    if (!m_sendingOutbox.isEmpty())
        return true;
    // Nach dem Versand legt APPEND die Kopie in "Gesendet" ab.
    return std::any_of(m_imap.cbegin(), m_imap.cend(), [](const ImapAccount *imap) {
        return imap->isInteractiveBusy();
    });
}

void MailService::processOutbox()
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, account_id, blob_path, sender, recipients, subject, reference_message, reference_kind "
                             "FROM outbox WHERE send_after <= ? ORDER BY id"));
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    while (q.next()) {
        const qint64 id = q.value(0).toLongLong();
        const qint64 accountId = q.value(1).toLongLong();
        if (m_sendingOutbox.contains(id))
            continue;

        const Account account = m_accounts->account(accountId);
        const bool oauth = account.authMethod == AuthMethod::OAuth2;
        const QString secret = m_secretCache.value(accountId);
        if (!account.isValid() || (oauth ? !m_tokens || !m_imap.contains(accountId) : secret.isEmpty()))
            continue;

        const QString blob = q.value(2).toString();
        const QByteArray raw = BlobStore::load(blob);
        const QString sender = q.value(3).toString();
        const QStringList recipients = q.value(4).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString subject = q.value(5).toString();
        const qint64 reference = q.value(6).toLongLong();
        const QString referenceKind = q.value(7).toString();

        m_sendingOutbox.insert(id);
        auto finished = [=, this](bool ok, const QString &error) {
            m_sendingOutbox.remove(id);
            if (!ok) {
                QSqlQuery u(Database::connection());
                // Nach einem Fehlschlag in 5 Minuten erneut versuchen.
                u.prepare(QStringLiteral("UPDATE outbox SET attempts = attempts + 1, last_error = ?, send_after = ? WHERE id = ?"));
                u.addBindValue(error);
                u.addBindValue(QDateTime::currentSecsSinceEpoch() + 5 * 60);
                u.addBindValue(id);
                u.exec();
                Q_EMIT errorOccurred(accountId, i18n("Sending \"%1\" failed: %2", subject, error));
                Q_EMIT outboxChanged();
                return;
            }
            afterSent(id, accountId, raw, reference, referenceKind);
            BlobStore::remove(blob);
            Q_EMIT messageSent(accountId, subject);
        };
        if (!oauth) {
            m_smtp->send(account, secret, sender, recipients, raw, finished);
            continue;
        }
        m_tokens->accessToken(account.secretKey(), account.oauthProvider, [=, this](const QString &token, const QString &error) {
            if (token.isEmpty())
                finished(false, error);
            else
                m_smtp->send(account, token, sender, recipients, raw, [=, this](bool ok, const QString &error) {
                    if (!ok)
                        m_tokens->invalidate(account.secretKey());
                    finished(ok, error);
                });
        });
    }
}

void MailService::afterSent(qint64 outboxId, qint64 accountId, const QByteArray &raw, qint64 referenceMessage, const QString &referenceKind)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM outbox WHERE id = ?"));
    q.addBindValue(outboxId);
    q.exec();
    Q_EMIT outboxChanged();

    const Account account = m_accounts->account(accountId);
    // Gmail legt gesendete Nachrichten selbst im Ordner "Gesendet" ab.
    const bool serverSavesSent = account.outgoingHost.endsWith(QLatin1String("gmail.com"));
    if (ImapAccount *imap = m_imap.value(accountId); imap && !serverSavesSent) {
        imap->append(QString::fromLatin1(FolderRole::Sent), raw, {"\\Seen"});
    } else if (account.type == AccountType::Pop3) {
        const qint64 sent = MailStore::ensureFolderForRole(accountId, QString::fromLatin1(FolderRole::Sent));
        if (sent > 0 && MailStore::insertRaw(accountId, sent, raw, MessageFlag::Seen, QStringLiteral("local")) > 0)
            Q_EMIT folderUpdated(sent);
    }

    // Bezugsnachricht als beantwortet/weitergeleitet markieren.
    if (referenceMessage > 0) {
        const MessageLocation loc = MailStore::location(referenceMessage);
        if (loc.id > 0) {
            const bool forward = referenceKind == QLatin1String("forward");
            MailStore::updateFlags(loc.id, loc.flags | (forward ? MessageFlag::Forwarded : MessageFlag::Answered));
            if (loc.uid > 0)
                MailStore::enqueueOp(loc.accountId, QStringLiteral("flags"), loc.folderPath, loc.uid, forward ? QStringLiteral("+$Forwarded") : QStringLiteral("+\\Answered"));
            flushPendingOps(loc.accountId);
            Q_EMIT folderUpdated(loc.folderId);
        }
    }
}

} // namespace Fjordpost

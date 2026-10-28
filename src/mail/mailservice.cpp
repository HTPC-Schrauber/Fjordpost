#include "mailservice.h"

#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "mail/imap/imapaccount.h"
#include "mail/smtp/smtpsender.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QLoggingCategory>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcService, "mopmail.service")

namespace MopMail {

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
}

MailService::~MailService()
{
    for (ImapAccount *imap : std::as_const(m_imap))
        imap->shutdown();
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
    if (!account.enabled || account.type != AccountType::Imap || m_imap.contains(account.id))
        return; // POP3 folgt in M2

    auto *imap = new ImapAccount(account, this);
    m_imap.insert(account.id, imap);
    const qint64 id = account.id;

    connect(imap, &ImapAccount::syncingChanged, this, [this, id](bool syncing) {
        if (syncing)
            m_syncing.insert(id);
        else
            m_syncing.remove(id);
        Q_EMIT syncingChanged();
    });
    connect(imap, &ImapAccount::errorOccurred, this, [this, id](const QString &message) {
        Q_EMIT errorOccurred(id, message);
    });
    connect(imap, &ImapAccount::foldersChanged, this, [this, id] {
        Q_EMIT foldersChanged(id);
    });
    connect(imap, &ImapAccount::folderUpdated, this, &MailService::folderUpdated);
    connect(imap, &ImapAccount::newMessages, this, &MailService::newMessages);
    connect(imap, &ImapAccount::bodyFetched, this, &MailService::bodyFetched);

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

void MailService::removeAccount(qint64 accountId)
{
    if (ImapAccount *imap = m_imap.take(accountId)) {
        imap->shutdown();
        imap->deleteLater();
    }
    m_secretCache.remove(accountId);
    m_syncing.remove(accountId);
    m_secrets->remove(Account::secretKeyFor(accountId));
    Q_EMIT syncingChanged();
}

void MailService::updateAccount(const Account &account)
{
    // Einfach neu aufbauen: Serverdaten oder Zugangsdaten können sich geändert haben.
    if (ImapAccount *imap = m_imap.take(account.id)) {
        imap->shutdown();
        imap->deleteLater();
    }
    m_secretCache.remove(account.id);
    addAccount(account);
}

void MailService::syncAll()
{
    for (ImapAccount *imap : std::as_const(m_imap))
        imap->sync();
    processOutbox();
}

void MailService::syncAccount(qint64 accountId)
{
    if (ImapAccount *imap = m_imap.value(accountId))
        imap->sync();
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

QString MailService::queueMessage(qint64 accountId, const OutgoingMessage &message, qint64 referenceMessage, const QString &referenceKind, int delaySeconds)
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

    Q_EMIT outboxChanged();
    if (delaySeconds <= 0)
        processOutbox();
    return {};
}

QString MailService::saveDraft(qint64 accountId, const OutgoingMessage &message)
{
    QString error;
    const QByteArray raw = MessageBuilder::build(message, nullptr, &error);
    if (raw.isEmpty())
        return error;
    ImapAccount *imap = m_imap.value(accountId);
    if (!imap)
        return i18n("Drafts can only be saved for IMAP accounts at the moment.");
    imap->append(QString::fromLatin1(FolderRole::Drafts), raw, {"\\Seen", "\\Draft"});
    return {};
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
        const QString secret = m_secretCache.value(accountId);
        if (!account.isValid() || secret.isEmpty())
            continue;

        const QString blob = q.value(2).toString();
        const QByteArray raw = BlobStore::load(blob);
        const QString sender = q.value(3).toString();
        const QStringList recipients = q.value(4).toString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        const QString subject = q.value(5).toString();
        const qint64 reference = q.value(6).toLongLong();
        const QString referenceKind = q.value(7).toString();

        m_sendingOutbox.insert(id);
        m_smtp->send(account, secret, sender, recipients, raw, [=, this](bool ok, const QString &error) {
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
    if (ImapAccount *imap = m_imap.value(accountId); imap && !serverSavesSent)
        imap->append(QString::fromLatin1(FolderRole::Sent), raw, {"\\Seen"});

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

} // namespace MopMail

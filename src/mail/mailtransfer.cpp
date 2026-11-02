#include "mailtransfer.h"

#include "core/accountmanager.h"
#include "mail/mailservice.h"
#include "mail/mime/mbox.h"
#include "mail/mime/messageparser.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/mailstore.h"

#include <KLocalizedString>

#include <QFileInfo>
#include <QSqlQuery>

namespace Fjordpost {

namespace {
// Antwortet der Server so lange nicht, wird der Vorgang abgebrochen.
constexpr int watchdogMs = 2 * 60 * 1000;
}

MailTransfer::MailTransfer(MailService *mail, AccountManager *accounts, QObject *parent)
    : QObject(parent)
    , m_mail(mail)
    , m_accounts(accounts)
{
    m_watchdog.setSingleShot(true);
    m_watchdog.setInterval(watchdogMs);
    connect(&m_watchdog, &QTimer::timeout, this, [this] {
        finish(i18n("The transfer was stopped because the server did not respond. %1 messages were processed.", m_done));
    });
    connect(m_mail, &MailService::bodyFetched, this, [this](qint64 messageId, bool ok) {
        if (m_mode != Mode::Export || messageId != m_waitingFor)
            return;
        m_waitingFor = 0;
        if (ok)
            writeExport(messageId);
        else
            ++m_skipped;
        scheduleNext();
    });
}

MailTransfer::~MailTransfer() = default;

void MailTransfer::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    Q_EMIT statusChanged();
}

void MailTransfer::scheduleNext()
{
    QTimer::singleShot(0, this, [this] {
        if (m_mode == Mode::Import)
            importNext();
        else if (m_mode == Mode::Export)
            exportNext();
    });
}

// ------------------------------------------------------------------- Import

QString MailTransfer::startImport(qint64 folderId, const QStringList &paths)
{
    if (isBusy())
        return i18n("Another import or export is still running.");
    if (paths.isEmpty())
        return i18n("No files selected.");

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT account_id FROM folders WHERE id = ?"));
    q.addBindValue(folderId);
    if (!q.exec() || !q.next())
        return i18n("Please choose a folder of an account.");
    m_accountId = q.value(0).toLongLong();
    if (!m_mail->canStoreMessages(m_accountId))
        return i18n("The account is not connected, so messages cannot be imported into it right now.");

    m_folderId = folderId;
    m_paths = paths;
    m_done = 0;
    m_skipped = 0;
    m_failedFile.clear();
    m_mode = Mode::Import;
    Q_EMIT busyChanged();
    setStatus(i18n("Importing messages…"));
    scheduleNext();
    return {};
}

QByteArray MailTransfer::nextRawMessage()
{
    while (true) {
        if (m_reader) {
            const QByteArray raw = m_reader->next();
            if (!raw.isEmpty())
                return raw;
            m_reader.reset();
            m_file.reset();
        }
        if (m_paths.isEmpty())
            return {};

        const QString path = m_paths.takeFirst();
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly)) {
            m_failedFile = QFileInfo(path).fileName();
            continue;
        }
        if (path.endsWith(QLatin1String(".eml"), Qt::CaseInsensitive)) {
            if (const QByteArray raw = file->readAll(); !raw.isEmpty())
                return raw;
            continue;
        }
        m_file = std::move(file);
        m_reader = std::make_unique<Mbox::Reader>(m_file.get());
    }
}

void MailTransfer::importNext()
{
    const QByteArray raw = nextRawMessage();
    if (raw.isEmpty()) {
        m_mail->syncFolder(m_folderId);
        QString summary = i18np("%1 message imported.", "%1 messages imported.", m_done);
        if (m_skipped > 0)
            summary += QLatin1Char(' ') + i18np("%1 was already there.", "%1 were already there.", m_skipped);
        if (!m_failedFile.isEmpty())
            summary += QLatin1Char(' ') + i18n("%1 could not be read.", m_failedFile);
        finish(summary);
        return;
    }

    const ParsedMessage headers = MessageParser::parseHeaders(MessageParser::headOf(raw));
    if (MailStore::hasMessageId(m_folderId, headers.messageId)) {
        ++m_skipped;
        scheduleNext();
        return;
    }

    m_watchdog.start();
    m_mail->storeMessage(m_accountId, m_folderId, raw, Mbox::flagsFromHeaders(raw), this, [this](bool ok) {
        afterImported(ok);
    });
}

void MailTransfer::afterImported(bool ok)
{
    m_watchdog.stop();
    if (m_mode != Mode::Import)
        return;
    if (!ok) {
        finish(i18n("The import was stopped because a message could not be stored. %1 messages were imported.", m_done));
        return;
    }
    ++m_done;
    if (m_done % 25 == 0) {
        setStatus(i18n("%1 messages imported…", m_done));
        Q_EMIT m_mail->folderUpdated(m_folderId);
    }
    scheduleNext();
}

// ------------------------------------------------------------------- Export

QString MailTransfer::startExport(const QList<qint64> &messageIds, const QString &path, bool eml)
{
    if (isBusy())
        return i18n("Another import or export is still running.");
    if (messageIds.isEmpty())
        return i18n("There are no messages to export.");

    auto output = std::make_unique<QSaveFile>(path);
    if (!output->open(QIODevice::WriteOnly))
        return i18n("The file %1 could not be written.", path);

    m_output = std::move(output);
    m_ids = messageIds;
    m_total = int(messageIds.size());
    m_eml = eml;
    m_done = 0;
    m_skipped = 0;
    m_waitingFor = 0;
    m_folderId = 0;
    m_mode = Mode::Export;
    Q_EMIT busyChanged();
    setStatus(i18n("Exporting messages…"));
    scheduleNext();
    return {};
}

void MailTransfer::exportNext()
{
    if (m_ids.isEmpty()) {
        m_watchdog.stop();
        if (m_done == 0) {
            m_output->cancelWriting();
            finish(i18n("Nothing was exported: the messages could not be loaded from the server."));
            return;
        }
        if (!m_output->commit()) {
            finish(i18n("The file could not be written."));
            return;
        }
        QString summary = i18np("%1 message exported.", "%1 messages exported.", m_done);
        if (m_skipped > 0)
            summary += QLatin1Char(' ') + i18np("%1 could not be loaded and is missing.", "%1 could not be loaded and are missing.", m_skipped);
        finish(summary);
        return;
    }

    const qint64 id = m_ids.takeFirst();
    const MessageLocation loc = MailStore::location(id);
    if (loc.id == 0) {
        ++m_skipped;
        scheduleNext();
        return;
    }
    if (!loc.bodyFetched || loc.blobPath.isEmpty()) {
        // Von IMAP liegen oft nur die Kopfzeilen vor.
        if (loc.uid > 0 && m_mail->canStoreMessages(loc.accountId)) {
            m_waitingFor = id;
            m_watchdog.start();
            m_mail->fetchBody(id);
        } else {
            ++m_skipped;
            scheduleNext();
        }
        return;
    }
    writeExport(id);
    scheduleNext();
}

void MailTransfer::writeExport(qint64 messageId)
{
    const MessageLocation loc = MailStore::location(messageId);
    const QByteArray raw = BlobStore::load(loc.blobPath);
    if (raw.isEmpty()) {
        ++m_skipped;
        return;
    }
    if (m_eml) {
        m_output->write(raw);
    } else {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT from_email, date FROM messages WHERE id = ?"));
        q.addBindValue(messageId);
        const bool ok = q.exec() && q.next();
        m_output->write(Mbox::entry(raw, ok ? q.value(0).toString() : QString(), QDateTime::fromSecsSinceEpoch(ok ? q.value(1).toLongLong() : 0), loc.flags));
    }
    ++m_done;
    if (m_done % 25 == 0)
        setStatus(i18n("%1 of %2 messages exported…", m_done, m_total));
}

// ---------------------------------------------------------------- Abschluss

void MailTransfer::cancel()
{
    if (!isBusy())
        return;
    finish(m_mode == Mode::Import ? i18n("Import cancelled after %1 messages.", m_done) : i18n("Export cancelled."));
}

void MailTransfer::finish(const QString &summary)
{
    m_watchdog.stop();
    const qint64 folder = m_folderId;
    if (m_output)
        m_output->cancelWriting(); // nach commit() wirkungslos
    m_output.reset();
    m_reader.reset();
    m_file.reset();
    m_paths.clear();
    m_ids.clear();
    m_mode = Mode::Idle;
    setStatus(QString());
    Q_EMIT busyChanged();
    Q_EMIT finished(summary, folder);
}

} // namespace Fjordpost

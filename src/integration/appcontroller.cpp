#include "appcontroller.h"

#include "core/accountmanager.h"
#include "integration/accountsetup.h"
#include "integration/colorscheme.h"
#include "mail/mailservice.h"
#include "mail/mime/messagebuilder.h"
#include "mail/mime/messageparser.h"
#include "models/accountmodel.h"
#include "models/foldermodel.h"
#include "models/messagelistmodel.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <KTextToHTML>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QPalette>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextDocumentFragment>

namespace Fjordpost {

namespace {

// Klartext als HTML darstellen. colors: Hintergrund, Text, Link, gedämpft.
QString plainTextToHtml(const QString &text, QStringList colors = {})
{
    if (colors.size() < 4) {
        const QPalette pal = QGuiApplication::palette();
        colors = {pal.color(QPalette::Base).name(), pal.color(QPalette::Text).name(), pal.color(QPalette::Link).name(),
                  pal.color(QPalette::PlaceholderText).name()};
    }
    QString html = KTextToHTML::convertToHtml(text, KTextToHTML::PreserveSpaces | KTextToHTML::HighlightText);
    // Zitate abschwächen (Zeilen, die mit &gt; beginnen)
    static const QRegularExpression quoteLine(QStringLiteral("(^|<br />)((?:&gt;|&nbsp;)*&gt;[^<]*)"));
    html.replace(quoteLine, QStringLiteral("\\1<span class=\"quote\">\\2</span>"));
    return QStringLiteral(
               "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>"
               "body{font-family:sans-serif;font-size:10.5pt;line-height:1.6;color:%2;background:%1;margin:0;padding:8px 28px 24px;overflow-wrap:anywhere}"
               "a{color:%3}.quote{color:%4}"
               "</style></head><body>%5</body></html>")
        .arg(colors.at(0), colors.at(1), colors.at(2), colors.at(3), html);
}

// HTML-Mails gehen davon aus, auf weißem Grund dargestellt zu werden.
QString wrapHtml(const QString &html)
{
    if (html.contains(QLatin1String("<html"), Qt::CaseInsensitive))
        return html;
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>body{background:#fff;color:#000;"
                          "font-family:sans-serif;font-size:10pt;margin:0;padding:16px 28px}</style></head><body>%1</body></html>")
        .arg(html);
}

bool hasRemoteContent(const QString &html)
{
    static const QRegularExpression remote(QStringLiteral("(src|background|href)\\s*=\\s*[\"']?\\s*https?://|url\\(\\s*[\"']?https?://"),
                                           QRegularExpression::CaseInsensitiveOption);
    // href allein lädt nichts; nur eingebettete Ressourcen zählen.
    static const QRegularExpression embedded(QStringLiteral("(src|background)\\s*=\\s*[\"']?\\s*https?://|url\\(\\s*[\"']?https?://"),
                                             QRegularExpression::CaseInsensitiveOption);
    return remote.match(html).hasMatch() && embedded.match(html).hasMatch();
}

// Mittellanges Datum für Kopfzeilen und Zitate, z. B. "Mi., 28.10.26, 13:48"
QString mediumDate(const QDateTime &date)
{
    const QLocale locale;
    return QStringLiteral("%1, %2").arg(locale.dayName(date.date().dayOfWeek(), QLocale::ShortFormat), locale.toString(date, QLocale::ShortFormat));
}

QString folderDisplayName(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT f.name FROM messages m JOIN folders f ON f.id = m.folder_id WHERE m.id = ?"));
    q.addBindValue(messageId);
    return q.exec() && q.next() ? q.value(0).toString() : QString();
}

QString formatSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString prefixed(const QString &subject, const QString &prefix, const QStringList &known)
{
    const QString trimmed = subject.trimmed();
    for (const QString &k : known) {
        if (trimmed.startsWith(k, Qt::CaseInsensitive))
            return trimmed;
    }
    return prefix + trimmed;
}

QStringList withoutAddress(const QStringList &list, const QString &own)
{
    QStringList result;
    for (const QString &entry : list) {
        if (!entry.contains(own, Qt::CaseInsensitive))
            result << entry;
    }
    return result;
}

} // namespace

AppController::AppController(AccountManager *accounts, MailService *mail, AccountSetup *setup, QObject *parent)
    : QObject(parent)
    , m_accountManager(accounts)
    , m_mail(mail)
    , m_setup(setup)
    , m_accountModel(new AccountModel(accounts, this))
    , m_folderModel(new FolderModel(this))
    , m_messageModel(new MessageListModel(this))
{
    m_role = QString::fromLatin1(FolderRole::Inbox);
    m_title = i18n("All Inboxes");
    applyFilter();
    refreshCounts();

    connect(accounts, &AccountManager::accountAdded, this, &AppController::accountsChanged);
    connect(accounts, &AccountManager::accountRemoved, this, [this](qint64 id) {
        if (m_accountId == id)
            selectAccount(0);
        Q_EMIT accountsChanged();
        refreshCounts();
        m_messageModel->refresh();
    });
    connect(accounts, &AccountManager::accountChanged, this, &AppController::accountsChanged);

    connect(mail, &MailService::syncingChanged, this, &AppController::syncingChanged);
    connect(mail, &MailService::errorOccurred, this, [this](qint64, const QString &message) {
        setError(message);
    });
    connect(mail, &MailService::foldersChanged, this, [this](qint64 accountId) {
        if (m_accountId == 0 || m_accountId == accountId)
            m_folderModel->reload();
        // Neu angelegtes Konto: Posteingang wählen, sobald er bekannt ist.
        if (m_accountId == accountId && m_folderId == 0)
            selectAccount(accountId);
    });
    connect(mail, &MailService::folderUpdated, this, [this](qint64) {
        m_messageModel->refresh();
        refreshCounts();
    });
    connect(mail, &MailService::bodyFetched, this, [this](qint64 messageId, bool ok) {
        if (m_current.value(QStringLiteral("id")).toLongLong() != messageId)
            return;
        if (ok) {
            loadCurrent(messageId);
        } else {
            m_current[QStringLiteral("loading")] = false;
            m_current[QStringLiteral("html")] = plainTextToHtml(i18n("The message could not be loaded from the server."), m_readerColors);
            Q_EMIT currentMessageChanged();
        }
    });
    connect(mail, &MailService::messageSent, this, [this](qint64, const QString &subject) {
        Q_EMIT notify(i18n("Message \"%1\" sent.", subject));
    });
}

QObject *AppController::accountsObject() const
{
    return m_accountModel;
}

QObject *AppController::foldersObject() const
{
    return m_folderModel;
}

QObject *AppController::messagesObject() const
{
    return m_messageModel;
}

QObject *AppController::accountSetupObject() const
{
    return m_setup;
}

bool AppController::hasAccounts() const
{
    return !m_accountManager->accounts().isEmpty();
}

bool AppController::syncing() const
{
    return m_mail->isSyncing();
}

void AppController::setError(const QString &message)
{
    if (m_lastError == message)
        return;
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

void AppController::dismissError()
{
    setError(QString());
}

void AppController::syncNow()
{
    dismissError();
    if (m_accountId > 0)
        m_mail->syncAccount(m_accountId);
    else
        m_mail->syncAll();
}

void AppController::setSearchText(const QString &text)
{
    if (m_search == text)
        return;
    m_search = text;
    Q_EMIT searchTextChanged();
    applyFilter();
}

void AppController::setQuickFilter(const QString &filter)
{
    if (m_quickFilter == filter)
        return;
    m_quickFilter = filter;
    Q_EMIT quickFilterChanged();
    applyFilter();
}

QString AppController::selectionSubtitle() const
{
    if (m_accountId > 0)
        return m_accountManager->account(m_accountId).email;
    const int count = int(m_accountManager->accounts().size());
    return i18np("%1 account connected", "%1 accounts connected", count);
}

void AppController::selectAccount(qint64 accountId)
{
    m_accountId = accountId;
    m_folderModel->setAccountId(accountId);

    // Beim Kontowechsel immer im Posteingang landen
    m_role = QString::fromLatin1(FolderRole::Inbox);
    m_folderId = 0;
    if (accountId > 0) {
        m_folderId = MailStore::folderIdForRole(accountId, m_role);
        m_title = m_accountManager->account(accountId).name;
    } else {
        m_title = i18n("All Inboxes");
    }
    if (!m_search.isEmpty()) {
        m_search.clear();
        Q_EMIT searchTextChanged();
    }
    Q_EMIT selectionChanged();
    applyFilter();
}

void AppController::selectFolder(qint64 folderId, const QString &role, const QString &name)
{
    m_folderId = folderId;
    m_role = role;
    m_title = name;
    if (!m_search.isEmpty()) {
        m_search.clear();
        Q_EMIT searchTextChanged();
    }
    Q_EMIT selectionChanged();
    applyFilter();
    // Ordner beim Öffnen frisch abgleichen
    if (folderId > 0)
        m_mail->syncFolder(folderId);
}

void AppController::applyFilter()
{
    MessageListModel::Filter f;
    f.accountId = m_accountId;
    f.folderId = m_folderId;
    f.role = m_role;
    f.search = m_search;
    f.quick = m_quickFilter;
    m_messageModel->setFilter(f);

    if (!m_current.isEmpty()) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
    }
}

void AppController::refreshCounts()
{
    m_accountModel->reload();
    m_folderModel->reload();

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM messages m JOIN folders f ON f.id = m.folder_id "
                             "WHERE f.role = 'inbox' AND (m.flags & ?) = 0"));
    q.addBindValue(int(MessageFlag::Seen));
    q.exec();
    const int unread = q.next() ? q.value(0).toInt() : 0;
    if (unread != m_totalUnread) {
        m_totalUnread = unread;
        Q_EMIT totalUnreadChanged();
    }
}

// ---------------------------------------------------------------- Anzeige

void AppController::openMessage(qint64 messageId)
{
    if (m_remoteAllowed) {
        m_remoteAllowed = false;
        Q_EMIT remoteContentAllowedChanged();
    }
    loadCurrent(messageId);

    const MessageLocation loc = MailStore::location(messageId);
    if (loc.id > 0 && !(loc.flags & MessageFlag::Seen))
        setFlags(messageId, loc.flags | MessageFlag::Seen, QStringLiteral("+\\Seen"));
}

void AppController::loadCurrent(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "SELECT m.id, m.subject, m.from_name, m.from_email, m.to_list, m.cc_list, m.date, m.flags, m.body_text, "
        "a.name, a.color, m.blob_path, m.body_fetched, m.uid FROM messages m JOIN accounts a ON a.id = m.account_id WHERE m.id = ?"));
    q.addBindValue(messageId);
    q.exec();
    if (!q.next()) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
        return;
    }

    const int flags = q.value(7).toInt();
    const QDateTime date = QDateTime::fromSecsSinceEpoch(q.value(6).toLongLong());
    const QString blobPath = q.value(11).toString();
    const bool bodyFetched = q.value(12).toBool();
    const qint64 uid = q.value(13).toLongLong();

    m_current = {
        {QStringLiteral("id"), q.value(0)},
        {QStringLiteral("subject"), q.value(1).toString().isEmpty() ? i18n("(No subject)") : q.value(1).toString()},
        {QStringLiteral("fromName"), q.value(2).toString().isEmpty() ? q.value(3) : q.value(2)},
        {QStringLiteral("fromEmail"), q.value(3)},
        {QStringLiteral("to"), q.value(4)},
        {QStringLiteral("cc"), q.value(5)},
        {QStringLiteral("dateText"), mediumDate(date)},
        {QStringLiteral("accountName"), q.value(9)},
        {QStringLiteral("accountColor"), q.value(10)},
        {QStringLiteral("flagged"), (flags & MessageFlag::Flagged) != 0},
        {QStringLiteral("unread"), (flags & MessageFlag::Seen) == 0},
        {QStringLiteral("loading"), false},
        {QStringLiteral("hasRemoteContent"), false},
        {QStringLiteral("attachments"), QVariantList()},
        {QStringLiteral("folderName"), folderDisplayName(messageId)},
    };

    if (bodyFetched && !blobPath.isEmpty()) {
        const ParsedMessage parsed = MessageParser::parse(BlobStore::load(blobPath));
        if (!parsed.html.isEmpty()) {
            m_current[QStringLiteral("html")] = wrapHtml(parsed.html);
            m_current[QStringLiteral("hasRemoteContent")] = hasRemoteContent(parsed.html);
        } else {
            m_current[QStringLiteral("html")] = plainTextToHtml(parsed.plainText, m_readerColors);
        }
        QVariantList attachments;
        for (const AttachmentInfo &a : parsed.attachments) {
            attachments << QVariantMap{
                {QStringLiteral("index"), a.index},
                {QStringLiteral("name"), a.fileName.isEmpty() ? i18n("Unnamed attachment") : a.fileName},
                {QStringLiteral("size"), formatSize(a.size)},
                {QStringLiteral("mimeType"), a.mimeType},
            };
        }
        m_current[QStringLiteral("attachments")] = attachments;
    } else if (uid > 0) {
        // Inhalt fehlt noch: vom Server holen, Anzeige folgt über bodyFetched().
        m_current[QStringLiteral("loading")] = true;
        m_current[QStringLiteral("html")] = QString();
        m_mail->fetchBody(messageId);
    } else {
        // Lokale Nachricht ohne Server-Kopie (z. B. Demo-Daten)
        m_current[QStringLiteral("html")] = plainTextToHtml(q.value(8).toString(), m_readerColors);
    }
    Q_EMIT currentMessageChanged();
}

void AppController::allowRemoteContent()
{
    m_remoteAllowed = true;
    Q_EMIT remoteContentAllowedChanged();
    // Neu darstellen, damit die Ressourcen geladen werden
    const QVariantMap current = m_current;
    m_current.clear();
    Q_EMIT currentMessageChanged();
    m_current = current;
    Q_EMIT currentMessageChanged();
}

// ------------------------------------------------------- Kontextleiste usw.

void AppController::setReaderColors(const QString &background, const QString &text, const QString &link, const QString &muted)
{
    const QStringList colors{background, text, link, muted};
    if (colors == m_readerColors)
        return;
    m_readerColors = colors;
    const qint64 id = m_current.value(QStringLiteral("id")).toLongLong();
    if (id > 0 && !m_current.value(QStringLiteral("loading")).toBool())
        loadCurrent(id);
}

void AppController::applyColorScheme(const QVariantMap &colors)
{
    ColorScheme::apply(colors);
}

int AppController::messageCountFrom(const QString &email) const
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM messages WHERE from_email = ? COLLATE NOCASE"));
    q.addBindValue(email);
    return q.exec() && q.next() ? q.value(0).toInt() : 0;
}

void AppController::showMessagesFrom(const QString &email)
{
    setSearchText(email);
}

QString AppController::cacheSize() const
{
    qint64 total = QFileInfo(Database::path()).size() + QFileInfo(Database::path() + QStringLiteral("-wal")).size();
    QDirIterator it(QDir(Database::dataDir()).filePath(QStringLiteral("blobs")), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return formatSize(total);
}

void AppController::markAllRead()
{
    // Alle sichtbaren ungelesenen Nachrichten der aktuellen Liste
    QList<qint64> ids;
    for (int row = 0; row < m_messageModel->rowCount(); ++row) {
        const QModelIndex idx = m_messageModel->index(row);
        if (idx.data(MessageListModel::UnreadRole).toBool())
            ids << idx.data(MessageListModel::MessageIdRole).toLongLong();
    }
    for (qint64 id : std::as_const(ids))
        setRead(id, true);
}

// ---------------------------------------------------------------- Aktionen

void AppController::setFlags(qint64 messageId, int flags, const QString &imapChange)
{
    const MessageLocation loc = MailStore::location(messageId);
    if (loc.id == 0)
        return;
    MailStore::updateFlags(messageId, flags);
    if (loc.uid > 0) {
        MailStore::enqueueOp(loc.accountId, QStringLiteral("flags"), loc.folderPath, loc.uid, imapChange);
        m_mail->flushPendingOps(loc.accountId);
    }
    m_messageModel->updateFlags(messageId, flags);
    if (m_current.value(QStringLiteral("id")).toLongLong() == messageId) {
        m_current[QStringLiteral("flagged")] = (flags & MessageFlag::Flagged) != 0;
        m_current[QStringLiteral("unread")] = (flags & MessageFlag::Seen) == 0;
        Q_EMIT currentMessageChanged();
    }
    refreshCounts();
}

void AppController::setRead(qint64 messageId, bool read)
{
    const int flags = MailStore::location(messageId).flags;
    setFlags(messageId, read ? (flags | MessageFlag::Seen) : (flags & ~MessageFlag::Seen), read ? QStringLiteral("+\\Seen") : QStringLiteral("-\\Seen"));
}

void AppController::toggleFlagged(qint64 messageId)
{
    const int flags = MailStore::location(messageId).flags;
    const bool flag = !(flags & MessageFlag::Flagged);
    setFlags(messageId, flags ^ MessageFlag::Flagged, flag ? QStringLiteral("+\\Flagged") : QStringLiteral("-\\Flagged"));
}

void AppController::moveToRole(qint64 messageId, const QString &role)
{
    const MessageLocation loc = MailStore::location(messageId);
    if (loc.id == 0)
        return;
    const qint64 target = MailStore::ensureFolderForRole(loc.accountId, role);
    if (target == 0 || target == loc.folderId) {
        if (target == 0)
            Q_EMIT notify(i18n("This account has no folder for this action."));
        return;
    }
    MailStore::moveLocally(messageId, target);
    if (loc.uid > 0) {
        MailStore::enqueueOp(loc.accountId, QStringLiteral("move"), loc.folderPath, loc.uid, MailStore::folder(target).path);
        m_mail->flushPendingOps(loc.accountId);
    }
    m_messageModel->removeMessage(messageId);
    if (m_current.value(QStringLiteral("id")).toLongLong() == messageId) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
    }
    refreshCounts();
}

void AppController::deleteMessage(qint64 messageId)
{
    const MessageLocation loc = MailStore::location(messageId);
    if (loc.id == 0)
        return;
    if (loc.folderRole != QLatin1String(FolderRole::Trash)) {
        moveToRole(messageId, QString::fromLatin1(FolderRole::Trash));
        return;
    }
    // Im Papierkorb: endgültig löschen.
    if (loc.uid > 0) {
        MailStore::enqueueOp(loc.accountId, QStringLiteral("expunge"), loc.folderPath, loc.uid);
        m_mail->flushPendingOps(loc.accountId);
    }
    BlobStore::remove(loc.blobPath);
    MailStore::removeMessages({messageId});
    m_messageModel->removeMessage(messageId);
    if (m_current.value(QStringLiteral("id")).toLongLong() == messageId) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
    }
    refreshCounts();
}

void AppController::archiveMessage(qint64 messageId)
{
    moveToRole(messageId, QString::fromLatin1(FolderRole::Archive));
}

// ---------------------------------------------------------------- Composer

QVariantList AppController::identities() const
{
    QVariantList result;
    const auto accounts = m_accountManager->accounts();
    for (const Account &a : accounts) {
        const QString name = a.displayName.isEmpty() ? a.email : QStringLiteral("%1 <%2>").arg(a.displayName, a.email);
        result << QVariantMap{{QStringLiteral("accountId"), a.id}, {QStringLiteral("text"), name}, {QStringLiteral("color"), a.color}};
    }
    return result;
}

QVariantMap AppController::composeData(qint64 messageId, const QString &mode) const
{
    QVariantMap data{{QStringLiteral("mode"), mode}, {QStringLiteral("referenceId"), messageId}};
    qint64 accountId = m_accountId;

    const MessageLocation loc = MailStore::location(messageId);
    if (mode == QLatin1String("new") || loc.id == 0) {
        if (accountId <= 0 && !m_accountManager->accounts().isEmpty())
            accountId = m_accountManager->accounts().constFirst().id;
        data[QStringLiteral("accountId")] = accountId;
        return data;
    }

    accountId = loc.accountId;
    data[QStringLiteral("accountId")] = accountId;
    const QString own = m_accountManager->account(accountId).email;

    ParsedMessage parsed;
    const QByteArray raw = BlobStore::load(loc.blobPath);
    if (!raw.isEmpty()) {
        parsed = MessageParser::parse(raw);
    } else {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT subject, from_name, from_email, to_list, cc_list, date, body_text, message_id, refs, reply_to "
                                 "FROM messages WHERE id = ?"));
        q.addBindValue(messageId);
        q.exec();
        if (q.next()) {
            parsed.subject = q.value(0).toString();
            parsed.fromName = q.value(1).toString();
            parsed.fromEmail = q.value(2).toString();
            parsed.to = q.value(3).toString();
            parsed.cc = q.value(4).toString();
            parsed.date = QDateTime::fromSecsSinceEpoch(q.value(5).toLongLong());
            parsed.plainText = q.value(6).toString();
            parsed.messageId = q.value(7).toString().toUtf8();
            for (const QString &r : q.value(8).toString().split(QLatin1Char(' '), Qt::SkipEmptyParts))
                parsed.references << r.toUtf8();
            parsed.replyTo = q.value(9).toString();
        }
    }

    QString body = parsed.plainText;
    if (body.trimmed().isEmpty() && !parsed.html.isEmpty())
        body = QTextDocumentFragment::fromHtml(parsed.html).toPlainText();

    const QString sender = parsed.fromName.isEmpty() ? parsed.fromEmail : QStringLiteral("%1 <%2>").arg(parsed.fromName, parsed.fromEmail);
    const QString dateText = mediumDate(parsed.date);

    if (mode == QLatin1String("forward")) {
        data[QStringLiteral("subject")] = prefixed(parsed.subject, QStringLiteral("Fwd: "), {QStringLiteral("fwd:"), QStringLiteral("wg:"), QStringLiteral("fw:")});
        data[QStringLiteral("body")] = QStringLiteral("\n\n-------- %1 --------\n%2: %3\n%4: %5\n%6: %7\n%8: %9\n\n%10")
                                           .arg(i18n("Forwarded Message"), i18n("Subject"), parsed.subject, i18n("Date"), dateText, i18n("From"), sender,
                                                i18n("To"), parsed.to, body);
        // Anhänge der Originalnachricht übernehmen
        QStringList paths;
        for (const AttachmentInfo &a : parsed.attachments) {
            const QString path = attachmentTempPath(messageId, a.index, a.fileName);
            QFile f(path);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(MessageParser::attachmentData(raw, a.index));
                paths << path;
            }
        }
        data[QStringLiteral("attachments")] = paths;
        return data;
    }

    // Antworten
    QStringList to = MessageBuilder::splitAddresses(parsed.replyTo.isEmpty() ? sender : parsed.replyTo);
    QStringList cc;
    if (mode == QLatin1String("replyAll")) {
        to << withoutAddress(MessageBuilder::splitAddresses(parsed.to), own);
        cc = withoutAddress(MessageBuilder::splitAddresses(parsed.cc), own);
    }
    to.removeDuplicates();
    data[QStringLiteral("to")] = to.join(QStringLiteral(", "));
    data[QStringLiteral("cc")] = cc.join(QStringLiteral(", "));
    data[QStringLiteral("subject")] = prefixed(parsed.subject, QStringLiteral("Re: "), {QStringLiteral("re:"), QStringLiteral("aw:")});

    QStringList quoted;
    const auto lines = body.split(QLatin1Char('\n'));
    for (const QString &line : lines)
        quoted << (line.startsWith(QLatin1Char('>')) ? QStringLiteral(">") : QStringLiteral("> ")) + line;
    data[QStringLiteral("body")] = QStringLiteral("\n\n%1\n%2").arg(i18nc("@info reply header", "On %1, %2 wrote:", dateText, sender), quoted.join(QLatin1Char('\n')));

    QList<QByteArray> refs = parsed.references;
    if (!parsed.messageId.isEmpty())
        refs << parsed.messageId;
    QStringList refStrings;
    for (const QByteArray &r : refs)
        refStrings << QString::fromUtf8(r);
    data[QStringLiteral("inReplyTo")] = QString::fromUtf8(parsed.messageId);
    data[QStringLiteral("references")] = refStrings;
    return data;
}

OutgoingMessage AppController::outgoingFromMap(const QVariantMap &data) const
{
    const Account account = m_accountManager->account(data.value(QStringLiteral("accountId")).toLongLong());
    OutgoingMessage m;
    m.fromName = account.displayName;
    m.fromEmail = account.email;
    m.to = MessageBuilder::splitAddresses(data.value(QStringLiteral("to")).toString());
    m.cc = MessageBuilder::splitAddresses(data.value(QStringLiteral("cc")).toString());
    m.bcc = MessageBuilder::splitAddresses(data.value(QStringLiteral("bcc")).toString());
    m.subject = data.value(QStringLiteral("subject")).toString();
    m.plainText = data.value(QStringLiteral("body")).toString();
    for (const QVariant &v : data.value(QStringLiteral("attachments")).toList()) {
        const QUrl url = v.toUrl();
        m.attachmentPaths << (url.isLocalFile() ? url.toLocalFile() : v.toString());
    }
    m.inReplyTo = data.value(QStringLiteral("inReplyTo")).toString().toUtf8();
    for (const QVariant &r : data.value(QStringLiteral("references")).toList())
        m.references << r.toString().toUtf8();
    return m;
}

QString AppController::sendMessage(const QVariantMap &data)
{
    const qint64 accountId = data.value(QStringLiteral("accountId")).toLongLong();
    const QString mode = data.value(QStringLiteral("mode")).toString();
    const qint64 reference = (mode == QLatin1String("new")) ? 0 : data.value(QStringLiteral("referenceId")).toLongLong();
    return m_mail->queueMessage(accountId, outgoingFromMap(data), reference, mode == QLatin1String("forward") ? QStringLiteral("forward") : QStringLiteral("reply"));
}

QString AppController::sendQuickReply(const QString &text)
{
    const qint64 id = m_current.value(QStringLiteral("id")).toLongLong();
    if (id <= 0 || text.trimmed().isEmpty())
        return i18n("Please enter a message.");
    QVariantMap data = composeData(id, QStringLiteral("reply"));
    data[QStringLiteral("body")] = QString(text + data.value(QStringLiteral("body")).toString());
    return sendMessage(data);
}

QString AppController::saveDraft(const QVariantMap &data)
{
    return m_mail->saveDraft(data.value(QStringLiteral("accountId")).toLongLong(), outgoingFromMap(data));
}

// ----------------------------------------------------------------- Anhänge

QString AppController::attachmentTempPath(qint64 messageId, int index, const QString &fileName) const
{
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(QStringLiteral("fjordpost/%1-%2").arg(messageId).arg(index));
    QDir().mkpath(dir);
    QString name = fileName.isEmpty() ? QStringLiteral("attachment") : fileName;
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    return QDir(dir).filePath(name);
}

void AppController::openAttachment(qint64 messageId, int index)
{
    const MessageLocation loc = MailStore::location(messageId);
    const QByteArray raw = BlobStore::load(loc.blobPath);
    const ParsedMessage parsed = MessageParser::parse(raw);
    if (index < 0 || index >= parsed.attachments.size())
        return;
    const QString path = attachmentTempPath(messageId, index, parsed.attachments.at(index).fileName);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(MessageParser::attachmentData(raw, index));
    f.close();
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool AppController::saveAttachment(qint64 messageId, int index, const QUrl &target)
{
    const MessageLocation loc = MailStore::location(messageId);
    const QByteArray data = MessageParser::attachmentData(BlobStore::load(loc.blobPath), index);
    QFile f(target.toLocalFile());
    if (data.isEmpty() || !f.open(QIODevice::WriteOnly)) {
        Q_EMIT notify(i18n("The attachment could not be saved."));
        return false;
    }
    f.write(data);
    return true;
}

} // namespace Fjordpost

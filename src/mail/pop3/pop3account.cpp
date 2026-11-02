#include "pop3account.h"

#include "mail/mime/messageparser.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <KLocalizedString>

namespace Fjordpost {

namespace {
// Beim Abruf vieler Nachrichten die Liste zwischendurch aktualisieren.
constexpr int progressInterval = 20;
}

Pop3Account::Pop3Account(const Account &account, QObject *parent)
    : QObject(parent)
    , m_account(account)
{
}

Pop3Account::~Pop3Account()
{
    shutdown();
}

void Pop3Account::setCredentials(const Account &account, const QString &secret)
{
    m_account = account;
    m_secret = secret;
}

void Pop3Account::ensureFolders()
{
    const struct {
        const char *role;
        QString path;
        QString name;
    } folders[] = {
        {FolderRole::Inbox, QStringLiteral("INBOX"), i18n("Inbox")},
        {FolderRole::Drafts, QStringLiteral("Drafts"), i18n("Drafts")},
        {FolderRole::Sent, QStringLiteral("Sent"), i18n("Sent")},
        {FolderRole::Archive, QStringLiteral("Archive"), i18n("Archive")},
        {FolderRole::Junk, QStringLiteral("Junk"), i18n("Spam")},
        {FolderRole::Trash, QStringLiteral("Trash"), i18n("Trash")},
    };
    for (const auto &f : folders) {
        const QString role = QString::fromLatin1(f.role);
        if (MailStore::folderIdForRole(m_account.id, role) == 0)
            MailStore::upsertFolder(m_account.id, f.path, f.name, role, QStringLiteral("/"), MailStore::roleSortOrder(role));
    }
    m_inboxId = MailStore::folderIdForRole(m_account.id, QString::fromLatin1(FolderRole::Inbox));
    Q_EMIT foldersChanged();
}

void Pop3Account::sync()
{
    if (m_client || m_secret.isEmpty())
        return;
    m_client = new Pop3Client(this);
    Q_EMIT syncingChanged(true);

    connect(m_client, &Pop3Client::loggedIn, this, [this] {
        m_client->listUids();
    });
    connect(m_client, &Pop3Client::uidsListed, this, &Pop3Account::onListed);
    connect(m_client, &Pop3Client::messageRetrieved, this, &Pop3Account::onRetrieved);
    connect(m_client, &Pop3Client::messageRemoved, this, &Pop3Account::retrieveNext);
    connect(m_client, &Pop3Client::finished, this, &Pop3Account::finish);
    connect(m_client, &Pop3Client::failed, this, [this](const QString &message) {
        Q_EMIT errorOccurred(i18n("Fetching mail from %1 failed: %2", m_account.incomingHost, message));
        finish();
    });
    m_client->connectAndLogin(m_account, m_secret);
}

void Pop3Account::onListed(const QList<Pop3Entry> &entries)
{
    const QSet<QByteArray> known = MailStore::pop3KnownUids(m_account.id);
    m_firstSync = known.isEmpty();

    QSet<QByteArray> onServer;
    m_queue.clear();
    for (const Pop3Entry &e : entries) {
        onServer.insert(e.uidl);
        if (!known.contains(e.uidl))
            m_queue << e;
    }
    // Vom Server verschwundene UIDs vergessen, die Tabelle wüchse sonst endlos.
    MailStore::forgetPop3Uids(m_account.id, known - onServer);
    retrieveNext();
}

void Pop3Account::retrieveNext()
{
    if (!m_client)
        return;
    if (m_queue.isEmpty()) {
        m_client->quit();
        return;
    }
    const Pop3Entry next = m_queue.constFirst();
    m_currentUidl = next.uidl;
    m_client->retrieve(next.number);
}

void Pop3Account::onRetrieved(int number, const QByteArray &raw)
{
    // Dieselbe Nachricht (z. B. über zwei Adressen zugestellt) nicht doppelt ablegen.
    const ParsedMessage headers = MessageParser::parseHeaders(MessageParser::headOf(raw));
    if (!MailStore::hasMessageId(m_inboxId, headers.messageId)) {
        const qint64 id = MailStore::insertRaw(m_account.id, m_inboxId, raw, 0, QStringLiteral("pop3"));
        if (id == 0) {
            // Nicht als abgerufen vermerken und nichts löschen: beim nächsten Mal erneut versuchen.
            Q_EMIT errorOccurred(i18n("A message from %1 could not be saved.", m_account.incomingHost));
            m_queue.clear();
            m_client->quit();
            return;
        }
        m_new << id;
        if (m_new.size() % progressInterval == 0)
            Q_EMIT folderUpdated(m_inboxId);
    }

    // Erst nach dem Sichern vermerken: ein Abbruch verliert so keine Nachricht.
    MailStore::rememberPop3Uid(m_account.id, m_currentUidl);
    m_queue.removeFirst();
    if (m_account.leaveOnServer)
        retrieveNext();
    else
        m_client->remove(number);
}

void Pop3Account::finish()
{
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    m_queue.clear();
    reportNew();
    Q_EMIT syncingChanged(false);
}

void Pop3Account::reportNew()
{
    if (m_new.isEmpty())
        return;
    Q_EMIT folderUpdated(m_inboxId);
    Q_EMIT newMessages(std::exchange(m_new, {}), !m_firstSync);
}

void Pop3Account::shutdown()
{
    if (m_client) {
        m_client->abort();
        m_client->deleteLater();
        m_client = nullptr;
    }
}

} // namespace Fjordpost

#include "imapconnection.h"

#include <KIMAP/CapabilitiesJob>
#include <KIMAP/LoginJob>
#include <KIMAP/LogoutJob>
#include <KIMAP/Session>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcImap, "fjordpost.imap")

namespace Fjordpost {

namespace {

KIMAP::LoginJob::EncryptionMode encryptionFor(Security s)
{
    switch (s) {
    case Security::None:
        return KIMAP::LoginJob::Unencrypted;
    case Security::StartTls:
        return KIMAP::LoginJob::STARTTLS;
    case Security::Tls:
        break;
    }
    return KIMAP::LoginJob::SSLorTLS;
}

} // namespace

ImapConnection::ImapConnection(const Account &account, const QString &name, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_name(name)
{
}

ImapConnection::~ImapConnection()
{
    if (m_session)
        m_session->close();
}

void ImapConnection::setCredentials(const Account &account, const QString &secret)
{
    const bool changed = account.incomingHost != m_account.incomingHost || account.incomingPort != m_account.incomingPort
        || account.incomingSecurity != m_account.incomingSecurity || account.username != m_account.username || secret != m_secret;
    m_account = account;
    m_secret = secret;
    if (changed && m_session && !m_running)
        resetSession();
}

bool ImapConnection::isConnected() const
{
    return m_session && m_session->state() != KIMAP::Session::Disconnected;
}

void ImapConnection::enqueue(Task task, bool urgent)
{
    if (urgent)
        m_queue.prepend(std::move(task));
    else
        m_queue.append(std::move(task));
    if (!m_running)
        runNext();
}

void ImapConnection::runNext()
{
    if (m_queue.isEmpty()) {
        m_running = false;
        return;
    }
    m_running = true;

    if (!m_loggedIn || !isConnected()) {
        login([this](bool ok) {
            if (!ok) {
                // Ohne Anmeldung ist keine Aufgabe ausführbar.
                m_queue.clear();
                m_running = false;
                return;
            }
            runNext();
        });
        return;
    }

    Task task = m_queue.takeFirst();
    QPointer<ImapConnection> self(this);
    task(m_session, [self] {
        if (self)
            QMetaObject::invokeMethod(self, &ImapConnection::runNext, Qt::QueuedConnection);
    });
}

void ImapConnection::resetSession()
{
    if (m_session) {
        m_session->disconnect(this);
        m_session->close();
        m_session->deleteLater();
    }
    m_session = nullptr;
    m_loggedIn = false;
    m_selected.clear();
}

void ImapConnection::login(std::function<void(bool)> then)
{
    resetSession();
    if (m_account.incomingHost.isEmpty()) {
        Q_EMIT loginFailed(QStringLiteral("No server configured"));
        then(false);
        return;
    }

    m_session = new KIMAP::Session(m_account.incomingHost, quint16(m_account.incomingPort), this);
    m_session->setObjectName(m_name);
    m_session->setTimeout(60);
    connect(m_session, &KIMAP::Session::connectionLost, this, [this] {
        qCInfo(lcImap) << m_name << "Verbindung verloren";
        m_loggedIn = false;
        m_selected.clear();
        Q_EMIT connectionLost();
    });

    auto *job = createLoginJob(m_session, m_account, m_secret);
    connect(job, &KJob::result, this, [this, job, then = std::move(then)](KJob *) mutable {
        if (job->error()) {
            qCWarning(lcImap) << m_name << "Anmeldung fehlgeschlagen:" << job->errorString();
            Q_EMIT loginFailed(job->errorString());
            resetSession();
            then(false);
            return;
        }
        m_loggedIn = true;
        auto *caps = new KIMAP::CapabilitiesJob(m_session);
        connect(caps, &KJob::result, this, [this, caps, then = std::move(then)](KJob *) {
            m_capabilities = caps->capabilities();
            qCDebug(lcImap) << m_name << "Capabilities:" << m_capabilities;
            then(true);
        });
        caps->start();
    });
    job->start();
}

KIMAP::LoginJob *ImapConnection::createLoginJob(KIMAP::Session *session, const Account &account, const QString &secret)
{
    auto *job = new KIMAP::LoginJob(session);
    job->setUserName(account.username.isEmpty() ? account.email : account.username);
    job->setPassword(secret);
    job->setEncryptionMode(encryptionFor(account.incomingSecurity));
    job->setAuthenticationMode(account.authMethod == AuthMethod::OAuth2 ? KIMAP::LoginJob::XOAuth2 : KIMAP::LoginJob::ClearText);
    return job;
}

void ImapConnection::disconnectFromServer()
{
    m_queue.clear();
    if (m_session && m_loggedIn) {
        // Sitzung erst nach der Antwort auf LOGOUT schließen, sonst geht der Befehl verloren.
        KIMAP::Session *session = m_session;
        session->disconnect(this);
        auto *logout = new KIMAP::LogoutJob(session);
        connect(logout, &KJob::result, session, [session] {
            session->close();
            session->deleteLater();
        });
        logout->start();
        m_session = nullptr;
    }
    resetSession();
    m_running = false;
}

} // namespace Fjordpost

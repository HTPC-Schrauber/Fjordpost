#include "accountsetup.h"

#include "auth/oauthtokens.h"
#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "dav/davservice.h"
#include "mail/imap/imapconnection.h"
#include "mail/pop3/pop3client.h"
#include "mail/smtp/smtpsender.h"

#include <KIMAP/LoginJob>
#include <KIMAP/LogoutJob>
#include <KIMAP/Session>
#include <KLocalizedString>

namespace Fjordpost {

namespace {

// Farben für neue Konten (Breeze-Akzentfarben)
const char *const accountColors[] = {"#3daee9", "#e93d58", "#27ae60", "#f67400", "#9b59b6", "#1abc9c", "#fdbc4b", "#e91e63"};

Security securityFromIndex(int index)
{
    switch (index) {
    case 0:
        return Security::None;
    case 1:
        return Security::StartTls;
    default:
        return Security::Tls;
    }
}

int indexFromSecurity(Security s)
{
    switch (s) {
    case Security::None:
        return 0;
    case Security::StartTls:
        return 1;
    case Security::Tls:
        break;
    }
    return 2;
}

} // namespace

AccountSetup::AccountSetup(AccountManager *accounts, SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_accounts(accounts)
    , m_secrets(secrets)
    , m_autoconfig(new AutoConfig(this))
    , m_smtp(new SmtpSender(this))
{
    connect(m_autoconfig, &AutoConfig::progress, this, &AccountSetup::setStatus);
    connect(m_autoconfig, &AutoConfig::finished, this, &AccountSetup::applyDiscovered);
}

void AccountSetup::applyDiscovered(const DiscoveredSettings &settings)
{
    m_settings = settings;
    m_incomingCandidate = 0;
    m_outgoingCandidate = 0;
    applyIncomingCandidate();
    applyOutgoingCandidate();
    setBusy(false);
    setStatus(QString());
    Q_EMIT discovered();
}

void AccountSetup::reset()
{
    cancel();
    m_settings = {};
    m_name.clear();
    m_displayName.clear();
    m_email.clear();
    m_username.clear();
    m_pop3 = false;
    m_leaveOnServer = true;
    m_incomingHost.clear();
    m_incomingPort = 993;
    m_incomingSecurity = 2;
    m_outgoingHost.clear();
    m_outgoingPort = 465;
    m_outgoingSecurity = 2;
    m_needsOAuth = false;
    m_oauthProvider.clear();
    m_oauth = {};
    setError(QString());
    setStatus(QString());
    Q_EMIT settingsChanged();
}

void AccountSetup::cancel()
{
    ++m_generation;
    m_autoconfig->abort();
    if (m_tokens)
        m_tokens->client()->abort();
    setBusy(false);
}

void AccountSetup::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void AccountSetup::setStatus(const QString &status)
{
    if (m_status != status) {
        m_status = status;
        Q_EMIT statusChanged();
    }
}

void AccountSetup::setError(const QString &error)
{
    if (m_error != error) {
        m_error = error;
        Q_EMIT errorTextChanged();
    }
}

void AccountSetup::discover(const QString &email)
{
    if (!email.contains(QLatin1Char('@'))) {
        setError(i18n("Please enter a valid email address."));
        return;
    }
    m_email = email.trimmed();
    if (m_name.isEmpty())
        m_name = m_email.section(QLatin1Char('@'), 1);
    setError(QString());
    setBusy(true);
    ++m_generation;
    m_autoconfig->discover(m_email);
}

void AccountSetup::applyIncomingCandidate()
{
    if (m_incomingCandidate < m_settings.incoming.size()) {
        const ServerSettings &in = m_settings.incoming.at(m_incomingCandidate);
        m_pop3 = in.type == AccountType::Pop3;
        m_incomingHost = in.host;
        m_incomingPort = in.port;
        m_incomingSecurity = indexFromSecurity(in.security);
        m_username = in.username.isEmpty() ? m_email : in.username;
        m_needsOAuth = in.oauth;
        m_oauthProvider = m_pop3 ? QString() : OAuthProvider::idForImapHost(in.host);
        Q_EMIT settingsChanged();
    }
}

void AccountSetup::applyOutgoingCandidate()
{
    if (m_outgoingCandidate < m_settings.outgoing.size()) {
        const ServerSettings &out = m_settings.outgoing.at(m_outgoingCandidate);
        m_outgoingHost = out.host;
        m_outgoingPort = out.port;
        m_outgoingSecurity = indexFromSecurity(out.security);
        Q_EMIT settingsChanged();
    }
}

Account AccountSetup::currentAccount() const
{
    Account a;
    a.name = m_name.isEmpty() ? m_email : m_name;
    a.displayName = m_displayName;
    a.email = m_email;
    a.username = m_username.isEmpty() ? m_email : m_username;
    a.type = m_pop3 ? AccountType::Pop3 : AccountType::Imap;
    a.incomingHost = m_incomingHost.trimmed();
    a.incomingPort = m_incomingPort;
    a.incomingSecurity = securityFromIndex(m_incomingSecurity);
    a.outgoingHost = m_outgoingHost.trimmed();
    a.outgoingPort = m_outgoingPort;
    a.outgoingSecurity = securityFromIndex(m_outgoingSecurity);
    a.leaveOnServer = m_leaveOnServer;
    if (!m_oauth.accessToken.isEmpty()) {
        a.authMethod = AuthMethod::OAuth2;
        a.oauthProvider = m_oauthProvider;
    }
    return a;
}

void AccountSetup::testAndSave(const QString &password)
{
    if (m_email.isEmpty() || m_incomingHost.isEmpty() || m_outgoingHost.isEmpty()) {
        setError(i18n("Please fill in all server settings."));
        return;
    }
    if (password.isEmpty()) {
        setError(i18n("Please enter your password."));
        return;
    }
    setError(QString());
    setBusy(true);
    ++m_generation;
    m_oauth = {};

    m_entered = {m_pop3, m_username, m_incomingHost, m_incomingPort, m_incomingSecurity,
                 m_outgoingHost, m_outgoingPort, m_outgoingSecurity, m_incomingCandidate, m_outgoingCandidate};
    m_tryIncomingCandidates = m_settings.guessed && showsIncomingCandidate();
    m_tryOutgoingCandidates = showsOutgoingCandidate();
    testIncoming(password);
}

bool AccountSetup::showsIncomingCandidate() const
{
    if (m_incomingCandidate >= m_settings.incoming.size())
        return false;
    const ServerSettings &in = m_settings.incoming.at(m_incomingCandidate);
    return (in.type == AccountType::Pop3) == m_pop3 && in.host == m_incomingHost.trimmed() && in.port == m_incomingPort
        && indexFromSecurity(in.security) == m_incomingSecurity;
}

bool AccountSetup::showsOutgoingCandidate() const
{
    if (m_outgoingCandidate >= m_settings.outgoing.size())
        return false;
    const ServerSettings &out = m_settings.outgoing.at(m_outgoingCandidate);
    return out.host == m_outgoingHost.trimmed() && out.port == m_outgoingPort && indexFromSecurity(out.security) == m_outgoingSecurity;
}

void AccountSetup::restoreEntered(bool incoming, bool outgoing)
{
    if (incoming) {
        m_pop3 = m_entered.pop3;
        m_username = m_entered.username;
        m_incomingHost = m_entered.incomingHost;
        m_incomingPort = m_entered.incomingPort;
        m_incomingSecurity = m_entered.incomingSecurity;
        m_incomingCandidate = m_entered.incomingCandidate;
    }
    if (outgoing) {
        m_outgoingHost = m_entered.outgoingHost;
        m_outgoingPort = m_entered.outgoingPort;
        m_outgoingSecurity = m_entered.outgoingSecurity;
        m_outgoingCandidate = m_entered.outgoingCandidate;
    }
    Q_EMIT settingsChanged();
}

void AccountSetup::testIncoming(const QString &password)
{
    const Account account = currentAccount();
    setStatus(i18n("Signing in to %1…", account.incomingHost));
    const int generation = m_generation;

    if (account.type == AccountType::Pop3) {
        testPop3Login(account, password, this, [this, password, generation](bool ok, const QString &error) {
            if (generation == m_generation)
                incomingTested(ok, error, password);
        });
        return;
    }

    auto *session = new KIMAP::Session(account.incomingHost, quint16(account.incomingPort), this);
    session->setTimeout(20);
    auto *login = ImapConnection::createLoginJob(session, account, password);
    connect(login, &KJob::result, this, [this, session, login, password, generation](KJob *) {
        const bool ok = !login->error();
        const QString error = login->errorString();
        if (ok)
            (new KIMAP::LogoutJob(session))->start();
        session->deleteLater();
        if (generation == m_generation)
            incomingTested(ok, error, password);
    });
    login->start();
}

void AccountSetup::incomingTested(bool ok, const QString &error, const QString &password)
{
    if (ok) {
        testOutgoing(password);
        return;
    }
    // Bei geratenen Einstellungen nächsten Kandidaten probieren.
    if (m_tryIncomingCandidates && m_incomingCandidate + 1 < m_settings.incoming.size()) {
        ++m_incomingCandidate;
        applyIncomingCandidate();
        testIncoming(password);
        return;
    }
    restoreEntered(true, true);
    setBusy(false);
    setStatus(QString());
    setError(i18n("Could not sign in to the incoming server: %1", error));
}

void AccountSetup::testOutgoing(const QString &password)
{
    const Account account = currentAccount();
    setStatus(i18n("Checking outgoing server %1…", account.outgoingHost));
    const int generation = m_generation;
    m_smtp->testLogin(account, password, [this, password, generation](bool ok, const QString &error) {
        if (generation != m_generation)
            return;
        if (ok) {
            save(password);
            return;
        }
        if (m_tryOutgoingCandidates && m_outgoingCandidate + 1 < m_settings.outgoing.size()) {
            ++m_outgoingCandidate;
            applyOutgoingCandidate();
            testOutgoing(password);
            return;
        }
        // Ein funktionierender Eingangsserver bleibt stehen, nur SMTP zurücksetzen.
        restoreEntered(false, true);
        setBusy(false);
        setStatus(QString());
        setError(i18n("Could not sign in to the outgoing server: %1", error));
    });
}

QString AccountSetup::oauthProviderName() const
{
    return OAuthProvider::forId(m_oauthProvider).name;
}

bool AccountSetup::oauthConfigured() const
{
    return m_tokens && OAuthProvider::forId(m_oauthProvider).isConfigured();
}

bool AccountSetup::oauthHasDav() const
{
    return m_dav && OAuthProvider::forId(m_oauthProvider).hasDav();
}

void AccountSetup::signInWithBrowser(bool withDav)
{
    const OAuthProvider provider = OAuthProvider::forId(m_oauthProvider);
    if (!m_tokens || !provider.isConfigured()) {
        setError(i18n("Sign-in through %1 is not set up: the client ID is missing in fjordpost.conf.", provider.name));
        return;
    }
    setError(QString());
    setBusy(true);
    ++m_generation;
    const int generation = m_generation;
    m_oauth = {};
    m_oauthWithDav = withDav && oauthHasDav();
    setStatus(i18n("Waiting for the sign-in in your web browser…"));

    QStringList scopes = provider.mailScopes;
    if (m_oauthWithDav)
        scopes += provider.davScopes;
    m_tokens->client()->authorize(provider, scopes, m_email, [this, generation, provider](const OAuthClient::Result &result) {
        if (generation != m_generation)
            return;
        QString error = result.error;
        if (error.isEmpty() && result.refreshToken.isEmpty())
            error = i18n("%1 did not grant lasting access. Please try again.", provider.name);
        if (!error.isEmpty()) {
            setBusy(false);
            setStatus(QString());
            setError(error);
            return;
        }
        m_oauth = result;
        // Der Anbieter gibt die Server vor; angemeldet wird mit der Adresse des Kontos
        // (bei Aliasen die Hauptadresse).
        m_entered = {m_pop3, m_username, m_incomingHost, m_incomingPort, m_incomingSecurity,
                     m_outgoingHost, m_outgoingPort, m_outgoingSecurity, m_incomingCandidate, m_outgoingCandidate};
        m_pop3 = false;
        m_username = result.email.isEmpty() ? m_email : result.email;
        m_incomingHost = provider.imapHosts.constFirst();
        m_incomingPort = 993;
        m_incomingSecurity = indexFromSecurity(Security::Tls);
        m_outgoingHost = provider.smtpHost;
        m_outgoingPort = provider.smtpPort;
        m_outgoingSecurity = indexFromSecurity(provider.smtpStartTls ? Security::StartTls : Security::Tls);
        Q_EMIT settingsChanged();
        m_tryIncomingCandidates = false;
        m_tryOutgoingCandidates = false;
        testIncoming(result.accessToken);
    });
}

void AccountSetup::save(const QString &password)
{
    setStatus(i18n("Saving account…"));
    Account account = currentAccount();
    account.color = QColor(QString::fromLatin1(accountColors[m_accounts->accounts().size() % std::size(accountColors)]));
    account.sortOrder = int(m_accounts->accounts().size());
    // Erst deaktiviert anlegen, damit der Abgleich nicht vor dem Speichern
    // des Passworts startet; danach aktivieren.
    account.enabled = false;
    account = m_accounts->save(account);
    if (!account.isValid()) {
        setBusy(false);
        setError(i18n("The account could not be saved."));
        return;
    }

    const qint64 id = account.id;
    auto stored = [this, id](bool ok) {
        Account a = m_accounts->account(id);
        if (!ok) {
            m_accounts->remove(id);
            setBusy(false);
            setStatus(QString());
            setError(i18n("The password could not be stored in the wallet. Is KWallet available?"));
            return;
        }
        a.enabled = true;
        m_accounts->save(a);
        finishSaved(id);
    };
    if (account.authMethod == AuthMethod::OAuth2)
        m_tokens->store(account.secretKey(), account.oauthProvider, m_oauth, stored);
    else
        m_secrets->write(account.secretKey(), password, stored);
}

void AccountSetup::finishSaved(qint64 accountId)
{
    if (!m_oauthWithDav || m_oauth.accessToken.isEmpty() || !m_dav) {
        setBusy(false);
        setStatus(QString());
        Q_EMIT saved(accountId);
        return;
    }
    setStatus(i18n("Setting up calendars and contacts…"));
    const OAuthProvider provider = OAuthProvider::forId(m_oauthProvider);
    m_dav->addOAuthAccount(provider, m_username, m_oauth, accountId, [this, accountId, provider](const QString &error) {
        setBusy(false);
        setStatus(QString());
        Q_EMIT saved(accountId);
        // Das Mailkonto bleibt; Kalender lassen sich später über die DAV-Einstellungen nachtragen.
        if (!error.isEmpty())
            Q_EMIT warning(i18n("Calendars and contacts from %1 could not be set up: %2", provider.name, error));
    });
}

} // namespace Fjordpost

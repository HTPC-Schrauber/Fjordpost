#include "oauthtokens.h"

#include <KLocalizedString>

#include <QPointer>

namespace Fjordpost {

OAuthTokens::OAuthTokens(SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
    , m_client(new OAuthClient(this))
{
}

void OAuthTokens::accessToken(const QString &secretKey, const QString &providerId, Callback callback)
{
    Entry &entry = m_entries[secretKey];
    entry.provider = providerId;
    if (!entry.accessToken.isEmpty() && QDateTime::currentDateTimeUtc().secsTo(entry.expiresAt) > marginSeconds) {
        callback(entry.accessToken, {});
        return;
    }
    entry.waiting << std::move(callback);
    if (entry.busy)
        return;
    entry.busy = true;

    if (!entry.refreshToken.isEmpty()) {
        renew(secretKey);
        return;
    }
    m_secrets->read(secretKey, [this, secretKey](bool ok, const QString &secret) {
        if (!m_entries.contains(secretKey))
            return;
        if (!ok || secret.isEmpty()) {
            deliver(secretKey, {}, i18n("No sign-in stored for this account. Please sign in again."));
            Q_EMIT signInRequired(secretKey);
            return;
        }
        m_entries[secretKey].refreshToken = secret;
        renew(secretKey);
    });
}

void OAuthTokens::renew(const QString &secretKey)
{
    const Entry &entry = m_entries[secretKey];
    const OAuthProvider provider = OAuthProvider::forId(entry.provider);
    if (!provider.isConfigured()) {
        deliver(secretKey, {}, i18n("Sign-in through %1 is not set up: the client ID is missing in fjordpost.conf.", provider.isValid() ? provider.name : entry.provider));
        return;
    }
    const QString used = entry.refreshToken;
    m_client->refresh(provider, used, [this, secretKey, used](const OAuthClient::Result &result) {
        if (!m_entries.contains(secretKey))
            return;
        Entry &entry = m_entries[secretKey];
        if (!result.error.isEmpty()) {
            deliver(secretKey, {}, result.error);
            if (result.invalidGrant)
                Q_EMIT signInRequired(secretKey);
            return;
        }
        entry.accessToken = result.accessToken;
        entry.expiresAt = result.expiresAt;
        // Microsoft gibt bei jeder Erneuerung ein neues Refresh-Token aus.
        if (!result.refreshToken.isEmpty() && result.refreshToken != used) {
            entry.refreshToken = result.refreshToken;
            m_secrets->write(secretKey, result.refreshToken);
        }
        deliver(secretKey, entry.accessToken, {});
    });
}

void OAuthTokens::deliver(const QString &secretKey, const QString &token, const QString &error)
{
    Entry &entry = m_entries[secretKey];
    entry.busy = false;
    const QList<Callback> waiting = std::exchange(entry.waiting, {});
    for (const Callback &callback : waiting)
        callback(token, error);
}

void OAuthTokens::invalidate(const QString &secretKey)
{
    auto it = m_entries.find(secretKey);
    if (it != m_entries.end())
        it->accessToken.clear();
}

void OAuthTokens::store(const QString &secretKey, const QString &providerId, const OAuthClient::Result &result, SecretStore::WriteCallback done)
{
    m_secrets->write(secretKey, result.refreshToken, [this, secretKey, providerId, result, done = std::move(done)](bool ok) {
        if (ok) {
            Entry &entry = m_entries[secretKey];
            entry.provider = providerId;
            entry.refreshToken = result.refreshToken;
            entry.accessToken = result.accessToken;
            entry.expiresAt = result.expiresAt;
        }
        if (done)
            done(ok);
    });
}

void OAuthTokens::forget(const QString &secretKey)
{
    // Wartende bekommen noch eine Antwort, sonst hingen ihre Verbindungen.
    if (m_entries.contains(secretKey))
        deliver(secretKey, {}, i18n("The account was removed."));
    m_entries.remove(secretKey);
}

TokenSource OAuthTokens::fetcher(const QString &secretKey, const QString &providerId)
{
    QPointer<OAuthTokens> self(this);
    return [self, secretKey, providerId](Callback callback) {
        if (!self) {
            callback({}, i18n("The application is shutting down."));
            return;
        }
        self->accessToken(secretKey, providerId, std::move(callback));
    };
}

} // namespace Fjordpost

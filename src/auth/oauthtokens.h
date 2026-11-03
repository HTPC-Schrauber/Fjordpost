#pragma once

#include "auth/oauthclient.h"
#include "core/account.h"
#include "core/secretstore.h"

#include <QDateTime>
#include <QHash>
#include <QObject>

#include <functional>

namespace Fjordpost {

// Verwaltet die Tokens aller OAuth-Konten. Im SecretStore liegt nur das
// Refresh-Token (unter dem secretKey des Kontos); Zugriffstokens leben eine
// Stunde und bleiben im Speicher.
class OAuthTokens : public QObject
{
    Q_OBJECT
public:
    using Callback = TokenCallback;

    explicit OAuthTokens(SecretStore *secrets, QObject *parent = nullptr);

    OAuthClient *client() const { return m_client; }

    // Liefert ein Zugriffstoken, das noch einige Minuten gilt; erneuert es bei Bedarf.
    // Gleichzeitige Anfragen für dasselbe Konto teilen sich eine Erneuerung.
    void accessToken(const QString &secretKey, const QString &providerId, Callback callback);
    // Nach abgelehnter Anmeldung am Server: beim nächsten Mal neu holen
    void invalidate(const QString &secretKey);
    // Ergebnis einer Anmeldung im Browser sichern
    void store(const QString &secretKey, const QString &providerId, const OAuthClient::Result &result, SecretStore::WriteCallback done);
    void forget(const QString &secretKey);

    // Fertige Funktion für Verbindungen, die vor jeder Anmeldung ein Token brauchen
    TokenSource fetcher(const QString &secretKey, const QString &providerId);

    static constexpr int marginSeconds = 5 * 60;

Q_SIGNALS:
    // Refresh-Token fehlt, ist abgelaufen oder widerrufen: Anmeldung im Browser nötig
    void signInRequired(const QString &secretKey);

private:
    struct Entry {
        QString provider;
        QString refreshToken;
        QString accessToken;
        QDateTime expiresAt;
        QList<Callback> waiting;
        bool busy = false;
    };
    void renew(const QString &secretKey);
    void deliver(const QString &secretKey, const QString &token, const QString &error);

    SecretStore *m_secrets;
    OAuthClient *m_client;
    QHash<QString, Entry> m_entries;
};

} // namespace Fjordpost

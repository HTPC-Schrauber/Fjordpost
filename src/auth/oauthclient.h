#pragma once

#include "auth/oauthprovider.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>

#include <functional>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;
class QUrlQuery;

namespace Fjordpost {

// OAuth2 für Desktop-Apps (RFC 8252): Anmeldung im Browser des Nutzers, Rückkehr
// über einen lokalen HTTP-Server auf localhost, Code-Tausch mit PKCE (RFC 7636).
// Bewusst ohne Qt NetworkAuth, dessen API sich zwischen 6.8 und 6.9 stark geändert hat.
class OAuthClient : public QObject
{
    Q_OBJECT
public:
    struct Result {
        QString error;             // leer bei Erfolg
        bool invalidGrant = false; // Refresh-Token widerrufen oder abgelaufen: neu anmelden
        QString accessToken;
        QString refreshToken;      // nur bei der Anmeldung und wenn der Anbieter es erneuert
        QDateTime expiresAt;
        QString email;             // aus dem ID-Token, falls vorhanden
    };
    using Callback = std::function<void(const Result &result)>;
    using BrowserOpener = std::function<bool(const QUrl &url)>;

    explicit OAuthClient(QObject *parent = nullptr);
    ~OAuthClient() override;

    // Öffnet den Browser und wartet auf die Rückkehr. Nur ein Vorgang gleichzeitig.
    void authorize(const OAuthProvider &provider, const QStringList &scopes, const QString &loginHint, Callback callback);
    // Bricht eine laufende Anmeldung ab; der Rückruf entfällt.
    void abort();
    void refresh(const OAuthProvider &provider, const QString &refreshToken, Callback callback);

    // Für Tests: statt des Browsers aufrufen
    void setBrowserOpener(BrowserOpener opener) { m_openBrowser = std::move(opener); }

    static QString emailFromIdToken(const QByteArray &idToken);
    static QByteArray codeChallenge(const QByteArray &verifier);

private:
    void readRedirect(QTcpSocket *socket);
    void finishAuthorization(const Result &result);
    void requestToken(const OAuthProvider &provider, const QUrlQuery &body, Callback callback);
    void closeServers();

    QNetworkAccessManager *m_network;
    BrowserOpener m_openBrowser;

    // laufende Anmeldung
    QList<QTcpServer *> m_servers;
    OAuthProvider m_provider;
    QString m_redirectUri;
    QByteArray m_state;
    QByteArray m_verifier;
    Callback m_callback;
    QPointer<QObject> m_timeout;
};

} // namespace Fjordpost

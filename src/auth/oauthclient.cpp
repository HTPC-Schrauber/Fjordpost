#include "oauthclient.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcOAuth, "fjordpost.oauth")

namespace Fjordpost {

namespace {

constexpr int authorizationTimeoutMs = 10 * 60 * 1000;
constexpr int requestTimeoutMs = 30 * 1000;
constexpr qsizetype maxRequestSize = 16 * 1024;

QByteArray base64Url(const QByteArray &data)
{
    return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(bytes.data()), count / int(sizeof(quint32)));
    return bytes;
}

QByteArray page(const QString &title, const QString &text)
{
    // Kleine Seite für den Browser nach der Rückkehr; Texte sind eigene, nicht von außen.
    const QString html = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>Fjordpost</title>"
                                        "<style>body{font-family:sans-serif;margin:4em auto;max-width:32em;text-align:center}</style></head>"
                                        "<body><h1>%1</h1><p>%2</p></body></html>")
                             .arg(title.toHtmlEscaped(), text.toHtmlEscaped());
    return html.toUtf8();
}

OAuthClient::Result failed(const QString &error)
{
    OAuthClient::Result result;
    result.error = error;
    return result;
}

void respond(QTcpSocket *socket, const QByteArray &status, const QByteArray &body)
{
    socket->write("HTTP/1.1 " + status + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + QByteArray::number(body.size())
                  + "\r\nConnection: close\r\n\r\n" + body);
    socket->disconnectFromHost();
}

} // namespace

OAuthClient::OAuthClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_openBrowser([](const QUrl &url) {
        return QDesktopServices::openUrl(url);
    })
{
}

OAuthClient::~OAuthClient()
{
    closeServers();
}

QByteArray OAuthClient::codeChallenge(const QByteArray &verifier)
{
    return base64Url(QCryptographicHash::hash(verifier, QCryptographicHash::Sha256));
}

QString OAuthClient::emailFromIdToken(const QByteArray &idToken)
{
    // Die Signatur prüfen wir nicht: Das Token kommt direkt per TLS vom Token-Endpunkt.
    const QList<QByteArray> parts = idToken.split('.');
    if (parts.size() < 2)
        return {};
    const auto decoded = QByteArray::fromBase64Encoding(parts.at(1), QByteArray::Base64UrlEncoding);
    if (!decoded)
        return {};
    const QJsonObject claims = QJsonDocument::fromJson(*decoded).object();
    QString email = claims.value(QLatin1String("email")).toString();
    if (email.isEmpty()) // Microsoft-Firmenkonten
        email = claims.value(QLatin1String("preferred_username")).toString();
    return email.contains(QLatin1Char('@')) ? email : QString();
}

void OAuthClient::authorize(const OAuthProvider &provider, const QStringList &scopes, const QString &loginHint, Callback callback)
{
    abort();
    m_provider = provider;
    m_callback = std::move(callback);
    m_state = base64Url(randomBytes(16));
    m_verifier = base64Url(randomBytes(48));

    // Der Browser löst "localhost" je nach System nach IPv4 oder IPv6 auf: auf beiden lauschen.
    auto *v4 = new QTcpServer(this);
    if (!v4->listen(QHostAddress::LocalHost)) {
        delete v4;
        finishAuthorization(failed(i18n("No local port for the sign-in could be opened.")));
        return;
    }
    m_servers << v4;
    auto *v6 = new QTcpServer(this);
    if (v6->listen(QHostAddress::LocalHostIPv6, v4->serverPort()))
        m_servers << v6;
    else
        delete v6;
    for (QTcpServer *server : std::as_const(m_servers)) {
        connect(server, &QTcpServer::newConnection, this, [this, server] {
            while (QTcpSocket *socket = server->nextPendingConnection()) {
                // Der Server wird nach der Rückkehr geschlossen, die Antwortseite muss trotzdem ankommen.
                socket->setParent(this);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    readRedirect(socket);
                });
            }
        });
    }
    // ohne Pfad: Microsoft vergleicht mit der registrierten Adresse "http://localhost" (Port egal)
    m_redirectUri = QStringLiteral("http://localhost:%1").arg(v4->serverPort());

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    query.addQueryItem(QStringLiteral("client_id"), provider.clientId);
    query.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    query.addQueryItem(QStringLiteral("scope"), scopes.join(QLatin1Char(' ')));
    query.addQueryItem(QStringLiteral("state"), QString::fromLatin1(m_state));
    query.addQueryItem(QStringLiteral("code_challenge"), QString::fromLatin1(codeChallenge(m_verifier)));
    query.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    if (!loginHint.isEmpty())
        query.addQueryItem(QStringLiteral("login_hint"), loginHint);
    for (const auto &[key, value] : provider.extraParameters)
        query.addQueryItem(key, value);
    // '+' im Wert (z. B. Adressen) sonst als Leerzeichen gelesen
    QUrl url = provider.authorizationUrl;
    url.setQuery(query.query(QUrl::FullyEncoded).replace(QLatin1Char('+'), QLatin1String("%2B")));

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this] {
        finishAuthorization(failed(i18n("The sign-in in the browser was not completed in time.")));
    });
    timer->start(authorizationTimeoutMs);
    m_timeout = timer;

    if (!m_openBrowser(url))
        finishAuthorization(failed(i18n("The web browser could not be opened.")));
}

void OAuthClient::abort()
{
    closeServers();
    m_callback = nullptr;
}

void OAuthClient::closeServers()
{
    for (QTcpServer *server : std::as_const(m_servers)) {
        server->close();
        server->deleteLater();
    }
    m_servers.clear();
    if (m_timeout)
        m_timeout->deleteLater();
    m_timeout = nullptr;
}

void OAuthClient::finishAuthorization(const Result &result)
{
    closeServers();
    Callback callback = std::exchange(m_callback, nullptr);
    if (callback)
        callback(result);
}

void OAuthClient::readRedirect(QTcpSocket *socket)
{
    if (!m_callback) {
        socket->abort();
        return;
    }
    // Ganze Kopfzeilen abwarten; mehr als die Anfragezeile brauchen wir nicht.
    QByteArray buffer = socket->property("buffer").toByteArray() + socket->readAll();
    if (buffer.size() > maxRequestSize) {
        socket->abort();
        return;
    }
    if (!buffer.contains("\r\n\r\n")) {
        socket->setProperty("buffer", buffer);
        return;
    }
    const QList<QByteArray> requestLine = buffer.left(buffer.indexOf("\r\n")).split(' ');
    if (requestLine.size() < 2 || requestLine.at(0) != "GET") {
        respond(socket, "400 Bad Request", {});
        return;
    }
    const QUrl target = QUrl::fromEncoded(requestLine.at(1));
    const QUrlQuery query(target);
    if (target.path() != QLatin1String("/") || query.queryItemValue(QStringLiteral("state")).toLatin1() != m_state) {
        // z. B. favicon.ico oder ein fremder Aufruf: nicht als Antwort werten
        respond(socket, "404 Not Found", {});
        return;
    }

    const QString code = query.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
    if (code.isEmpty()) {
        const QString error = query.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
        respond(socket, "200 OK", page(i18n("Sign-in cancelled"), i18n("You can close this window and return to Fjordpost.")));
        qCInfo(lcOAuth) << "Anmeldung abgelehnt:" << error;
        finishAuthorization(failed(error == QLatin1String("access_denied") ? i18n("The sign-in was cancelled.") : i18n("The provider refused the sign-in (%1).", error)));
        return;
    }
    respond(socket, "200 OK", page(i18n("Signed in"), i18n("You can close this window and return to Fjordpost.")));

    // Ab hier kein weiterer Rückweg nötig; den Code gegen Tokens tauschen.
    closeServers();
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("authorization_code"));
    body.addQueryItem(QStringLiteral("code"), code);
    body.addQueryItem(QStringLiteral("redirect_uri"), m_redirectUri);
    body.addQueryItem(QStringLiteral("code_verifier"), QString::fromLatin1(m_verifier));
    requestToken(m_provider, body, [this](const Result &result) {
        finishAuthorization(result);
    });
}

void OAuthClient::refresh(const OAuthProvider &provider, const QString &refreshToken, Callback callback)
{
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    requestToken(provider, body, std::move(callback));
}

void OAuthClient::requestToken(const OAuthProvider &provider, const QUrlQuery &parameters, Callback callback)
{
    QUrlQuery body = parameters;
    body.addQueryItem(QStringLiteral("client_id"), provider.clientId);
    if (!provider.clientSecret.isEmpty())
        body.addQueryItem(QStringLiteral("client_secret"), provider.clientSecret);

    QNetworkRequest request(provider.tokenUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Fjordpost"));
    request.setTransferTimeout(requestTimeoutMs);
    // Formularkodierung: '+' wäre sonst ein Leerzeichen
    const QByteArray data = body.query(QUrl::FullyEncoded).replace(QLatin1Char('+'), QLatin1String("%2B")).toUtf8();
    QNetworkReply *reply = m_network->post(request, data);
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        Result result;
        if (status == 0) {
            result.error = reply->errorString();
        } else if (status != 200 || json.value(QLatin1String("access_token")).toString().isEmpty()) {
            const QString code = json.value(QLatin1String("error")).toString();
            result.invalidGrant = code == QLatin1String("invalid_grant");
            const QString description = json.value(QLatin1String("error_description")).toString();
            qCWarning(lcOAuth) << "Token-Anfrage fehlgeschlagen:" << status << code << description;
            result.error = result.invalidGrant ? i18n("The sign-in has expired or was revoked. Please sign in again.")
                                               : i18n("The provider rejected the request (%1).", code.isEmpty() ? QString::number(status) : code);
        } else {
            result.accessToken = json.value(QLatin1String("access_token")).toString();
            result.refreshToken = json.value(QLatin1String("refresh_token")).toString();
            const int expiresIn = json.value(QLatin1String("expires_in")).toInt(3600);
            result.expiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
            result.email = emailFromIdToken(json.value(QLatin1String("id_token")).toString().toLatin1());
        }
        callback(result);
    });
}

} // namespace Fjordpost

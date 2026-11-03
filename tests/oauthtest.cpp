#include "auth/oauthclient.h"
#include "auth/oauthprovider.h"
#include "dav/davclient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

#include <functional>

using namespace Fjordpost;

namespace {

// Minimaler HTTP-Server: eine Anfrage pro Verbindung, Antwort aus dem Handler.
class FakeHttp : public QObject
{
public:
    struct Request {
        QByteArray method;
        QByteArray path;
        QHash<QByteArray, QByteArray> headers; // Namen kleingeschrieben
        QByteArray body;
    };
    using Handler = std::function<QPair<int, QByteArray>(const Request &request)>;

    explicit FakeHttp(Handler handler)
        : m_handler(std::move(handler))
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    read(socket);
                });
            }
        });
    }

    QUrl url(const QString &path) const { return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path)); }
    QList<Request> requests;

private:
    void read(QTcpSocket *socket)
    {
        QByteArray data = socket->property("data").toByteArray() + socket->readAll();
        socket->setProperty("data", data);
        const qsizetype end = data.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Request r;
        const QList<QByteArray> lines = data.left(end).split('\n');
        const QList<QByteArray> first = lines.constFirst().trimmed().split(' ');
        r.method = first.value(0);
        r.path = first.value(1);
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0)
                r.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        const int length = r.headers.value("content-length").toInt();
        if (data.size() < end + 4 + length)
            return;
        r.body = data.mid(end + 4, length);
        requests << r;
        const auto [status, body] = m_handler(r);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: application/json\r\nContent-Length: "
                      + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    Handler m_handler;
};

QByteArray idToken(const QJsonObject &claims)
{
    const auto part = [](const QByteArray &data) {
        return data.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    };
    return part("{\"alg\":\"none\"}") + '.' + part(QJsonDocument(claims).toJson(QJsonDocument::Compact)) + ".sig";
}

OAuthProvider fakeProvider(const FakeHttp &http)
{
    OAuthProvider p;
    p.id = QStringLiteral("fake");
    p.name = QStringLiteral("Fake");
    p.authorizationUrl = QUrl(QStringLiteral("https://auth.example.com/authorize"));
    p.tokenUrl = http.url(QStringLiteral("/token"));
    p.clientId = QStringLiteral("client+id");
    p.clientSecret = QStringLiteral("geheim");
    p.extraParameters = {{QStringLiteral("access_type"), QStringLiteral("offline")}};
    return p;
}

} // namespace

class OAuthTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pkceChallenge()
    {
        // Beispiel aus RFC 7636, Anhang B
        QCOMPARE(OAuthClient::codeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"), QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void idTokenEmail()
    {
        QCOMPARE(OAuthClient::emailFromIdToken(idToken({{QStringLiteral("email"), QStringLiteral("max@gmail.com")}})), QStringLiteral("max@gmail.com"));
        // Microsoft-Firmenkonten ohne email-Claim
        QCOMPARE(OAuthClient::emailFromIdToken(idToken({{QStringLiteral("preferred_username"), QStringLiteral("max@firma.de")}})), QStringLiteral("max@firma.de"));
        QCOMPARE(OAuthClient::emailFromIdToken(idToken({{QStringLiteral("preferred_username"), QStringLiteral("kein-mail")}})), QString());
        QCOMPARE(OAuthClient::emailFromIdToken("kaputt"), QString());
    }

    void providers()
    {
        QCOMPARE(OAuthProvider::idForImapHost(QStringLiteral("IMAP.gmail.com")), QStringLiteral("google"));
        QCOMPARE(OAuthProvider::idForImapHost(QStringLiteral("outlook.office365.com")), QStringLiteral("microsoft"));
        QCOMPARE(OAuthProvider::idForImapHost(QStringLiteral("imap.example.com")), QString());
        QVERIFY(OAuthProvider::forId(QStringLiteral("google")).hasDav());
        QVERIFY(!OAuthProvider::forId(QStringLiteral("microsoft")).hasDav());
        QVERIFY(!OAuthProvider::forId(QStringLiteral("unbekannt")).isValid());
    }

    void authorizeFlow()
    {
        FakeHttp http([](const FakeHttp::Request &) {
            QJsonObject reply{{QStringLiteral("access_token"), QStringLiteral("zugriff")},
                              {QStringLiteral("refresh_token"), QStringLiteral("erneuern")},
                              {QStringLiteral("expires_in"), 3599},
                              {QStringLiteral("id_token"), QString::fromLatin1(idToken({{QStringLiteral("email"), QStringLiteral("max@gmail.com")}}))}};
            return qMakePair(200, QJsonDocument(reply).toJson());
        });

        OAuthClient client;
        QNetworkAccessManager browser;
        QUrl opened;
        client.setBrowserOpener([&](const QUrl &url) {
            opened = url;
            // Wie der Browser nach der Zustimmung: zurück zur lokalen Adresse
            const QUrlQuery q(url);
            QUrl back(q.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
            back.setQuery(QStringLiteral("state=%1&code=4/abc%2Bdef").arg(q.queryItemValue(QStringLiteral("state"))));
            back.setHost(QStringLiteral("127.0.0.1"));
            QNetworkReply *reply = browser.get(QNetworkRequest(back));
            connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
            return true;
        });

        bool done = false;
        OAuthClient::Result result;
        client.authorize(fakeProvider(http), {QStringLiteral("mail"), QStringLiteral("openid")}, QStringLiteral("max+test@gmail.com"), [&](const OAuthClient::Result &r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(result.error, QString());
        QCOMPARE(result.accessToken, QStringLiteral("zugriff"));
        QCOMPARE(result.refreshToken, QStringLiteral("erneuern"));
        QCOMPARE(result.email, QStringLiteral("max@gmail.com"));
        QVERIFY(QDateTime::currentDateTimeUtc().secsTo(result.expiresAt) > 3500);

        const QUrlQuery auth(opened);
        QCOMPARE(auth.queryItemValue(QStringLiteral("client_id"), QUrl::FullyDecoded), QStringLiteral("client+id"));
        QCOMPARE(auth.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded), QStringLiteral("mail openid"));
        QCOMPARE(auth.queryItemValue(QStringLiteral("login_hint"), QUrl::FullyDecoded), QStringLiteral("max+test@gmail.com"));
        QCOMPARE(auth.queryItemValue(QStringLiteral("access_type")), QStringLiteral("offline"));
        QCOMPARE(auth.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));

        QCOMPARE(http.requests.size(), 1);
        const QUrlQuery body(QString::fromUtf8(http.requests.constFirst().body));
        QCOMPARE(body.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
        QCOMPARE(body.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("4/abc+def"));
        QCOMPARE(body.queryItemValue(QStringLiteral("client_secret")), QStringLiteral("geheim"));
        QCOMPARE(body.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded), auth.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
        // PKCE: der Verifier passt zur Challenge aus der Anmeldeadresse
        QCOMPARE(OAuthClient::codeChallenge(body.queryItemValue(QStringLiteral("code_verifier")).toLatin1()), auth.queryItemValue(QStringLiteral("code_challenge")).toLatin1());
    }

    void authorizeWrongStateIgnored()
    {
        FakeHttp http([](const FakeHttp::Request &) {
            return qMakePair(500, QByteArray());
        });
        OAuthClient client;
        QNetworkAccessManager browser;
        int status = 0;
        client.setBrowserOpener([&](const QUrl &url) {
            QUrl back(QUrlQuery(url).queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
            back.setQuery(QStringLiteral("state=falsch&code=x"));
            back.setHost(QStringLiteral("127.0.0.1"));
            QNetworkReply *reply = browser.get(QNetworkRequest(back));
            connect(reply, &QNetworkReply::finished, reply, [&status, reply] {
                status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                reply->deleteLater();
            });
            return true;
        });
        bool done = false;
        client.authorize(fakeProvider(http), {}, {}, [&](const OAuthClient::Result &) {
            done = true;
        });
        QTRY_COMPARE(status, 404);
        QVERIFY(!done);
        QVERIFY(http.requests.isEmpty());
        client.abort();
    }

    void authorizeDenied()
    {
        FakeHttp http([](const FakeHttp::Request &) {
            return qMakePair(500, QByteArray());
        });
        OAuthClient client;
        QNetworkAccessManager browser;
        client.setBrowserOpener([&](const QUrl &url) {
            const QUrlQuery q(url);
            QUrl back(q.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
            back.setQuery(QStringLiteral("state=%1&error=access_denied").arg(q.queryItemValue(QStringLiteral("state"))));
            back.setHost(QStringLiteral("127.0.0.1"));
            QNetworkReply *reply = browser.get(QNetworkRequest(back));
            connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
            return true;
        });
        bool done = false;
        OAuthClient::Result result;
        client.authorize(fakeProvider(http), {}, {}, [&](const OAuthClient::Result &r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.accessToken.isEmpty());
        QVERIFY(http.requests.isEmpty());
    }

    void refreshInvalidGrant()
    {
        FakeHttp http([](const FakeHttp::Request &) {
            return qMakePair(400, QByteArray(R"({"error":"invalid_grant","error_description":"Token has been expired or revoked."})"));
        });
        OAuthClient client;
        bool done = false;
        OAuthClient::Result result;
        client.refresh(fakeProvider(http), QStringLiteral("alt"), [&](const OAuthClient::Result &r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY(result.invalidGrant);
        QVERIFY(!result.error.isEmpty());
        const QUrlQuery body(QString::fromUtf8(http.requests.constFirst().body));
        QCOMPARE(body.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(body.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("alt"));
    }

    void davBearerRetry()
    {
        // Erstes Token gilt nicht mehr: nach 401 genau einmal mit frischem Token wiederholen.
        FakeHttp http([](const FakeHttp::Request &r) {
            return r.headers.value("authorization") == "Bearer neu" ? qMakePair(207, QByteArray("<d:multistatus xmlns:d=\"DAV:\"/>")) : qMakePair(401, QByteArray());
        });
        DavClient client;
        QString token = QStringLiteral("alt");
        int invalidated = 0;
        client.setTokenSource([&](TokenCallback callback) {
            callback(token, {});
        }, [&] {
            ++invalidated;
            token = QStringLiteral("neu");
        });
        bool done = false;
        int status = 0;
        client.propfind(http.url(QStringLiteral("/dav/")), 0, "<x/>", [&](const DavReply &reply) {
            status = reply.status;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(status, 207);
        QCOMPARE(invalidated, 1);
        QCOMPARE(http.requests.size(), 2);

        // Bleibt es bei 401, kein endloses Wiederholen
        token = QStringLiteral("kaputt");
        client.setTokenSource([&](TokenCallback callback) {
            callback(QStringLiteral("kaputt"), {});
        }, [&] {
            ++invalidated;
        });
        done = false;
        client.propfind(http.url(QStringLiteral("/dav/")), 0, "<x/>", [&](const DavReply &reply) {
            status = reply.status;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(status, 401);
        QCOMPARE(invalidated, 2);
        QCOMPARE(http.requests.size(), 4);
    }
};

QTEST_GUILESS_MAIN(OAuthTest)
#include "oauthtest.moc"

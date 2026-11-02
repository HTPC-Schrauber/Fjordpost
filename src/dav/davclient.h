#pragma once

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace Fjordpost {

// Eine Eigenschaft aus einer Multistatus-Antwort
struct DavProperty {
    QString text;        // Textinhalt
    QStringList hrefs;   // enthaltene <href>
    QStringList names;   // lokale Namen der Kindelemente (auch verschachtelt)
    QStringList comps;   // name-Attribute von <comp>
};

struct DavResource {
    QString href;
    QHash<QString, DavProperty> props; // nach lokalem Namen; nur erfolgreiche propstat
    bool has(const QString &name) const { return props.contains(name); }
    QString text(const QString &name) const { return props.value(name).text; }
};

struct DavReply {
    int status = 0;          // HTTP-Status, 0 = keine Verbindung
    QString error;           // lesbare Meldung bei Fehlern
    QList<DavResource> resources;
    QByteArray body;
    QString etag;
    QByteArray davHeader;    // Kopfzeile "DAV" (OPTIONS)
    QUrl url;                // tatsächlich beantwortete Adresse (nach Umleitungen)
    bool ok() const { return status >= 200 && status < 300; }
};

// Schlanker WebDAV-Client. Umleitungen werden selbst verfolgt, damit
// Anmeldedaten nicht versehentlich an fremde Hosts oder über http gehen.
class DavClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const DavReply &reply)>;

    explicit DavClient(QObject *parent = nullptr);

    void setBasicAuth(const QString &username, const QString &password);
    // Für OAuth2 (M4): ersetzt Basic-Anmeldung
    void setBearerToken(const QString &token);

    void propfind(const QUrl &url, int depth, const QByteArray &body, Callback callback);
    void report(const QUrl &url, int depth, const QByteArray &body, Callback callback);
    void options(const QUrl &url, Callback callback);
    // ifMatch: nur ersetzen, wenn der ETag noch stimmt; leer = nur neu anlegen.
    void put(const QUrl &url, const QByteArray &data, const QByteArray &contentType, const QString &ifMatch, Callback callback);
    void remove(const QUrl &url, const QString &ifMatch, Callback callback);
    // Sammlungen anlegen und ändern
    void mkcalendar(const QUrl &url, const QByteArray &body, Callback callback);
    void mkcol(const QUrl &url, const QByteArray &body, Callback callback);
    void proppatch(const QUrl &url, const QByteArray &body, Callback callback);

    static QList<DavResource> parseMultistatus(const QByteArray &xml);

private:
    struct Request {
        QByteArray verb;
        QUrl url;
        QByteArray body;
        QByteArray contentType;
        QHash<QByteArray, QByteArray> headers;
        int redirects = 0;
        Callback callback;
    };
    void send(Request request);

    QNetworkAccessManager *m_network;
    QByteArray m_authorization;
};

} // namespace Fjordpost

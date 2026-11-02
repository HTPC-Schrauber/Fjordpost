#include "davclient.h"

#include <KLocalizedString>

#include <QBuffer>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(lcDav, "fjordpost.dav")

namespace Fjordpost {

namespace {

constexpr int maxRedirects = 5;
constexpr int timeoutMs = 60 * 1000;

// Liest eine Eigenschaft bis zum schließenden Element.
DavProperty readProperty(QXmlStreamReader &xml)
{
    DavProperty p;
    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement: {
            ++depth;
            const QString name = xml.name().toString();
            if (name == QLatin1String("href")) {
                p.hrefs << xml.readElementText().trimmed();
                --depth; // readElementText verbraucht das Endelement
            } else {
                p.names << name;
                if (name == QLatin1String("comp"))
                    p.comps << xml.attributes().value(QLatin1String("name")).toString();
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            --depth;
            break;
        case QXmlStreamReader::Characters:
        case QXmlStreamReader::EntityReference:
            p.text += xml.text();
            break;
        default:
            break;
        }
    }
    p.text = p.text.trimmed();
    return p;
}

} // namespace

QList<DavResource> DavClient::parseMultistatus(const QByteArray &data)
{
    QList<DavResource> result;
    QXmlStreamReader xml(data);
    DavResource current;
    QHash<QString, DavProperty> propstat;
    bool propstatOk = true;

    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("response")) {
                current = DavResource();
            } else if (name == QLatin1String("propstat")) {
                propstat.clear();
                propstatOk = true;
            } else if (name == QLatin1String("href")) {
                current.href = xml.readElementText().trimmed();
            } else if (name == QLatin1String("status")) {
                const QString line = xml.readElementText();
                propstatOk = line.contains(QLatin1String(" 2"));
            } else if (name == QLatin1String("prop")) {
                while (xml.readNextStartElement()) {
                    const QString key = xml.name().toString();
                    propstat.insert(key, readProperty(xml));
                }
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const auto name = xml.name();
            if (name == QLatin1String("propstat")) {
                if (propstatOk)
                    current.props.insert(propstat);
            } else if (name == QLatin1String("response")) {
                if (!current.href.isEmpty())
                    result << current;
            }
        }
    }
    if (xml.hasError())
        qCWarning(lcDav) << "Multistatus nicht lesbar:" << xml.errorString();
    return result;
}

DavClient::DavClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

void DavClient::setBasicAuth(const QString &username, const QString &password)
{
    m_authorization = "Basic " + (username + QLatin1Char(':') + password).toUtf8().toBase64();
}

void DavClient::setBearerToken(const QString &token)
{
    m_authorization = "Bearer " + token.toUtf8();
}

void DavClient::propfind(const QUrl &url, int depth, const QByteArray &body, Callback callback)
{
    send({"PROPFIND", url, body, "application/xml; charset=utf-8", {{"Depth", QByteArray::number(depth)}}, 0, std::move(callback)});
}

void DavClient::report(const QUrl &url, int depth, const QByteArray &body, Callback callback)
{
    send({"REPORT", url, body, "application/xml; charset=utf-8", {{"Depth", QByteArray::number(depth)}}, 0, std::move(callback)});
}

void DavClient::options(const QUrl &url, Callback callback)
{
    send({"OPTIONS", url, {}, {}, {}, 0, std::move(callback)});
}

void DavClient::put(const QUrl &url, const QByteArray &data, const QByteArray &contentType, const QString &ifMatch, Callback callback)
{
    QHash<QByteArray, QByteArray> headers;
    if (ifMatch.isEmpty())
        headers.insert("If-None-Match", "*");
    else
        headers.insert("If-Match", ifMatch.toUtf8());
    send({"PUT", url, data, contentType, headers, 0, std::move(callback)});
}

void DavClient::remove(const QUrl &url, const QString &ifMatch, Callback callback)
{
    QHash<QByteArray, QByteArray> headers;
    if (!ifMatch.isEmpty())
        headers.insert("If-Match", ifMatch.toUtf8());
    send({"DELETE", url, {}, {}, headers, 0, std::move(callback)});
}

void DavClient::mkcalendar(const QUrl &url, const QByteArray &body, Callback callback)
{
    send({"MKCALENDAR", url, body, "application/xml; charset=utf-8", {}, 0, std::move(callback)});
}

void DavClient::mkcol(const QUrl &url, const QByteArray &body, Callback callback)
{
    send({"MKCOL", url, body, "application/xml; charset=utf-8", {}, 0, std::move(callback)});
}

void DavClient::proppatch(const QUrl &url, const QByteArray &body, Callback callback)
{
    send({"PROPPATCH", url, body, "application/xml; charset=utf-8", {}, 0, std::move(callback)});
}

void DavClient::send(Request r)
{
    QNetworkRequest request(r.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("Fjordpost"));
    if (!m_authorization.isEmpty())
        request.setRawHeader("Authorization", m_authorization);
    if (!r.contentType.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, r.contentType);
    for (auto it = r.headers.cbegin(); it != r.headers.cend(); ++it)
        request.setRawHeader(it.key(), it.value());

    auto *buffer = new QBuffer;
    buffer->setData(r.body);
    buffer->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_network->sendCustomRequest(request, r.verb, buffer);
    buffer->setParent(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, r = std::move(r)]() mutable {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (status >= 300 && status < 400 && status != 304 && r.redirects < maxRedirects) {
            // Rohwert: header(LocationHeader) ist bei relativen Zielen leer, Server senden aber oft "Location: /".
            const QByteArray location = reply->rawHeader("Location");
            const QUrl target = reply->url().resolved(QUrl::fromEncoded(location));
            // Keine Passwörter über unverschlüsselte Umleitungen oder zu anderen Hosts
            const bool downgrade = r.url.scheme() == QLatin1String("https") && target.scheme() != QLatin1String("https");
            if (!location.isEmpty() && target.isValid() && !downgrade && target.host().compare(r.url.host(), Qt::CaseInsensitive) == 0) {
                r.url = target;
                ++r.redirects;
                send(std::move(r));
                return;
            }
            qCWarning(lcDav) << "Umleitung abgelehnt:" << r.url << "→" << target;
        }

        DavReply result;
        result.status = status;
        result.url = r.url;
        result.body = reply->readAll();
        result.etag = QString::fromUtf8(reply->rawHeader("ETag"));
        result.davHeader = reply->rawHeader("DAV");
        if (status == 207)
            result.resources = parseMultistatus(result.body);
        if (status == 0) {
            result.error = reply->errorString();
        } else if (status == 401 || status == 403) {
            result.error = i18n("The server rejected the user name or password.");
        } else if (!result.ok()) {
            result.error = i18n("The server replied with error %1.", status);
        }
        if (!result.error.isEmpty())
            qCDebug(lcDav) << r.verb << r.url << status << result.error;
        r.callback(result);
    });
}

} // namespace Fjordpost

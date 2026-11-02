#include "davdiscovery.h"

#include <KLocalizedString>

#include <QDnsLookup>

namespace Fjordpost {

namespace {

const QByteArray principalQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:current-user-principal/></d:prop></d:propfind>";
const QByteArray homeQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
    "xmlns:a=\"urn:ietf:params:xml:ns:carddav\"><d:prop><c:calendar-home-set/><a:addressbook-home-set/></d:prop></d:propfind>";

} // namespace

DavDiscovery::DavDiscovery(DavClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

QList<QUrl> DavDiscovery::candidates(const QString &input)
{
    QString text = input.trimmed();
    if (const qsizetype at = text.lastIndexOf(QLatin1Char('@')); at >= 0 && !text.contains(QLatin1Char('/')))
        text = text.mid(at + 1);
    if (!text.contains(QLatin1String("://")))
        text.prepend(QLatin1String("https://"));
    const QUrl url(text, QUrl::TolerantMode);
    if (!url.isValid() || url.host().isEmpty())
        return {};

    QList<QUrl> result;
    const bool bare = url.path().isEmpty() || url.path() == QLatin1String("/");
    if (bare) {
        for (const auto &path : {QStringLiteral("/.well-known/caldav"), QStringLiteral("/.well-known/carddav")}) {
            QUrl u = url;
            u.setPath(path);
            result << u;
        }
    }
    QUrl root = url;
    if (bare)
        root.setPath(QStringLiteral("/"));
    result << root;
    return result;
}

void DavDiscovery::start(const QString &input, Callback callback)
{
    m_callback = std::move(callback);
    m_queue = candidates(input);
    if (m_queue.isEmpty()) {
        m_result.error = i18n("Please enter a valid server address.");
        finish();
        return;
    }

    // Ohne Schema darf DNS (SRV) den eigentlichen Server benennen.
    const QString text = input.trimmed();
    if (text.contains(QLatin1String("://"))) {
        tryNext();
        return;
    }
    const QString domain = m_queue.constFirst().host();
    auto *lookup = new QDnsLookup(QDnsLookup::SRV, QStringLiteral("_caldavs._tcp.") + domain, this);
    connect(lookup, &QDnsLookup::finished, this, [this, lookup] {
        lookup->deleteLater();
        const auto records = lookup->serviceRecords();
        if (lookup->error() == QDnsLookup::NoError && !records.isEmpty() && records.constFirst().target() != QLatin1String(".")) {
            const auto &srv = records.constFirst();
            QUrl url;
            url.setScheme(QStringLiteral("https"));
            url.setHost(srv.target());
            if (srv.port() != 443)
                url.setPort(srv.port());
            m_queue = candidates(url.toString()) + m_queue;
        }
        tryNext();
    });
    lookup->lookup();
}

void DavDiscovery::tryNext()
{
    if (!m_result.calendarHome.isEmpty() && !m_result.addressbookHome.isEmpty()) {
        checkSchedule();
        return;
    }
    if (m_queue.isEmpty()) {
        if (!m_result.calendarHome.isEmpty() || !m_result.addressbookHome.isEmpty())
            checkSchedule();
        else {
            m_result.error = m_lastError.isEmpty() ? i18n("No calendars or address books were found at this address.") : m_lastError;
            finish();
        }
        return;
    }
    probe(m_queue.takeFirst());
}

void DavDiscovery::probe(const QUrl &url)
{
    m_client->propfind(url, 0, principalQuery, [this, url](const DavReply &reply) {
        // Anmeldefehler zählen mehr als ein 404 eines späteren Versuchs.
        if (!reply.error.isEmpty() && (m_lastError.isEmpty() || reply.status == 401 || reply.status == 403))
            m_lastError = reply.error;
        if (reply.status == 401) {
            m_queue.clear();
            tryNext();
            return;
        }
        if (reply.status != 207 || reply.resources.isEmpty()) {
            tryNext();
            return;
        }
        const QStringList hrefs = reply.resources.constFirst().props.value(QStringLiteral("current-user-principal")).hrefs;
        readPrincipal(hrefs.isEmpty() ? reply.url : reply.url.resolved(QUrl(hrefs.constFirst())));
    });
}

void DavDiscovery::readPrincipal(const QUrl &principal)
{
    m_client->propfind(principal, 0, homeQuery, [this, principal](const DavReply &reply) {
        if (reply.status == 207 && !reply.resources.isEmpty()) {
            const DavResource &r = reply.resources.constFirst();
            const QStringList calendars = r.props.value(QStringLiteral("calendar-home-set")).hrefs;
            const QStringList books = r.props.value(QStringLiteral("addressbook-home-set")).hrefs;
            if (m_result.calendarHome.isEmpty() && !calendars.isEmpty())
                m_result.calendarHome = reply.url.resolved(QUrl(calendars.constFirst())).toString();
            if (m_result.addressbookHome.isEmpty() && !books.isEmpty())
                m_result.addressbookHome = reply.url.resolved(QUrl(books.constFirst())).toString();
            if (!calendars.isEmpty() || !books.isEmpty())
                m_result.baseUrl = reply.url.adjusted(QUrl::RemovePath);
        }
        tryNext();
    });
}

void DavDiscovery::checkSchedule()
{
    const QUrl home(m_result.calendarHome);
    if (!home.isValid() || home.isEmpty()) {
        finish();
        return;
    }
    m_client->options(home, [this](const DavReply &reply) {
        m_result.autoSchedule = reply.davHeader.contains("calendar-auto-schedule");
        finish();
    });
}

void DavDiscovery::finish()
{
    if (m_callback)
        m_callback(m_result);
}

} // namespace Fjordpost

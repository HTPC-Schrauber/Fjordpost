#include "core/secretstore.h"
#include "dav/davclient.h"
#include "dav/davdiscovery.h"
#include "dav/davservice.h"
#include "dav/davsync.h"
#include "pim/pimdata.h"
#include "storage/database.h"
#include "storage/davstore.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace Fjordpost;

namespace {

// Kleiner DAV-Server im Speicher: ein Benutzer, ein Kalender, ein Adressbuch.
class FakeDavServer : public QObject
{
public:
    struct Resource {
        QByteArray data;
        QByteArray etag;
    };

    FakeDavServer()
    {
        m_server.listen(QHostAddress::LocalHost);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                onRead(socket);
            });
        });
    }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }

    void setResource(const QByteArray &path, const QByteArray &data)
    {
        m_resources[path] = {data, makeEtag(data)};
        ++m_generation;
    }
    void removeResource(const QByteArray &path)
    {
        m_resources.remove(path);
        ++m_generation;
    }
    QList<QByteArray> resourcePaths() const { return m_resources.keys(); }
    QByteArray resource(const QByteArray &path) const { return m_resources.value(path).data; }
    bool contains(const QByteArray &path) const { return m_resources.contains(path); }
    int multigets = 0;
    int puts = 0;
    bool autoSchedule = false;
    bool denyCollections = false; // lehnt Anlegen, Umbenennen und Löschen ab
    bool denyColors = false;      // kennt die Farb-Eigenschaften nicht
    QHash<QByteArray, QByteArray> colors; // Pfad → Farbe
    QByteArray lastCollectionBody;
    QHash<QByteArray, QByteArray> extraCalendars;  // Pfad → Name
    QHash<QByteArray, QByteArray> extraBooks;
    QByteArray requiredAuth = "Basic " + QByteArray("max:geheim").toBase64();

private:
    static QByteArray makeEtag(const QByteArray &data) { return '"' + QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex() + '"'; }

    void onRead(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        QHash<QByteArray, QByteArray> headers;
        for (int i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines[i].indexOf(':');
            headers.insert(lines[i].left(colon).toLower(), lines[i].mid(colon + 1).trimmed());
        }
        const int length = headers.value("content-length").toInt();
        if (buffer.size() < headerEnd + 4 + length)
            return;
        const QByteArray body = buffer.mid(headerEnd + 4, length);
        buffer.remove(0, headerEnd + 4 + length);
        const QList<QByteArray> request = lines[0].trimmed().split(' ');
        respond(socket, request[0], request[1], headers, body);
    }

    void send(QTcpSocket *socket, int status, const QByteArray &body = {}, const QList<QByteArray> &extra = {})
    {
        QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n";
        for (const QByteArray &h : extra)
            out += h + "\r\n";
        socket->write(out + "\r\n" + body);
    }

    static QByteArray displayName(const QByteArray &body)
    {
        static const QRegularExpression name(QStringLiteral("<d:displayname>([^<]*)</d:displayname>"));
        return name.match(QString::fromUtf8(body)).captured(1).toUtf8();
    }

    QByteArray propResponse(const QByteArray &href, const QByteArray &props)
    {
        return "<d:response><d:href>" + href + "</d:href><d:propstat><d:prop>" + props + "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
    }

    QByteArray multistatus(const QByteArray &inner)
    {
        return "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:a=\"urn:ietf:params:xml:ns:carddav\" "
               "xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:i=\"http://apple.com/ns/ical/\" xmlns:inf=\"http://inf-it.com/ns/ab/\">" + inner + "</d:multistatus>";
    }

    void respond(QTcpSocket *socket, const QByteArray &verb, const QByteArray &path, const QHash<QByteArray, QByteArray> &headers, const QByteArray &body)
    {
        if (headers.value("authorization") != requiredAuth) {
            send(socket, 401, {}, {"WWW-Authenticate: Basic realm=\"x\""});
            return;
        }
        if (verb == "OPTIONS") {
            send(socket, 200, {}, {autoSchedule ? "DAV: 1, 3, calendar-access, calendar-auto-schedule" : "DAV: 1, 3, calendar-access"});
            return;
        }
        if (verb == "MKCALENDAR" || verb == "MKCOL") {
            lastCollectionBody = body;
            if (denyCollections) {
                send(socket, 403);
                return;
            }
            (verb == "MKCOL" ? extraBooks : extraCalendars).insert(path, displayName(body));
            ++m_generation;
            send(socket, 201);
            return;
        }
        if (verb == "PROPPATCH") {
            lastCollectionBody = body;
            static const QRegularExpression colorTag(QStringLiteral("<(i:calendar-color|inf:addressbook-color)>([^<]*)<"));
            const auto color = colorTag.match(QString::fromUtf8(body));
            if (color.hasMatch()) {
                if (denyColors) {
                    send(socket, 207, multistatus("<d:response><d:href>" + path + "</d:href><d:propstat><d:prop><" + color.captured(1).toUtf8() + "/></d:prop>"
                                                  "<d:status>HTTP/1.1 403 Forbidden</d:status></d:propstat></d:response>"));
                    return;
                }
                colors.insert(path, color.captured(2).toUtf8());
                ++m_generation;
                send(socket, 207, multistatus(propResponse(path, "<" + color.captured(1).toUtf8() + "/>")));
                return;
            }
            if (denyCollections) {
                // Ablehnung steckt in der Multistatus-Antwort, nicht im HTTP-Status
                send(socket, 207, multistatus("<d:response><d:href>" + path + "</d:href><d:propstat><d:prop><d:displayname/></d:prop>"
                                              "<d:status>HTTP/1.1 403 Forbidden</d:status></d:propstat></d:response>"));
                return;
            }
            if (extraCalendars.contains(path))
                extraCalendars.insert(path, displayName(body));
            if (extraBooks.contains(path))
                extraBooks.insert(path, displayName(body));
            ++m_generation;
            send(socket, 207, multistatus(propResponse(path, "<d:displayname/>")));
            return;
        }
        if (verb == "DELETE" && (extraCalendars.contains(path) || extraBooks.contains(path))) {
            if (denyCollections) {
                send(socket, 403);
                return;
            }
            extraCalendars.remove(path);
            extraBooks.remove(path);
            ++m_generation;
            send(socket, 204);
            return;
        }
        if (path == "/.well-known/caldav" || path == "/.well-known/carddav") {
            send(socket, 301, {}, {"Location: /dav/"});
            return;
        }
        if (verb == "PROPFIND") {
            handlePropfind(socket, path, headers.value("depth"));
        } else if (verb == "REPORT") {
            ++multigets;
            QByteArray inner;
            static const QRegularExpression href(QStringLiteral("<d:href>([^<]+)</d:href>"));
            auto it = href.globalMatch(QString::fromUtf8(body));
            while (it.hasNext()) {
                const QByteArray p = it.next().captured(1).toUtf8();
                if (!m_resources.contains(p))
                    continue;
                const Resource &r = m_resources[p];
                const QByteArray element = p.endsWith(".vcf") ? "a:address-data" : "c:calendar-data";
                inner += propResponse(p, "<d:getetag>" + r.etag + "</d:getetag><" + element + ">" + r.data.toPercentEncoding("", "") .replace("%0D%0A", "\n").replace("%0A", "\n") + "</" + element + ">");
            }
            send(socket, 207, QByteArray::fromPercentEncoding(multistatus(inner)));
        } else if (verb == "PUT") {
            ++puts;
            const bool exists = m_resources.contains(path);
            if ((headers.contains("if-none-match") && exists) || (headers.contains("if-match") && (!exists || m_resources[path].etag != headers.value("if-match")))) {
                send(socket, 412);
                return;
            }
            setResource(path, body);
            send(socket, exists ? 204 : 201, {}, {"ETag: " + m_resources[path].etag});
        } else if (verb == "DELETE") {
            if (headers.contains("if-match") && m_resources.value(path).etag != headers.value("if-match")) {
                send(socket, 412);
                return;
            }
            removeResource(path);
            send(socket, 204);
        } else {
            send(socket, 405);
        }
    }

    void handlePropfind(QTcpSocket *socket, const QByteArray &path, const QByteArray &depth)
    {
        QByteArray inner;
        // "/" antwortet bewusst nicht: die Erkennung muss der relativen Umleitung von .well-known folgen.
        if (path == "/dav/") {
            inner = propResponse(path, "<d:current-user-principal><d:href>/principals/max/</d:href></d:current-user-principal>");
        } else if (path == "/principals/max/") {
            inner = propResponse(path, "<c:calendar-home-set><d:href>/home/</d:href></c:calendar-home-set><a:addressbook-home-set><d:href>/home/</d:href></a:addressbook-home-set>");
        } else if (path == "/home/") {
            const QByteArray ctag = QByteArray::number(m_generation);
            inner = propResponse("/home/", "<d:resourcetype><d:collection/></d:resourcetype>");
            inner += propResponse("/home/cal/", "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype><d:displayname>Mein &amp; Kalender</d:displayname>"
                                                "<cs:getctag>" + ctag + "</cs:getctag><i:calendar-color>#FF8800FF</i:calendar-color>"
                                                "<c:supported-calendar-component-set><c:comp name=\"VEVENT\"/><c:comp name=\"VTODO\"/></c:supported-calendar-component-set>"
                                                "<d:current-user-privilege-set><d:privilege><d:all/></d:privilege></d:current-user-privilege-set>");
            inner += propResponse("/home/book/", "<d:resourcetype><d:collection/><a:addressbook/></d:resourcetype><d:displayname>Adressen</d:displayname><cs:getctag>" + ctag + "</cs:getctag>");
            for (auto it = extraCalendars.cbegin(); it != extraCalendars.cend(); ++it)
                inner += propResponse(it.key(), "<d:resourcetype><d:collection/><c:calendar/></d:resourcetype><d:displayname>" + it.value() + "</d:displayname><cs:getctag>" + ctag + "</cs:getctag>"
                                                 "<i:calendar-color>" + colors.value(it.key()) + "</i:calendar-color>");
            for (auto it = extraBooks.cbegin(); it != extraBooks.cend(); ++it)
                inner += propResponse(it.key(), "<d:resourcetype><d:collection/><a:addressbook/></d:resourcetype><d:displayname>" + it.value() + "</d:displayname><cs:getctag>" + ctag + "</cs:getctag>"
                                                 + (colors.contains(it.key()) ? "<inf:addressbook-color>" + colors.value(it.key()) + "</inf:addressbook-color>" : QByteArray()));
        } else if ((path == "/home/cal/" || path == "/home/book/" || extraCalendars.contains(path) || extraBooks.contains(path)) && depth == "1") {
            inner = propResponse(path, "<d:resourcetype><d:collection/></d:resourcetype>");
            for (auto it = m_resources.cbegin(); it != m_resources.cend(); ++it) {
                if (it.key().startsWith(path))
                    inner += propResponse(it.key(), "<d:getetag>" + it.value().etag + "</d:getetag><d:resourcetype/>");
            }
        } else {
            send(socket, 404);
            return;
        }
        send(socket, 207, multistatus(inner));
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QHash<QByteArray, Resource> m_resources;
    int m_generation = 1;
};

QByteArray ics(const QByteArray &uid, const QByteArray &summary)
{
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//T//EN\r\nBEGIN:VEVENT\r\nUID:" + uid
        + "\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260105T100000Z\r\nDTEND:20260105T110000Z\r\nSUMMARY:" + summary + "\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
}

QByteArray vcard(const QByteArray &uid, const QByteArray &name)
{
    return "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:" + uid + "\r\nFN:" + name + "\r\nN:" + name + ";;;;\r\nEMAIL:" + uid + "@example.org\r\nEND:VCARD\r\n";
}

class MemorySecrets : public SecretStore
{
public:
    void read(const QString &, ReadCallback callback) override { callback(true, QStringLiteral("geheim")); }
};

} // namespace

class DavTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;

    DavAccount makeAccount(FakeDavServer &server)
    {
        DavAccount a;
        a.name = QStringLiteral("t");
        a.url = server.base() + QLatin1Char('/');
        a.username = QStringLiteral("max");
        a.calendarHome = server.base() + QStringLiteral("/home/");
        a.addressbookHome = a.calendarHome;
        a.id = DavStore::saveAccount(a);
        return a;
    }

    bool runSync(DavSync &sync)
    {
        QSignalSpy spy(&sync, &DavSync::finished);
        sync.sync();
        return spy.wait(5000) && spy.first().at(0).toBool();
    }

    bool runFlush(DavSync &sync)
    {
        QSignalSpy spy(&sync, &DavSync::finished);
        sync.flush();
        return spy.wait(5000) && spy.first().at(0).toBool();
    }

    // Wartet auf das Ergebnis einer Sammlungs-Aktion; "timeout", wenn keines kommt.
    QString collectionCall(const std::function<void(DavSync::Done)> &call)
    {
        QString result = QStringLiteral("timeout");
        bool done = false;
        call([&](const QString &error) {
            result = error;
            done = true;
        });
        return QTest::qWaitFor([&] {
            return done;
        }, 5000) ? result : QStringLiteral("timeout");
    }

    qint64 collectionId(const DavAccount &a, const QString &kind)
    {
        return DavStore::collections(a.id, kind).constFirst().id;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(Database::initialize(m_dir.filePath(QStringLiteral("t.db"))));
    }

    // Die Verbindung gilt pro Thread; daher eine Datenbank, die geleert wird.
    void init()
    {
        const auto accounts = DavStore::accounts();
        for (const DavAccount &a : accounts)
            DavStore::removeAccount(a.id);
    }

    void candidates()
    {
        QCOMPARE(DavDiscovery::candidates(QStringLiteral("max@example.org")).constFirst(), QUrl(QStringLiteral("https://example.org/.well-known/caldav")));
        QCOMPARE(DavDiscovery::candidates(QStringLiteral("cloud.example.org")).size(), 3);
        // Ein ausdrücklicher Pfad wird unverändert verwendet.
        const auto direct = DavDiscovery::candidates(QStringLiteral("https://x.org/remote.php/dav"));
        QCOMPARE(direct.size(), 1);
        QCOMPARE(direct.constFirst(), QUrl(QStringLiteral("https://x.org/remote.php/dav")));
        QVERIFY(DavDiscovery::candidates(QStringLiteral("  ")).isEmpty());
    }

    void multistatusParsing()
    {
        const QByteArray xml = "<?xml version=\"1.0\"?><D:multistatus xmlns:D=\"DAV:\"><D:response><D:href>/a/</D:href>"
                               "<D:propstat><D:prop><D:displayname>Ok</D:displayname></D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat>"
                               "<D:propstat><D:prop><D:getetag/></D:prop><D:status>HTTP/1.1 404 Not Found</D:status></D:propstat></D:response></D:multistatus>";
        const auto list = DavClient::parseMultistatus(xml);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).text(QStringLiteral("displayname")), QStringLiteral("Ok"));
        QVERIFY(!list.at(0).has(QStringLiteral("getetag")));
    }

    void discovery()
    {
        FakeDavServer server;
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavDiscovery discovery(&client);
        DavDiscovery::Result result;
        bool done = false;
        server.autoSchedule = true;
        discovery.start(server.base(), [&](const DavDiscovery::Result &r) {
            result = r;
            done = true;
        });
        QTRY_VERIFY(done);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.calendarHome, server.base() + QStringLiteral("/home/"));
        QCOMPARE(result.addressbookHome, server.base() + QStringLiteral("/home/"));
        QVERIFY(result.autoSchedule);
    }

    void discoveryWrongPassword()
    {
        FakeDavServer server;
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("falsch"));
        DavDiscovery discovery(&client);
        QString error;
        discovery.start(server.base(), [&](const DavDiscovery::Result &r) {
            error = r.error;
        });
        QTRY_VERIFY(!error.isEmpty());
        QVERIFY(error.contains(QLatin1String("password")));
    }

    void pullThenIncremental()
    {
        FakeDavServer server;
        server.setResource("/home/cal/e1.ics", ics("e1", "Eins"));
        server.setResource("/home/cal/e2.ics", ics("e2", "Zwei"));
        server.setResource("/home/book/c1.vcf", vcard("c1", "Anna"));
        const DavAccount account = makeAccount(server);
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavSync sync(account, &client);

        QVERIFY(runSync(sync));
        const auto calendars = DavStore::collections(account.id, QStringLiteral("calendar"));
        QCOMPARE(calendars.size(), 1);
        QCOMPARE(calendars.at(0).name, QStringLiteral("Mein & Kalender"));
        QCOMPARE(calendars.at(0).color, QStringLiteral("#FF8800"));
        QVERIFY(calendars.at(0).hasTodos);
        QVERIFY(!calendars.at(0).readOnly);
        QCOMPARE(DavStore::itemStates(calendars.at(0).id).size(), 2);
        QCOMPARE(DavStore::contacts({}).size(), 1);
        QCOMPARE(DavStore::contacts({}).constFirst().emails, QStringList{QStringLiteral("c1@example.org")});

        // Nichts geändert: kein weiterer Abruf
        const int before = server.multigets;
        QVERIFY(runSync(sync));
        QCOMPARE(server.multigets, before);

        // Änderung, Löschung, Neuzugang auf dem Server
        server.setResource("/home/cal/e1.ics", ics("e1", "Eins neu"));
        server.removeResource("/home/cal/e2.ics");
        server.setResource("/home/cal/e3.ics", ics("e3", "Drei"));
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::itemByUid(QStringLiteral("e1")).title, QStringLiteral("Eins neu"));
        QCOMPARE(DavStore::itemByUid(QStringLiteral("e2")).id, qint64(0));
        QCOMPARE(DavStore::itemByUid(QStringLiteral("e3")).title, QStringLiteral("Drei"));
    }

    void localChangesReachServer()
    {
        FakeDavServer server;
        server.setResource("/home/cal/e1.ics", ics("e1", "Eins"));
        const DavAccount account = makeAccount(server);
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavSync sync(account, &client);
        QVERIFY(runSync(sync));
        const qint64 calendar = collectionId(account, QStringLiteral("calendar"));

        // Neu
        DavItem created;
        created.uid = QStringLiteral("n1");
        created.collectionId = calendar;
        created.href = server.base() + QStringLiteral("/home/cal/n1.ics");
        created.raw = QString::fromLatin1(ics("n1", "Neu"));
        created.pending = DavItem::Create;
        QVERIFY(Pim::deriveCalendarFields(created));
        QVERIFY(DavStore::saveItem(created));

        // Ändern
        DavItem changed = DavStore::itemByUid(QStringLiteral("e1"));
        changed.raw = QString::fromLatin1(ics("e1", "Lokal"));
        changed.pending = DavItem::Modify;
        QVERIFY(Pim::deriveCalendarFields(changed));
        QVERIFY(DavStore::saveItem(changed));

        QVERIFY(runFlush(sync));
        QVERIFY(server.resource("/home/cal/n1.ics").contains("SUMMARY:Neu"));
        QVERIFY(server.resource("/home/cal/e1.ics").contains("SUMMARY:Lokal"));
        QCOMPARE(DavStore::pendingItems(account.id).size(), 0);
        QVERIFY(!DavStore::item(created.id).etag.isEmpty());

        // Eigene Änderungen werden beim nächsten Abgleich nicht erneut geholt.
        const int before = server.multigets;
        QVERIFY(runSync(sync));
        QCOMPARE(server.multigets, before);

        // Löschen
        DavItem gone = DavStore::item(created.id);
        gone.pending = DavItem::Delete;
        QVERIFY(DavStore::saveItem(gone));
        QVERIFY(runFlush(sync));
        QVERIFY(!server.contains("/home/cal/n1.ics"));
        QCOMPARE(DavStore::item(created.id).id, qint64(0));
    }

    void conflictServerWins()
    {
        FakeDavServer server;
        server.setResource("/home/cal/e1.ics", ics("e1", "Original"));
        const DavAccount account = makeAccount(server);
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavSync sync(account, &client);
        QVERIFY(runSync(sync));

        server.setResource("/home/cal/e1.ics", ics("e1", "Vom Server"));
        DavItem item = DavStore::itemByUid(QStringLiteral("e1"));
        item.raw = QString::fromLatin1(ics("e1", "Lokal"));
        item.pending = DavItem::Modify;
        QVERIFY(Pim::deriveCalendarFields(item));
        QVERIFY(DavStore::saveItem(item));

        QSignalSpy conflicts(&sync, &DavSync::conflict);
        QVERIFY(runFlush(sync));
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(DavStore::item(item.id).title, QStringLiteral("Vom Server"));
        QCOMPARE(DavStore::item(item.id).pending, int(DavItem::Clean));
        QVERIFY(server.resource("/home/cal/e1.ics").contains("Vom Server"));
    }

    void pendingSurvivesPull()
    {
        FakeDavServer server;
        server.setResource("/home/cal/e1.ics", ics("e1", "Original"));
        const DavAccount account = makeAccount(server);
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavSync sync(account, &client);
        QVERIFY(runSync(sync));

        // Offline gelöscht/geändert und Verbindung weg: nichts geht verloren.
        DavItem item = DavStore::itemByUid(QStringLiteral("e1"));
        item.raw = QString::fromLatin1(ics("e1", "Lokal"));
        item.pending = DavItem::Modify;
        QVERIFY(Pim::deriveCalendarFields(item));
        QVERIFY(DavStore::saveItem(item));

        server.requiredAuth = "kaputt";
        QSignalSpy spy(&sync, &DavSync::finished);
        sync.sync();
        QVERIFY(spy.wait(5000));
        QVERIFY(!spy.first().at(0).toBool());
        QCOMPARE(DavStore::item(item.id).pending, int(DavItem::Modify));
        QCOMPARE(DavStore::item(item.id).title, QStringLiteral("Lokal"));
    }

    void collectionsCreateRenameRemove()
    {
        FakeDavServer server;
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavAccount account;
        account.url = server.base();
        account.username = QStringLiteral("max");
        account.calendarHome = QStringLiteral("/home/");
        account.addressbookHome = QStringLiteral("/home/");
        account.id = DavStore::saveAccount(account);
        DavSync sync(account, &client);
        QVERIFY(runSync(sync));

        // Neue, leere Sammlung muss die Oberfläche erfahren
        QSignalSpy changed(&sync, &DavSync::dataChanged);
        QVERIFY(collectionCall([&](DavSync::Done done) {
            sync.createCollection(QStringLiteral("todos"), QStringLiteral("A & B"), QStringLiteral("#10b981"), std::move(done));
        }).isEmpty());
        QVERIFY(server.lastCollectionBody.contains("A &amp; B"));
        QVERIFY(server.lastCollectionBody.contains("name=\"VTODO\""));
        QVERIFY(!server.lastCollectionBody.contains("VEVENT"));
        QCOMPARE(server.extraCalendars.size(), 1);
        QVERIFY(!server.extraCalendars.keys().constFirst().contains("A"));
        QTRY_VERIFY(!sync.isBusy());
        QTRY_VERIFY(changed.size() > 0);

        QVERIFY(collectionCall([&](DavSync::Done done) {
            sync.createCollection(QStringLiteral("contacts"), QStringLiteral("Freunde"), {}, std::move(done));
        }).isEmpty());
        QVERIFY(server.lastCollectionBody.contains("addressbook"));
        QCOMPARE(server.extraBooks.size(), 1);
        QTRY_VERIFY(!sync.isBusy());

        const auto find = [&](const QByteArray &path) {
            const auto all = DavStore::collections(account.id);
            for (const DavCollection &c : all) {
                if (QUrl(c.href).path().toUtf8() == path)
                    return c;
            }
            return DavCollection();
        };
        const QByteArray listPath = server.extraCalendars.keys().constFirst();
        DavCollection list = find(listPath);
        QVERIFY(list.id > 0);

        // Abgelehnt: Fehlermeldung, kein falscher Hinweis auf das Passwort, nichts ändert sich lokal.
        server.denyCollections = true;
        const QString renameError = collectionCall([&](DavSync::Done done) {
            sync.updateCollection(list, QStringLiteral("Nein"), {}, std::move(done));
        });
        QVERIFY(!renameError.isEmpty());
        QCOMPARE(find(listPath).name, QStringLiteral("A & B"));
        const QString createError = collectionCall([&](DavSync::Done done) {
            sync.createCollection(QStringLiteral("events"), QStringLiteral("Nein"), {}, std::move(done));
        });
        QVERIFY(!createError.isEmpty());
        QVERIFY(!createError.contains(QLatin1String("password")));
        const QString removeError = collectionCall([&](DavSync::Done done) {
            sync.removeCollection(list, std::move(done));
        });
        QVERIFY(!removeError.isEmpty());
        QVERIFY(find(listPath).id > 0);
        server.denyCollections = false;

        QVERIFY(collectionCall([&](DavSync::Done done) {
            sync.updateCollection(list, QStringLiteral("Neu"), {}, std::move(done));
        }).isEmpty());
        QCOMPARE(find(listPath).name, QStringLiteral("Neu"));
        QCOMPARE(server.extraCalendars.value(listPath), QByteArray("Neu"));
        list = find(listPath);
        QCOMPARE(list.color, QStringLiteral("#10b981"));

        // Farbe ändern
        QCOMPARE(collectionCall([&](DavSync::Done done) {
            sync.updateCollection(list, list.name, QStringLiteral("#f43f5e"), std::move(done));
        }), QString());
        QCOMPARE(server.colors.value(listPath), QByteArray("#f43f5e"));
        QCOMPARE(find(listPath).color, QStringLiteral("#f43f5e"));
        list = find(listPath);

        // Kalender: Ablehnung ist ein Fehler, der Name kommt trotzdem an.
        server.denyColors = true;
        QVERIFY(!collectionCall([&](DavSync::Done done) {
            sync.updateCollection(list, QStringLiteral("Neu 2"), QStringLiteral("#3b82f6"), std::move(done));
        }).isEmpty());
        QCOMPARE(find(listPath).name, QStringLiteral("Neu 2"));
        QCOMPARE(find(listPath).color, QStringLiteral("#f43f5e"));
        list = find(listPath);

        // Adressbuch: Farbe bleibt lokal und überlebt den Abgleich.
        const QByteArray bookPath = server.extraBooks.keys().constFirst();
        QCOMPARE(collectionCall([&](DavSync::Done done) {
            sync.updateCollection(find(bookPath), QStringLiteral("Freunde"), QStringLiteral("#a855f7"), std::move(done));
        }), QString());
        QVERIFY(!server.colors.contains(bookPath));
        QVERIFY(runSync(sync));
        QCOMPARE(find(bookPath).color, QStringLiteral("#a855f7"));
        server.denyColors = false;
        // Dasselbe beim Anlegen: Das Buch entsteht, die Farbe gilt lokal.
        server.denyColors = true;
        QCOMPARE(collectionCall([&](DavSync::Done done) {
            sync.createCollection(QStringLiteral("contacts"), QStringLiteral("Lokal"), QStringLiteral("#64748b"), std::move(done));
        }), QString());
        QTRY_VERIFY(!sync.isBusy());
        server.denyColors = false;
        QByteArray localBook;
        for (auto it = server.extraBooks.cbegin(); it != server.extraBooks.cend(); ++it) {
            if (it.value() == "Lokal")
                localBook = it.key();
        }
        QCOMPARE(find(localBook).color, QStringLiteral("#64748b"));

        // Inhalte verschwinden mit der Sammlung
        DavItem task;
        task.uid = QStringLiteral("weg");
        task.collectionId = list.id;
        task.href = list.href + QStringLiteral("weg.ics");
        task.title = QStringLiteral("x");
        task.kind = QStringLiteral("todo");
        task.raw = QStringLiteral("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");
        QVERIFY(DavStore::saveItem(task));
        QVERIFY(collectionCall([&](DavSync::Done done) {
            sync.removeCollection(list, std::move(done));
        }).isEmpty());
        QVERIFY(!server.extraCalendars.contains(listPath));
        QCOMPARE(find(listPath).id, qint64(0));
        QCOMPARE(DavStore::itemByUid(QStringLiteral("weg")).id, qint64(0));
    }

    void transferMoveAndCopy()
    {
        FakeDavServer server;
        server.setResource("/home/cal/e.ics", ics("termin", "Termin"));
        server.setResource("/home/book/c.vcf", vcard("kontakt", "Eva"));
        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        DavAccount account;
        account.url = server.base();
        account.username = QStringLiteral("max");
        account.calendarHome = QStringLiteral("/home/");
        account.addressbookHome = QStringLiteral("/home/");
        account.id = DavStore::saveAccount(account);
        DavSync sync(account, &client);
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("events"), QStringLiteral("Ziel"), {}, std::move(d)); }), QString());
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("contacts"), QStringLiteral("Ziel"), {}, std::move(d)); }), QString());
        QTRY_VERIFY(!sync.isBusy());
        QVERIFY(runSync(sync));

        DavCollection calendar;
        DavCollection book;
        const auto all = DavStore::collections(account.id);
        for (const DavCollection &c : all) {
            if (c.name == QLatin1String("Ziel"))
                (c.kind == QLatin1String("calendar") ? calendar : book) = c;
        }
        QVERIFY(calendar.id > 0);
        QVERIFY(book.id > 0);
        const QByteArray calendarPath = QUrl(calendar.href).path().toUtf8();
        const QByteArray bookPath = QUrl(book.href).path().toUtf8();
        const auto inTarget = [&](const QByteArray &path) {
            QByteArrayList found;
            const auto keys = server.resourcePaths();
            for (const QByteArray &k : keys) {
                if (k.startsWith(path))
                    found << k;
            }
            return found;
        };

        // Kopie: neue UID, Original bleibt
        const qint64 contactId = DavStore::contactByEmail(QStringLiteral("kontakt@example.org")).id;
        QVERIFY(contactId > 0);
        QCOMPARE(Pim::transferItem(contactId, book.id, false), QString());
        QVERIFY(runSync(sync));
        QVERIFY(server.contains("/home/book/c.vcf"));
        QCOMPARE(inTarget(bookPath).size(), 1);
        const QByteArray copied = server.resource(inTarget(bookPath).constFirst());
        QVERIFY(copied.contains("Eva"));
        QVERIFY(!copied.contains("UID:kontakt"));

        // Verschieben: gleiche UID, Original weg
        const qint64 eventId = DavStore::itemByUid(QStringLiteral("termin")).id;
        QCOMPARE(Pim::transferItem(eventId, calendar.id, true), QString());
        QCOMPARE(DavStore::item(eventId).pending, int(DavItem::Delete));
        QVERIFY(runSync(sync));
        QVERIFY(!server.contains("/home/cal/e.ics"));
        QCOMPARE(inTarget(calendarPath).size(), 1);
        QVERIFY(server.resource(inTarget(calendarPath).constFirst()).contains("UID:termin"));
        QCOMPARE(DavStore::itemByUid(QStringLiteral("termin")).collectionId, calendar.id);

        // Noch nicht auf dem Server: Es darf kein DELETE geben, nur der neue Ort zählt.
        DavItem offline;
        offline.collectionId = calendar.id;
        offline.uid = QStringLiteral("neu");
        offline.href = calendar.href + QStringLiteral("neu.ics");
        offline.raw = QString::fromUtf8(ics("neu", "Neu"));
        offline.pending = DavItem::Create;
        QVERIFY(Pim::deriveCalendarFields(offline));
        QVERIFY(DavStore::saveItem(offline));
        const qint64 plainCalendar = [&] {
            const auto list = DavStore::collections(account.id, QStringLiteral("calendar"));
            for (const DavCollection &c : list) {
                if (c.id != calendar.id)
                    return c.id;
            }
            return qint64(0);
        }();
        QCOMPARE(Pim::transferItem(offline.id, plainCalendar, true), QString());
        QCOMPARE(DavStore::item(offline.id).id, qint64(0));

        // Unsinnige Ziele
        QVERIFY(!Pim::transferItem(contactId, calendar.id, true).isEmpty());
        QVERIFY(!Pim::transferItem(contactId, DavStore::item(contactId).collectionId, false).isEmpty());
    }

    // Gegen einen echten Server (Radicale aus dev/docker-compose.yml):
    //   FJORDPOST_TEST_DAV=http://127.0.0.1:5232 build/bin/davtest realServer
    void realServer()
    {
        const QString base = qEnvironmentVariable("FJORDPOST_TEST_DAV");
        if (base.isEmpty())
            QSKIP("FJORDPOST_TEST_DAV nicht gesetzt");

        DavClient client;
        client.setBasicAuth(QStringLiteral("max"), QStringLiteral("geheim"));
        const QString run = QString::number(QDateTime::currentMSecsSinceEpoch());
        const QUrl calendarUrl(base + QStringLiteral("/max/test-%1-cal/").arg(run));
        const QUrl bookUrl(base + QStringLiteral("/max/test-%1-book/").arg(run));

        // Radicale legt das Heimatverzeichnis beim ersten Zugriff an; Sammlungen per MKCOL.
        const auto mkcol = [&](const QUrl &url, const QByteArray &type) {
            QNetworkAccessManager network;
            QNetworkRequest request(url);
            request.setRawHeader("Authorization", "Basic " + QByteArray("max:geheim").toBase64());
            request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/xml"));
            const QByteArray body = "<?xml version=\"1.0\"?><d:mkcol xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
                                    "xmlns:a=\"urn:ietf:params:xml:ns:carddav\"><d:set><d:prop><d:resourcetype><d:collection/>" + type
                + "</d:resourcetype><d:displayname>Test</d:displayname></d:prop></d:set></d:mkcol>";
            QNetworkReply *reply = network.sendCustomRequest(request, "MKCOL", body);
            QSignalSpy done(reply, &QNetworkReply::finished);
            QVERIFY(done.wait(5000));
            QVERIFY2(reply->error() == QNetworkReply::NoError, reply->readAll().constData());
            reply->deleteLater();
        };
        mkcol(calendarUrl, "<c:calendar/>");
        mkcol(bookUrl, "<a:addressbook/>");

        DavDiscovery discovery(&client);
        DavDiscovery::Result found;
        bool done = false;
        discovery.start(base, [&](const DavDiscovery::Result &r) {
            found = r;
            done = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY2(found.error.isEmpty(), qPrintable(found.error));
        QVERIFY(!found.calendarHome.isEmpty());

        DavAccount account;
        account.name = QStringLiteral("radicale");
        account.url = found.baseUrl.toString();
        account.username = QStringLiteral("max");
        account.calendarHome = found.calendarHome;
        account.addressbookHome = found.addressbookHome;
        account.id = DavStore::saveAccount(account);
        DavSync sync(account, &client);
        QVERIFY(runSync(sync));

        DavCollection calendar;
        DavCollection book;
        const auto all = DavStore::collections(account.id);
        for (const DavCollection &c : all) {
            if (c.href.contains(run) && c.kind == QLatin1String("calendar"))
                calendar = c;
            if (c.href.contains(run) && c.kind == QLatin1String("addressbook"))
                book = c;
        }
        QVERIFY(calendar.id > 0);
        QVERIFY(book.id > 0);

        // Umlaute, Serie und Kontakt einmal hin und zurück
        DavItem event;
        event.uid = QStringLiteral("e-") + run;
        event.collectionId = calendar.id;
        event.href = calendar.href + event.uid + QStringLiteral(".ics");
        const QDateTime start = QDateTime::currentDateTime().addDays(1);
        event.raw = Pim::applyEventFields({},
                                          {{QStringLiteral("summary"), QStringLiteral("Prüfung & Köln")},
                                           {QStringLiteral("startMs"), double(start.toMSecsSinceEpoch())},
                                           {QStringLiteral("endMs"), double(start.addSecs(3600).toMSecsSinceEpoch())},
                                           {QStringLiteral("repeat"), QStringLiteral("weekly")}},
                                          event.uid);
        event.pending = DavItem::Create;
        QVERIFY(Pim::deriveCalendarFields(event));
        QVERIFY(DavStore::saveItem(event));

        DavItem contact;
        contact.uid = QStringLiteral("c-") + run;
        contact.collectionId = book.id;
        contact.href = book.href + contact.uid + QStringLiteral(".vcf");
        contact.raw = Pim::applyContactFields({},
                                              {{QStringLiteral("name"), QStringLiteral("Jörg Müller")}, {QStringLiteral("emails"), QStringList{QStringLiteral("j@example.org")}}},
                                              contact.uid);
        contact.pending = DavItem::Create;
        QVERIFY(Pim::deriveContactFields(contact));
        QVERIFY(DavStore::saveItem(contact));
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::pendingItems(account.id).size(), 0);

        // Frisch vom Server: alles lokale verwerfen und neu holen
        DavStore::removeItem(event.id);
        DavStore::removeItem(contact.id);
        DavStore::setCollectionCtag(calendar.id, {});
        DavStore::setCollectionCtag(book.id, {});
        QVERIFY(runSync(sync));
        const DavItem pulled = DavStore::itemByUid(event.uid);
        QCOMPARE(pulled.title, QStringLiteral("Prüfung & Köln"));
        QCOMPARE(pulled.rangeEnd, qint64(0));
        QCOMPARE(DavStore::contactByEmail(QStringLiteral("j@example.org")).title, QStringLiteral("Jörg Müller"));

        // Serverseitig ändern lässt sich hier nicht; stattdessen veralteten ETag prüfen.
        DavItem stale = pulled;
        stale.etag = QStringLiteral("\"falsch\"");
        stale.raw = Pim::applyEventFields(stale.raw,
                                          {{QStringLiteral("summary"), QStringLiteral("Lokal")},
                                           {QStringLiteral("startMs"), double(start.toMSecsSinceEpoch())},
                                           {QStringLiteral("endMs"), double(start.addSecs(3600).toMSecsSinceEpoch())},
                                           {QStringLiteral("repeat"), QStringLiteral("weekly")}},
                                          {});
        stale.pending = DavItem::Modify;
        QVERIFY(Pim::deriveCalendarFields(stale));
        QVERIFY(DavStore::saveItem(stale));
        QSignalSpy conflicts(&sync, &DavSync::conflict);
        QVERIFY(runSync(sync));
        QCOMPARE(conflicts.size(), 1);
        QCOMPARE(DavStore::itemByUid(event.uid).title, QStringLiteral("Prüfung & Köln"));

        // Löschen kommt beim Server an.
        DavItem gone = DavStore::itemByUid(event.uid);
        gone.pending = DavItem::Delete;
        QVERIFY(DavStore::saveItem(gone));
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::itemByUid(event.uid).id, qint64(0));

        // Verschieben und Kopieren zwischen zwei Kalendern
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("events"), QStringLiteral("Ziel ") + run, {}, std::move(d)); }), QString());
        QTRY_VERIFY(!sync.isBusy());
        QVERIFY(runSync(sync));
        const DavCollection moveTarget = [&] {
            const auto list = DavStore::collections(account.id, QStringLiteral("calendar"));
            for (const DavCollection &c : list) {
                if (c.name == QStringLiteral("Ziel ") + run)
                    return c;
            }
            return DavCollection();
        }();
        QVERIFY(moveTarget.id > 0);
        DavItem moving;
        moving.uid = QStringLiteral("m-") + run;
        moving.collectionId = calendar.id;
        moving.href = calendar.href + moving.uid + QStringLiteral(".ics");
        moving.raw = Pim::applyEventFields({},
                                           {{QStringLiteral("summary"), QStringLiteral("Unterwegs")},
                                            {QStringLiteral("startMs"), double(start.toMSecsSinceEpoch())},
                                            {QStringLiteral("endMs"), double(start.addSecs(3600).toMSecsSinceEpoch())}},
                                           moving.uid);
        moving.pending = DavItem::Create;
        QVERIFY(Pim::deriveCalendarFields(moving));
        QVERIFY(DavStore::saveItem(moving));
        QVERIFY(runSync(sync));
        const qint64 movingId = DavStore::itemByUid(moving.uid).id;
        QCOMPARE(Pim::transferItem(movingId, moveTarget.id, false), QString());
        QCOMPARE(Pim::transferItem(movingId, moveTarget.id, true), QString());
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::pendingItems(account.id).size(), 0);
        // Frisch vom Server: im Ziel liegen Kopie und Original, in der Quelle nichts mehr
        DavStore::setCollectionCtag(calendar.id, {});
        DavStore::setCollectionCtag(moveTarget.id, {});
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::itemByUid(moving.uid).collectionId, moveTarget.id);
        QCOMPARE(DavStore::eventsInRange(start.addSecs(-3600).toMSecsSinceEpoch(), start.addSecs(7200).toMSecsSinceEpoch()).size(), 2);
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.removeCollection(moveTarget, std::move(d)); }), QString());

        // Sammlungen über den Client anlegen, umbenennen und löschen
        const auto byName = [&](const QString &name, const QString &kind) {
            const auto list = DavStore::collections(account.id, kind);
            for (const DavCollection &c : list) {
                if (c.name == name)
                    return c;
            }
            return DavCollection();
        };
        const QString newCalendar = QStringLiteral("Ärger & Co ") + run;
        const QString newList = QStringLiteral("Liste ") + run;
        const QString newBook = QStringLiteral("Bücher ") + run;
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("events"), newCalendar, QStringLiteral("#10b981"), std::move(d)); }), QString());
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("todos"), newList, {}, std::move(d)); }), QString());
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.createCollection(QStringLiteral("contacts"), newBook, QStringLiteral("#a855f7"), std::move(d)); }), QString());
        QTRY_VERIFY(!sync.isBusy());
        QVERIFY(runSync(sync));

        DavCollection createdCalendar = byName(newCalendar, QStringLiteral("calendar"));
        const DavCollection createdList = byName(newList, QStringLiteral("calendar"));
        const DavCollection createdBook = byName(newBook, QStringLiteral("addressbook"));
        QVERIFY(createdCalendar.id > 0);
        QVERIFY(createdCalendar.hasEvents);
        QVERIFY(!createdCalendar.hasTodos);
        QVERIFY(createdList.id > 0);
        QVERIFY(createdList.hasTodos);
        QVERIFY(!createdList.hasEvents);
        QVERIFY(createdBook.id > 0);
        QCOMPARE(createdCalendar.color, QStringLiteral("#10b981"));
        QCOMPARE(createdBook.color, QStringLiteral("#a855f7"));

        const QString renamed = QStringLiteral("Umbenannt ") + run;
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.updateCollection(createdCalendar, renamed, QStringLiteral("#f59e0b"), std::move(d)); }), QString());
        QVERIFY(runSync(sync)); // Name muss vom Server zurückkommen
        QCOMPARE(DavStore::collection(createdCalendar.id).name, renamed);
        QCOMPARE(DavStore::collection(createdCalendar.id).color, QStringLiteral("#f59e0b"));
        QCOMPARE(collectionCall([&](DavSync::Done d) { sync.updateCollection(createdBook, createdBook.name, QStringLiteral("#06b6d4"), std::move(d)); }), QString());
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::collection(createdBook.id).color, QStringLiteral("#06b6d4"));

        // Dabei auch aufräumen, was dieser Lauf angelegt hat
        const QList<DavCollection> created{DavStore::collection(createdCalendar.id), createdList, createdBook, calendar, book};
        for (const DavCollection &c : created)
            QCOMPARE(collectionCall([&](DavSync::Done d) { sync.removeCollection(c, std::move(d)); }), QString());
        QVERIFY(runSync(sync));
        QCOMPARE(DavStore::collection(createdCalendar.id).id, qint64(0));
        QCOMPARE(DavStore::collection(book.id).id, qint64(0));
    }

    void serviceAddAndRemove()
    {
        FakeDavServer server;
        server.setResource("/home/book/c1.vcf", vcard("c1", "Anna"));
        MemorySecrets secrets;
        DavService service(&secrets);
        QString error = QStringLiteral("offen");
        QSignalSpy changed(&service, &DavService::dataChanged);
        service.addAccount(server.base(), QStringLiteral("max"), QStringLiteral("geheim"), 0, [&](const QString &e) {
            error = e;
        });
        QTRY_VERIFY(error != QLatin1String("offen"));
        // write() geht an den echten SecretStore; ohne Wallet meldet er einen Fehler.
        if (!error.isEmpty())
            QSKIP("Kein Passwortspeicher in der Testumgebung");
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(DavStore::contacts({}).size(), 1);
        service.removeAccount(DavStore::accounts().constFirst().id);
        QCOMPARE(DavStore::contacts({}).size(), 0);
    }
};

QTEST_GUILESS_MAIN(DavTest)
#include "davtest.moc"

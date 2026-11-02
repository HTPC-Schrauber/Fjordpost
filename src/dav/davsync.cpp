#include "davsync.h"

#include "dav/davclient.h"
#include "pim/pimdata.h"

#include <KLocalizedString>

#include <QColor>
#include <QUuid>
#include <QLoggingCategory>
#include <QSet>
#include <QUrl>

Q_DECLARE_LOGGING_CATEGORY(lcDav)

namespace Fjordpost {

namespace {

const QByteArray collectionsQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
    "xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:i=\"http://apple.com/ns/ical/\" xmlns:inf=\"http://inf-it.com/ns/ab/\"><d:prop><d:resourcetype/><d:displayname/>"
    "<cs:getctag/><d:sync-token/><i:calendar-color/><inf:addressbook-color/><c:supported-calendar-component-set/><d:current-user-privilege-set/></d:prop></d:propfind>";
const QByteArray etagQuery =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/><d:resourcetype/></d:prop></d:propfind>";

QByteArray multiget(const DavCollection &collection, const QStringList &paths)
{
    const bool calendar = collection.kind == QLatin1String("calendar");
    QByteArray xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>";
    xml += calendar ? "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop><d:getetag/><c:calendar-data/></d:prop>"
                    : "<c:addressbook-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:carddav\"><d:prop><d:getetag/><c:address-data/></d:prop>";
    for (const QString &path : paths)
        xml += "<d:href>" + path.toHtmlEscaped().toUtf8() + "</d:href>";
    xml += calendar ? "</c:calendar-multiget>" : "</c:addressbook-multiget>";
    return xml;
}

// 403 meldet der Client als falsches Passwort; hier heißt es, dass der Server die Aktion nicht erlaubt.
QString collectionError(const DavReply &reply)
{
    if (reply.status == 403 || reply.status == 405 || reply.status == 501)
        return i18n("The server does not allow this action.");
    return reply.error;
}

// Apple-Farben haben oft einen Alphakanal (#RRGGBBAA), QColor erwartet #AARRGGBB.
QString normalizeColor(const QString &text)
{
    QString c = text.trimmed();
    if (c.size() == 9 && c.startsWith(QLatin1Char('#')))
        c.truncate(7);
    return QColor(c).isValid() ? c : QString();
}

} // namespace

DavSync::DavSync(const DavAccount &account, DavClient *client, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_client(client)
{
}

QUrl DavSync::url(const QString &href) const
{
    return QUrl(m_account.url).resolved(QUrl(href));
}

void DavSync::createCollection(const QString &kind, const QString &name, const QString &color, Done done)
{
    const bool contacts = kind == QLatin1String("contacts");
    QString home = contacts ? m_account.addressbookHome : m_account.calendarHome;
    if (home.isEmpty()) {
        done(contacts ? i18n("This account has no address books.") : i18n("This account has no calendars."));
        return;
    }
    if (!home.endsWith(QLatin1Char('/')))
        home += QLatin1Char('/');
    // Nie aus dem Namen bilden: Der kann sich ändern und Zeichen enthalten, die in Pfaden stören.
    const QUrl target = url(home + QUuid::createUuid().toString(QUuid::WithoutBraces) + QLatin1Char('/'));
    const QByteArray displayName = "<d:displayname>" + name.toHtmlEscaped().toUtf8() + "</d:displayname>";

    DavCollection created;
    created.accountId = m_account.id;
    created.kind = contacts ? QStringLiteral("addressbook") : QStringLiteral("calendar");
    created.href = target.toString();
    created.name = name;
    created.hasEvents = kind == QLatin1String("events");
    created.hasTodos = kind == QLatin1String("todos");
    created.color = contacts ? QString() : color;

    auto onReply = [this, created, contacts, color, done = std::move(done)](const DavReply &reply) mutable {
        if (!reply.ok()) {
            done(collectionError(reply));
            return;
        }
        created.id = DavStore::saveCollection(created);
        Q_EMIT dataChanged();
        if (contacts && !color.isEmpty()) {
            // Die Sammlung existiert; eine fehlgeschlagene Farbe ändert daran nichts.
            applyColor(created, color, [this, done = std::move(done)](const QString &) {
                sync();
                done({});
            });
            return;
        }
        sync();
        done({});
    };
    if (contacts) {
        // Farbe nicht im MKCOL: Das ist "alles oder nichts", und addressbook-color kennt nicht jeder Server.
        m_client->mkcol(target,
                        "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:mkcol xmlns:d=\"DAV:\" xmlns:a=\"urn:ietf:params:xml:ns:carddav\"><d:set><d:prop>"
                        "<d:resourcetype><d:collection/><a:addressbook/></d:resourcetype>" + displayName + "</d:prop></d:set></d:mkcol>",
                        std::move(onReply));
        return;
    }
    const QByteArray component = kind == QLatin1String("todos") ? "VTODO" : "VEVENT";
    const QByteArray colorProperty = color.isEmpty() ? QByteArray() : "<i:calendar-color>" + color.toHtmlEscaped().toUtf8() + "</i:calendar-color>";
    m_client->mkcalendar(target,
                         "<?xml version=\"1.0\" encoding=\"utf-8\"?><c:mkcalendar xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
                         "xmlns:i=\"http://apple.com/ns/ical/\"><d:set><d:prop>"
                         + displayName + colorProperty + "<c:supported-calendar-component-set><c:comp name=\"" + component + "\"/></c:supported-calendar-component-set>"
                         "</d:prop></d:set></c:mkcalendar>",
                         std::move(onReply));
}

void DavSync::patchProperty(const DavCollection &collection, const QByteArray &property, const QString &localName, PatchDone done)
{
    const QByteArray body = "<?xml version=\"1.0\" encoding=\"utf-8\"?><d:propertyupdate xmlns:d=\"DAV:\" xmlns:i=\"http://apple.com/ns/ical/\" "
                            "xmlns:inf=\"http://inf-it.com/ns/ab/\"><d:set><d:prop>" + property + "</d:prop></d:set></d:propertyupdate>";
    m_client->proppatch(url(collection.href), body, [localName, done = std::move(done)](const DavReply &reply) {
        if (!reply.ok()) {
            done(collectionError(reply), reply.status == 403 || reply.status == 405 || reply.status == 501);
            return;
        }
        // Eine Ablehnung kommt als 207 mit dem Status in der Antwort; nur Erfolge landen in props.
        const bool applied = std::any_of(reply.resources.cbegin(), reply.resources.cend(), [&localName](const DavResource &r) {
            return r.has(localName);
        });
        if (reply.status == 207 && !applied) {
            done(i18n("The server does not allow this action."), true);
            return;
        }
        done({}, false);
    });
}

void DavSync::updateCollection(const DavCollection &collection, const QString &name, const QString &color, Done done)
{
    if (name == collection.name) {
        applyColor(collection, color, std::move(done));
        return;
    }
    // Name und Farbe getrennt: PROPPATCH ist "alles oder nichts", eine abgelehnte Farbe verhinderte sonst das Umbenennen.
    const QByteArray property = "<d:displayname>" + name.toHtmlEscaped().toUtf8() + "</d:displayname>";
    patchProperty(collection, property, QStringLiteral("displayname"),
                  [this, collection, name, color, done = std::move(done)](const QString &error, bool) mutable {
        if (!error.isEmpty()) {
            done(error);
            return;
        }
        DavCollection changed = collection;
        changed.name = name;
        DavStore::saveCollection(changed);
        Q_EMIT dataChanged();
        applyColor(changed, color, std::move(done));
    });
}

void DavSync::applyColor(const DavCollection &collection, const QString &color, Done done)
{
    if (color.isEmpty() || color.compare(collection.color, Qt::CaseInsensitive) == 0) {
        done({});
        return;
    }
    const bool calendar = collection.kind == QLatin1String("calendar");
    const QByteArray value = color.toHtmlEscaped().toUtf8();
    const QByteArray tag = calendar ? "i:calendar-color" : "inf:addressbook-color";
    const QByteArray property = "<" + tag + ">" + value + "</" + tag + ">";
    patchProperty(collection, property, calendar ? QStringLiteral("calendar-color") : QStringLiteral("addressbook-color"),
                  [this, collection, color, calendar, done = std::move(done)](const QString &error, bool rejected) {
        // Für Adressbücher gibt es keinen Standard: Lehnt der Server ab, gilt die Farbe nur hier.
        // Kalender nicht: Dort schriebe der nächste Abgleich die Farbe des Servers zurück.
        if (!error.isEmpty() && (calendar || !rejected)) {
            done(error);
            return;
        }
        DavCollection changed = collection;
        changed.color = color;
        DavStore::saveCollection(changed);
        Q_EMIT dataChanged();
        done({});
    });
}

void DavSync::removeCollection(const DavCollection &collection, Done done)
{
    m_client->remove(url(collection.href), {}, [this, id = collection.id, done = std::move(done)](const DavReply &reply) {
        // 404: schon weg, also auch lokal entfernen
        if (!reply.ok() && reply.status != 404) {
            done(collectionError(reply));
            return;
        }
        DavStore::removeCollection(id);
        Q_EMIT dataChanged();
        done({});
    });
}

void DavSync::sync()
{
    if (m_running) {
        m_again = true;
        return;
    }
    m_running = true;
    m_pull = true;
    m_changed = false;
    m_warning.clear();
    m_flushQueue = DavStore::pendingItems(m_account.id);
    flushNext();
}

void DavSync::flush()
{
    if (m_running) {
        m_again = true;
        return;
    }
    m_running = true;
    m_pull = false;
    m_changed = false;
    m_warning.clear();
    m_flushQueue = DavStore::pendingItems(m_account.id);
    flushNext();
}

void DavSync::flushNext()
{
    if (m_flushQueue.isEmpty()) {
        if (m_pull)
            fetchCollections();
        else
            finish({});
        return;
    }
    const DavItem item = m_flushQueue.takeFirst();
    const qint64 id = item.id;
    const QUrl target = url(item.href);

    auto onConflict = [this, id, title = item.title] {
        // Der Server gewinnt: Beim nächsten Abgleich kommt seine Fassung.
        DavItem current = DavStore::item(id);
        if (current.id > 0) {
            current.pending = DavItem::Clean;
            current.etag.clear();
            DavStore::saveItem(current);
        }
        DavStore::setCollectionCtag(current.collectionId, {});
        m_pull = true; // Die Serverfassung sofort holen, nicht erst beim nächsten Lauf
        Q_EMIT conflict(title);
    };

    if (item.pending == DavItem::Delete) {
        m_client->remove(target, item.etag, [this, id, onConflict](const DavReply &reply) {
            if (reply.ok() || reply.status == 404)
                DavStore::removeItem(id);
            else if (reply.status == 412)
                onConflict();
            else if (m_warning.isEmpty())
                m_warning = reply.error;
            if (reply.status == 0 || reply.status == 401) {
                finish(reply.error);
                return;
            }
            flushNext();
        });
        return;
    }

    const bool contact = item.kind == QLatin1String("contact");
    const QByteArray type = contact ? "text/vcard; charset=utf-8" : "text/calendar; charset=utf-8";
    const QString ifMatch = item.pending == DavItem::Create ? QString() : item.etag;
    m_client->put(target, item.raw.toUtf8(), type, ifMatch, [this, id, onConflict](const DavReply &reply) {
        if (reply.ok()) {
            DavItem current = DavStore::item(id);
            if (current.id > 0) {
                current.pending = DavItem::Clean;
                // Ohne ETag in der Antwort (der Server darf den Inhalt ändern)
                // holt der nächste Abgleich die Ressource neu.
                current.etag = reply.etag;
                DavStore::saveItem(current);
            }
            m_changed = true;
        } else if (reply.status == 412) {
            onConflict();
        } else if (m_warning.isEmpty()) {
            m_warning = reply.error;
        }
        if (reply.status == 0 || reply.status == 401) {
            finish(reply.error);
            return;
        }
        flushNext();
    });
}

void DavSync::fetchCollections()
{
    QStringList homes;
    if (!m_account.calendarHome.isEmpty())
        homes << m_account.calendarHome;
    if (!m_account.addressbookHome.isEmpty() && !homes.contains(m_account.addressbookHome))
        homes << m_account.addressbookHome;
    m_seen.clear();
    readHome(homes, 0);
}

void DavSync::readHome(const QStringList &homes, int index)
{
    if (index >= homes.size()) {
        // Auf dem Server verschwundene Sammlungen entfernen
        const auto known = DavStore::collections(m_account.id);
        for (const DavCollection &c : known) {
            const bool found = std::any_of(m_seen.cbegin(), m_seen.cend(), [&c](const DavCollection &s) {
                return s.href == c.href;
            });
            if (!found) {
                DavStore::removeCollection(c.id);
                m_changed = true;
            }
        }
        m_collections = m_seen;
        syncNextCollection();
        return;
    }
    m_client->propfind(url(homes.at(index)), 1, collectionsQuery, [this, homes, index](const DavReply &reply) {
        if (reply.status != 207) {
            finish(reply.error.isEmpty() ? i18n("The calendars could not be read.") : reply.error);
            return;
        }
        const auto known = DavStore::collections(m_account.id);
        for (const DavResource &r : reply.resources) {
            const QStringList types = r.props.value(QStringLiteral("resourcetype")).names;
            const bool calendar = types.contains(QLatin1String("calendar"));
            if (!calendar && !types.contains(QLatin1String("addressbook")))
                continue;

            DavCollection c;
            c.accountId = m_account.id;
            c.kind = calendar ? QStringLiteral("calendar") : QStringLiteral("addressbook");
            c.href = reply.url.resolved(QUrl(r.href)).toString();
            c.name = r.text(QStringLiteral("displayname"));
            if (c.name.isEmpty())
                c.name = QUrl(c.href).fileName(QUrl::FullyDecoded).isEmpty() ? QUrl(c.href).adjusted(QUrl::StripTrailingSlash).fileName() : QUrl(c.href).fileName();
            c.color = normalizeColor(r.text(calendar ? QStringLiteral("calendar-color") : QStringLiteral("addressbook-color")));
            const QStringList comps = r.props.value(QStringLiteral("supported-calendar-component-set")).comps;
            c.hasEvents = calendar && (comps.isEmpty() || comps.contains(QLatin1String("VEVENT")));
            c.hasTodos = calendar && (comps.isEmpty() || comps.contains(QLatin1String("VTODO")));
            if (r.has(QStringLiteral("current-user-privilege-set"))) {
                const QStringList privileges = r.props.value(QStringLiteral("current-user-privilege-set")).names;
                c.readOnly = !privileges.contains(QLatin1String("write")) && !privileges.contains(QLatin1String("write-content"))
                    && !privileges.contains(QLatin1String("all"));
            }
            // Auch leere Sammlungen müssen in der Oberfläche erscheinen, ohne dass ein Eintrag sich ändert.
            const auto same = std::find_if(known.cbegin(), known.cend(), [&c](const DavCollection &k) {
                return k.href == c.href;
            });
            // Ohne Farbe vom Server bleibt die lokale (Adressbücher auf Servern ohne addressbook-color).
            if (c.color.isEmpty() && same != known.cend())
                c.color = same->color;
            if (same == known.cend() || same->name != c.name || same->color != c.color || same->readOnly != c.readOnly)
                m_changed = true;
            c.id = DavStore::saveCollection(c);
            // sync-token dient als Ersatz, falls der Server kein getctag kennt.
            c.ctag = r.has(QStringLiteral("getctag")) ? r.text(QStringLiteral("getctag")) : r.text(QStringLiteral("sync-token"));
            if (c.id > 0)
                m_seen << c;
        }
        readHome(homes, index + 1);
    });
}

void DavSync::syncNextCollection()
{
    if (m_collections.isEmpty()) {
        finish({});
        return;
    }
    const DavCollection remote = m_collections.takeFirst();
    const DavCollection local = DavStore::collection(remote.id);
    if (!remote.ctag.isEmpty() && remote.ctag == local.ctag) {
        syncNextCollection();
        return;
    }
    compareItems(local, remote.ctag);
}

void DavSync::compareItems(const DavCollection &collection, const QString &ctag)
{
    m_client->propfind(url(collection.href), 1, etagQuery, [this, collection, ctag](const DavReply &reply) {
        if (reply.status != 207) {
            finish(reply.error.isEmpty() ? i18n("The calendars could not be read.") : reply.error);
            return;
        }
        QHash<QString, DavStore::ItemState> local;
        const auto states = DavStore::itemStates(collection.id);
        for (const auto &s : states)
            local.insert(s.href, s);

        QSet<QString> remote;
        QStringList toFetch;
        for (const DavResource &r : reply.resources) {
            if (r.props.value(QStringLiteral("resourcetype")).names.contains(QLatin1String("collection")))
                continue;
            const QString href = reply.url.resolved(QUrl(r.href)).toString();
            remote.insert(href);
            const auto known = local.constFind(href);
            // Lokale Änderungen haben Vorrang; ihr ETag-Vergleich erfolgt beim Senden.
            if (known != local.cend() && known->pending != DavItem::Clean)
                continue;
            if (known == local.cend() || known->etag != r.text(QStringLiteral("getetag")))
                toFetch << href;
        }
        for (const auto &s : states) {
            if (!remote.contains(s.href) && s.pending != DavItem::Create) {
                DavStore::removeItem(s.id);
                m_changed = true;
            }
        }
        fetchBatch(collection, ctag, toFetch);
    });
}

void DavSync::fetchBatch(const DavCollection &collection, const QString &ctag, QStringList hrefs)
{
    if (hrefs.isEmpty()) {
        DavStore::setCollectionCtag(collection.id, ctag);
        syncNextCollection();
        return;
    }
    const QStringList batch = hrefs.mid(0, multigetBatch);
    hrefs.remove(0, batch.size());
    QStringList paths;
    for (const QString &href : batch)
        paths << QUrl(href).path(QUrl::FullyEncoded);

    m_client->report(url(collection.href), 1, multiget(collection, paths), [this, collection, ctag, hrefs](const DavReply &reply) {
        if (reply.status != 207) {
            finish(reply.error.isEmpty() ? i18n("The calendars could not be read.") : reply.error);
            return;
        }
        for (const DavResource &r : reply.resources)
            storeResource(collection, r);
        fetchBatch(collection, ctag, hrefs);
    });
}

void DavSync::storeResource(const DavCollection &collection, const DavResource &resource)
{
    const bool calendar = collection.kind == QLatin1String("calendar");
    const QString data = resource.text(calendar ? QStringLiteral("calendar-data") : QStringLiteral("address-data"));
    if (data.isEmpty())
        return;
    const QString href = QUrl(m_account.url).resolved(QUrl(resource.href)).toString();

    DavItem item = DavStore::itemByHref(collection.id, href);
    if (item.pending != DavItem::Clean)
        return;
    item.collectionId = collection.id;
    item.href = href;
    item.etag = resource.text(QStringLiteral("getetag"));
    item.raw = data;
    const bool ok = calendar ? Pim::deriveCalendarFields(item) : Pim::deriveContactFields(item);
    if (!ok) {
        // Nicht unser Fehler und kein Grund, den Abgleich abzubrechen.
        qCWarning(lcDav) << "Ressource nicht lesbar:" << href;
        return;
    }
    if (DavStore::saveItem(item))
        m_changed = true;
}

void DavSync::finish(const QString &error)
{
    m_running = false;
    m_flushQueue.clear();
    m_collections.clear();
    if (m_changed)
        Q_EMIT dataChanged();
    const QString message = error.isEmpty() ? m_warning : error;
    Q_EMIT finished(message.isEmpty(), message);
    if (m_again) {
        m_again = false;
        sync();
    }
}

} // namespace Fjordpost

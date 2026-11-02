#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace Fjordpost {

// Ein CalDAV/CardDAV-Zugang. Das Passwort liegt im SecretStore (secretKey()).
struct DavAccount {
    qint64 id = 0;
    QString name;
    QString url;
    QString username;
    qint64 mailAccountId = 0; // Absender für Einladungen, 0 = keiner
    QString calendarHome;
    QString addressbookHome;
    bool autoSchedule = false; // Server verschickt Einladungen selbst (RFC 6638)

    QString secretKey() const { return QStringLiteral("dav-%1").arg(id); }
};

struct DavCollection {
    qint64 id = 0;
    qint64 accountId = 0;
    QString kind; // "calendar", "addressbook"
    QString href;
    QString name;
    QString color;
    QString ctag;
    bool hasEvents = false;
    bool hasTodos = false;
    bool readOnly = false;
    bool visible = true;
};

// Eine Ressource auf dem Server: Termin(serie), Aufgabe oder Kontakt.
// raw ist die Wahrheit; die übrigen Felder sind daraus abgeleitet (pim/pimdata).
struct DavItem {
    enum Pending { Clean = 0, Create = 1, Modify = 2, Delete = 3 };

    qint64 id = 0;
    qint64 collectionId = 0;
    QString kind; // "event", "todo", "contact"
    QString href;
    QString etag;
    QString uid;
    QString raw;
    int pending = Clean;
    QString title;    // Titel bzw. Anzeigename
    QString subtitle; // Ort bzw. Firma
    qint64 rangeStart = 0; // ms seit 1970
    qint64 rangeEnd = 0;   // 0 = endlos
    bool hasAlarm = false;
    qint64 due = 0;
    bool completed = false;
    int priority = 0;
    QStringList emails; // kleingeschrieben
};

namespace DavStore {

QList<DavAccount> accounts();
DavAccount account(qint64 id);
qint64 saveAccount(const DavAccount &account); // liefert die ID oder 0
bool removeAccount(qint64 id);

QList<DavCollection> collections(qint64 accountId = 0, const QString &kind = {});
DavCollection collection(qint64 id);
qint64 saveCollection(const DavCollection &collection);
void removeCollection(qint64 id);
void setCollectionVisible(qint64 id, bool visible);
void setCollectionCtag(qint64 id, const QString &ctag);
// Farbe des Servers oder eine stabile Ersatzfarbe
QString displayColor(const DavCollection &collection);

DavItem item(qint64 id);
DavItem itemByHref(qint64 collectionId, const QString &href);
// Termin/Aufgabe zu einer UID (noch nicht gelöschte), über alle Kalender
DavItem itemByUid(const QString &uid);
// false, wenn ein Pflichtfeld verletzt wurde; setzt item.id bei neuen Einträgen.
bool saveItem(DavItem &item);
void removeItem(qint64 id);

// Alle bekannten Ressourcen einer Sammlung: href → etag
struct ItemState {
    QString href;
    QString etag;
    int pending = 0;
    qint64 id = 0;
};
QList<ItemState> itemStates(qint64 collectionId);
QList<DavItem> pendingItems(qint64 accountId);

// Termine sichtbarer Kalender, die [from, to) berühren können
QList<DavItem> eventsInRange(qint64 from, qint64 to);
QList<DavItem> alarmItems(qint64 from, qint64 to);
QList<DavItem> todos(qint64 collectionId, bool includeCompleted);
// Kontakte sichtbarer Adressbücher, sortiert; search filtert Name, Firma, Adressen
QList<DavItem> contacts(const QString &search, qint64 collectionId = 0);
DavItem contactByEmail(const QString &email);
// Vorschläge "Name <adresse>" für die Adresseingabe
QStringList completeAddress(const QString &prefix, int limit);

} // namespace DavStore
} // namespace Fjordpost

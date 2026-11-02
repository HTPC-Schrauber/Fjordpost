#pragma once

#include "storage/davstore.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

namespace Fjordpost {

class DavClient;
struct DavResource;

// Gleicht ein DAV-Konto ab: erst lokale Änderungen senden, dann Sammlungen und
// Inhalte holen. Sammlungen werden über CTag und ETags verglichen; das ist
// langsamer als sync-collection, funktioniert aber mit jedem Server.
class DavSync : public QObject
{
    Q_OBJECT
public:
    DavSync(const DavAccount &account, DavClient *client, QObject *parent = nullptr);

    void sync();
    // Nur ausstehende lokale Änderungen senden
    void flush();
    bool isBusy() const { return m_running; }

    // Sammlungen ändern geht direkt zum Server und wird nicht vorgemerkt: er darf ablehnen,
    // und das muss sofort sichtbar sein. done erhält eine Fehlermeldung oder einen leeren String.
    using Done = std::function<void(const QString &error)>;
    // kind: "events", "todos", "contacts"
    void createCollection(const QString &kind, const QString &name, const QString &color, Done done);
    // color leer = unverändert
    void updateCollection(const DavCollection &collection, const QString &name, const QString &color, Done done);
    void removeCollection(const DavCollection &collection, Done done);
    DavClient *client() const { return m_client; }

    static constexpr int multigetBatch = 50;

Q_SIGNALS:
    void finished(bool ok, const QString &error);
    void dataChanged();
    // Der Server hatte eine neuere Fassung; die lokale Änderung wurde verworfen.
    void conflict(const QString &title);

private:
    using PatchDone = std::function<void(const QString &error, bool rejected)>;
    void patchProperty(const DavCollection &collection, const QByteArray &property, const QString &localName, PatchDone done);
    void applyColor(const DavCollection &collection, const QString &color, Done done);
    void flushNext();
    void fetchCollections();
    void readHome(const QStringList &homes, int index);
    void syncNextCollection();
    void compareItems(const DavCollection &collection, const QString &ctag);
    void fetchBatch(const DavCollection &collection, const QString &ctag, QStringList hrefs);
    void storeResource(const DavCollection &collection, const DavResource &resource);
    void finish(const QString &error);
    QUrl url(const QString &href) const;

    DavAccount m_account;
    DavClient *m_client;
    bool m_running = false;
    bool m_pull = false;
    bool m_again = false;
    bool m_changed = false;
    QString m_warning;
    QList<DavItem> m_flushQueue;
    QList<DavCollection> m_collections;
    QList<DavCollection> m_seen;
};

} // namespace Fjordpost

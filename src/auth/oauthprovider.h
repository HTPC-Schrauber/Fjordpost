#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace Fjordpost {

// Ein Anbieter für die Anmeldung über OAuth2 (Google, Microsoft). Adressen und
// Bereiche stehen im Code, Client-ID und -Secret kommen aus der Konfiguration
// (Gruppe [OAuth] in fjordpost.conf, Beispiel in dev/fjordpost.conf.example).
struct OAuthProvider {
    QString id;   // "google", "microsoft"
    QString name; // für die Oberfläche
    QUrl authorizationUrl;
    QUrl tokenUrl;
    QString clientId;
    QString clientSecret; // Google verlangt es auch bei Desktop-Apps; Microsoft nicht
    QStringList mailScopes;
    QStringList davScopes; // leer = Anbieter hat kein CalDAV/CardDAV
    QList<QPair<QString, QString>> extraParameters; // zusätzlich an die Anmeldeadresse

    // Server, an denen der Anbieter erkannt wird bzw. die er vorgibt
    QStringList imapHosts;
    QString smtpHost;
    int smtpPort = 0;
    bool smtpStartTls = false;
    // Einstiegspunkte für DavDiscovery
    QUrl caldavUrl;
    QUrl carddavUrl;

    bool isValid() const { return !id.isEmpty(); }
    bool isConfigured() const { return !clientId.isEmpty(); }
    bool hasDav() const { return !davScopes.isEmpty(); }

    // Mit Client-ID und -Secret aus der Konfiguration; ungültig bei unbekannter id
    static OAuthProvider forId(const QString &id);
    // Anbieter zu einem IMAP-Server, leer wenn keiner passt
    static QString idForImapHost(const QString &host);
};

} // namespace Fjordpost

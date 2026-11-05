#pragma once

#include <QSqlDatabase>
#include <QString>

namespace Fjordpost {

// Zugriff auf die SQLite-Datenbank. Jeder Thread erhält eine eigene
// Verbindung (QSqlDatabase-Verbindungen sind nicht threadübergreifend nutzbar).
class Database
{
public:
    // Muss einmal beim Start (im Hauptthread) aufgerufen werden.
    // Legt die Datei an und führt ausstehende Migrationen aus.
    static bool initialize(const QString &path);

    // Verbindung für den aktuellen Thread.
    static QSqlDatabase connection();

    static QString path();
    // Verzeichnis der Datenbank; auch Wurzel für den BlobStore.
    static QString dataDir();
    static int schemaVersion();
    static int latestSchemaVersion();
    // Kurze Zufallskennung dieses Datenverzeichnisses (z. B. für Namen im SecretStore)
    static QString profileId();
    // Lesbarer, je Datenverzeichnis eindeutiger Name für einen SecretStore-Eintrag,
    // z. B. "Mail: max@example.de [3f9a·1]". Nicht übersetzt: Er muss stabil bleiben.
    static QString secretEntryName(const QString &kind, const QString &label, qint64 id);

private:
    static bool migrate(QSqlDatabase &db);
};

} // namespace Fjordpost

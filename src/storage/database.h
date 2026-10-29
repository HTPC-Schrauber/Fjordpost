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

private:
    static bool migrate(QSqlDatabase &db);
};

} // namespace Fjordpost

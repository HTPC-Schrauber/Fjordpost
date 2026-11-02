#pragma once

#include <QByteArray>
#include <QDateTime>

class QIODevice;

namespace Fjordpost::Mbox {

// Ein Eintrag im mboxrd-Format: "From "-Zeile, Nachricht mit LF-Zeilenenden, Leerzeile.
// Der Lesestatus wandert in die übliche Kopfzeile "Status", gesetzte Markierungen in "X-Status".
QByteArray entry(const QByteArray &raw, const QString &sender, const QDateTime &date, int flags);

// MessageFlag-Bitmaske aus Status/X-Status bzw. X-Mozilla-Status (Thunderbird).
// Ohne diese Angaben gilt die Nachricht als gelesen: Importe sind in der Regel Archive.
int flagsFromHeaders(const QByteArray &raw);

// Liest eine mbox-Datei Nachricht für Nachricht, ohne sie ganz in den Speicher zu laden.
class Reader
{
public:
    explicit Reader(QIODevice *device);
    // Nächste Nachricht ohne "From "-Zeile; leer am Dateiende.
    QByteArray next();

private:
    QIODevice *m_device;
    bool m_started = false;
};

} // namespace Fjordpost::Mbox

#pragma once

#include <QByteArray>
#include <QString>

namespace MopMail {

// Ablage der Roh-Nachrichten (RFC 822) als Dateien unterhalb des
// Datenverzeichnisses. In der Datenbank steht nur der relative Pfad.
namespace BlobStore {

// Speichert die Daten und gibt den relativen Pfad zurück (leer bei Fehler).
QString store(qint64 accountId, const QString &category, const QByteArray &data);
QByteArray load(const QString &relativePath);
bool remove(const QString &relativePath);
QString absolutePath(const QString &relativePath);

} // namespace BlobStore
} // namespace MopMail

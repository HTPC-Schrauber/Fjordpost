#pragma once

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace Fjordpost {

struct Label {
    qint64 id = 0;
    QString name;
    QString color;
    QString keyword; // IMAP-Schlüsselwort (klein geschrieben)
    int sortOrder = 0;
};

// Labels gelten für alle Konten. Auf dem Server werden sie als IMAP-Schlüsselwort
// gespeichert; Programme wie Thunderbird erkennen gleichnamige Tags daran wieder.
namespace LabelStore {

QList<Label> labels();
Label label(qint64 id);
// Liefert die neue ID oder 0 (z. B. Name schon vergeben).
qint64 create(const QString &name, const QString &color);
bool update(qint64 id, const QString &name, const QString &color);
bool remove(qint64 id);
// Anzahl der Nachrichten je Label (ohne Papierkorb/Spam)
QHash<qint64, int> counts();

QList<qint64> labelsOf(qint64 messageId);
// Ändert nur die lokale Zuordnung; true, wenn sich etwas geändert hat.
bool setLabel(qint64 messageId, qint64 labelId, bool on);
// Nachrichten mit diesem Label
QList<qint64> messagesWith(qint64 labelId);

// Abgleich mit dem Server: Schlüsselwörter (klein geschrieben) je Nachricht eines
// Ordners bzw. Labels einer Nachricht aus Serverschlüsselwörtern setzen.
// Unbekannte Schlüsselwörter werden ignoriert.
QHash<qint64, QSet<QString>> keywordsInFolder(qint64 folderId);
void applyKeywords(qint64 messageId, const QSet<QString> &keywords);
// Schlüsselwörter aus IMAP-Flags (ohne Systemflags wie \Seen oder $Forwarded)
QSet<QString> keywordsFromFlags(const QList<QByteArray> &flags);

// Schlüsselwort aus einem Labelnamen (ASCII-Atom, ohne Umlaute)
QString keywordFor(const QString &name);

} // namespace LabelStore
} // namespace Fjordpost

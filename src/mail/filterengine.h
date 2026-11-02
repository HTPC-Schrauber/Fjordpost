#pragma once

#include "storage/filterstore.h"

#include <QList>
#include <QSet>

namespace Fjordpost {

// Die Felder, nach denen Regeln filtern. Beim Eintreffen einer IMAP-Nachricht sind nur
// die Kopfzeilen bekannt, daher gibt es keine Bedingungen auf den Nachrichtentext.
struct FilterFields {
    QString fromName;
    QString fromEmail;
    QString to;
    QString cc;
    QString subject;
};

struct FilterResult {
    QList<qint64> untouched;   // ungelesen und noch im Ursprungsordner (für Benachrichtigungen)
    QSet<qint64> folders;      // geänderte Ordner
    QSet<qint64> accounts;     // Konten mit neuen Offline-Operationen
    int matched = 0;           // Nachrichten, auf die mindestens eine Regel gepasst hat
};

namespace FilterEngine {

bool matches(const FilterRule &rule, const FilterFields &fields);
// Wendet die Regeln an: lokale Änderung sofort, Serverseite über pending_ops.
// Der Aufrufer schickt die Operationen der gemeldeten Konten ab.
FilterResult apply(const QList<qint64> &messageIds, const QList<FilterRule> &rules);

} // namespace FilterEngine
} // namespace Fjordpost

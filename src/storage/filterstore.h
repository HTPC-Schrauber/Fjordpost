#pragma once

#include <QList>
#include <QString>

namespace Fjordpost {

struct FilterCondition {
    QString field; // "from", "to" (An + Cc), "subject", "any"
    QString op;    // "contains", "notContains", "is", "startsWith", "endsWith"
    QString value;
};

struct FilterAction {
    QString type;     // "read", "flag", "label", "archive", "junk", "trash", "move"
    QString argument; // "label": Label-ID, "move": Ordnerpfad (nur bei Regeln für ein Konto)
};

struct FilterRule {
    qint64 id = 0;
    QString name;
    bool enabled = true;
    qint64 accountId = 0; // 0 = alle Konten
    bool matchAll = true; // alle Bedingungen statt mindestens einer
    QList<FilterCondition> conditions;
    QList<FilterAction> actions;
    bool stop = false; // danach keine weiteren Regeln anwenden
    int sortOrder = 0;
};

namespace FilterStore {

// In Anwendungsreihenfolge
QList<FilterRule> rules();
FilterRule rule(qint64 id);
// Legt die Regel an (id 0) oder ändert sie; liefert die ID oder 0.
qint64 save(FilterRule rule);
bool remove(qint64 id);
// Tauscht die Regel mit ihrem Vorgänger (delta -1) bzw. Nachfolger (+1).
void move(qint64 id, int delta);
void removeForAccount(qint64 accountId);

} // namespace FilterStore
} // namespace Fjordpost

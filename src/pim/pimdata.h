#pragma once

#include "storage/davstore.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace Fjordpost::Pim {

// Eine konkrete Vorkommnis eines Termins (Serien aufgelöst)
struct Occurrence {
    qint64 itemId = 0;
    QString title;
    QString location;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    bool recurring = false;
};

// Füllt die abgeleiteten Felder (title, range_*, due, ...) aus item.raw und
// setzt kind und uid. false, wenn raw nicht lesbar ist.
bool deriveCalendarFields(DavItem &item);
bool deriveContactFields(DavItem &item);

// Vorkommnisse in [from, to), nach Beginn sortiert
QList<Occurrence> occurrences(const DavItem &item, const QDateTime &from, const QDateTime &to);
// Erinnerungszeitpunkte in [from, to)
struct AlarmHit {
    QString title;
    QDateTime when;  // Beginn des Termins
};
QList<AlarmHit> alarmsBetween(const DavItem &item, const QDateTime &from, const QDateTime &to);

// Felder für Oberfläche und Formulare. Termin: summary, location, description,
// startMs, endMs, allDay, repeat ("none", "daily", "weekly", "monthly", "yearly",
// "custom"), reminderMinutes (-1 = keine), attendees (Liste "Name <adresse>"),
// organizer. Aufgabe: summary, description, dueMs, priority, completed, reminderMinutes.
QVariantMap eventFields(const QString &raw);
QVariantMap todoFields(const QString &raw);
// Übernimmt die Felder in raw (leer = neue Ressource) und erhält alles,
// was das Formular nicht kennt. Liefert das neue iCalendar oder einen leeren String.
// Termine: bei inhaltlichen Änderungen wird SEQUENCE erhöht.
QString applyEventFields(const QString &raw, const QVariantMap &fields, const QString &uid);
QString applyTodoFields(const QString &raw, const QVariantMap &fields, const QString &uid);
QString setTodoCompleted(const QString &raw, bool completed);

// Kontakt: name, organization, jobTitle, emails, phones, address, birthdayMs, note
QVariantMap contactFields(const QString &raw);
QString applyContactFields(const QString &raw, const QVariantMap &fields, const QString &uid);

// Neue Datei-URL in einer Sammlung. Nie aus der UID bilden: Bei Einladungen kommt sie von außen.
QString newHref(const DavCollection &collection, const QString &extension);
// Kopie einer Ressource mit neuer UID; alle übrigen Zeilen bleiben erhalten. Termine verlieren
// Organisator und Teilnehmer, sonst verschickte ein Server mit Auto-Schedule neue Einladungen.
QString rawWithNewUid(const QString &raw, const QString &uid);
// Legt die Ressource in der Zielsammlung an (ausstehend) und merkt beim Verschieben das Original
// zum Löschen vor. Es hat die kleinere id, geht also zuerst zum Server; so stört die gleiche UID nicht.
// Fehlermeldung oder leerer String.
QString transferItem(qint64 itemId, qint64 targetCollectionId, bool move);

} // namespace Fjordpost::Pim

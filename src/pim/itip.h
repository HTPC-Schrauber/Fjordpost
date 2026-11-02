#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace Fjordpost::Itip {

// Inhalt einer Einladungs-Mail (RFC 5546)
struct Invitation {
    bool valid = false;
    QString method; // "REQUEST", "REPLY", "CANCEL", ...
    QString uid;
    int sequence = 0;
    QString summary;
    QString location;
    QString description;
    QDateTime start;
    QDateTime end;
    bool allDay = false;
    QString organizer;      // Adresse
    QString organizerName;
    QStringList attendees;  // "Name <adresse>"
    // Bei REPLY: Absender der Antwort und seine Entscheidung
    QString replyFrom;
    QString replyStatus; // "ACCEPTED", "TENTATIVE", "DECLINED"
};

Invitation parse(const QByteArray &ics);

// Die zu speichernde Ressource: ohne METHOD, eigene Teilnahme auf status gesetzt.
QString toCalendarObject(const QByteArray &ics, const QString &ownEmail, const QString &status);
// METHOD:REPLY mit genau dem eigenen Teilnehmer
QByteArray buildReply(const QByteArray &ics, const QString &ownEmail, const QString &status);
// METHOD:REQUEST bzw. CANCEL aus der gespeicherten Ressource (für alle Teilnehmer)
QByteArray buildRequest(const QString &raw);
QByteArray buildCancel(const QString &raw);
// Trägt die Antwort eines Teilnehmers in die eigene Ressource ein; leer, wenn nichts passt.
QString applyReply(const QString &raw, const QString &attendeeEmail, const QString &status);
// Eigene Teilnahme in einer Ressource ("ACCEPTED", ...), leer wenn nicht eingeladen
QString statusOf(const QString &raw, const QString &email);
// Lesetext für Mailprogramme ohne Kalenderunterstützung
QString plainTextSummary(const Invitation &invitation);

} // namespace Fjordpost::Itip

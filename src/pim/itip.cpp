#include "itip.h"

#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KLocalizedString>

#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

using namespace KCalendarCore;

namespace Fjordpost::Itip {

namespace {

MemoryCalendar::Ptr parseCalendar(const QString &ics)
{
    MemoryCalendar::Ptr calendar(new MemoryCalendar(QTimeZone::systemTimeZone()));
    ICalFormat format;
    return format.fromString(calendar, ics) ? calendar : MemoryCalendar::Ptr();
}

Event::Ptr masterOf(const MemoryCalendar::Ptr &calendar)
{
    const Event::List events = calendar ? calendar->rawEvents() : Event::List();
    for (const Event::Ptr &e : events) {
        if (!e->hasRecurrenceId())
            return e;
    }
    return events.isEmpty() ? Event::Ptr() : events.constFirst();
}

// METHOD steht auf einer eigenen Zeile des VCALENDAR; KCalendarCore behält sie nicht.
QString methodOf(const QString &ics)
{
    static const QRegularExpression re(QStringLiteral("^METHOD:\\s*([A-Za-z-]+)\\s*$"), QRegularExpression::MultilineOption);
    return re.match(QString(ics).remove(QLatin1Char('\r'))).captured(1).toUpper();
}

QString statusName(Attendee::PartStat status)
{
    switch (status) {
    case Attendee::Accepted:
        return QStringLiteral("ACCEPTED");
    case Attendee::Tentative:
        return QStringLiteral("TENTATIVE");
    case Attendee::Declined:
        return QStringLiteral("DECLINED");
    default:
        return QStringLiteral("NEEDS-ACTION");
    }
}

Attendee::PartStat statusFrom(const QString &name)
{
    if (name == QLatin1String("ACCEPTED"))
        return Attendee::Accepted;
    if (name == QLatin1String("TENTATIVE"))
        return Attendee::Tentative;
    if (name == QLatin1String("DECLINED"))
        return Attendee::Declined;
    return Attendee::NeedsAction;
}

// Setzt die Teilnahme; false, wenn die Adresse nicht eingeladen ist.
bool setStatus(const Event::Ptr &event, const QString &email, Attendee::PartStat status)
{
    Attendee::List list = event->attendees();
    bool found = false;
    for (Attendee &a : list) {
        if (a.email().compare(email, Qt::CaseInsensitive) == 0) {
            a.setStatus(status);
            a.setRSVP(false);
            found = true;
        }
    }
    if (found)
        event->setAttendees(list);
    return found;
}

} // namespace

Invitation parse(const QByteArray &ics)
{
    Invitation inv;
    const QString text = QString::fromUtf8(ics);
    const Event::Ptr e = masterOf(parseCalendar(text));
    if (!e)
        return inv;
    inv.valid = true;
    inv.method = methodOf(text);
    inv.uid = e->uid();
    inv.sequence = e->revision();
    inv.summary = e->summary();
    inv.location = e->location();
    inv.description = e->description();
    inv.start = e->dtStart();
    inv.end = e->dtEnd();
    inv.allDay = e->allDay();
    inv.organizer = e->organizer().email();
    inv.organizerName = e->organizer().name();
    const auto attendees = e->attendees();
    for (const Attendee &a : attendees)
        inv.attendees << (a.name().isEmpty() ? a.email() : QStringLiteral("%1 <%2>").arg(a.name(), a.email()));
    if (inv.method == QLatin1String("REPLY") && !attendees.isEmpty()) {
        inv.replyFrom = attendees.constFirst().email();
        inv.replyStatus = statusName(attendees.constFirst().status());
    }
    return inv;
}

QString toCalendarObject(const QByteArray &ics, const QString &ownEmail, const QString &status)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(QString::fromUtf8(ics));
    const Event::Ptr e = masterOf(calendar);
    if (!e)
        return {};
    setStatus(e, ownEmail, statusFrom(status));
    ICalFormat format;
    return format.toString(calendar); // schreibt kein METHOD
}

QByteArray buildReply(const QByteArray &ics, const QString &ownEmail, const QString &status)
{
    const Event::Ptr e = masterOf(parseCalendar(QString::fromUtf8(ics)));
    if (!e || !setStatus(e, ownEmail, statusFrom(status)))
        return {};
    Attendee::List own;
    const auto attendees = e->attendees();
    for (const Attendee &a : attendees) {
        if (a.email().compare(ownEmail, Qt::CaseInsensitive) == 0)
            own << a;
    }
    e->setAttendees(own);
    ICalFormat format;
    return format.createScheduleMessage(e, iTIPReply).toUtf8();
}

QByteArray buildRequest(const QString &raw)
{
    const Event::Ptr e = masterOf(parseCalendar(raw));
    if (!e)
        return {};
    ICalFormat format;
    return format.createScheduleMessage(e, iTIPRequest).toUtf8();
}

QByteArray buildCancel(const QString &raw)
{
    const Event::Ptr e = masterOf(parseCalendar(raw));
    if (!e)
        return {};
    e->setStatus(Incidence::StatusCanceled);
    e->setRevision(e->revision() + 1);
    ICalFormat format;
    return format.createScheduleMessage(e, iTIPCancel).toUtf8();
}

QString applyReply(const QString &raw, const QString &attendeeEmail, const QString &status)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    const Event::Ptr e = masterOf(calendar);
    if (!e || !setStatus(e, attendeeEmail, statusFrom(status)))
        return {};
    ICalFormat format;
    return format.toString(calendar);
}

QString statusOf(const QString &raw, const QString &email)
{
    const Event::Ptr e = masterOf(parseCalendar(raw));
    if (!e)
        return {};
    const auto attendees = e->attendees();
    for (const Attendee &a : attendees) {
        if (a.email().compare(email, Qt::CaseInsensitive) == 0)
            return statusName(a.status());
    }
    return {};
}

QString plainTextSummary(const Invitation &inv)
{
    const QLocale locale;
    QString text = inv.summary + QLatin1Char('\n');
    text += inv.allDay ? locale.toString(inv.start.date(), QLocale::LongFormat) : locale.toString(inv.start.toLocalTime(), QLocale::LongFormat);
    if (!inv.location.isEmpty())
        text += QLatin1Char('\n') + inv.location;
    if (!inv.description.isEmpty())
        text += QLatin1String("\n\n") + inv.description;
    return text;
}

} // namespace Fjordpost::Itip

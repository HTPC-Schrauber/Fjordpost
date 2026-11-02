#include "pimdata.h"

#include <KCalendarCore/Alarm>
#include <KCalendarCore/Attendee>
#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/OccurrenceIterator>
#include <KCalendarCore/Person>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/Todo>
#include <KContacts/Addressee>
#include <KContacts/VCardConverter>

#include <KLocalizedString>

#include <QRegularExpression>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

#include <algorithm>

using namespace KCalendarCore;

namespace Fjordpost::Pim {

namespace {

MemoryCalendar::Ptr parseCalendar(const QString &raw)
{
    MemoryCalendar::Ptr calendar(new MemoryCalendar(QTimeZone::systemTimeZone()));
    if (raw.isEmpty())
        return calendar;
    ICalFormat format;
    if (!format.fromString(calendar, raw))
        return {};
    return calendar;
}

// Hauptteil einer Ressource: die Instanz ohne RECURRENCE-ID
template<typename List>
auto masterOf(const List &list) -> typename List::value_type
{
    for (const auto &i : list) {
        if (!i->hasRecurrenceId())
            return i;
    }
    return list.isEmpty() ? typename List::value_type() : list.constFirst();
}

qint64 toMs(const QDateTime &dt)
{
    return dt.isValid() ? dt.toMSecsSinceEpoch() : 0;
}

QDateTime fromMs(const QVariant &v)
{
    const qint64 ms = qint64(v.toDouble());
    return ms > 0 ? QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::systemTimeZone()) : QDateTime();
}

QString serialize(const MemoryCalendar::Ptr &calendar)
{
    ICalFormat format;
    return format.toString(calendar);
}

int reminderMinutes(const Incidence::Ptr &incidence)
{
    const auto alarms = incidence->alarms();
    for (const Alarm::Ptr &a : alarms) {
        if (a->hasStartOffset())
            return -a->startOffset().asSeconds() / 60;
        if (a->hasEndOffset())
            return -a->endOffset().asSeconds() / 60;
    }
    return -1;
}

void applyReminder(const Incidence::Ptr &incidence, int minutes)
{
    if (reminderMinutes(incidence) == minutes)
        return;
    const auto alarms = incidence->alarms();
    for (const Alarm::Ptr &a : alarms) {
        if (a->hasStartOffset() || a->hasEndOffset())
            incidence->removeAlarm(a);
    }
    if (minutes < 0)
        return;
    Alarm::Ptr alarm = incidence->newAlarm();
    alarm->setDisplayAlarm(incidence->summary());
    // Aufgaben haben meist keinen Beginn: RELATED=END bezieht sich auf die Fälligkeit.
    if (incidence->type() == IncidenceBase::TypeTodo)
        alarm->setEndOffset(Duration(-minutes * 60));
    else
        alarm->setStartOffset(Duration(-minutes * 60));
    alarm->setEnabled(true);
}

QString attendeeText(const Attendee &a)
{
    return a.name().isEmpty() ? a.email() : QStringLiteral("%1 <%2>").arg(a.name(), a.email());
}

// "Name <adresse>" oder "adresse"
void splitAddress(const QString &text, QString *name, QString *email)
{
    const qsizetype lt = text.lastIndexOf(QLatin1Char('<'));
    if (lt >= 0 && text.endsWith(QLatin1Char('>'))) {
        *name = text.left(lt).trimmed().remove(QLatin1Char('"'));
        *email = text.mid(lt + 1, text.size() - lt - 2).trimmed();
    } else {
        name->clear();
        *email = text.trimmed();
    }
}

QString repeatOf(const Incidence::Ptr &incidence)
{
    if (!incidence->recurs())
        return QStringLiteral("none");
    const Recurrence *r = incidence->recurrence();
    if (r->frequency() != 1 || r->duration() != -1 || r->rRules().size() != 1 || !r->rDateTimes().isEmpty())
        return QStringLiteral("custom");
    switch (r->recurrenceType()) {
    case Recurrence::rDaily:
        return QStringLiteral("daily");
    case Recurrence::rWeekly:
        return QStringLiteral("weekly");
    case Recurrence::rMonthlyDay:
    case Recurrence::rMonthlyPos:
        return QStringLiteral("monthly");
    case Recurrence::rYearlyMonth:
    case Recurrence::rYearlyDay:
    case Recurrence::rYearlyPos:
        return QStringLiteral("yearly");
    default:
        return QStringLiteral("custom");
    }
}

void applyRepeat(const Incidence::Ptr &incidence, const QString &repeat)
{
    if (repeat == QLatin1String("custom") || repeat == repeatOf(incidence))
        return;
    Recurrence *r = incidence->recurrence();
    r->clear();
    if (repeat == QLatin1String("daily"))
        r->setDaily(1);
    else if (repeat == QLatin1String("weekly"))
        r->setWeekly(1);
    else if (repeat == QLatin1String("monthly"))
        r->setMonthly(1);
    else if (repeat == QLatin1String("yearly"))
        r->setYearly(1);
}

} // namespace

bool deriveCalendarFields(DavItem &item)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(item.raw);
    if (!calendar)
        return false;
    const Event::List events = calendar->rawEvents();
    const Todo::List todos = calendar->rawTodos();
    if (!events.isEmpty()) {
        const Event::Ptr master = masterOf(events);
        item.kind = QStringLiteral("event");
        item.uid = master->uid();
        item.title = master->summary();
        item.subtitle = master->location();
        QDateTime start = master->dtStart();
        // Ganztägige Enden sind einschließlich: bis zum Ende des Tages zählen.
        QDateTime end = master->allDay() ? master->dtEnd().addDays(1) : master->dtEnd();
        bool open = false;
        bool alarm = !master->alarms().isEmpty();
        for (const Event::Ptr &e : events) {
            alarm = alarm || !e->alarms().isEmpty();
            if (e->dtStart() < start)
                start = e->dtStart();
            if (e->dtEnd() > end)
                end = e->allDay() ? e->dtEnd().addDays(1) : e->dtEnd();
            if (e->recurs()) {
                const QDateTime last = e->recurrence()->endDateTime();
                if (e->recurrence()->duration() == -1 || !last.isValid())
                    open = true;
                else if (last.addSecs(e->dtStart().secsTo(e->dtEnd()) + 86400) > end)
                    end = last.addSecs(e->dtStart().secsTo(e->dtEnd()) + 86400);
            }
        }
        item.rangeStart = toMs(start);
        item.rangeEnd = open ? 0 : qMax(toMs(end), toMs(start) + 1);
        item.hasAlarm = alarm;
        item.emails.clear();
        return true;
    }
    if (!todos.isEmpty()) {
        const Todo::Ptr master = masterOf(todos);
        item.kind = QStringLiteral("todo");
        item.uid = master->uid();
        item.title = master->summary();
        item.subtitle = master->location();
        item.due = master->hasDueDate() ? toMs(master->dtDue(true)) : 0;
        item.completed = master->isCompleted();
        item.priority = master->priority();
        // Der Wecker orientiert sich am Fälligkeitsdatum.
        item.rangeStart = item.due;
        item.rangeEnd = item.due > 0 ? item.due + 1 : 0;
        item.hasAlarm = !master->alarms().isEmpty() && item.due > 0 && !item.completed;
        item.emails.clear();
        return true;
    }
    return false;
}

bool deriveContactFields(DavItem &item)
{
    KContacts::VCardConverter converter;
    const KContacts::Addressee a = converter.parseVCard(item.raw.toUtf8());
    if (a.isEmpty())
        return false;
    item.kind = QStringLiteral("contact");
    item.uid = a.uid();
    item.title = a.realName();
    if (item.title.isEmpty())
        item.title = a.organization();
    if (item.title.isEmpty())
        item.title = a.preferredEmail();
    item.subtitle = a.organization();
    item.emails = a.emails();
    return true;
}

QList<Occurrence> occurrences(const DavItem &item, const QDateTime &from, const QDateTime &to)
{
    QList<Occurrence> result;
    const MemoryCalendar::Ptr calendar = parseCalendar(item.raw);
    if (!calendar)
        return result;
    OccurrenceIterator it(*calendar, from, to);
    while (it.hasNext()) {
        it.next();
        const Incidence::Ptr incidence = it.incidence();
        if (incidence->type() != IncidenceBase::TypeEvent || incidence->status() == Incidence::StatusCanceled)
            continue;
        const auto event = incidence.staticCast<Event>();
        Occurrence o;
        o.itemId = item.id;
        o.title = event->summary();
        o.location = event->location();
        o.start = it.occurrenceStartDate();
        o.end = event->endDateForStart(o.start);
        o.allDay = event->allDay();
        o.recurring = event->recurs() || event->hasRecurrenceId();
        result << o;
    }
    std::sort(result.begin(), result.end(), [](const Occurrence &a, const Occurrence &b) {
        return a.start < b.start;
    });
    return result;
}

QList<AlarmHit> alarmsBetween(const DavItem &item, const QDateTime &from, const QDateTime &to)
{
    QList<AlarmHit> result;
    const MemoryCalendar::Ptr calendar = parseCalendar(item.raw);
    if (!calendar)
        return result;
    if (item.kind == QLatin1String("todo")) {
        const Todo::Ptr todo = masterOf(calendar->rawTodos());
        if (!todo)
            return result;
        const auto alarms = todo->alarms();
        for (const Alarm::Ptr &a : alarms) {
            const QDateTime due = todo->dtDue(true);
            const QDateTime when = a->hasEndOffset() ? due.addSecs(a->endOffset().asSeconds())
                : a->hasStartOffset()                ? due.addSecs(a->startOffset().asSeconds())
                                                     : a->time();
            if (when >= from && when < to)
                result << AlarmHit{todo->summary(), due};
        }
        return result;
    }
    // Vorlauf: die Erinnerung liegt vor dem Beginn, also bis zu einer Woche weiter suchen.
    OccurrenceIterator it(*calendar, from, to.addDays(7));
    while (it.hasNext()) {
        it.next();
        const auto alarms = it.incidence()->alarms();
        for (const Alarm::Ptr &a : alarms) {
            if (!a->enabled())
                continue;
            const QDateTime when = a->hasStartOffset() ? it.occurrenceStartDate().addSecs(a->startOffset().asSeconds()) : a->time();
            if (when >= from && when < to)
                result << AlarmHit{it.incidence()->summary(), it.occurrenceStartDate()};
        }
    }
    return result;
}

QVariantMap eventFields(const QString &raw)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    if (!calendar)
        return {};
    const Event::Ptr e = masterOf(calendar->rawEvents());
    if (!e)
        return {};
    QStringList attendees;
    const auto list = e->attendees();
    for (const Attendee &a : list)
        attendees << attendeeText(a);
    // Ganztägig: das Formular arbeitet mit dem letzten Tag einschließlich.
    return {{QStringLiteral("summary"), e->summary()},
            {QStringLiteral("location"), e->location()},
            {QStringLiteral("description"), e->description()},
            {QStringLiteral("startMs"), double(toMs(e->dtStart()))},
            {QStringLiteral("endMs"), double(toMs(e->dtEnd()))},
            {QStringLiteral("allDay"), e->allDay()},
            {QStringLiteral("repeat"), repeatOf(e)},
            {QStringLiteral("reminderMinutes"), reminderMinutes(e)},
            {QStringLiteral("attendees"), attendees},
            {QStringLiteral("organizer"), e->organizer().email()}};
}

QVariantMap todoFields(const QString &raw)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    if (!calendar)
        return {};
    const Todo::Ptr t = masterOf(calendar->rawTodos());
    if (!t)
        return {};
    return {{QStringLiteral("summary"), t->summary()},
            {QStringLiteral("description"), t->description()},
            {QStringLiteral("dueMs"), t->hasDueDate() ? double(toMs(t->dtDue(true))) : 0.0},
            {QStringLiteral("priority"), t->priority()},
            {QStringLiteral("completed"), t->isCompleted()},
            {QStringLiteral("reminderMinutes"), reminderMinutes(t)}};
}

QString applyEventFields(const QString &raw, const QVariantMap &f, const QString &uid)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    if (!calendar)
        return {};
    Event::Ptr e = masterOf(calendar->rawEvents());
    const bool isNew = !e;
    if (isNew) {
        e = Event::Ptr(new Event);
        e->setUid(uid);
    }

    const bool allDay = f.value(QStringLiteral("allDay")).toBool();
    QDateTime start = fromMs(f.value(QStringLiteral("startMs")));
    QDateTime end = fromMs(f.value(QStringLiteral("endMs")));
    if (!start.isValid())
        return {};
    if (!end.isValid() || end < start)
        end = allDay ? start : start.addSecs(3600);
    const QString summary = f.value(QStringLiteral("summary")).toString();
    const QString location = f.value(QStringLiteral("location")).toString();
    const bool timeChanged = isNew || e->allDay() != allDay || toMs(e->dtStart()) != toMs(start) || toMs(e->dtEnd()) != toMs(end);

    if (!isNew && (timeChanged || e->summary() != summary || e->location() != location))
        e->setRevision(e->revision() + 1); // RFC 5546: Teilnehmer erkennen so die neue Fassung

    if (timeChanged) {
        e->setAllDay(allDay);
        e->setDtStart(start);
        e->setDtEnd(end);
    }
    e->setSummary(summary);
    e->setLocation(location);
    e->setDescription(f.value(QStringLiteral("description")).toString());
    applyRepeat(e, f.value(QStringLiteral("repeat"), QStringLiteral("none")).toString());
    applyReminder(e, f.value(QStringLiteral("reminderMinutes"), -1).toInt());

    if (f.contains(QStringLiteral("attendees"))) {
        const QStringList wanted = f.value(QStringLiteral("attendees")).toStringList();
        Attendee::List old = e->attendees();
        Attendee::List next;
        for (const QString &text : wanted) {
            QString name, email;
            splitAddress(text, &name, &email);
            if (email.isEmpty())
                continue;
            auto known = std::find_if(old.cbegin(), old.cend(), [&email](const Attendee &a) {
                return a.email().compare(email, Qt::CaseInsensitive) == 0;
            });
            if (known != old.cend())
                next << *known; // Rückmeldung (PARTSTAT) behalten
            else
                next << Attendee(name, email, true, Attendee::NeedsAction, Attendee::ReqParticipant);
        }
        e->setAttendees(next);
    }
    if (const QString organizer = f.value(QStringLiteral("organizer")).toString(); !organizer.isEmpty()) {
        QString name, email;
        splitAddress(organizer, &name, &email);
        e->setOrganizer(Person(name, email));
    }
    if (isNew)
        calendar->addEvent(e);
    return serialize(calendar);
}

QString applyTodoFields(const QString &raw, const QVariantMap &f, const QString &uid)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    if (!calendar)
        return {};
    Todo::Ptr t = masterOf(calendar->rawTodos());
    const bool isNew = !t;
    if (isNew) {
        t = Todo::Ptr(new Todo);
        t->setUid(uid);
    }
    t->setSummary(f.value(QStringLiteral("summary")).toString());
    t->setDescription(f.value(QStringLiteral("description")).toString());
    const QDateTime due = fromMs(f.value(QStringLiteral("dueMs")));
    t->setDtDue(due);
    t->setPriority(f.value(QStringLiteral("priority")).toInt());
    // Eine Erinnerung ohne Fälligkeit hat keinen Bezugspunkt.
    applyReminder(t, due.isValid() ? f.value(QStringLiteral("reminderMinutes"), -1).toInt() : -1);
    if (isNew)
        calendar->addTodo(t);
    return serialize(calendar);
}

QString setTodoCompleted(const QString &raw, bool completed)
{
    const MemoryCalendar::Ptr calendar = parseCalendar(raw);
    const Todo::Ptr t = calendar ? masterOf(calendar->rawTodos()) : Todo::Ptr();
    if (!t)
        return {};
    t->setCompleted(completed);
    if (completed)
        t->setCompleted(QDateTime::currentDateTimeUtc());
    return serialize(calendar);
}

QVariantMap contactFields(const QString &raw)
{
    KContacts::VCardConverter converter;
    const KContacts::Addressee a = converter.parseVCard(raw.toUtf8());
    QStringList phones;
    const auto numbers = a.phoneNumbers();
    for (const KContacts::PhoneNumber &p : numbers)
        phones << p.number();
    const auto addresses = a.addresses();
    return {{QStringLiteral("name"), a.realName()},
            {QStringLiteral("organization"), a.organization()},
            {QStringLiteral("jobTitle"), a.title()},
            {QStringLiteral("emails"), a.emails()},
            {QStringLiteral("phones"), phones},
            {QStringLiteral("address"), addresses.isEmpty() ? QString() : addresses.constFirst().formatted(KContacts::AddressFormatStyle::Postal).trimmed()},
            {QStringLiteral("birthdayMs"), double(toMs(a.birthday()))},
            {QStringLiteral("note"), a.note()}};
}

QString applyContactFields(const QString &raw, const QVariantMap &f, const QString &uid)
{
    KContacts::VCardConverter converter;
    KContacts::Addressee a = raw.isEmpty() ? KContacts::Addressee() : converter.parseVCard(raw.toUtf8());
    if (raw.isEmpty())
        a.setUid(uid);

    const QString name = f.value(QStringLiteral("name")).toString().trimmed();
    if (a.realName() != name) {
        a.setNameFromString(name);
        a.setFormattedName(name);
    }
    a.setOrganization(f.value(QStringLiteral("organization")).toString());
    a.setTitle(f.value(QStringLiteral("jobTitle")).toString());
    a.setNote(f.value(QStringLiteral("note")).toString());

    // Vorhandene Einträge (mit ihren Typen) bleiben erhalten, wenn sie noch vorkommen.
    const QStringList wantedEmails = f.value(QStringLiteral("emails")).toStringList();
    KContacts::Email::List emails;
    const auto oldEmails = a.emailList();
    for (const QString &text : wantedEmails) {
        const auto known = std::find_if(oldEmails.cbegin(), oldEmails.cend(), [&text](const KContacts::Email &e) {
            return e.mail() == text;
        });
        emails << (known != oldEmails.cend() ? *known : KContacts::Email(text));
    }
    a.setEmailList(emails);

    const QStringList wantedPhones = f.value(QStringLiteral("phones")).toStringList();
    const auto oldPhones = a.phoneNumbers();
    for (const KContacts::PhoneNumber &p : oldPhones) {
        if (!wantedPhones.contains(p.number()))
            a.removePhoneNumber(p);
    }
    for (const QString &number : wantedPhones) {
        const bool known = std::any_of(oldPhones.cbegin(), oldPhones.cend(), [&number](const KContacts::PhoneNumber &p) {
            return p.number() == number;
        });
        if (!known)
            a.insertPhoneNumber(KContacts::PhoneNumber(number));
    }

    const QString address = f.value(QStringLiteral("address")).toString().trimmed();
    const auto oldAddresses = a.addresses();
    const QString oldAddress = oldAddresses.isEmpty() ? QString() : oldAddresses.constFirst().formatted(KContacts::AddressFormatStyle::Postal).trimmed();
    if (address != oldAddress) {
        for (const KContacts::Address &old : oldAddresses)
            a.removeAddress(old);
        if (!address.isEmpty()) {
            KContacts::Address postal(KContacts::Address::Home);
            postal.setLabel(address);
            postal.setStreet(address);
            a.insertAddress(postal);
        }
    }

    const QDateTime birthday = fromMs(f.value(QStringLiteral("birthdayMs")));
    if (birthday.isValid())
        a.setBirthday(birthday.date());
    else if (a.birthday().isValid())
        a.setBirthday(QDate());

    return QString::fromUtf8(converter.createVCard(a, KContacts::VCardConverter::v3_0));
}

QString newHref(const DavCollection &collection, const QString &extension)
{
    QString base = collection.href;
    if (!base.endsWith(QLatin1Char('/')))
        base += QLatin1Char('/');
    return QUrl(base).resolved(QUrl(QUuid::createUuid().toString(QUuid::WithoutBraces) + extension)).toString();
}

QString rawWithNewUid(const QString &raw, const QString &uid)
{
    QStringList lines = raw.split(QLatin1Char('\n'));
    QStringList result;
    bool skipping = false;
    bool replaced = false;
    for (QString line : std::as_const(lines)) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        // Fortsetzungszeile einer gefalteten Eigenschaft (RFC 5545, 3.1)
        if (line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) {
            if (!skipping)
                result << line;
            continue;
        }
        const qsizetype end = line.indexOf(QRegularExpression(QStringLiteral("[:;]")));
        const QString name = line.left(end).toUpper();
        skipping = false;
        if (name == QLatin1String("UID")) {
            result << QStringLiteral("UID:") + uid;
            replaced = true;
            skipping = true;
        } else if (name == QLatin1String("ORGANIZER") || name == QLatin1String("ATTENDEE")) {
            skipping = true;
        } else {
            result << line;
        }
    }
    if (!replaced) {
        // Die UID ist in vCards nicht Pflicht.
        for (qsizetype i = result.size() - 1; i >= 0; --i) {
            if (result.at(i).startsWith(QLatin1String("END:"), Qt::CaseInsensitive)) {
                result.insert(i, QStringLiteral("UID:") + uid);
                break;
            }
        }
    }
    return result.join(QLatin1String("\r\n"));
}

QString transferItem(qint64 itemId, qint64 targetCollectionId, bool move)
{
    const DavItem item = DavStore::item(itemId);
    const DavCollection source = DavStore::collection(item.collectionId);
    const DavCollection target = DavStore::collection(targetCollectionId);
    if (item.id == 0 || item.pending == DavItem::Delete || source.id == 0)
        return i18n("The entry no longer exists.");
    if (target.id == 0 || target.id == source.id)
        return i18n("Please choose a different destination.");
    if (target.readOnly)
        return i18n("\"%1\" is read-only.", target.name);
    if (move && source.readOnly)
        return i18n("\"%1\" is read-only.", source.name);
    const bool contact = item.kind == QLatin1String("contact");
    if (contact ? target.kind != QLatin1String("addressbook") : (item.kind == QLatin1String("event") ? !target.hasEvents : !target.hasTodos))
        return i18n("\"%1\" cannot hold this kind of entry.", target.name);

    // Dort wäre Löschen und Neuanlegen eine Absage mit neuer Einladung an alle Teilnehmer.
    if (move && item.kind == QLatin1String("event") && !eventFields(item.raw).value(QStringLiteral("attendees")).toStringList().isEmpty()
        && (DavStore::account(source.accountId).autoSchedule || DavStore::account(target.accountId).autoSchedule))
        return i18n("Events with guests cannot be moved on this server, because the guests would be notified.");

    DavItem copy = item;
    copy.id = 0;
    copy.etag.clear();
    copy.collectionId = target.id;
    copy.href = newHref(target, contact ? QStringLiteral(".vcf") : QStringLiteral(".ics"));
    copy.pending = DavItem::Create;
    if (!move) {
        copy.uid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        copy.raw = rawWithNewUid(item.raw, copy.uid);
    }
    if (!(contact ? deriveContactFields(copy) : deriveCalendarFields(copy)) || !DavStore::saveItem(copy))
        return i18n("The entry could not be saved.");

    if (move) {
        if (item.pending == DavItem::Create) {
            DavStore::removeItem(item.id); // war noch gar nicht auf dem Server
        } else {
            DavItem old = item;
            old.pending = DavItem::Delete;
            DavStore::saveItem(old);
        }
    }
    return {};
}

} // namespace Fjordpost::Pim

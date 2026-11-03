#include "mail/mime/messagebuilder.h"
#include "mail/mime/messageparser.h"
#include "pim/itip.h"
#include "pim/pimdata.h"
#include "storage/database.h"
#include "storage/davstore.h"

#include <QTemporaryDir>
#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace Fjordpost;

namespace {

const char *const recurring =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"
    "BEGIN:VEVENT\r\nUID:series\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260105T100000Z\r\nDTEND:20260105T110000Z\r\n"
    "SUMMARY:Jour fixe\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\nEXDATE:20260112T100000Z\r\nX-CUSTOM:bleibt\r\nEND:VEVENT\r\n"
    "BEGIN:VEVENT\r\nUID:series\r\nDTSTAMP:20260101T000000Z\r\nRECURRENCE-ID:20260119T100000Z\r\nDTSTART:20260120T150000Z\r\n"
    "DTEND:20260120T160000Z\r\nSUMMARY:Jour fixe verschoben\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

const char *const invitation =
    "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nMETHOD:REQUEST\r\n"
    "BEGIN:VEVENT\r\nUID:inv-1\r\nDTSTAMP:20260101T000000Z\r\nSEQUENCE:2\r\nDTSTART:20260310T090000Z\r\nDTEND:20260310T100000Z\r\n"
    "SUMMARY:Workshop\r\nLOCATION:Raum 1\r\nORGANIZER;CN=Anna:mailto:anna@example.org\r\n"
    "ATTENDEE;CN=Max;PARTSTAT=NEEDS-ACTION;RSVP=TRUE:mailto:max@example.org\r\n"
    "ATTENDEE;CN=Eva;PARTSTAT=ACCEPTED:mailto:eva@example.org\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";

QDateTime utc(int y, int m, int d, int h = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, 0), QTimeZone::utc());
}

} // namespace

class PimTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void recurringSeries()
    {
        DavItem item;
        item.id = 1;
        item.raw = QString::fromLatin1(recurring);
        QVERIFY(Pim::deriveCalendarFields(item));
        QCOMPARE(item.kind, QStringLiteral("event"));
        QCOMPARE(item.uid, QStringLiteral("series"));
        QVERIFY(item.rangeEnd > 0); // COUNT: die Serie endet

        const auto list = Pim::occurrences(item, utc(2026, 1, 1), utc(2026, 2, 28));
        // Vier Termine, einer per EXDATE gestrichen, einer verschoben
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).start.toUTC(), utc(2026, 1, 5, 10));
        QCOMPARE(list.at(1).start.toUTC(), utc(2026, 1, 20, 15));
        QCOMPARE(list.at(1).title, QStringLiteral("Jour fixe verschoben"));
        QCOMPARE(list.at(2).start.toUTC(), utc(2026, 1, 26, 10));
    }

    void deleteFromSeries()
    {
        const QString raw = QString::fromLatin1(recurring);
        const auto starts = [](const QString &changed) {
            DavItem item;
            item.raw = changed;
            QList<QDateTime> result;
            if (!Pim::deriveCalendarFields(item))
                return result;
            const auto list = Pim::occurrences(item, utc(2026, 1, 1), utc(2026, 2, 28));
            for (const auto &o : list)
                result << o.start.toUTC();
            return result;
        };
        // Der verschobene Termin wird mit seinem neuen Beginn angezeigt, gemeint ist seine RECURRENCE-ID.
        const QDateTime moved = utc(2026, 1, 20, 15).toLocalTime();
        QCOMPARE(Pim::recurrenceIdAt(raw, moved).toUTC(), utc(2026, 1, 19, 10));
        QCOMPARE(Pim::recurrenceIdAt(raw, utc(2026, 1, 26, 10)).toUTC(), utc(2026, 1, 26, 10));

        const QString withoutMoved = Pim::excludeOccurrence(raw, moved);
        QCOMPARE(starts(withoutMoved), (QList<QDateTime>{utc(2026, 1, 5, 10), utc(2026, 1, 26, 10)}));
        QVERIFY(!withoutMoved.contains(QLatin1String("RECURRENCE-ID")));
        QVERIFY(withoutMoved.contains(QRegularExpression(QStringLiteral("X-CUSTOM(;[^:\\r\\n]*)?:bleibt"))));

        const QString withoutLast = Pim::excludeOccurrence(raw, utc(2026, 1, 26, 10).toLocalTime());
        QCOMPARE(starts(withoutLast), (QList<QDateTime>{utc(2026, 1, 5, 10), utc(2026, 1, 20, 15)}));

        const QString ended = Pim::endSeriesBefore(raw, moved);
        QCOMPARE(starts(ended), (QList<QDateTime>{utc(2026, 1, 5, 10)}));
        QVERIFY(!ended.contains(QLatin1String("RECURRENCE-ID")));

        // Kein Serientermin: nichts zu tun
        QVERIFY(Pim::excludeOccurrence(QString::fromLatin1(invitation), utc(2026, 3, 10, 9)).isEmpty());

        // Absage nur für das eine Vorkommnis
        const QString cancel = QString::fromUtf8(Itip::buildCancel(raw, utc(2026, 1, 26, 10)));
        QVERIFY(cancel.contains(QLatin1String("METHOD:CANCEL")));
        QVERIFY(cancel.contains(QLatin1String("RECURRENCE-ID")));
        QVERIFY(!cancel.contains(QLatin1String("RRULE")));
    }

    void editKeepsUnknownProperties()
    {
        const QString raw = Pim::applyEventFields(QString::fromLatin1(recurring),
                                                  {{QStringLiteral("summary"), QStringLiteral("Neu")},
                                                   {QStringLiteral("startMs"), double(utc(2026, 1, 5, 10).toMSecsSinceEpoch())},
                                                   {QStringLiteral("endMs"), double(utc(2026, 1, 5, 11).toMSecsSinceEpoch())},
                                                   {QStringLiteral("repeat"), QStringLiteral("custom")}},
                                                  QString());
        QVERIFY(raw.contains(QLatin1String("SUMMARY:Neu")));
        QVERIFY(raw.contains(QRegularExpression(QStringLiteral("X-CUSTOM(;[^:\\r\\n]*)?:bleibt"))));
        QVERIFY(raw.contains(QLatin1String("RECURRENCE-ID")));
        QVERIFY(raw.contains(QLatin1String("FREQ=WEEKLY")));
    }

    void newEventRoundTrip()
    {
        const QDateTime start = utc(2026, 6, 1, 8);
        const QString raw = Pim::applyEventFields({},
                                                  {{QStringLiteral("summary"), QStringLiteral("Prüfung")},
                                                   {QStringLiteral("startMs"), double(start.toMSecsSinceEpoch())},
                                                   {QStringLiteral("endMs"), double(start.addSecs(1800).toMSecsSinceEpoch())},
                                                   {QStringLiteral("repeat"), QStringLiteral("weekly")},
                                                   {QStringLiteral("reminderMinutes"), 15}},
                                                  QStringLiteral("u1"));
        DavItem item;
        item.raw = raw;
        QVERIFY(Pim::deriveCalendarFields(item));
        QCOMPARE(item.uid, QStringLiteral("u1"));
        QCOMPARE(item.rangeEnd, qint64(0)); // endlos
        QVERIFY(item.hasAlarm);

        const QVariantMap fields = Pim::eventFields(raw);
        QCOMPARE(fields.value(QStringLiteral("summary")).toString(), QStringLiteral("Prüfung"));
        QCOMPARE(fields.value(QStringLiteral("repeat")).toString(), QStringLiteral("weekly"));
        QCOMPARE(fields.value(QStringLiteral("reminderMinutes")).toInt(), 15);

        const auto hits = Pim::alarmsBetween(item, start.addSecs(-1000), start.addSecs(-800));
        QCOMPARE(hits.size(), 1);
        QCOMPARE(hits.at(0).when.toUTC(), start);
    }

    void allDayEnd()
    {
        const QDateTime day(QDate(2026, 7, 1), QTime(0, 0));
        const QString raw = Pim::applyEventFields({},
                                                  {{QStringLiteral("summary"), QStringLiteral("Feiertag")},
                                                   {QStringLiteral("startMs"), double(day.toMSecsSinceEpoch())},
                                                   {QStringLiteral("endMs"), double(day.toMSecsSinceEpoch())},
                                                   {QStringLiteral("allDay"), true}},
                                                  QStringLiteral("a1"));
        DavItem item;
        item.raw = raw;
        QVERIFY(Pim::deriveCalendarFields(item));
        // Der Tag muss auch abends noch getroffen werden.
        QVERIFY(item.rangeEnd > QDateTime(QDate(2026, 7, 1), QTime(20, 0)).toMSecsSinceEpoch());
        QCOMPARE(Pim::occurrences(item, QDateTime(QDate(2026, 7, 1), QTime(0, 0)), QDateTime(QDate(2026, 7, 2), QTime(0, 0))).size(), 1);
    }

    void todoCycle()
    {
        const QDateTime due = utc(2026, 5, 1, 12);
        QString raw = Pim::applyTodoFields({},
                                           {{QStringLiteral("summary"), QStringLiteral("Bericht")},
                                            {QStringLiteral("dueMs"), double(due.toMSecsSinceEpoch())},
                                            {QStringLiteral("priority"), 1}},
                                           QStringLiteral("t1"));
        DavItem item;
        item.raw = raw;
        QVERIFY(Pim::deriveCalendarFields(item));
        QCOMPARE(item.kind, QStringLiteral("todo"));
        QCOMPARE(item.due, due.toMSecsSinceEpoch());
        QCOMPARE(item.priority, 1);
        QVERIFY(!item.completed);

        raw = Pim::applyTodoFields(raw,
                                   {{QStringLiteral("summary"), QStringLiteral("Bericht")},
                                    {QStringLiteral("dueMs"), double(due.toMSecsSinceEpoch())},
                                    {QStringLiteral("priority"), 1},
                                    {QStringLiteral("reminderMinutes"), 60}},
                                   {});
        QVERIFY(raw.contains(QLatin1String("RELATED=END")));
        QCOMPARE(Pim::todoFields(raw).value(QStringLiteral("reminderMinutes")).toInt(), 60);
        item.raw = raw;
        QVERIFY(Pim::deriveCalendarFields(item));
        QCOMPARE(Pim::alarmsBetween(item, due.addSecs(-3700), due.addSecs(-3500)).size(), 1);

        item.raw = Pim::setTodoCompleted(raw, true);
        QVERIFY(Pim::deriveCalendarFields(item));
        QVERIFY(item.completed);
        item.raw = Pim::setTodoCompleted(item.raw, false);
        QVERIFY(Pim::deriveCalendarFields(item));
        QVERIFY(!item.completed);
    }

    void contactRoundTrip()
    {
        QString raw = Pim::applyContactFields({},
                                              {{QStringLiteral("name"), QStringLiteral("Jörg Müller")},
                                               {QStringLiteral("organization"), QStringLiteral("Fjord AG")},
                                               {QStringLiteral("emails"), QStringList{QStringLiteral("Joerg@Example.org"), QStringLiteral("j@example.net")}},
                                               {QStringLiteral("phones"), QStringList{QStringLiteral("+49 40 1")}},
                                               {QStringLiteral("address"), QStringLiteral("Hafenstraße 1")}},
                                              QStringLiteral("c1"));
        DavItem item;
        item.raw = raw;
        QVERIFY(Pim::deriveContactFields(item));
        QCOMPARE(item.uid, QStringLiteral("c1"));
        QCOMPARE(item.title, QStringLiteral("Jörg Müller"));
        QCOMPARE(item.emails.size(), 2);

        QVariantMap fields = Pim::contactFields(raw);
        QCOMPARE(fields.value(QStringLiteral("phones")).toStringList(), QStringList{QStringLiteral("+49 40 1")});
        QVERIFY(fields.value(QStringLiteral("address")).toString().contains(QStringLiteral("Hafenstraße 1")));

        fields[QStringLiteral("emails")] = QStringList{QStringLiteral("j@example.net")};
        fields[QStringLiteral("phones")] = QStringList();
        raw = Pim::applyContactFields(raw, fields, {});
        const QVariantMap after = Pim::contactFields(raw);
        QCOMPARE(after.value(QStringLiteral("emails")).toStringList(), QStringList{QStringLiteral("j@example.net")});
        QVERIFY(after.value(QStringLiteral("phones")).toStringList().isEmpty());
    }

    void invitationParsing()
    {
        const Itip::Invitation inv = Itip::parse(invitation);
        QVERIFY(inv.valid);
        QCOMPARE(inv.method, QStringLiteral("REQUEST"));
        QCOMPARE(inv.uid, QStringLiteral("inv-1"));
        QCOMPARE(inv.sequence, 2);
        QCOMPARE(inv.organizer, QStringLiteral("anna@example.org"));
        QCOMPARE(inv.attendees.size(), 2);
    }

    void invitationReply()
    {
        const QByteArray reply = Itip::buildReply(invitation, QStringLiteral("MAX@example.org"), QStringLiteral("ACCEPTED"));
        QVERIFY(reply.contains("METHOD:REPLY"));
        QVERIFY(reply.contains("PARTSTAT=ACCEPTED"));
        QVERIFY(!reply.contains("eva@example.org")); // nur der eigene Teilnehmer

        const Itip::Invitation parsed = Itip::parse(reply);
        QCOMPARE(parsed.replyFrom, QStringLiteral("max@example.org"));
        QCOMPARE(parsed.replyStatus, QStringLiteral("ACCEPTED"));

        // Fremde Adresse: keine Antwort
        QVERIFY(Itip::buildReply(invitation, QStringLiteral("x@example.org"), QStringLiteral("ACCEPTED")).isEmpty());

        const QString stored = Itip::toCalendarObject(invitation, QStringLiteral("max@example.org"), QStringLiteral("TENTATIVE"));
        QVERIFY(!stored.contains(QLatin1String("METHOD:")));
        QCOMPARE(Itip::statusOf(stored, QStringLiteral("max@example.org")), QStringLiteral("TENTATIVE"));

        const QString updated = Itip::applyReply(stored, QStringLiteral("eva@example.org"), QStringLiteral("DECLINED"));
        QCOMPARE(Itip::statusOf(updated, QStringLiteral("eva@example.org")), QStringLiteral("DECLINED"));
    }

    void invitationInMail()
    {
        OutgoingMessage m;
        m.fromName = QStringLiteral("Anna");
        m.fromEmail = QStringLiteral("anna@example.org");
        m.to = {QStringLiteral("max@example.org")};
        m.subject = QStringLiteral("Einladung");
        m.plainText = QStringLiteral("Workshop");
        m.calendarData = invitation;
        m.calendarMethod = QStringLiteral("REQUEST");
        const QByteArray raw = MessageBuilder::build(m);
        QVERIFY(raw.contains("multipart/alternative"));
        QVERIFY(raw.contains("text/calendar"));
        QVERIFY(raw.contains("method=REQUEST") || raw.contains("method=\"REQUEST\""));

        const ParsedMessage parsed = MessageParser::parse(raw);
        QCOMPARE(parsed.plainText.trimmed(), QStringLiteral("Workshop"));
        QVERIFY(Itip::parse(parsed.calendarData).valid);
    }

    void storeQueries()
    {
        QTemporaryDir dir;
        QVERIFY(Database::initialize(dir.filePath(QStringLiteral("t.db"))));
        DavAccount a;
        a.name = QStringLiteral("x");
        a.url = QStringLiteral("https://x/");
        a.id = DavStore::saveAccount(a);
        QVERIFY(a.id > 0);
        DavCollection book;
        book.accountId = a.id;
        book.kind = QStringLiteral("addressbook");
        book.href = QStringLiteral("https://x/b/");
        book.id = DavStore::saveCollection(book);
        QVERIFY(book.id > 0);

        DavItem c;
        c.collectionId = book.id;
        c.href = QStringLiteral("https://x/b/1.vcf");
        c.raw = Pim::applyContactFields({},
                                        {{QStringLiteral("name"), QStringLiteral("Eva 100% Ernst")}, {QStringLiteral("emails"), QStringList{QStringLiteral("Eva@Example.org")}}},
                                        QStringLiteral("c1"));
        QVERIFY(Pim::deriveContactFields(c));
        QVERIFY(DavStore::saveItem(c));

        QCOMPARE(DavStore::contacts(QStringLiteral("ernst")).size(), 1);
        QCOMPARE(DavStore::contacts(QStringLiteral("100%")).size(), 1);
        QCOMPARE(DavStore::contacts(QStringLiteral("1%0")).size(), 0); // % ist kein Platzhalter
        QCOMPARE(DavStore::contacts({}).constFirst().emails, QStringList{QStringLiteral("eva@example.org")});
        QCOMPARE(DavStore::contactByEmail(QStringLiteral("EVA@example.org")).id, c.id);
        QCOMPARE(DavStore::completeAddress(QStringLiteral("ern"), 5), QStringList{QStringLiteral("Eva 100% Ernst <eva@example.org>")});
        QCOMPARE(DavStore::completeAddress(QStringLiteral("eva@"), 5).size(), 1);
        QCOMPARE(DavStore::completeAddress(QStringLiteral("xx"), 5).size(), 0);

        // Ausgeblendete Adressbücher verschwinden aus der Liste
        DavStore::setCollectionVisible(book.id, false);
        QCOMPARE(DavStore::contacts({}).size(), 0);

        // Entfernen des Kontos räumt alles ab
        QVERIFY(DavStore::removeAccount(a.id));
        QCOMPARE(DavStore::item(c.id).id, qint64(0));
    }
};

QTEST_GUILESS_MAIN(PimTest)
#include "pimtest.moc"

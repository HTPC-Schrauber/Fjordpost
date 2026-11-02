#include "pimcontroller.h"

#include "core/accountmanager.h"
#include "dav/davservice.h"
#include "mail/mailservice.h"
#include "mail/mime/messagebuilder.h"
#include "mail/mime/messageparser.h"
#include "models/contactmodel.h"
#include "pim/itip.h"
#include "pim/pimdata.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/davstore.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QLocale>
#include <QSqlQuery>
#include <QTimeZone>
#include <QUrl>
#include <QUuid>

namespace Fjordpost {

namespace {

QString newUid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString addressOnly(const QString &text)
{
    const qsizetype lt = text.lastIndexOf(QLatin1Char('<'));
    return (lt >= 0 ? text.mid(lt + 1).remove(QLatin1Char('>')) : text).trimmed();
}

bool sameAddresses(QStringList a, QStringList b)
{
    for (QString &s : a)
        s = addressOnly(s).toLower();
    for (QString &s : b)
        s = addressOnly(s).toLower();
    a.sort();
    b.sort();
    return a == b;
}

} // namespace

PimController::PimController(AccountManager *accounts, MailService *mail, DavService *dav, QObject *parent)
    : QObject(parent)
    , m_accounts(accounts)
    , m_mail(mail)
    , m_dav(dav)
    , m_contacts(new ContactModel(this))
{
    connect(dav, &DavService::dataChanged, this, [this] {
        m_contacts->reload();
        ++m_revision;
        Q_EMIT revisionChanged();
    });
    connect(dav, &DavService::errorOccurred, this, &PimController::notify);
    connect(dav, &DavService::conflict, this, [this](const QString &title) {
        Q_EMIT notify(i18n("\"%1\" was changed on the server; your change was discarded.", title));
    });
}

QObject *PimController::contactsObject() const
{
    return m_contacts;
}

bool PimController::hasDavAccounts() const
{
    return !DavStore::accounts().isEmpty();
}

void PimController::changed(qint64 collectionId)
{
    m_contacts->reload();
    ++m_revision;
    Q_EMIT revisionChanged();
    if (collectionId > 0)
        m_dav->flush(DavStore::collection(collectionId).accountId);
}

QVariantList PimController::davAccounts() const
{
    QVariantList result;
    const auto accounts = DavStore::accounts();
    for (const DavAccount &a : accounts) {
        result << QVariantMap{{QStringLiteral("id"), a.id},
                              {QStringLiteral("name"), a.name},
                              {QStringLiteral("url"), a.url},
                              {QStringLiteral("username"), a.username},
                              {QStringLiteral("mailAccountId"), a.mailAccountId},
                              {QStringLiteral("autoSchedule"), a.autoSchedule}};
    }
    return result;
}

void PimController::addDavAccount(const QString &address, const QString &username, const QString &password, qint64 mailAccountId)
{
    m_dav->addAccount(address, username, password, mailAccountId, [this](const QString &error) {
        if (error.isEmpty()) {
            ++m_revision;
            Q_EMIT revisionChanged();
        }
        Q_EMIT davAccountResult(error);
    });
}

void PimController::removeDavAccount(qint64 id)
{
    m_dav->removeAccount(id);
    m_contacts->reload();
    ++m_revision;
    Q_EMIT revisionChanged();
}

void PimController::setDavMailAccount(qint64 id, qint64 mailAccountId)
{
    DavAccount account = DavStore::account(id);
    if (account.id == 0)
        return;
    account.mailAccountId = mailAccountId;
    DavStore::saveAccount(account);
}

QVariantList PimController::collections(const QString &kind) const
{
    QVariantList result;
    const auto all = DavStore::collections(0, kind == QLatin1String("contacts") ? QStringLiteral("addressbook") : QStringLiteral("calendar"));
    for (const DavCollection &c : all) {
        if ((kind == QLatin1String("events") && !c.hasEvents) || (kind == QLatin1String("todos") && !c.hasTodos))
            continue;
        result << QVariantMap{{QStringLiteral("id"), c.id},
                              {QStringLiteral("name"), c.name},
                              {QStringLiteral("color"), DavStore::displayColor(c)},
                              {QStringLiteral("ownColor"), c.color}, // leer: Ersatzfarbe, nichts gewählt
                              {QStringLiteral("visible"), c.visible},
                              {QStringLiteral("readOnly"), c.readOnly}};
    }
    return result;
}

QString PimController::transferItem(qint64 itemId, qint64 targetCollectionId, bool move)
{
    const qint64 source = DavStore::item(itemId).collectionId;
    const QString error = Pim::transferItem(itemId, targetCollectionId, move);
    if (!error.isEmpty())
        return error;
    changed(targetCollectionId);
    const qint64 sourceAccount = DavStore::collection(source).accountId;
    if (move && sourceAccount != DavStore::collection(targetCollectionId).accountId)
        m_dav->flush(sourceAccount);
    return {};
}

void PimController::setCollectionVisible(qint64 id, bool visible)
{
    DavStore::setCollectionVisible(id, visible);
    changed(0);
}

QVariantList PimController::occurrences(double fromMs, double toMs) const
{
    QVariantList result;
    const QTimeZone zone = QTimeZone::systemTimeZone();
    const QDateTime from = QDateTime::fromMSecsSinceEpoch(qint64(fromMs), zone);
    const QDateTime to = QDateTime::fromMSecsSinceEpoch(qint64(toMs), zone);
    const auto items = DavStore::eventsInRange(qint64(fromMs), qint64(toMs));

    QList<Pim::Occurrence> all;
    QHash<qint64, QString> colors;
    for (const DavItem &item : items) {
        all += Pim::occurrences(item, from, to);
        if (!colors.contains(item.collectionId))
            colors.insert(item.collectionId, DavStore::displayColor(DavStore::collection(item.collectionId)));
    }
    QHash<qint64, qint64> collectionOf;
    for (const DavItem &item : items)
        collectionOf.insert(item.id, item.collectionId);
    std::sort(all.begin(), all.end(), [](const Pim::Occurrence &a, const Pim::Occurrence &b) {
        // Ganztägige Termine stehen über denen mit Uhrzeit.
        return a.start.date() != b.start.date() ? a.start < b.start : (a.allDay != b.allDay ? a.allDay : a.start < b.start);
    });

    const QLocale locale;
    for (const Pim::Occurrence &o : std::as_const(all)) {
        result << QVariantMap{{QStringLiteral("itemId"), o.itemId},
                              {QStringLiteral("title"), o.title.isEmpty() ? i18n("(No title)") : o.title},
                              {QStringLiteral("location"), o.location},
                              {QStringLiteral("startMs"), double(o.start.toMSecsSinceEpoch())},
                              {QStringLiteral("endMs"), double(o.end.toMSecsSinceEpoch())},
                              {QStringLiteral("allDay"), o.allDay},
                              {QStringLiteral("recurring"), o.recurring},
                              {QStringLiteral("color"), colors.value(collectionOf.value(o.itemId))},
                              {QStringLiteral("timeText"), o.allDay ? i18n("All day") : locale.toString(o.start.toLocalTime().time(), QLocale::ShortFormat)}};
    }
    return result;
}

void PimController::createCollection(qint64 accountId, const QString &kind, const QString &name, const QString &color)
{
    m_dav->createCollection(accountId, kind, name.trimmed(), color, [this](const QString &error) {
        Q_EMIT collectionResult(error);
    });
}

void PimController::updateCollection(qint64 id, const QString &name, const QString &color)
{
    m_dav->updateCollection(id, name.trimmed(), color, [this](const QString &error) {
        Q_EMIT collectionResult(error);
    });
}

void PimController::deleteCollection(qint64 id)
{
    m_dav->removeCollection(id, [this](const QString &error) {
        Q_EMIT collectionResult(error);
    });
}

QString PimController::writableError(const DavCollection &collection) const
{
    if (collection.id == 0)
        return i18n("Please choose a calendar or address book first.");
    if (collection.readOnly)
        return i18n("\"%1\" is read-only.", collection.name);
    return {};
}

QString PimController::storeNew(DavItem &item, const DavCollection &collection, const QString &extension)
{
    item.collectionId = collection.id;
    item.href = Pim::newHref(collection, extension);
    item.pending = DavItem::Create;
    return DavStore::saveItem(item) ? QString() : i18n("The entry could not be saved.");
}

QString PimController::organizerFor(const DavCollection &collection) const
{
    const Account mail = m_accounts->account(DavStore::account(collection.accountId).mailAccountId);
    if (!mail.isValid())
        return {};
    return mail.displayName.isEmpty() ? mail.email : QStringLiteral("%1 <%2>").arg(mail.displayName, mail.email);
}

QVariantMap PimController::event(qint64 itemId) const
{
    const DavItem item = DavStore::item(itemId);
    QVariantMap fields = Pim::eventFields(item.raw);
    fields[QStringLiteral("itemId")] = item.id;
    fields[QStringLiteral("collectionId")] = item.collectionId;
    fields[QStringLiteral("readOnly")] = DavStore::collection(item.collectionId).readOnly;
    return fields;
}

QString PimController::saveEvent(const QVariantMap &input)
{
    QVariantMap fields = input;
    const qint64 itemId = fields.value(QStringLiteral("itemId")).toLongLong();
    if (fields.value(QStringLiteral("summary")).toString().trimmed().isEmpty())
        return i18n("Please enter a title.");

    DavItem item = itemId > 0 ? DavStore::item(itemId) : DavItem();
    const DavCollection collection = DavStore::collection(itemId > 0 ? item.collectionId : fields.value(QStringLiteral("collectionId")).toLongLong());
    if (const QString error = writableError(collection); !error.isEmpty())
        return error;

    const QVariantMap before = itemId > 0 ? Pim::eventFields(item.raw) : QVariantMap();
    const QStringList attendees = fields.value(QStringLiteral("attendees")).toStringList();
    const QString organizer = before.value(QStringLiteral("organizer")).toString();
    if (!attendees.isEmpty() && organizer.isEmpty()) {
        const DavAccount dav = DavStore::account(collection.accountId);
        QString own = organizerFor(collection);
        if (own.isEmpty() && dav.autoSchedule && dav.username.contains(QLatin1Char('@')))
            own = dav.username;
        if (own.isEmpty())
            return i18n("To invite people, assign an email account to this calendar in Settings.");
        fields[QStringLiteral("organizer")] = own;
    }

    if (itemId == 0)
        item.uid = newUid();
    item.raw = Pim::applyEventFields(item.raw, fields, item.uid);
    if (item.raw.isEmpty() || !Pim::deriveCalendarFields(item))
        return i18n("The entry could not be saved.");

    if (itemId == 0) {
        if (const QString error = storeNew(item, collection, QStringLiteral(".ics")); !error.isEmpty())
            return error;
    } else {
        item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
        if (!DavStore::saveItem(item))
            return i18n("The entry could not be saved.");
    }

    // Eigene Termine: Teilnehmer nur bei inhaltlichen Änderungen benachrichtigen
    QString warning;
    const QVariantMap after = Pim::eventFields(item.raw);
    const QString ownOrganizer = addressOnly(after.value(QStringLiteral("organizer")).toString());
    const Account mail = m_accounts->account(DavStore::account(collection.accountId).mailAccountId);
    const bool ours = mail.isValid() && ownOrganizer.compare(mail.email, Qt::CaseInsensitive) == 0;
    bool relevant = !sameAddresses(before.value(QStringLiteral("attendees")).toStringList(), attendees);
    for (const auto &key : {QStringLiteral("summary"), QStringLiteral("location"), QStringLiteral("startMs"), QStringLiteral("endMs"), QStringLiteral("allDay")})
        relevant = relevant || before.value(key) != after.value(key);
    if (ours && relevant && !attendees.isEmpty())
        warning = sendSchedule(collection, Itip::buildRequest(item.raw), QStringLiteral("REQUEST"), item.title, attendees);

    changed(collection.id);
    // Der Termin ist gespeichert; das Formular darf schließen.
    if (!warning.isEmpty())
        Q_EMIT notify(warning);
    return {};
}

QString PimController::sendSchedule(const DavCollection &collection, const QByteArray &ics, const QString &method, const QString &summary, const QStringList &recipients)
{
    const DavAccount dav = DavStore::account(collection.accountId);
    if (dav.autoSchedule)
        return {}; // Der Server benachrichtigt die Teilnehmer selbst.
    const Account mail = m_accounts->account(dav.mailAccountId);
    if (!mail.isValid())
        return i18n("Saved, but no invitation was sent: no email account is assigned to this calendar.");
    if (ics.isEmpty() || recipients.isEmpty())
        return {};

    OutgoingMessage message;
    message.fromName = mail.displayName;
    message.fromEmail = mail.email;
    message.to = recipients;
    message.subject = summary;
    message.plainText = Itip::plainTextSummary(Itip::parse(ics));
    message.calendarData = ics;
    message.calendarMethod = method;
    return m_mail->queueMessage(mail.id, message);
}

QString PimController::removeItem(qint64 itemId)
{
    DavItem item = DavStore::item(itemId);
    if (item.id == 0)
        return {};
    const DavCollection collection = DavStore::collection(item.collectionId);
    if (const QString error = writableError(collection); !error.isEmpty())
        return error;
    if (item.pending == DavItem::Create) {
        DavStore::removeItem(item.id); // nie beim Server angekommen
    } else {
        item.pending = DavItem::Delete;
        DavStore::saveItem(item);
    }
    changed(collection.id);
    return {};
}

QString PimController::deleteEvent(qint64 itemId)
{
    const DavItem item = DavStore::item(itemId);
    const DavCollection collection = DavStore::collection(item.collectionId);
    const QVariantMap fields = Pim::eventFields(item.raw);
    const QStringList attendees = fields.value(QStringLiteral("attendees")).toStringList();
    const Account mail = m_accounts->account(DavStore::account(collection.accountId).mailAccountId);
    const bool ours = mail.isValid() && addressOnly(fields.value(QStringLiteral("organizer")).toString()).compare(mail.email, Qt::CaseInsensitive) == 0;

    QByteArray cancel;
    if (ours && !attendees.isEmpty())
        cancel = Itip::buildCancel(item.raw);
    const QString title = item.title;
    const QString error = removeItem(itemId);
    if (error.isEmpty() && !cancel.isEmpty())
        return sendSchedule(collection, cancel, QStringLiteral("CANCEL"), i18n("Cancelled: %1", title), attendees);
    return error;
}

QVariantList PimController::tasks(qint64 collectionId, bool includeCompleted) const
{
    QVariantList result;
    const auto items = DavStore::todos(collectionId, includeCompleted);
    const QLocale locale;
    const QDate today = QDate::currentDate();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QHash<qint64, QVariantMap> info;
    for (const DavItem &i : items) {
        if (!info.contains(i.collectionId)) {
            const DavCollection c = DavStore::collection(i.collectionId);
            info.insert(i.collectionId, {{QStringLiteral("name"), c.name}, {QStringLiteral("color"), DavStore::displayColor(c)}});
        }
        QString dueText;
        if (i.due > 0) {
            const QDate date = QDateTime::fromMSecsSinceEpoch(i.due).date();
            dueText = date == today ? i18n("Today") : date == today.addDays(1) ? i18n("Tomorrow") : locale.toString(date, QLocale::ShortFormat);
        }
        result << QVariantMap{{QStringLiteral("itemId"), i.id},
                              {QStringLiteral("title"), i.title.isEmpty() ? i18n("(No title)") : i.title},
                              {QStringLiteral("dueMs"), double(i.due)},
                              {QStringLiteral("dueText"), dueText},
                              {QStringLiteral("overdue"), i.due > 0 && i.due < now && !i.completed},
                              {QStringLiteral("completed"), i.completed},
                              {QStringLiteral("priority"), i.priority},
                              {QStringLiteral("listName"), info.value(i.collectionId).value(QStringLiteral("name"))},
                              {QStringLiteral("color"), info.value(i.collectionId).value(QStringLiteral("color"))}};
    }
    return result;
}

QVariantMap PimController::task(qint64 itemId) const
{
    const DavItem item = DavStore::item(itemId);
    QVariantMap fields = Pim::todoFields(item.raw);
    fields[QStringLiteral("itemId")] = item.id;
    fields[QStringLiteral("collectionId")] = item.collectionId;
    return fields;
}

QString PimController::saveTask(const QVariantMap &fields)
{
    const qint64 itemId = fields.value(QStringLiteral("itemId")).toLongLong();
    if (fields.value(QStringLiteral("summary")).toString().trimmed().isEmpty())
        return i18n("Please enter a title.");
    DavItem item = itemId > 0 ? DavStore::item(itemId) : DavItem();
    const DavCollection collection = DavStore::collection(itemId > 0 ? item.collectionId : fields.value(QStringLiteral("collectionId")).toLongLong());
    if (const QString error = writableError(collection); !error.isEmpty())
        return error;

    if (itemId == 0)
        item.uid = newUid();
    item.raw = Pim::applyTodoFields(item.raw, fields, item.uid);
    if (item.raw.isEmpty() || !Pim::deriveCalendarFields(item))
        return i18n("The entry could not be saved.");
    if (itemId == 0) {
        if (const QString error = storeNew(item, collection, QStringLiteral(".ics")); !error.isEmpty())
            return error;
    } else {
        item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
        if (!DavStore::saveItem(item))
            return i18n("The entry could not be saved.");
    }
    changed(collection.id);
    return {};
}

QString PimController::setTaskCompleted(qint64 itemId, bool completed)
{
    DavItem item = DavStore::item(itemId);
    const DavCollection collection = DavStore::collection(item.collectionId);
    if (const QString error = writableError(collection); !error.isEmpty())
        return error;
    item.raw = Pim::setTodoCompleted(item.raw, completed);
    if (item.raw.isEmpty() || !Pim::deriveCalendarFields(item))
        return i18n("The entry could not be saved.");
    item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
    DavStore::saveItem(item);
    changed(collection.id);
    return {};
}

QString PimController::deleteTask(qint64 itemId)
{
    return removeItem(itemId);
}

QVariantMap PimController::contact(qint64 itemId) const
{
    const DavItem item = DavStore::item(itemId);
    if (item.id == 0)
        return {};
    const DavCollection book = DavStore::collection(item.collectionId);
    QVariantMap fields = Pim::contactFields(item.raw);
    fields[QStringLiteral("itemId")] = item.id;
    fields[QStringLiteral("collectionId")] = item.collectionId;
    fields[QStringLiteral("readOnly")] = book.readOnly;
    fields[QStringLiteral("bookName")] = book.name;
    fields[QStringLiteral("bookColor")] = DavStore::displayColor(book);
    return fields;
}

QVariantMap PimController::contactByEmail(const QString &email) const
{
    const DavItem item = DavStore::contactByEmail(email);
    return item.id > 0 ? contact(item.id) : QVariantMap();
}

QString PimController::saveContact(const QVariantMap &fields)
{
    const qint64 itemId = fields.value(QStringLiteral("itemId")).toLongLong();
    if (fields.value(QStringLiteral("name")).toString().trimmed().isEmpty())
        return i18n("Please enter a name.");
    DavItem item = itemId > 0 ? DavStore::item(itemId) : DavItem();
    const DavCollection collection = DavStore::collection(itemId > 0 ? item.collectionId : fields.value(QStringLiteral("collectionId")).toLongLong());
    if (const QString error = writableError(collection); !error.isEmpty())
        return error;

    if (itemId == 0)
        item.uid = newUid();
    item.raw = Pim::applyContactFields(item.raw, fields, item.uid);
    if (item.raw.isEmpty() || !Pim::deriveContactFields(item))
        return i18n("The entry could not be saved.");
    if (itemId == 0) {
        if (const QString error = storeNew(item, collection, QStringLiteral(".vcf")); !error.isEmpty())
            return error;
    } else {
        item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
        if (!DavStore::saveItem(item))
            return i18n("The entry could not be saved.");
    }
    changed(collection.id);
    return {};
}

QString PimController::deleteContact(qint64 itemId)
{
    return removeItem(itemId);
}

QStringList PimController::completeAddress(const QString &text) const
{
    return DavStore::completeAddress(text.trimmed(), 6);
}

QByteArray PimController::calendarPart(qint64 messageId, qint64 *accountId, QString *sender) const
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT blob_path, account_id, body_fetched, from_email FROM messages WHERE id = ?"));
    q.addBindValue(messageId);
    q.exec();
    if (!q.next() || !q.value(2).toBool() || q.value(0).toString().isEmpty())
        return {};
    if (accountId)
        *accountId = q.value(1).toLongLong();
    if (sender)
        *sender = q.value(3).toString();
    return MessageParser::parse(BlobStore::load(q.value(0).toString())).calendarData;
}

// Antworten und Absagen dürfen den Kalender nur ändern, wenn sie vom Teilnehmer
// bzw. Organisator stammen; sonst könnte jeder per Mail Termine entfernen.
bool PimController::mayChange(const Itip::Invitation &invitation, const DavItem &item, const QString &sender) const
{
    if (DavStore::collection(item.collectionId).readOnly)
        return false;
    if (invitation.method == QLatin1String("REPLY"))
        return sender.compare(invitation.replyFrom, Qt::CaseInsensitive) == 0;
    if (invitation.method == QLatin1String("CANCEL")) {
        const QString organizer = addressOnly(Pim::eventFields(item.raw).value(QStringLiteral("organizer")).toString());
        return sender.compare(organizer, Qt::CaseInsensitive) == 0;
    }
    return true;
}

QVariantMap PimController::invitation(qint64 messageId) const
{
    qint64 accountId = 0;
    const QByteArray ics = calendarPart(messageId, &accountId);
    if (ics.isEmpty())
        return {};
    const Itip::Invitation inv = Itip::parse(ics);
    if (!inv.valid)
        return {};

    const QLocale locale;
    QString when = inv.allDay ? locale.toString(inv.start.date(), QLocale::LongFormat)
                              : locale.toString(inv.start.toLocalTime(), QLocale::ShortFormat) + QStringLiteral(" – ")
            + (inv.end.toLocalTime().date() == inv.start.toLocalTime().date() ? locale.toString(inv.end.toLocalTime().time(), QLocale::ShortFormat)
                                                                              : locale.toString(inv.end.toLocalTime(), QLocale::ShortFormat));

    const DavItem existing = DavStore::itemByUid(inv.uid);
    QString sender;
    calendarPart(messageId, nullptr, &sender);
    const QString ownEmail = m_accounts->account(accountId).email;
    QString ownStatus;
    if (existing.id > 0)
        ownStatus = Itip::statusOf(existing.raw, ownEmail);
    return {{QStringLiteral("method"), inv.method},
            {QStringLiteral("summary"), inv.summary.isEmpty() ? i18n("(No title)") : inv.summary},
            {QStringLiteral("whenText"), when},
            {QStringLiteral("location"), inv.location},
            {QStringLiteral("organizer"), inv.organizerName.isEmpty() ? inv.organizer : inv.organizerName},
            {QStringLiteral("attendees"), inv.attendees},
            {QStringLiteral("replyFrom"), inv.replyFrom},
            {QStringLiteral("replyStatus"), inv.replyStatus},
            {QStringLiteral("inCalendar"), existing.id > 0 && mayChange(inv, existing, sender)},
            {QStringLiteral("ownStatus"), ownStatus},
            {QStringLiteral("canSchedule"), !DavStore::collections(0, QStringLiteral("calendar")).isEmpty()}};
}

QString PimController::respondToInvitation(qint64 messageId, const QString &status)
{
    qint64 accountId = 0;
    const QByteArray ics = calendarPart(messageId, &accountId);
    const Itip::Invitation inv = Itip::parse(ics);
    if (!inv.valid)
        return i18n("The invitation could not be read.");
    const Account mail = m_accounts->account(accountId);

    DavItem item = DavStore::itemByUid(inv.uid);
    DavCollection collection = item.id > 0 ? DavStore::collection(item.collectionId) : DavCollection();
    if (item.id > 0) {
        if (const QString error = writableError(collection); !error.isEmpty())
            return error;
    }
    if (item.id == 0 && status != QLatin1String("DECLINED")) {
        const auto calendars = DavStore::collections(0, QStringLiteral("calendar"));
        for (const DavCollection &c : calendars) {
            if (c.hasEvents && !c.readOnly) {
                collection = c;
                break;
            }
        }
        if (collection.id == 0)
            return i18n("Connect a calendar first to answer invitations.");
    }

    if (status == QLatin1String("DECLINED")) {
        if (item.id > 0) {
            if (const QString error = removeItem(item.id); !error.isEmpty())
                return error;
        }
    } else {
        item.raw = Itip::toCalendarObject(ics, mail.email, status);
        if (item.raw.isEmpty() || !Pim::deriveCalendarFields(item))
            return i18n("The invitation could not be read.");
        if (item.id == 0) {
            if (const QString error = storeNew(item, collection, QStringLiteral(".ics")); !error.isEmpty())
                return error;
        } else {
            item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
            DavStore::saveItem(item);
        }
        changed(collection.id);
    }

    // Beim Absagen ohne vorhandenen Kalendereintrag ist die Mail der einzige Weg
    // zum Organisator; mit Server-Scheduling übernimmt der die Antwort.
    if (collection.id > 0 && DavStore::account(collection.accountId).autoSchedule)
        return {};
    const QByteArray reply = Itip::buildReply(ics, mail.email, status);
    if (reply.isEmpty())
        return i18n("Your address is not on the guest list, so no reply was sent.");

    OutgoingMessage message;
    message.fromName = mail.displayName;
    message.fromEmail = mail.email;
    message.to = {inv.organizer};
    message.subject = (status == QLatin1String("ACCEPTED")    ? i18n("Accepted: %1", inv.summary)
                           : status == QLatin1String("DECLINED") ? i18n("Declined: %1", inv.summary)
                                                                 : i18n("Tentative: %1", inv.summary));
    message.plainText = Itip::plainTextSummary(inv);
    message.calendarData = reply;
    message.calendarMethod = QStringLiteral("REPLY");
    return m_mail->queueMessage(mail.id, message);
}

QString PimController::applyInvitation(qint64 messageId)
{
    QString sender;
    const QByteArray ics = calendarPart(messageId, nullptr, &sender);
    const Itip::Invitation inv = Itip::parse(ics);
    if (!inv.valid)
        return i18n("The invitation could not be read.");
    DavItem item = DavStore::itemByUid(inv.uid);
    if (item.id == 0)
        return i18n("This event is not in your calendar.");
    if (!mayChange(inv, item, sender))
        return i18n("This message does not come from the organizer or guest of the event.");

    if (inv.method == QLatin1String("CANCEL"))
        return removeItem(item.id);
    if (inv.method != QLatin1String("REPLY"))
        return {};
    const QString raw = Itip::applyReply(item.raw, inv.replyFrom, inv.replyStatus);
    if (raw.isEmpty())
        return i18n("This person is not on the guest list.");
    item.raw = raw;
    if (!Pim::deriveCalendarFields(item))
        return i18n("The entry could not be saved.");
    item.pending = item.pending == DavItem::Create ? DavItem::Create : DavItem::Modify;
    DavStore::saveItem(item);
    changed(item.collectionId);
    return {};
}

} // namespace Fjordpost

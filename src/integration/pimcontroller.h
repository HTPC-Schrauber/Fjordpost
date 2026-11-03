#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace Fjordpost {

class AccountManager;
class ContactModel;
class DavService;
class MailService;
struct DavCollection;
struct DavItem;
namespace Itip {
struct Invitation;
}

// QML-Fassade für Kalender, Aufgaben, Kontakte und Einladungen (App.pim).
// Zeiten sind Millisekunden seit 1970 (JavaScript-Date). Aktionen liefern eine
// Fehlermeldung oder einen leeren String. Änderungen wirken sofort lokal und
// gehen über dav_items.pending zum Server.
class PimController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *contacts READ contactsObject CONSTANT)
    // Steigt mit jeder Datenänderung; QML-Bindungen hängen sich daran.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool hasDavAccounts READ hasDavAccounts NOTIFY revisionChanged)

public:
    PimController(AccountManager *accounts, MailService *mail, DavService *dav, QObject *parent = nullptr);

    QObject *contactsObject() const;
    int revision() const { return m_revision; }
    bool hasDavAccounts() const;

    // Konten
    Q_INVOKABLE QVariantList davAccounts() const;
    // Ergebnis kommt über davAccountResult
    Q_INVOKABLE void addDavAccount(const QString &address, const QString &username, const QString &password, qint64 mailAccountId);
    Q_INVOKABLE void removeDavAccount(qint64 id);
    Q_INVOKABLE void setDavMailAccount(qint64 id, qint64 mailAccountId);

    // Sammlungen; kind: "events", "todos", "contacts"
    Q_INVOKABLE QVariantList collections(const QString &kind) const;
    Q_INVOKABLE void setCollectionVisible(qint64 id, bool visible);
    // Termin, Aufgabe oder Kontakt verschieben (move) oder kopieren; Fehlermeldung oder leerer String
    Q_INVOKABLE QString transferItem(qint64 itemId, qint64 targetCollectionId, bool move);
    // Ergebnis kommt über collectionResult
    Q_INVOKABLE void createCollection(qint64 accountId, const QString &kind, const QString &name, const QString &color);
    Q_INVOKABLE void updateCollection(qint64 id, const QString &name, const QString &color);
    Q_INVOKABLE void deleteCollection(qint64 id);

    // Termine
    Q_INVOKABLE QVariantList occurrences(double fromMs, double toMs) const;
    Q_INVOKABLE QVariantMap event(qint64 itemId) const;
    Q_INVOKABLE QString saveEvent(const QVariantMap &fields);
    // scope bei Serien: "all", "this" oder "following" (ab dem Vorkommnis bei occurrenceMs)
    Q_INVOKABLE QString deleteEvent(qint64 itemId, const QString &scope = QStringLiteral("all"), double occurrenceMs = 0);

    // Aufgaben
    Q_INVOKABLE QVariantList tasks(qint64 collectionId, bool includeCompleted) const;
    Q_INVOKABLE QVariantMap task(qint64 itemId) const;
    Q_INVOKABLE QString saveTask(const QVariantMap &fields);
    Q_INVOKABLE QString setTaskCompleted(qint64 itemId, bool completed);
    Q_INVOKABLE QString deleteTask(qint64 itemId);

    // Kontakte
    Q_INVOKABLE QVariantMap contact(qint64 itemId) const;
    Q_INVOKABLE QVariantMap contactByEmail(const QString &email) const;
    Q_INVOKABLE QString saveContact(const QVariantMap &fields);
    Q_INVOKABLE QString deleteContact(qint64 itemId);
    Q_INVOKABLE QStringList completeAddress(const QString &text) const;

    // Einladungen in Mails: leere Map, wenn die Nachricht keine enthält
    Q_INVOKABLE QVariantMap invitation(qint64 messageId) const;
    // status: "ACCEPTED", "TENTATIVE", "DECLINED"
    Q_INVOKABLE QString respondToInvitation(qint64 messageId, const QString &status);
    // Antwort eines Teilnehmers eintragen bzw. abgesagten Termin entfernen
    Q_INVOKABLE QString applyInvitation(qint64 messageId);

Q_SIGNALS:
    void revisionChanged();
    void notify(const QString &message);
    void davAccountResult(const QString &error);
    void collectionResult(const QString &error);

private:
    void changed(qint64 collectionId);
    QByteArray calendarPart(qint64 messageId, qint64 *accountId, QString *sender = nullptr) const;
    bool mayChange(const Itip::Invitation &invitation, const DavItem &item, const QString &sender) const;
    QString storeNew(DavItem &item, const DavCollection &collection, const QString &extension);
    QString writableError(const DavCollection &collection) const;
    QString removeItem(qint64 itemId);
    // Einladung per Mail, wenn der Server das nicht selbst übernimmt
    QString sendSchedule(const DavCollection &collection, const QByteArray &ics, const QString &method, const QString &summary, const QStringList &recipients);
    QString organizerFor(const DavCollection &collection) const;

    AccountManager *m_accounts;
    MailService *m_mail;
    DavService *m_dav;
    ContactModel *m_contacts;
    int m_revision = 0;
};

} // namespace Fjordpost

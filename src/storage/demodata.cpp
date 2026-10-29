#include "demodata.h"

#include "core/accountmanager.h"
#include "storage/database.h"
#include "storage/messageflags.h"

#include <QDateTime>
#include <QSqlQuery>

namespace Fjordpost {

namespace {

struct DemoMail {
    const char *fromName;
    const char *fromEmail;
    const char *subject;
    const char *body;
};

const DemoMail demoMails[] = {
    {"Anna Becker", "anna.becker@example.org", "Treffen am Donnerstag",
     "Hallo,\n\npasst Dir Donnerstag um 14 Uhr für unser Projekttreffen? Ich würde den Raum im zweiten Stock reservieren.\n\nViele Grüße\nAnna"},
    {"GitHub", "noreply@github.com", "[fjordpost] Neue Pull-Request: IMAP IDLE-Unterstützung",
     "Eine neue Pull-Request wurde eröffnet.\n\nÄnderungen: IDLE-Unterstützung für sofortige Benachrichtigungen bei neuen Mails."},
    {"Deutsche Bahn", "service@bahn.example", "Ihre Buchungsbestätigung",
     "Vielen Dank für Ihre Buchung.\n\nHamburg Hbf → München Hbf\nAbfahrt 08:04, Gleis 13\n\nGute Reise!"},
    {"Thomas Wagner", "t.wagner@example.com", "Re: Angebot Webseite",
     "Hi,\n\ndanke für das Angebot. Wir würden gern mit Variante B starten. Kannst Du mir noch einen Zeitplan schicken?\n\nGruß Thomas"},
    {"KDE Community", "announce@kde.example", "Plasma 6 Update veröffentlicht",
     "Die KDE-Community freut sich, ein neues Plasma-Update ankündigen zu können. Es enthält zahlreiche Fehlerkorrekturen."},
    {"Lisa Hoffmann", "lisa@hoffmann.example", "Fotos vom Wochenende 📷",
     "Hey!\n\nHier sind die Fotos vom Wochenende. War richtig schön mit Euch.\n\nLiebe Grüße\nLisa"},
    {"Hosting GmbH", "billing@hosting.example", "Rechnung Oktober",
     "Sehr geehrte Damen und Herren,\n\nanbei erhalten Sie Ihre Rechnung für den Monat Oktober.\n\nMit freundlichen Grüßen\nIhre Hosting GmbH"},
    {"Markus Klein", "markus.klein@example.net", "Kurze Frage zum Server",
     "Moin,\n\nweißt Du, ob der Mailserver noch POP3 anbietet oder nur noch IMAP?\n\nMarkus"},
};

qint64 insertFolder(qint64 accountId, const QString &path, const QString &name, const QString &role, int order)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("INSERT INTO folders (account_id, path, name, role, sort_order) VALUES (?, ?, ?, ?, ?)"));
    q.addBindValue(accountId);
    q.addBindValue(path);
    q.addBindValue(name);
    q.addBindValue(role);
    q.addBindValue(order);
    q.exec();
    return q.lastInsertId().toLongLong();
}

void insertMail(qint64 accountId, qint64 folderId, const DemoMail &m, const QDateTime &date, int flags)
{
    const QString body = QString::fromUtf8(m.body);
    QString snippet = body.simplified();
    snippet.truncate(140);

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "INSERT INTO messages (account_id, folder_id, subject, from_name, from_email, to_list, date, flags, snippet, body_text) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(accountId);
    q.addBindValue(folderId);
    q.addBindValue(QString::fromUtf8(m.subject));
    q.addBindValue(QString::fromUtf8(m.fromName));
    q.addBindValue(QString::fromUtf8(m.fromEmail));
    q.addBindValue(QStringLiteral("ich@example.de"));
    q.addBindValue(date.toSecsSinceEpoch());
    q.addBindValue(flags);
    q.addBindValue(snippet);
    q.addBindValue(body);
    q.exec();
}

void seedAccount(AccountManager &accounts, const QString &name, const QString &email, const QColor &color, AccountType type, int offset)
{
    Account a;
    a.name = name;
    a.displayName = QStringLiteral("Max Mustermann");
    a.email = email;
    a.color = color;
    a.type = type;
    a.incomingHost = QStringLiteral("mail.example.de");
    a.outgoingHost = QStringLiteral("mail.example.de");
    a.username = email;
    a.sortOrder = offset;
    a = accounts.save(a);

    const qint64 inbox = insertFolder(a.id, QStringLiteral("INBOX"), QStringLiteral("Posteingang"), QLatin1String(FolderRole::Inbox), 0);
    insertFolder(a.id, QStringLiteral("Drafts"), QStringLiteral("Entwürfe"), QLatin1String(FolderRole::Drafts), 1);
    const qint64 sent = insertFolder(a.id, QStringLiteral("Sent"), QStringLiteral("Gesendet"), QLatin1String(FolderRole::Sent), 2);
    insertFolder(a.id, QStringLiteral("Archive"), QStringLiteral("Archiv"), QLatin1String(FolderRole::Archive), 3);
    insertFolder(a.id, QStringLiteral("Junk"), QStringLiteral("Spam"), QLatin1String(FolderRole::Junk), 4);
    insertFolder(a.id, QStringLiteral("Trash"), QStringLiteral("Papierkorb"), QLatin1String(FolderRole::Trash), 5);
    insertFolder(a.id, QStringLiteral("Projekte"), QStringLiteral("Projekte"), QString(), 6);

    const QDateTime now = QDateTime::currentDateTime();
    const int count = int(std::size(demoMails));
    for (int i = 0; i < count; ++i) {
        const DemoMail &m = demoMails[(i + offset) % count];
        const QDateTime date = now.addSecs(-qint64(i) * 3600 * (3 + offset * 5) - offset * 1800);
        int flags = i < 3 ? 0 : MessageFlag::Seen;
        if (i == 1 + offset)
            flags |= MessageFlag::Flagged;
        insertMail(a.id, inbox, m, date, flags);
    }
    insertMail(a.id, sent, demoMails[3], now.addDays(-2), MessageFlag::Seen);
}

} // namespace

void seedDemoData(AccountManager &accounts)
{
    if (!accounts.accounts().isEmpty())
        return;
    QSqlDatabase db = Database::connection();
    db.transaction();
    seedAccount(accounts, QStringLiteral("Privat"), QStringLiteral("max@example.de"), QColor(QStringLiteral("#3daee9")), AccountType::Imap, 0);
    seedAccount(accounts, QStringLiteral("Arbeit"), QStringLiteral("max.mustermann@firma.example"), QColor(QStringLiteral("#e93d58")), AccountType::Imap, 1);
    seedAccount(accounts, QStringLiteral("Alt-Konto"), QStringLiteral("max@altmail.example"), QColor(QStringLiteral("#27ae60")), AccountType::Pop3, 2);
    db.commit();
}

} // namespace Fjordpost

#include "demodata.h"

#include "core/accountmanager.h"
#include "storage/database.h"
#include "storage/labelstore.h"
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

// Liefert die ID; ohne thread wird die Nachricht ihr eigener Thread.
qint64 insertMail(qint64 accountId, qint64 folderId, const DemoMail &m, const QDateTime &date, int flags, const QString &messageId = {},
                  const QString &inReplyTo = {}, qint64 thread = 0)
{
    const QString body = QString::fromUtf8(m.body);
    QString snippet = body.simplified();
    snippet.truncate(140);

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "INSERT INTO messages (account_id, folder_id, subject, from_name, from_email, to_list, date, flags, snippet, body_text, "
        "message_id, in_reply_to) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
    q.addBindValue(messageId.isNull() ? QStringLiteral("") : messageId);
    q.addBindValue(inReplyTo.isNull() ? QStringLiteral("") : inReplyTo);
    q.exec();
    const qint64 id = q.lastInsertId().toLongLong();

    QSqlQuery t(Database::connection());
    t.prepare(QStringLiteral("UPDATE messages SET thread_id = ? WHERE id = ?"));
    t.addBindValue(thread > 0 ? thread : id);
    t.addBindValue(id);
    t.exec();
    return id;
}

// Längere Abstimmung im Wechsel (zum Testen des zusammengefassten Verlaufs)
void insertLongConversation(qint64 accountId, qint64 inbox, qint64 sent, const QDateTime &now)
{
    const char *const texts[] = {
        "Hallo Max,\n\nkönnen wir das Release auf Mittwoch legen?",
        "Mittwoch passt, ich bereite die Notizen vor.",
        "Danke! Brauchst Du noch die Übersetzungen?",
        "Ja, bis Dienstag wäre super.",
        "Die Übersetzungen liegen im Repository.",
        "Perfekt, dann taggen wir am Mittwoch um 10 Uhr.",
        "Release ist raus – danke für die Hilfe!",
    };
    qint64 thread = 0;
    QString previous;
    const int count = int(std::size(texts));
    for (int i = 0; i < count; ++i) {
        const bool mine = i % 2 == 1;
        const DemoMail m{mine ? "Max Mustermann" : "Sabine Roth", mine ? "max.mustermann@firma.example" : "s.roth@firma.example",
                         i == 0 ? "Release-Termin" : "Re: Release-Termin", texts[i]};
        const QString id = QStringLiteral("<release-%1@firma.example>").arg(i);
        const qint64 inserted = insertMail(accountId, mine ? sent : inbox, m, now.addSecs(-qint64(count - i) * 4 * 3600),
                                           (i == count - 1) ? 0 : MessageFlag::Seen, id, previous, thread);
        if (thread == 0)
            thread = inserted;
        previous = id;
    }
}

// Eine Konversation über Posteingang und Gesendet
void insertConversation(qint64 accountId, qint64 inbox, qint64 sent, const QDateTime &now)
{
    const DemoMail first{"Lisa Hoffmann", "lisa@hoffmann.example", "Planung Sommerfest",
                         "Hallo zusammen,\n\nwir sollten langsam das Sommerfest planen. Wer kümmert sich um den Grill?\n\nLisa"};
    const DemoMail reply{"Max Mustermann", "max@example.de", "Re: Planung Sommerfest",
                         "Hi Lisa,\n\nden Grill bringe ich mit. Soll ich auch Getränke organisieren?\n\nMax"};
    const DemoMail answer{"Lisa Hoffmann", "lisa@hoffmann.example", "Re: Planung Sommerfest",
                          "Super, danke! Getränke übernimmt Thomas. Dann sehen wir uns am Samstag um 15 Uhr.\n\nLisa"};
    const qint64 thread = insertMail(accountId, inbox, first, now.addDays(-3), MessageFlag::Seen, QStringLiteral("<fest-1@hoffmann.example>"));
    insertMail(accountId, sent, reply, now.addDays(-2), MessageFlag::Seen | MessageFlag::Answered, QStringLiteral("<fest-2@example.de>"),
               QStringLiteral("<fest-1@hoffmann.example>"), thread);
    insertMail(accountId, inbox, answer, now.addSecs(-5 * 3600), 0, QStringLiteral("<fest-3@hoffmann.example>"), QStringLiteral("<fest-2@example.de>"),
               thread);
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
    if (offset == 0)
        insertConversation(a.id, inbox, sent, now);
    if (offset == 1)
        insertLongConversation(a.id, inbox, sent, now);
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

    // Labels über Betreffzeilen zuweisen
    const auto assign = [](qint64 label, const QString &subjectPattern) {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("INSERT OR IGNORE INTO message_labels (message_id, label_id) SELECT id, ? FROM messages WHERE subject LIKE ?"));
        q.addBindValue(label);
        q.addBindValue(subjectPattern);
        q.exec();
    };
    const qint64 invoices = LabelStore::create(QStringLiteral("Rechnungen"), QStringLiteral("#10b981"));
    const qint64 projects = LabelStore::create(QStringLiteral("Projekte"), QStringLiteral("#6366f1"));
    LabelStore::create(QStringLiteral("Reisen"), QStringLiteral("#f59e0b"));
    assign(invoices, QStringLiteral("Rechnung%"));
    assign(projects, QStringLiteral("%Release-Termin%"));
    assign(projects, QStringLiteral("Treffen am Donnerstag"));
    db.commit();
}

} // namespace Fjordpost

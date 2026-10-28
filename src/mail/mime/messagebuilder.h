#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace MopMail {

// Inhalt einer zu sendenden Nachricht, wie ihn der Composer liefert.
struct OutgoingMessage {
    QString fromName;
    QString fromEmail;
    QStringList to;  // Einträge im Format "Name <adresse>" oder "adresse"
    QStringList cc;
    QStringList bcc;
    QString subject;
    QString plainText;
    QString html;    // optional; erzeugt multipart/alternative
    QStringList attachmentPaths;
    QByteArray inReplyTo;
    QList<QByteArray> references;
};

namespace MessageBuilder {

// Baut die RFC-822-Nachricht. Bcc wird bewusst nicht in die Kopfzeilen
// geschrieben, sondern nur im SMTP-Umschlag verwendet.
// Bei Fehlern (z. B. Anhang nicht lesbar) leeres Ergebnis und errorString gesetzt.
QByteArray build(const OutgoingMessage &message, QByteArray *messageId = nullptr, QString *errorString = nullptr);

// Reine E-Mail-Adressen aller Empfänger (für den SMTP-Umschlag).
QStringList envelopeRecipients(const OutgoingMessage &message);

// Zerlegt eine durch Komma/Semikolon getrennte Eingabe in Adressen.
QStringList splitAddresses(const QString &input);

} // namespace MessageBuilder
} // namespace MopMail

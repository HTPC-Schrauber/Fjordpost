#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace KMime {
class Message;
}

namespace Fjordpost {

struct AttachmentInfo {
    int index = 0; // Position in KMime::Content::attachments()
    QString fileName;
    QString mimeType;
    qint64 size = 0;
    QString contentId;
};

// Aufbereitete Sicht auf eine Nachricht für Datenbank und Anzeige.
struct ParsedMessage {
    QString subject;
    QString fromName;
    QString fromEmail;
    QString to;
    QString cc;
    QString bcc;
    QString replyTo;
    QDateTime date;
    QByteArray messageId;
    QByteArray inReplyTo;
    QList<QByteArray> references;

    // Nur bei vollständig geparsten Nachrichten gefüllt:
    QString plainText;
    QString html;
    QList<AttachmentInfo> attachments;
    QString snippet;
};

namespace MessageParser {

// Liest nur die Kopfzeilen aus (für Header-Fetch vom Server).
ParsedMessage parseHeaders(const KMime::Message &message);
// Wie oben, aber aus dem rohen Kopfbereich (CRLF oder LF).
ParsedMessage parseHeaders(const QByteArray &rawHead);

// Vollständige Nachricht inklusive Text, HTML und Anhängen.
ParsedMessage parse(const QByteArray &raw);

// Dekodierter Inhalt eines Anhangs (index aus AttachmentInfo).
QByteArray attachmentData(const QByteArray &raw, int index);

// Kurzer Vorschautext: zitierte Zeilen und Signatur entfernt, auf eine Zeile verdichtet.
QString makeSnippet(const QString &plainText, const QString &html);

} // namespace MessageParser
} // namespace Fjordpost

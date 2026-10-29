#include "messagebuilder.h"

#include "fjordpost-version.h"

#include <KLocalizedString>
#include <KMime/Message>
#include <KMime/Util>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QUuid>

namespace Fjordpost::MessageBuilder {

namespace {

KMime::Types::Mailbox mailbox(const QString &text)
{
    KMime::Types::Mailbox mb;
    mb.fromUnicodeString(text.trimmed());
    return mb;
}

KMime::Content *textPart(const QByteArray &mimeType, const QString &text)
{
    auto *part = new KMime::Content;
    part->contentType()->setMimeType(mimeType);
    part->contentType()->setCharset("utf-8");
    part->contentTransferEncoding()->setEncoding(KMime::Headers::CEquPr);
    part->setBody(text.toUtf8());
    return part;
}

// Setzt Text bzw. multipart/alternative direkt auf den Inhalt c.
void fillBody(KMime::Content &c, const OutgoingMessage &m)
{
    if (m.html.isEmpty()) {
        c.contentType()->setMimeType("text/plain");
        c.contentType()->setCharset("utf-8");
        c.contentTransferEncoding()->setEncoding(KMime::Headers::CEquPr);
        c.setBody(m.plainText.toUtf8());
        return;
    }
    c.contentType()->setMimeType("multipart/alternative");
    c.contentType()->setBoundary(KMime::multiPartBoundary());
    c.appendContent(textPart("text/plain", m.plainText));
    c.appendContent(textPart("text/html", m.html));
}

} // namespace

QStringList splitAddresses(const QString &input)
{
    QStringList result;
    const auto mailboxes = KMime::Types::Mailbox::listFromUnicodeString(QString(input).replace(QLatin1Char(';'), QLatin1Char(',')));
    for (const auto &mb : mailboxes) {
        if (mb.hasAddress())
            result << mb.prettyAddress();
    }
    return result;
}

QStringList envelopeRecipients(const OutgoingMessage &message)
{
    QStringList result;
    for (const QStringList *list : {&message.to, &message.cc, &message.bcc}) {
        for (const QString &entry : *list) {
            const QString address = QString::fromUtf8(mailbox(entry).address());
            if (!address.isEmpty() && !result.contains(address, Qt::CaseInsensitive))
                result << address;
        }
    }
    return result;
}

QByteArray build(const OutgoingMessage &m, QByteArray *messageId, QString *errorString)
{
    KMime::Message msg;

    KMime::Types::Mailbox from;
    from.setName(m.fromName);
    from.setAddress(m.fromEmail.toUtf8());
    msg.from()->addAddress(from);
    for (const QString &a : m.to)
        msg.to()->addAddress(mailbox(a));
    for (const QString &a : m.cc)
        msg.cc()->addAddress(mailbox(a));
    msg.subject()->fromUnicodeString(m.subject);
    msg.date()->setDateTime(QDateTime::currentDateTime());
    msg.userAgent()->from7BitString("Fjordpost/" FJORDPOST_VERSION_STRING);

    const QString domain = m.fromEmail.section(QLatin1Char('@'), 1);
    const QByteArray id = QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() + '@'
        + (domain.isEmpty() ? QByteArray("fjordpost.local") : domain.toUtf8());
    msg.messageID()->setIdentifier(id);
    if (messageId)
        *messageId = id;

    if (!m.inReplyTo.isEmpty())
        msg.inReplyTo()->appendIdentifier(m.inReplyTo);
    for (const QByteArray &ref : m.references)
        msg.references()->appendIdentifier(ref);

    if (m.attachmentPaths.isEmpty()) {
        fillBody(msg, m);
    } else {
        msg.contentType()->setMimeType("multipart/mixed");
        msg.contentType()->setBoundary(KMime::multiPartBoundary());
        auto *body = new KMime::Content;
        fillBody(*body, m);
        msg.appendContent(body);

        QMimeDatabase mimeDb;
        for (const QString &path : m.attachmentPaths) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (errorString)
                    *errorString = i18n("Cannot read attachment %1", path);
                return {};
            }
            auto *part = new KMime::Content;
            part->contentType()->setMimeType(mimeDb.mimeTypeForFile(path).name().toLatin1());
            part->contentType()->setName(QFileInfo(path).fileName());
            part->contentDisposition()->setDisposition(KMime::Headers::CDattachment);
            part->contentDisposition()->setFilename(QFileInfo(path).fileName());
            part->contentTransferEncoding()->setEncoding(KMime::Headers::CEbase64);
            part->setBody(file.readAll());
            msg.appendContent(part);
        }
    }

    msg.assemble();
    return KMime::LFtoCRLF(msg.encodedContent());
}

} // namespace Fjordpost::MessageBuilder

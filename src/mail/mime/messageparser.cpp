#include "messageparser.h"

#include <KMime/Message>
#include <KMime/Util>

#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace Fjordpost::MessageParser {

namespace {

QString mailboxList(const KMime::Types::Mailbox::List &list)
{
    return KMime::Types::Mailbox::listToUnicodeString(list);
}

QString attachmentName(const KMime::Content *c)
{
    QString name;
    if (const auto *cd = c->contentDisposition())
        name = cd->filename();
    if (name.isEmpty()) {
        if (const auto *ct = c->contentType())
            name = ct->name();
    }
    return name;
}

} // namespace

ParsedMessage parseHeaders(const KMime::Message &message)
{
    ParsedMessage p;
    if (const auto *h = message.subject())
        p.subject = h->asUnicodeString();
    if (const auto *h = message.from(); h && !h->mailboxes().isEmpty()) {
        const auto mb = h->mailboxes().constFirst();
        p.fromName = mb.name();
        p.fromEmail = QString::fromUtf8(mb.address());
    }
    if (const auto *h = message.to())
        p.to = mailboxList(h->mailboxes());
    if (const auto *h = message.cc())
        p.cc = mailboxList(h->mailboxes());
    if (const auto *h = message.bcc())
        p.bcc = mailboxList(h->mailboxes());
    if (const auto *h = message.replyTo())
        p.replyTo = mailboxList(h->mailboxes());
    if (const auto *h = message.date())
        p.date = h->dateTime();
    if (const auto *h = message.messageID())
        p.messageId = h->identifier();
    if (const auto *h = message.inReplyTo(); h && !h->identifiers().isEmpty())
        p.inReplyTo = h->identifiers().constFirst();
    if (const auto *h = message.references())
        p.references = h->identifiers();
    return p;
}

ParsedMessage parseHeaders(const QByteArray &rawHead)
{
    KMime::Message message;
    message.setHead(KMime::CRLFtoLF(rawHead));
    message.parse();
    return parseHeaders(message);
}

ParsedMessage parse(const QByteArray &raw)
{
    KMime::Message message;
    message.setContent(KMime::CRLFtoLF(raw));
    message.parse();

    ParsedMessage p = parseHeaders(message);

    if (const KMime::Content *html = message.mainBodyPart("text/html"); html && !KMime::isAttachment(html))
        p.html = html->decodedText();
    if (const KMime::Content *text = message.mainBodyPart("text/plain"); text && !KMime::isAttachment(text))
        p.plainText = text->decodedText();
    else if (const KMime::Content *text = message.textContent(); text && !KMime::isAttachment(text)
             && text->contentType()->mimeType() != "text/html")
        p.plainText = text->decodedText();

    const auto attachments = message.attachments();
    for (int i = 0; i < attachments.size(); ++i) {
        const KMime::Content *c = attachments.at(i);
        AttachmentInfo a;
        a.index = i;
        a.fileName = attachmentName(c);
        a.mimeType = QString::fromLatin1(c->contentType()->mimeType());
        a.size = c->decodedContent().size();
        if (const auto *cid = c->contentID())
            a.contentId = QString::fromUtf8(cid->identifier());
        p.attachments << a;
    }

    p.snippet = makeSnippet(p.plainText, p.html);
    return p;
}

QByteArray attachmentData(const QByteArray &raw, int index)
{
    KMime::Message message;
    message.setContent(KMime::CRLFtoLF(raw));
    message.parse();
    const auto attachments = message.attachments();
    if (index < 0 || index >= attachments.size())
        return {};
    return attachments.at(index)->decodedContent();
}

QString makeSnippet(const QString &plainText, const QString &html)
{
    QString text = plainText;
    if (text.trimmed().isEmpty() && !html.isEmpty())
        text = QTextDocumentFragment::fromHtml(html).toPlainText();

    QStringList kept;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line == QLatin1String("-- ")) // Signaturtrenner
            break;
        if (line.trimmed().startsWith(QLatin1Char('>')))
            continue;
        kept << line;
    }
    QString snippet = kept.join(QLatin1Char(' ')).simplified();
    constexpr int maxLength = 200;
    if (snippet.size() > maxLength)
        snippet = snippet.left(maxLength);
    return snippet;
}

} // namespace Fjordpost::MessageParser

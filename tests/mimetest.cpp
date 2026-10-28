#include "mail/mime/messagebuilder.h"
#include "mail/mime/messageparser.h"

#include <QTemporaryDir>
#include <QTest>

using namespace MopMail;

class MimeTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void plainTextRoundTrip()
    {
        OutgoingMessage out;
        out.fromName = QStringLiteral("Jürgen Müller");
        out.fromEmail = QStringLiteral("juergen@example.de");
        out.to = {QStringLiteral("Anna Schäfer <anna@example.org>")};
        out.cc = {QStringLiteral("bob@example.org")};
        out.bcc = {QStringLiteral("geheim@example.org")};
        out.subject = QStringLiteral("Grüße aus Köln – schöne Woche!");
        out.plainText = QStringLiteral("Hallo Anna,\n\nviele Grüße ☕\n\n> zitierte Zeile\n-- \nSignatur");
        out.inReplyTo = "parent@example.org";
        out.references = {"root@example.org", "parent@example.org"};

        QByteArray messageId;
        const QByteArray raw = MessageBuilder::build(out, &messageId);
        QVERIFY(!raw.isEmpty());
        QVERIFY(raw.contains("\r\n"));
        QVERIFY(!raw.contains("geheim@example.org")); // Bcc nie im Kopf

        const ParsedMessage p = MessageParser::parse(raw);
        QCOMPARE(p.subject, out.subject);
        QCOMPARE(p.fromName, out.fromName);
        QCOMPARE(p.fromEmail, out.fromEmail);
        QVERIFY(p.to.contains(QStringLiteral("anna@example.org")));
        QVERIFY(p.to.contains(QStringLiteral("Anna Schäfer")));
        QCOMPARE(p.messageId, messageId);
        QCOMPARE(p.inReplyTo, QByteArray("parent@example.org"));
        QCOMPARE(p.references.size(), 2);
        QVERIFY(p.plainText.contains(QStringLiteral("viele Grüße ☕")));
        QCOMPARE(p.snippet, QStringLiteral("Hallo Anna, viele Grüße ☕"));
        QVERIFY(p.attachments.isEmpty());

        QCOMPARE(MessageBuilder::envelopeRecipients(out),
                 QStringList({QStringLiteral("anna@example.org"), QStringLiteral("bob@example.org"), QStringLiteral("geheim@example.org")}));
    }

    void htmlAndAttachments()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("Rechnung März.pdf"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        const QByteArray payload("%PDF-1.4\n\x00\x01\x02 binär", 20);
        f.write(payload);
        f.close();

        OutgoingMessage out;
        out.fromEmail = QStringLiteral("a@example.de");
        out.to = {QStringLiteral("b@example.de")};
        out.subject = QStringLiteral("Mit Anhang");
        out.plainText = QStringLiteral("Text");
        out.html = QStringLiteral("<p><b>Text</b></p>");
        out.attachmentPaths = {path};

        const QByteArray raw = MessageBuilder::build(out);
        const ParsedMessage p = MessageParser::parse(raw);
        QCOMPARE(p.plainText.trimmed(), QStringLiteral("Text"));
        QVERIFY(p.html.contains(QStringLiteral("<b>Text</b>")));
        QCOMPARE(p.attachments.size(), 1);
        QCOMPARE(p.attachments.first().fileName, QStringLiteral("Rechnung März.pdf"));
        QCOMPARE(p.attachments.first().mimeType, QStringLiteral("application/pdf"));
        QCOMPARE(MessageParser::attachmentData(raw, 0), payload);
    }

    void missingAttachmentFails()
    {
        OutgoingMessage out;
        out.fromEmail = QStringLiteral("a@example.de");
        out.attachmentPaths = {QStringLiteral("/nonexistent/file")};
        QString error;
        QVERIFY(MessageBuilder::build(out, nullptr, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void snippetFromHtml()
    {
        QCOMPARE(MessageParser::makeSnippet(QString(), QStringLiteral("<html><body><h1>Hallo</h1><p>Welt &amp; mehr</p></body></html>")),
                 QStringLiteral("Hallo Welt & mehr"));
    }

    void splitAddresses()
    {
        QCOMPARE(MessageBuilder::splitAddresses(QStringLiteral("a@x.de; \"Müller, Hans\" <h@x.de>, b@x.de")).size(), 3);
    }
};

QTEST_GUILESS_MAIN(MimeTest)
#include "mimetest.moc"

#include "mail/autoconfig/autoconfig.h"

#include <QTest>

using namespace MopMail;

class AutoConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesThunderbirdFormat()
    {
        const QByteArray xml = R"(<?xml version="1.0"?>
<clientConfig version="1.1">
  <emailProvider id="example.de">
    <domain>example.de</domain>
    <displayName>Example Mail</displayName>
    <incomingServer type="pop3">
      <hostname>pop.example.de</hostname>
      <port>995</port>
      <socketType>SSL</socketType>
      <username>%EMAILADDRESS%</username>
      <authentication>password-cleartext</authentication>
    </incomingServer>
    <incomingServer type="imap">
      <hostname>imap.example.de</hostname>
      <port>143</port>
      <socketType>plain</socketType>
      <username>%EMAILLOCALPART%</username>
    </incomingServer>
    <incomingServer type="imap">
      <hostname>imap.example.de</hostname>
      <port>993</port>
      <socketType>SSL</socketType>
      <username>%EMAILLOCALPART%</username>
    </incomingServer>
    <outgoingServer type="smtp">
      <hostname>smtp.%EMAILDOMAIN%</hostname>
      <port>587</port>
      <socketType>STARTTLS</socketType>
      <username>%EMAILADDRESS%</username>
      <authentication>OAuth2</authentication>
    </outgoingServer>
  </emailProvider>
</clientConfig>)";

        const DiscoveredSettings s = AutoConfig::parse(xml, QStringLiteral("max.muster@example.de"));
        QVERIFY(s.isValid());
        QCOMPARE(s.providerName, QStringLiteral("Example Mail"));
        QCOMPARE(s.incoming.size(), 3);

        // IMAP mit TLS zuerst, unverschlüsseltes IMAP danach, POP3 zuletzt
        QCOMPARE(s.incoming.at(0).type, AccountType::Imap);
        QCOMPARE(s.incoming.at(0).port, 993);
        QCOMPARE(s.incoming.at(0).security, Security::Tls);
        QCOMPARE(s.incoming.at(0).username, QStringLiteral("max.muster"));
        QCOMPARE(s.incoming.at(1).security, Security::None);
        QCOMPARE(s.incoming.at(2).type, AccountType::Pop3);

        QCOMPARE(s.outgoing.size(), 1);
        QCOMPARE(s.outgoing.at(0).host, QStringLiteral("smtp.example.de"));
        QCOMPARE(s.outgoing.at(0).security, Security::StartTls);
        QCOMPARE(s.outgoing.at(0).username, QStringLiteral("max.muster@example.de"));
        QVERIFY(s.outgoing.at(0).oauth);
    }

    void rejectsGarbage()
    {
        QVERIFY(!AutoConfig::parse("<html><body>404</body></html>", QStringLiteral("a@b.de")).isValid());
        QVERIFY(!AutoConfig::parse("not xml at all <", QStringLiteral("a@b.de")).isValid());
    }

    void guessesCommonNames()
    {
        const DiscoveredSettings s = AutoConfig::guess(QStringLiteral("ich@Firma.example"));
        QVERIFY(s.guessed);
        QCOMPARE(s.incoming.first().host, QStringLiteral("imap.firma.example"));
        QCOMPARE(s.outgoing.first().host, QStringLiteral("smtp.firma.example"));
    }
};

QTEST_GUILESS_MAIN(AutoConfigTest)
#include "autoconfigtest.moc"

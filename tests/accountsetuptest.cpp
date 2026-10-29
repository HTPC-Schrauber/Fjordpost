// Konto-Assistent: eigene Eingaben dürfen nach einer fehlgeschlagenen Anmeldung
// nicht durch Vorschläge der automatischen Suche ersetzt werden.

#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "integration/accountsetup.h"

#include <KLocalizedString>
#include <QTest>

using namespace Fjordpost;

namespace {
// Auf Port 1 lauscht nichts: die Verbindung wird sofort abgelehnt.
constexpr int closedPort = 1;
const QString localHost = QStringLiteral("127.0.0.1");
} // namespace

class AccountSetupTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        KLocalizedString::setApplicationDomain("fjordpost");
    }

    void keepsEditedIncomingServer()
    {
        AccountManager accounts;
        SecretStore secrets;
        AccountSetup setup(&accounts, &secrets);
        setup.setProperty("email", QStringLiteral("max@fjordpost.invalid"));
        setup.applyDiscovered(AutoConfig::guess(QStringLiteral("max@fjordpost.invalid")));
        QVERIFY(setup.property("incomingHost").toString() != localHost);

        setup.setProperty("incomingHost", localHost);
        setup.setProperty("incomingPort", closedPort);
        setup.setProperty("incomingSecurity", 0);
        setup.testAndSave(QStringLiteral("geheim"));
        QTRY_VERIFY_WITH_TIMEOUT(!setup.busy(), 30000);

        QVERIFY(!setup.errorText().isEmpty());
        QCOMPARE(setup.property("incomingHost").toString(), localHost);
        QCOMPARE(setup.property("incomingPort").toInt(), closedPort);
        QCOMPARE(setup.property("incomingSecurity").toInt(), 0);
    }

    void keepsEditedOutgoingServer()
    {
        AccountManager accounts;
        SecretStore secrets;
        AccountSetup setup(&accounts, &secrets);
        setup.setProperty("email", QStringLiteral("max@fjordpost.invalid"));
        setup.applyDiscovered(AutoConfig::guess(QStringLiteral("max@fjordpost.invalid")));

        // POP3 wird (noch) nicht geprüft, es geht direkt zum SMTP-Test.
        setup.setProperty("pop3", true);
        setup.setProperty("outgoingHost", localHost);
        setup.setProperty("outgoingPort", closedPort);
        setup.setProperty("outgoingSecurity", 0);
        setup.testAndSave(QStringLiteral("geheim"));
        QTRY_VERIFY_WITH_TIMEOUT(!setup.busy(), 30000);

        QVERIFY(!setup.errorText().isEmpty());
        QCOMPARE(setup.property("pop3").toBool(), true);
        QCOMPARE(setup.property("outgoingHost").toString(), localHost);
        QCOMPARE(setup.property("outgoingPort").toInt(), closedPort);
        QCOMPARE(setup.property("outgoingSecurity").toInt(), 0);
    }
};

QTEST_GUILESS_MAIN(AccountSetupTest)
#include "accountsetuptest.moc"

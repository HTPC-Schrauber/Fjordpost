#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "integration/accountsetup.h"
#include "integration/appcontroller.h"
#include "integration/notifier.h"
#include "integration/remotecontentblocker.h"
#include "mail/mailservice.h"
#include "fjordpost-version.h"
#include "storage/database.h"
#include "storage/demodata.h"
#include "storage/mailstore.h"

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QEventLoop>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWebEngineProfile>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QtWebEngineQuick>

using namespace Fjordpost;

int main(int argc, char *argv[])
{
    // Muss vor dem Anlegen der Application-Instanz passieren.
    QtWebEngineQuick::initialize();

    // QApplication statt QGuiApplication: wird vom qqc2-desktop-style
    // (native Breeze-Optik der Bedienelemente) benötigt.
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("fjordpost");

    KAboutData about(QStringLiteral("fjordpost"),
                     QStringLiteral("Fjordpost"),
                     QStringLiteral(FJORDPOST_VERSION_STRING),
                     i18n("A modern email client for KDE Plasma"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 Fjordpost contributors"));
    // Bestimmt App-ID, D-Bus-Namen und den Konfigurationsordner (~/.config/fjordblau.de/).
    about.setOrganizationDomain("fjordblau.de");
    about.setDesktopFileName(QStringLiteral("de.fjordblau.Fjordpost"));
    KAboutData::setApplicationData(about);

    // Außerhalb von Plasma (z. B. WSLg, andere Desktops) setzt niemand ein
    // Icon-Theme; dann auf Breeze zurückfallen statt ohne Symbole dazustehen.
    if (QIcon::themeName().isEmpty() || QIcon::themeName() == QLatin1String("hicolor"))
        QIcon::setThemeName(QStringLiteral("breeze"));
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("mail-client"), QIcon::fromTheme(QStringLiteral("internet-mail"))));

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    KCrash::initialize();

    QCommandLineParser parser;
    QCommandLineOption demoOption(QStringLiteral("demo"), i18n("Fill an empty database with sample accounts and messages."));
    QCommandLineOption dataDirOption(QStringLiteral("data-dir"), i18n("Use a different data directory."), i18n("directory"));
    QCommandLineOption offlineOption(QStringLiteral("offline"), i18n("Do not connect to any server."));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), i18n("Save a screenshot of the main window and quit (for development)."), i18n("file"));
    parser.addOption(demoOption);
    parser.addOption(dataDirOption);
    parser.addOption(offlineOption);
    QCommandLineOption devActionOption(QStringLiteral("dev-action"), i18n("Trigger a UI action before the screenshot (open-first, compose, compose-window, reply, setup, settings, light, undo-send, open-thread, expand-thread, thread-history, label-menu, label-dialog, label-filter)."), i18n("action"));
    parser.addOption(screenshotOption);
    parser.addOption(devActionOption);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // Nur eine Instanz; ein zweiter Start bringt das vorhandene Fenster nach vorn.
    // Screenshot-Läufe (Entwicklung) dürfen neben einer laufenden Instanz starten.
    KDBusService service(parser.isSet(screenshotOption) ? KDBusService::Multiple : KDBusService::Unique);

    const QString dataDir = parser.isSet(dataDirOption) ? parser.value(dataDirOption)
                                                         : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!Database::initialize(QDir(dataDir).filePath(QStringLiteral("fjordpost.db"))))
        return 1;
    MailStore::rebuildThreads();

    AccountManager accounts;
    accounts.load();
    if (parser.isSet(demoOption))
        seedDemoData(accounts);

    SecretStore secrets;
    MailService mail(&accounts, &secrets);
    AccountSetup setup(&accounts, &secrets);
    AppController controller(&accounts, &mail, &setup);
    Notifier notifier;

    // Externe Inhalte in Mails nur nach Freigabe laden
    auto *blocker = new RemoteContentBlocker(&app);
    QQuickWebEngineProfile::defaultProfile()->setUrlRequestInterceptor(blocker);
    QObject::connect(&controller, &AppController::remoteContentAllowedChanged, blocker, [blocker, &controller] {
        blocker->setAllowed(controller.remoteContentAllowed());
    });

    notifier.setUnreadCount(controller.totalUnread());
    QObject::connect(&controller, &AppController::totalUnreadChanged, &notifier, [&notifier, &controller] {
        notifier.setUnreadCount(controller.totalUnread());
    });
    QObject::connect(&mail, &MailService::newMessages, &notifier, &Notifier::notifyNewMessages);
    QObject::connect(&notifier, &Notifier::openMessageRequested, &controller, &AppController::openMessage);

    qmlRegisterSingletonInstance("de.fjordblau.fjordpost.core", 1, 0, "App", &controller);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("de.fjordblau.fjordpost.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());

    auto activateWindow = [window] {
        window->show();
        window->raise();
        window->requestActivate();
    };
    QObject::connect(&service, &KDBusService::activateRequested, window, activateWindow);
    QObject::connect(&notifier, &Notifier::activateRequested, window, activateWindow);

    if (!parser.isSet(offlineOption) && !parser.isSet(screenshotOption))
        mail.start();

    if (parser.isSet(devActionOption)) {
        const QString action = parser.value(devActionOption);
        QTimer::singleShot(500, window, [window, action] {
            QMetaObject::invokeMethod(window, "devAction", Q_ARG(QVariant, action));
        });
    }

    if (parser.isSet(screenshotOption)) {
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(3000, window, [window, file] {
            // Oberstes sichtbares Fenster aufnehmen (z. B. den Composer).
            QQuickWindow *target = window;
            const auto windows = QGuiApplication::topLevelWindows();
            for (QWindow *w : windows) {
                if (auto *qw = qobject_cast<QQuickWindow *>(w); qw && qw->isVisible() && qw != window)
                    target = qw;
            }
            const bool ok = target->grabWindow().save(file);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    const int result = app.exec();

    // Zurückgehaltene Nachrichten ("Senden rückgängig") nicht bis zum nächsten
    // Start liegen lassen: jetzt senden und höchstens 30 s auf Versand und
    // Ablage in "Gesendet" warten. Die Fenster sind dann schon geschlossen.
    if (mail.sendDelayedNow()) {
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&loop, &mail] {
            if (!mail.hasPendingWork())
                loop.quit();
        });
        poll.start(200);
        QTimer::singleShot(30 * 1000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return result;
}

#include "core/accountmanager.h"
#include "integration/appcontroller.h"
#include "mopmail-version.h"
#include "storage/database.h"
#include "storage/demodata.h"

#include <KAboutData>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QQuickStyle>
#include <QStandardPaths>

using namespace MopMail;

int main(int argc, char *argv[])
{
    // QApplication statt QGuiApplication: wird vom qqc2-desktop-style
    // (native Breeze-Optik der Bedienelemente) benötigt.
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("mopmail");

    KAboutData about(QStringLiteral("mopmail"),
                     QStringLiteral("MopMail"),
                     QStringLiteral(MOPMAIL_VERSION_STRING),
                     i18n("A modern email client for KDE Plasma"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 MopMail contributors"));
    about.setDesktopFileName(QStringLiteral("org.mopmail.MopMail"));
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
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), i18n("Save a screenshot of the main window and quit (for development)."), i18n("file"));
    parser.addOption(demoOption);
    parser.addOption(dataDirOption);
    parser.addOption(screenshotOption);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    // Nur eine Instanz; ein zweiter Start bringt das vorhandene Fenster nach vorn.
    KDBusService service(KDBusService::Unique);

    const QString dataDir = parser.isSet(dataDirOption) ? parser.value(dataDirOption)
                                                         : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!Database::initialize(QDir(dataDir).filePath(QStringLiteral("mopmail.db"))))
        return 1;

    AccountManager accounts;
    accounts.load();
    if (parser.isSet(demoOption))
        seedDemoData(accounts);

    AppController controller(&accounts);
    qmlRegisterSingletonInstance("org.mopmail.core", 1, 0, "App", &controller);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule("org.mopmail.app", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(screenshotOption)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(1500, window, [window, file] {
            const bool ok = window && window->grabWindow().save(file);
            QCoreApplication::exit(ok ? 0 : 1);
        });
    }

    return app.exec();
}

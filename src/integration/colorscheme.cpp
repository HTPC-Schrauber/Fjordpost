#include "colorscheme.h"


#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QStandardPaths>

namespace MopMail::ColorScheme {

namespace {

QString rgb(const QVariant &value)
{
    const QColor c(value.toString());
    return QStringLiteral("%1,%2,%3").arg(c.red()).arg(c.green()).arg(c.blue());
}

void writeSet(KSharedConfigPtr config, const char *name, const QVariantMap &c, const QString &background, const QString &alternate)
{
    KConfigGroup g(config, QString::fromLatin1(name));
    g.writeEntry("BackgroundNormal", rgb(c.value(background)));
    g.writeEntry("BackgroundAlternate", rgb(c.value(alternate)));
    g.writeEntry("ForegroundNormal", rgb(c.value(QStringLiteral("text"))));
    g.writeEntry("ForegroundInactive", rgb(c.value(QStringLiteral("textInactive"))));
    g.writeEntry("ForegroundActive", rgb(c.value(QStringLiteral("accent"))));
    g.writeEntry("ForegroundLink", rgb(c.value(QStringLiteral("accent"))));
    g.writeEntry("ForegroundVisited", rgb(c.value(QStringLiteral("accent"))));
    g.writeEntry("ForegroundNegative", rgb(c.value(QStringLiteral("negative"))));
    g.writeEntry("ForegroundNeutral", rgb(c.value(QStringLiteral("neutral"))));
    g.writeEntry("ForegroundPositive", rgb(c.value(QStringLiteral("positive"))));
    g.writeEntry("DecorationFocus", rgb(c.value(QStringLiteral("accent"))));
    g.writeEntry("DecorationHover", rgb(c.value(QStringLiteral("accent"))));
}

} // namespace

void apply(const QVariantMap &colors)
{
    if (colors.isEmpty()) {
        qApp->setProperty("KDE_COLOR_SCHEME_PATH", QVariant());
        qApp->setPalette(KColorScheme::createApplicationPalette(KSharedConfig::openConfig()));
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QStringLiteral("mopmail.colors"));

    KSharedConfigPtr config = KSharedConfig::openConfig(path, KConfig::SimpleConfig);
    for (const QString &group : config->groupList())
        config->deleteGroup(group);
    KConfigGroup(config, QStringLiteral("General")).writeEntry("Name", QStringLiteral("MopMail"));
    // Inaktive Fenster nicht abdunkeln/entsättigen
    KConfigGroup(config, QStringLiteral("ColorEffects:Inactive")).writeEntry("Enable", false);
    KConfigGroup(config, QStringLiteral("ColorEffects:Inactive")).writeEntry("ChangeSelectionColor", false);

    const QString view = QStringLiteral("view");
    const QString alternate = QStringLiteral("viewAlternate");
    writeSet(config, "Colors:View", colors, view, alternate);
    writeSet(config, "Colors:Window", colors, QStringLiteral("window"), alternate);
    writeSet(config, "Colors:Button", colors, QStringLiteral("button"), alternate);
    writeSet(config, "Colors:Header", colors, QStringLiteral("header"), alternate);
    writeSet(config, "Colors:Tooltip", colors, QStringLiteral("button"), alternate);
    writeSet(config, "Colors:Complementary", colors, QStringLiteral("header"), alternate);

    // Auswahl: Akzentfarbe mit heller Schrift
    QVariantMap selection = colors;
    selection.insert(QStringLiteral("text"), colors.value(QStringLiteral("accentText")));
    selection.insert(QStringLiteral("textInactive"), colors.value(QStringLiteral("accentText")));
    writeSet(config, "Colors:Selection", selection, QStringLiteral("accent"), QStringLiteral("accent"));

    config->sync();
    config->reparseConfiguration();

    // Wird von Kirigamis KDE-Plattformthema und KColorScheme-Nutzern ausgewertet.
    qApp->setProperty("KDE_COLOR_SCHEME_PATH", path);
    qApp->setPalette(KColorScheme::createApplicationPalette(config));
}

} // namespace MopMail::ColorScheme

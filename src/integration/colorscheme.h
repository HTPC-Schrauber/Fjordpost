#pragma once

#include <QVariantMap>

namespace Fjordpost::ColorScheme {

// Setzt ein aus den Theme-Farben erzeugtes KDE-Farbschema für die ganze Anwendung,
// damit auch Dialoge, Menüs und Auswahllisten (die Kirigami bewusst auf das
// Farbschema zurücksetzt) die Fjordpost-Farben tragen. Eine leere Map stellt das
// Farbschema des Systems wieder her.
//
// Schlüssel: view, viewAlternate, window, button, header, text, textInactive,
// accent, accentText, negative, neutral, positive (Farben als "#rrggbb").
void apply(const QVariantMap &colors);

} // namespace Fjordpost::ColorScheme

import QtQuick
import org.kde.kirigami as Kirigami
import org.mopmail.app

// Kirigami-Dialog in den MopMail-Farben. Dialoge liegen im Overlay des Fensters
// und erben dessen Kirigami.Theme-Überschreibungen nicht.
Kirigami.Dialog {
    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.backgroundColor: Theme.elevated
    Kirigami.Theme.alternateBackgroundColor: Theme.card
    Kirigami.Theme.textColor: Theme.textPrimary
    Kirigami.Theme.disabledTextColor: Theme.textMuted
    Kirigami.Theme.highlightColor: Theme.accent
    Kirigami.Theme.highlightedTextColor: Theme.accentText
    Kirigami.Theme.hoverColor: Theme.accent
    Kirigami.Theme.focusColor: Theme.accent
    Kirigami.Theme.linkColor: Theme.accent
}

import QtQuick
import QtQuick.Controls as QQC2
import org.mopmail.app

// Überschrift in Großbuchstaben (POSTFACH, LABELS & FILTER, ...).
QQC2.Label {
    font.pointSize: Theme.fontXxs
    font.weight: Font.DemiBold
    font.capitalization: Font.AllUppercase
    font.letterSpacing: 0.8
    color: Theme.textMuted
}

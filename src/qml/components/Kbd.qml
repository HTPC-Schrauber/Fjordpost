import QtQuick
import QtQuick.Controls as QQC2
import de.fjordblau.fjordpost.app

// Tastenkürzel-Hinweis (z. B. "Strg+N").
Rectangle {
    id: kbd
    property alias text: label.text
    property bool inverted: false

    implicitWidth: label.implicitWidth + 10
    implicitHeight: label.implicitHeight + 4
    radius: 4
    color: inverted ? Qt.rgba(0, 0, 0, 0.2) : Qt.alpha(Theme.dock, 0.6)
    border.width: inverted ? 0 : 1
    border.color: Theme.border

    QQC2.Label {
        id: label
        anchors.centerIn: parent
        font.family: "monospace"
        font.pointSize: Theme.fontXxs
        color: kbd.inverted ? Qt.rgba(1, 1, 1, 0.9) : Theme.textMuted
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import org.mopmail.app

// Kleines Etikett mit Rahmen (Ordner, Labels, Status).
Rectangle {
    id: tag
    property alias text: label.text
    property color tint: Theme.textSecondary
    property bool outlined: true
    property bool mono: false

    implicitWidth: label.implicitWidth + 12
    implicitHeight: label.implicitHeight + 4
    radius: 4
    color: outlined ? Qt.alpha(tint, 0.12) : Theme.card
    border.width: outlined ? 1 : 0
    border.color: Qt.alpha(tint, 0.3)

    QQC2.Label {
        id: label
        anchors.centerIn: parent
        font.pointSize: Theme.fontXxs
        font.weight: Font.Medium
        font.family: tag.mono ? "monospace" : Qt.application.font.family
        color: tag.outlined ? tag.tint : Theme.textSecondary
    }
}

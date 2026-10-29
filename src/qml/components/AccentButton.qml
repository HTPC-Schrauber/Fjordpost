import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.app

// Primäre Schaltfläche in Akzentfarbe (Neue E-Mail, Senden).
QQC2.AbstractButton {
    id: control

    property string iconName
    property bool iconAfterText: false
    property string shortcutHint
    property bool fillWidth: false

    implicitHeight: 34
    leftPadding: 14
    rightPadding: 14
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    hoverEnabled: true
    Accessible.name: text

    background: Rectangle {
        radius: Theme.radiusLg
        color: control.enabled ? (control.hovered || control.pressed ? Theme.accentHover : Theme.accent) : Qt.alpha(Theme.accent, 0.4)
        layer.enabled: false
    }

    contentItem: RowLayout {
        id: row
        spacing: 8
        Kirigami.Icon {
            visible: control.iconName.length > 0 && !control.iconAfterText
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            source: control.iconName
            isMask: true
            color: Theme.accentText
        }
        QQC2.Label {
            Layout.fillWidth: control.fillWidth
            text: control.text
            color: Theme.accentText
            font.pointSize: Theme.fontXs
            font.weight: Font.Medium
        }
        Kirigami.Icon {
            visible: control.iconName.length > 0 && control.iconAfterText
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: control.iconName
            isMask: true
            color: Theme.accentText
        }
        Kbd {
            visible: control.shortcutHint.length > 0
            text: control.shortcutHint
            inverted: true
        }
    }
}

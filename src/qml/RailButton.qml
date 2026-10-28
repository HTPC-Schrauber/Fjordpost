import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import "components"

// Schaltfläche in der App-Leiste: Konto (Initialen) oder Symbol,
// mit Auswahlmarkierung links und Punkt für ungelesene Nachrichten.
QQC2.AbstractButton {
    id: control

    property string iconName
    property string initials
    property color tint: Theme.accent
    property bool selected: false
    property bool filled: false       // Akzent-gefüllt (Unified Inbox)
    property bool subtle: false        // Modul-Schalter (Mail, Kalender, ...)
    property int badge: 0

    implicitWidth: 56
    implicitHeight: 40
    hoverEnabled: true
    Accessible.name: text

    QQC2.ToolTip.text: text
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    // Aktive Markierung am linken Rand
    Rectangle {
        x: 0
        anchors.verticalCenter: parent.verticalCenter
        width: 4
        height: control.selected ? 20 : 0
        radius: 2
        color: Theme.accent
        visible: !control.subtle
        Behavior on height {
            NumberAnimation { duration: 150 }
        }
    }

    contentItem: Item {
        Rectangle {
            id: tile
            anchors.centerIn: parent
            width: 36
            height: 36
            radius: Theme.radiusLg
            color: {
                if (control.filled && control.selected)
                    return Theme.accent;
                if (control.subtle)
                    return control.selected ? Qt.alpha(Theme.accent, 0.15) : (control.hovered ? Theme.card : "transparent");
                if (control.initials.length > 0)
                    return control.hovered ? Qt.alpha(control.tint, 0.2) : Theme.card;
                return control.hovered ? Theme.card : (control.filled ? Theme.card : "transparent");
            }
            border.width: control.initials.length > 0 ? 1 : 0
            border.color: control.selected ? Qt.alpha(control.tint, 0.6) : Theme.border

            Kirigami.Icon {
                anchors.centerIn: parent
                visible: control.iconName.length > 0
                width: 16
                height: 16
                source: control.iconName
                isMask: true
                color: control.filled && control.selected ? Theme.accentText : (control.selected ? Theme.accent : (control.hovered ? Theme.textPrimary : Theme.textSecondary))
            }
            QQC2.Label {
                anchors.centerIn: parent
                visible: control.iconName.length === 0
                text: control.initials
                color: control.tint
                font.weight: Font.DemiBold
                font.pointSize: Theme.fontXs
            }
        }

        // Ungelesen-Punkt bzw. Zähler
        Rectangle {
            visible: control.badge > 0
            anchors.right: tile.right
            anchors.top: tile.top
            anchors.rightMargin: -4
            anchors.topMargin: -4
            height: 14
            width: Math.max(14, badgeLabel.implicitWidth + 8)
            radius: 7
            color: control.filled ? Theme.danger : Theme.accent
            border.width: 2
            border.color: Theme.dock
            QQC2.Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: control.badge > 99 ? "99+" : control.badge
                color: "white"
                font.pixelSize: 8
                font.bold: true
            }
        }
    }
}

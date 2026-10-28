import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// Runde Schaltfläche in der App-Leiste: Konto-Avatar oder Symbol, mit
// Auswahlmarkierung links und Zähler für ungelesene Nachrichten.
QQC2.AbstractButton {
    id: control

    property string iconName: ""
    property color avatarColor: Kirigami.Theme.highlightColor
    property bool selected: false
    property int badge: 0

    implicitWidth: Kirigami.Units.gridUnit * 3
    implicitHeight: Kirigami.Units.gridUnit * 2.75
    hoverEnabled: true

    QQC2.ToolTip.text: text
    QQC2.ToolTip.visible: hovered
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    Accessible.name: text

    contentItem: Item {
        // Auswahlmarkierung
        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: control.selected ? parent.height * 0.7 : (control.hovered ? parent.height * 0.35 : 0)
            radius: 1.5
            color: Kirigami.Theme.textColor
            Behavior on height { NumberAnimation { duration: Kirigami.Units.shortDuration } }
        }

        Rectangle {
            id: circle
            anchors.centerIn: parent
            width: Kirigami.Units.gridUnit * 2
            height: width
            radius: control.selected ? width * 0.3 : width / 2
            color: control.iconName.length > 0 ? (control.selected ? Kirigami.Theme.highlightColor : Qt.rgba(1, 1, 1, control.hovered ? 0.18 : 0.08))
                                               : control.avatarColor
            Behavior on radius { NumberAnimation { duration: Kirigami.Units.shortDuration } }

            Kirigami.Icon {
                anchors.centerIn: parent
                visible: control.iconName.length > 0
                width: Kirigami.Units.iconSizes.smallMedium
                height: width
                source: control.iconName
                color: Kirigami.Theme.textColor
                isMask: true
            }
            QQC2.Label {
                anchors.centerIn: parent
                visible: control.iconName.length === 0
                text: control.text.length > 0 ? control.text.charAt(0).toUpperCase() : "?"
                color: "white"
                font.bold: true
                font.pixelSize: circle.height * 0.45
            }
        }

        // Zähler ungelesener Nachrichten
        Rectangle {
            visible: control.badge > 0
            anchors.right: circle.right
            anchors.top: circle.top
            anchors.rightMargin: -width / 3
            anchors.topMargin: -height / 3
            height: Kirigami.Units.gridUnit * 0.9
            width: Math.max(height, badgeLabel.implicitWidth + Kirigami.Units.smallSpacing * 2)
            radius: height / 2
            color: Kirigami.Theme.negativeTextColor
            QQC2.Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: control.badge > 99 ? "99+" : control.badge
                color: "white"
                font.pixelSize: parent.height * 0.7
                font.bold: true
            }
        }
    }
}

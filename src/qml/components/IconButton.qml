import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.mopmail.app

// Flache, abgerundete Symbolschaltfläche (Toolbar, Kopfbereiche).
QQC2.AbstractButton {
    id: control

    property string iconName
    property color iconColor: Theme.textSecondary
    property color hoverColor: Theme.textPrimary
    property real iconSize: 16
    property bool active: false
    property string tooltip: text

    implicitWidth: Math.max(28, contentItem.implicitWidth + 12)
    implicitHeight: 28
    hoverEnabled: true
    display: QQC2.AbstractButton.IconOnly
    Accessible.name: tooltip

    QQC2.ToolTip.text: tooltip
    QQC2.ToolTip.visible: hovered && tooltip.length > 0
    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

    background: Rectangle {
        radius: Theme.radius
        color: control.active ? Qt.alpha(Theme.accent, 0.15) : (control.hovered || control.pressed ? Theme.card : "transparent")
    }

    contentItem: Row {
        spacing: 6
        leftPadding: control.display === QQC2.AbstractButton.IconOnly ? 0 : 4
        rightPadding: leftPadding
        Kirigami.Icon {
            anchors.verticalCenter: parent.verticalCenter
            width: control.iconSize
            height: control.iconSize
            source: control.iconName
            isMask: true
            color: control.active ? Theme.accent : (control.hovered ? control.hoverColor : control.iconColor)
        }
        QQC2.Label {
            anchors.verticalCenter: parent.verticalCenter
            visible: control.display !== QQC2.AbstractButton.IconOnly
            text: control.text
            font.pointSize: Theme.fontXs
            font.weight: Font.Medium
            color: control.hovered ? Theme.textPrimary : Theme.textSecondary
        }
    }
}

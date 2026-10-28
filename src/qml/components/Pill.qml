import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.app

// Filter-Pill (Alle / Ungelesen / Markiert / Anhang).
QQC2.AbstractButton {
    id: control
    property string iconName
    property bool selected: false

    implicitHeight: 24
    leftPadding: 10
    rightPadding: 10
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.selected ? Theme.accent : Theme.card
    }
    contentItem: RowLayout {
        id: row
        spacing: 4
        Kirigami.Icon {
            visible: control.iconName.length > 0
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: control.iconName
            isMask: true
            color: label.color
        }
        QQC2.Label {
            id: label
            text: control.text
            font.pointSize: Theme.fontXxs
            font.weight: Font.Medium
            color: control.selected ? Theme.accentText : (control.hovered ? Theme.textPrimary : Theme.textSecondary)
        }
    }
}

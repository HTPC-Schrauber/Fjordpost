import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.app

// Abhakkreis für Aufgaben; der Standard-Haken ist im dunklen Theme kaum sichtbar.
QQC2.AbstractButton {
    id: control

    checkable: true
    implicitWidth: 22
    implicitHeight: 22
    hoverEnabled: true

    contentItem: Item {}
    background: Rectangle {
        anchors.centerIn: parent
        width: 18
        height: 18
        radius: 9
        color: control.checked ? Theme.accent : (control.hovered ? Qt.alpha(Theme.accent, 0.2) : "transparent")
        border.width: 2
        border.color: control.checked || control.hovered ? Theme.accent : Theme.textSecondary
        Kirigami.Icon {
            anchors.centerIn: parent
            width: 12
            height: 12
            visible: control.checked
            source: "checkmark"
            isMask: true
            color: Theme.accentText
        }
    }
}

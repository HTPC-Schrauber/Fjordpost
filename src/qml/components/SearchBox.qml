import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.app

// Suchfeld im Stil des Entwurfs; meldet Änderungen verzögert.
QQC2.TextField {
    id: field

    signal searchRequested(string text)

    implicitHeight: 30
    leftPadding: 30
    rightPadding: clearButton.visible ? 26 : 10
    font.pointSize: Theme.fontXs
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectByMouse: true

    background: Rectangle {
        radius: Theme.radius
        color: Qt.alpha(Theme.card, 0.8)
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.border
    }

    Kirigami.Icon {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        source: "search"
        isMask: true
        color: Theme.textMuted
    }

    QQC2.AbstractButton {
        id: clearButton
        anchors.right: parent.right
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        visible: field.text.length > 0
        contentItem: Kirigami.Icon {
            source: "edit-clear"
            isMask: true
            color: Theme.textMuted
        }
        onClicked: {
            field.clear();
            field.searchRequested("");
        }
    }

    Timer {
        id: delay
        interval: 350
        onTriggered: field.searchRequested(field.text)
    }
    onTextEdited: delay.restart()
    onAccepted: {
        delay.stop();
        searchRequested(text);
    }
}

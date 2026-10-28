import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.mopmail.app

// Unaufdringliche Meldung unten rechts.
Rectangle {
    id: toast

    function show(message) {
        label.text = message;
        opacity = 1;
        hideTimer.restart();
    }

    implicitWidth: row.implicitWidth + 28
    implicitHeight: row.implicitHeight + 16
    radius: Theme.radiusLg
    color: Theme.elevated
    border.width: 1
    border.color: Theme.border
    opacity: 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: 180 }
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: Theme.accent
        }
        QQC2.Label {
            id: label
            font.pointSize: Theme.fontXs
            color: Theme.textPrimary
            Layout.maximumWidth: 420
            wrapMode: Text.Wrap
        }
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.opacity = 0
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import de.fjordblau.fjordpost.app

// Unaufdringliche Meldung unten rechts, optional mit Aktion (z. B. "Rückgängig").
// Bei einer Aktion zeigt ein Balken die verbleibende Zeit.
Rectangle {
    id: toast

    property var actionCallback: null
    // Verbleibender Anteil der Anzeigedauer (1 → 0)
    property real remaining: 1

    // options: { actionText, onAction, timeout (ms) }
    function show(message, options) {
        const opts = options ?? {};
        label.text = message;
        actionButton.text = opts.actionText ?? "";
        actionCallback = opts.onAction ?? null;
        hideTimer.interval = opts.timeout ?? 3500;
        opacity = 1;
        hideTimer.restart();
        progress.restart();
    }

    function hide() {
        hideTimer.stop();
        actionCallback = null;
        opacity = 0;
    }

    implicitWidth: row.implicitWidth + 28
    implicitHeight: row.implicitHeight + 16
    radius: Theme.radiusLg
    color: Theme.elevated
    border.width: 1
    border.color: Theme.border
    opacity: 0
    visible: opacity > 0
    clip: true

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
        QQC2.AbstractButton {
            id: actionButton
            visible: text.length > 0 && toast.actionCallback !== null
            Layout.leftMargin: 6
            hoverEnabled: true
            padding: 4
            leftPadding: 8
            rightPadding: 8
            contentItem: QQC2.Label {
                text: actionButton.text
                font.pointSize: Theme.fontXs
                font.weight: Font.DemiBold
                color: Theme.accent
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: actionButton.hovered ? Qt.alpha(Theme.accent, 0.15) : "transparent"
            }
            onClicked: {
                const callback = toast.actionCallback;
                toast.hide();
                if (callback)
                    callback();
            }
        }
    }

    Rectangle {
        id: bar
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: toast.width * toast.remaining
        height: 2
        visible: actionButton.visible
        color: Theme.accent
    }

    NumberAnimation {
        id: progress
        target: toast
        property: "remaining"
        from: 1
        to: 0
        duration: hideTimer.interval
    }

    Timer {
        id: hideTimer
        interval: 3500
        onTriggered: toast.hide()
    }
}

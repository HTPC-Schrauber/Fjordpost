import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Ältere Nachricht einer aufgeklappten Konversation: eingerückt und einzeilig.
QQC2.ItemDelegate {
    id: row

    required property int index
    required property var messageId
    required property string fromName
    required property string dateText
    required property string snippet
    required property bool unread
    required property bool sent

    readonly property bool active: ListView.isCurrentItem

    hoverEnabled: true
    leftPadding: 34
    rightPadding: 14
    topPadding: 8
    bottomPadding: 8

    onClicked: {
        ListView.view.currentIndex = index;
        ListView.view.forceActiveFocus();
    }

    background: Rectangle {
        color: row.active ? Qt.alpha(Theme.accent, Theme.dark ? 0.11 : 0.08) : (row.hovered ? Qt.alpha(Theme.card, 0.5) : Qt.alpha(Theme.card, 0.18))
        // Verbindungslinie zur Konversation
        Rectangle {
            x: 20
            width: 2
            height: parent.height
            color: row.active ? Theme.accent : Theme.border
        }
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.leftMargin: 34
            anchors.right: parent.right
            height: 1
            color: Theme.border
        }
    }

    contentItem: RowLayout {
        spacing: 8
        Rectangle {
            visible: row.unread
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: Theme.accent
        }
        Kirigami.Icon {
            visible: row.sent
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: "mail-sent"
            isMask: true
            color: Theme.textMuted
        }
        QQC2.Label {
            // Name vollständig, solange er höchstens ein Drittel der Zeile braucht
            Layout.preferredWidth: Math.min(Math.ceil(implicitWidth) + 1, row.availableWidth / 3)
            text: row.fromName
            elide: Text.ElideRight
            font.pointSize: Theme.fontXxs
            font.weight: row.unread ? Font.DemiBold : Font.Medium
            color: row.unread ? Theme.textPrimary : Theme.textSecondary
        }
        QQC2.Label {
            Layout.fillWidth: true
            text: row.snippet
            textFormat: Text.PlainText
            elide: Text.ElideRight
            maximumLineCount: 1
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }
        QQC2.Label {
            text: row.dateText
            font.family: "monospace"
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }
    }
}

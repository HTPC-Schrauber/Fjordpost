import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core
import "components"

// Nachrichtenkarte: Absender, Zeit, Betreff, Vorschau, Etiketten.
// Beim Überfahren erscheinen Schnellaktionen anstelle der Zeit.
QQC2.ItemDelegate {
    id: card

    required property int index
    required property var messageId
    required property string subject
    required property string fromName
    required property string fromEmail
    required property string dateText
    required property string snippet
    required property bool unread
    required property bool flagged
    required property bool hasAttachments
    required property color accountColor
    required property string folderName

    readonly property bool active: ListView.isCurrentItem
    readonly property bool unified: App.selectedAccountId === 0

    hoverEnabled: true
    padding: 14
    topPadding: 12
    bottomPadding: 12

    onClicked: {
        ListView.view.currentIndex = index;
        ListView.view.forceActiveFocus();
    }

    background: Rectangle {
        color: card.active ? Qt.alpha(Theme.accent, Theme.dark ? 0.11 : 0.08) : (card.hovered ? Qt.alpha(Theme.card, 0.5) : "transparent")
        Rectangle {
            visible: card.active
            width: 3
            height: parent.height
            color: Theme.accent
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }
    }

    contentItem: ColumnLayout {
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Rectangle {
                visible: card.unread
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: Theme.accent
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: card.fromName
                elide: Text.ElideRight
                font.pointSize: Theme.fontXs
                font.weight: card.unread ? Font.DemiBold : Font.Medium
                color: card.unread ? Theme.textPrimary : Theme.textSecondary
            }
            QQC2.Label {
                visible: !card.hovered
                text: card.dateText
                font.family: "monospace"
                font.pointSize: Theme.fontXxs
                color: Theme.textMuted
            }
            Row {
                visible: card.hovered
                spacing: 0
                IconButton {
                    implicitWidth: 22
                    implicitHeight: 20
                    iconSize: 13
                    iconName: "archive-insert"
                    tooltip: i18n("Archive (E)")
                    onClicked: App.archiveMessage(card.messageId)
                }
                IconButton {
                    implicitWidth: 22
                    implicitHeight: 20
                    iconSize: 13
                    iconName: "edit-delete"
                    tooltip: i18n("Delete (Del)")
                    onClicked: App.deleteMessage(card.messageId)
                }
                IconButton {
                    implicitWidth: 22
                    implicitHeight: 20
                    iconSize: 13
                    iconName: card.unread ? "mail-mark-read" : "mail-mark-unread"
                    tooltip: card.unread ? i18n("Mark as read") : i18n("Mark as unread (U)")
                    onClicked: App.setRead(card.messageId, card.unread)
                }
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: card.subject
            elide: Text.ElideRight
            font.pointSize: Theme.fontXs
            font.weight: card.unread ? Font.DemiBold : Font.Normal
            color: card.unread ? Theme.textPrimary : Theme.textSecondary
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: card.snippet
            elide: Text.ElideRight
            maximumLineCount: 1
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
            visible: card.unified || card.hasAttachments || card.flagged

            Tag {
                visible: card.unified
                text: card.folderName
                tint: card.accountColor
            }
            Rectangle {
                visible: card.hasAttachments
                implicitWidth: clipRow.implicitWidth + 10
                implicitHeight: 18
                radius: 4
                color: Theme.card
                Row {
                    id: clipRow
                    anchors.centerIn: parent
                    spacing: 3
                    Kirigami.Icon {
                        width: 10
                        height: 10
                        anchors.verticalCenter: parent.verticalCenter
                        source: "mail-attachment"
                        isMask: true
                        color: Theme.textMuted
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            QQC2.AbstractButton {
                implicitWidth: 16
                implicitHeight: 16
                visible: card.flagged || card.hovered
                onClicked: App.toggleFlagged(card.messageId)
                contentItem: Kirigami.Icon {
                    source: card.flagged ? "starred-symbolic" : "non-starred-symbolic"
                    isMask: true
                    color: card.flagged ? Theme.star : Theme.textMuted
                }
            }
        }
    }
}

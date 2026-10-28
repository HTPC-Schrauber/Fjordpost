import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.mopmail.core

// Eine Zeile der Nachrichtenliste: Avatar, Absender, Datum, Betreff, Vorschau.
// Beim Überfahren erscheinen Schnellaktionen anstelle des Datums.
QQC2.ItemDelegate {
    id: delegate

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

    highlighted: ListView.isCurrentItem
    hoverEnabled: true
    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing

    onClicked: {
        ListView.view.currentIndex = index;
        ListView.view.forceActiveFocus();
    }

    // Kontofarbe als schmaler Streifen links (hilfreich in der Unified Inbox)
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: delegate.accountColor
        visible: App.selectedAccountId === 0
    }

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing

        Item {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: Kirigami.Units.gridUnit * 2.2
            Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2

            Components.Avatar {
                anchors.fill: parent
                name: delegate.fromName
            }
            Rectangle {
                visible: delegate.unread
                anchors.left: parent.left
                anchors.top: parent.top
                width: Kirigami.Units.smallSpacing * 2.5
                height: width
                radius: width / 2
                color: Kirigami.Theme.highlightColor
                border.color: Kirigami.Theme.backgroundColor
                border.width: 1
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                QQC2.Label {
                    Layout.fillWidth: true
                    text: delegate.fromName
                    elide: Text.ElideRight
                    font.bold: delegate.unread
                }
                Kirigami.Icon {
                    visible: delegate.hasAttachments && !delegate.hovered
                    source: "mail-attachment"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                Kirigami.Icon {
                    visible: delegate.flagged && !delegate.hovered
                    source: "flag-red"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    visible: !delegate.hovered
                    text: delegate.dateText
                    font: Kirigami.Theme.smallFont
                    color: delegate.unread ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                }

                // Schnellaktionen
                Row {
                    visible: delegate.hovered
                    spacing: 0
                    Repeater {
                        model: [
                            { icon: "mail-folder-archive", tip: i18n("Archive (E)"), act: () => App.archiveMessage(delegate.messageId) },
                            { icon: "edit-delete", tip: i18n("Delete (Del)"), act: () => App.deleteMessage(delegate.messageId) },
                            { icon: delegate.flagged ? "flag-red" : "flag", tip: i18n("Flag (S)"), act: () => App.toggleFlagged(delegate.messageId) },
                            { icon: delegate.unread ? "mail-mark-read" : "mail-mark-unread", tip: delegate.unread ? i18n("Mark as read") : i18n("Mark as unread (U)"), act: () => App.setRead(delegate.messageId, delegate.unread) },
                        ]
                        QQC2.ToolButton {
                            required property var modelData
                            icon.name: modelData.icon
                            display: QQC2.AbstractButton.IconOnly
                            implicitHeight: Kirigami.Units.gridUnit * 1.2
                            implicitWidth: implicitHeight
                            QQC2.ToolTip.text: modelData.tip
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: modelData.act()
                        }
                    }
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: delegate.subject
                elide: Text.ElideRight
                font.bold: delegate.unread
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: delegate.snippet
                elide: Text.ElideRight
                maximumLineCount: 1
                color: Kirigami.Theme.disabledTextColor
                font: Kirigami.Theme.smallFont
            }
        }
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Rechte Seitenleiste (Mailbird-Stil): Kontakt, Agenda, Aufgaben.
Rectangle {
    id: drawer

    readonly property var msg: App.currentMessage
    readonly property bool hasMessage: msg.id !== undefined

    signal closeRequested()

    color: Theme.sidebar

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    QQC2.ScrollView {
        anchors.fill: parent
        anchors.leftMargin: 1
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            width: drawer.width - 1
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 12
                SectionLabel {
                    Layout.fillWidth: true
                    text: i18n("Contact & Context")
                }
                IconButton {
                    iconName: "window-close"
                    iconSize: 14
                    tooltip: i18n("Close sidebar")
                    onClicked: drawer.closeRequested()
                }
            }

            // Kontaktkarte
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: contactColumn.implicitHeight + 32
                radius: Theme.radiusXl
                color: Qt.alpha(Theme.card, 0.7)
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: contactColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    InitialsAvatar {
                        Layout.alignment: Qt.AlignHCenter
                        size: 56
                        name: drawer.hasMessage ? (drawer.msg.fromName ?? "") : ""
                        initials: drawer.hasMessage ? Theme.initials(drawer.msg.fromName) : "–"
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: drawer.hasMessage ? drawer.msg.fromName : i18n("No message selected")
                        font.pointSize: Theme.fontSm
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: drawer.hasMessage
                        horizontalAlignment: Text.AlignHCenter
                        text: drawer.msg.fromEmail ?? ""
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        visible: drawer.hasMessage
                        spacing: 8
                        IconButton {
                            display: QQC2.AbstractButton.TextBesideIcon
                            iconName: "view-history"
                            iconSize: 12
                            text: i18np("%1 mail", "%1 mails", drawer.hasMessage ? App.messageCountFrom(drawer.msg.fromEmail) : 0)
                            tooltip: i18n("Show all messages from this contact")
                            onClicked: App.showMessagesFrom(drawer.msg.fromEmail)
                        }
                    }
                }
            }

            // Agenda
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8
                RowLayout {
                    SectionLabel {
                        Layout.fillWidth: true
                        text: i18n("Today's Agenda")
                    }
                    QQC2.Label {
                        text: i18n("Today")
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.accent
                    }
                }
                Placeholder {
                    text: i18n("Your appointments appear here once a calendar (CalDAV) is connected.")
                }
            }

            // Aufgaben
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                spacing: 8
                SectionLabel {
                    text: i18n("Tasks")
                }
                Placeholder {
                    text: i18n("Tasks will be synchronized with your CalDAV server in a later version.")
                }
            }
        }
    }

    component Placeholder: Rectangle {
        property alias text: label.text
        Layout.fillWidth: true
        implicitHeight: label.implicitHeight + 20
        radius: Theme.radiusLg
        color: Qt.alpha(Theme.card, 0.5)
        border.width: 1
        border.color: Theme.border
        QQC2.Label {
            id: label
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }
    }
}

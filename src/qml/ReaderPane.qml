import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.mopmail.core

// Rechte Spalte: Anzeige der gewählten Nachricht.
// TODO(M1): HTML-Darstellung über QtWebEngine (abgeschottet, externe Inhalte blockiert).
QQC2.Pane {
    id: pane

    readonly property var msg: App.currentMessage
    readonly property bool hasMessage: msg.id !== undefined

    signal replyRequested(string mode)

    padding: 0
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 6
        visible: !pane.hasMessage
        icon.name: "mail-unread"
        text: i18n("No message selected")
        explanation: i18n("Select a message from the list to read it.")
    }

    ColumnLayout {
        anchors.fill: parent
        visible: pane.hasMessage
        spacing: 0

        // Aktionsleiste
        QQC2.ToolBar {
            Layout.fillWidth: true
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            background: Item {}

            Kirigami.ActionToolBar {
                anchors.fill: parent
                actions: [
                    Kirigami.Action {
                        icon.name: "mail-reply-sender"
                        text: i18n("Reply")
                        onTriggered: pane.replyRequested("reply")
                    },
                    Kirigami.Action {
                        icon.name: "mail-reply-all"
                        text: i18n("Reply All")
                        onTriggered: pane.replyRequested("replyAll")
                    },
                    Kirigami.Action {
                        icon.name: "mail-forward"
                        text: i18n("Forward")
                        onTriggered: pane.replyRequested("forward")
                    },
                    Kirigami.Action {
                        separator: true
                    },
                    Kirigami.Action {
                        icon.name: "mail-folder-archive"
                        text: i18n("Archive")
                        displayHint: Kirigami.DisplayHint.IconOnly
                        onTriggered: App.archiveMessage(pane.msg.id)
                    },
                    Kirigami.Action {
                        icon.name: "edit-delete"
                        text: i18n("Delete")
                        displayHint: Kirigami.DisplayHint.IconOnly
                        onTriggered: App.deleteMessage(pane.msg.id)
                    },
                    Kirigami.Action {
                        icon.name: pane.msg.flagged ? "flag-red" : "flag"
                        text: pane.msg.flagged ? i18n("Remove flag") : i18n("Flag")
                        displayHint: Kirigami.DisplayHint.IconOnly
                        onTriggered: App.toggleFlagged(pane.msg.id)
                    },
                    Kirigami.Action {
                        icon.name: "mail-mark-unread"
                        text: i18n("Mark as unread")
                        displayHint: Kirigami.DisplayHint.AlwaysHide
                        onTriggered: App.setRead(pane.msg.id, false)
                    }
                ]
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Kopfbereich
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 1
                text: pane.msg.subject ?? ""
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                Components.Avatar {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                    name: pane.msg.fromName ?? ""
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: "<b>%1</b> &lt;%2&gt;".arg(pane.msg.fromName ?? "").arg(pane.msg.fromEmail ?? "")
                        textFormat: Text.StyledText
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("To: %1", pane.msg.to ?? "")
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }

                ColumnLayout {
                    spacing: 0
                    QQC2.Label {
                        Layout.alignment: Qt.AlignRight
                        text: pane.msg.dateText ?? ""
                        color: Kirigami.Theme.disabledTextColor
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: Kirigami.Units.smallSpacing
                        Rectangle {
                            Layout.preferredWidth: Kirigami.Units.smallSpacing * 2
                            Layout.preferredHeight: Kirigami.Units.smallSpacing * 2
                            radius: Kirigami.Units.smallSpacing
                            color: pane.msg.accountColor ?? "transparent"
                        }
                        QQC2.Label {
                            text: pane.msg.accountName ?? ""
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
        }

        // Nachrichtentext
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QQC2.TextArea {
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                text: pane.msg.body ?? ""
                leftPadding: Kirigami.Units.gridUnit
                rightPadding: Kirigami.Units.gridUnit
                topPadding: Kirigami.Units.gridUnit
                background: Item {}
            }
        }
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtCore
import QtQuick.Dialogs
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.mopmail.core

// Rechte Spalte: Anzeige der gewählten Nachricht.
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
                        shortcut: "Ctrl+R"
                        onTriggered: pane.replyRequested("reply")
                    },
                    Kirigami.Action {
                        icon.name: "mail-reply-all"
                        text: i18n("Reply All")
                        shortcut: "Ctrl+Shift+R"
                        onTriggered: pane.replyRequested("replyAll")
                    },
                    Kirigami.Action {
                        icon.name: "mail-forward"
                        text: i18n("Forward")
                        shortcut: "Ctrl+L"
                        onTriggered: pane.replyRequested("forward")
                    },
                    Kirigami.Action {
                        separator: true
                    },
                    Kirigami.Action {
                        icon.name: "archive-insert"
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
                    },
                    Kirigami.Action {
                        icon.name: "mail-mark-junk"
                        text: i18n("Move to spam")
                        displayHint: Kirigami.DisplayHint.AlwaysHide
                        onTriggered: App.moveToRole(pane.msg.id, "junk")
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
                        text: "<b>%1</b> &lt;%2&gt;".arg(String(pane.msg.fromName ?? "").replace(/</g, "&lt;")).arg(pane.msg.fromEmail ?? "")
                        textFormat: Text.StyledText
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("To: %1", pane.msg.to ?? "")
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: (pane.msg.cc ?? "").length > 0
                        text: i18n("Cc: %1", pane.msg.cc ?? "")
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

            // Anhänge
            Flow {
                Layout.fillWidth: true
                visible: attachmentRepeater.count > 0
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    id: attachmentRepeater
                    model: pane.msg.attachments ?? []

                    QQC2.Button {
                        required property var modelData
                        icon.name: "mail-attachment"
                        text: "%1 (%2)".arg(modelData.name).arg(modelData.size)
                        QQC2.ToolTip.text: i18n("Open attachment; right-click to save")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: App.openAttachment(pane.msg.id, modelData.index)

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                saveDialog.attachmentIndex = parent.modelData.index;
                                saveDialog.selectedFile = saveDialog.currentFolder + "/" + parent.modelData.name;
                                saveDialog.open();
                            }
                        }
                    }
                }
            }
        }

        // Hinweis auf blockierte externe Inhalte
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit
            Layout.rightMargin: Kirigami.Units.gridUnit
            visible: (pane.msg.hasRemoteContent ?? false) && !App.remoteContentAllowed
            type: Kirigami.MessageType.Information
            text: i18n("Remote images were blocked to protect your privacy.")
            actions: Kirigami.Action {
                text: i18n("Load images")
                icon.name: "view-preview"
                onTriggered: App.allowRemoteContent()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
        }

        // Nachrichtentext
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            WebEngineView {
                id: web
                anchors.fill: parent
                visible: !(pane.msg.loading ?? false)

                settings.javascriptEnabled: false
                settings.javascriptCanOpenWindows: false
                settings.localContentCanAccessRemoteUrls: false
                settings.localContentCanAccessFileUrls: false
                settings.pluginsEnabled: false
                settings.autoLoadIconsForPage: false
                settings.focusOnNavigationEnabled: false
                backgroundColor: Kirigami.Theme.backgroundColor

                // Links nie in der Nachrichtenansicht öffnen, sondern im Browser.
                onNavigationRequested: request => {
                    if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation) {
                        Qt.openUrlExternally(request.url);
                        request.reject();
                    }
                }
                onNewWindowRequested: request => Qt.openUrlExternally(request.requestedUrl)

                function show() {
                    loadHtml(pane.msg.html ?? "", "about:blank");
                }
            }

            Connections {
                target: App
                function onCurrentMessageChanged() {
                    if (pane.hasMessage && !(pane.msg.loading ?? false))
                        web.show();
                }
            }

            QQC2.BusyIndicator {
                anchors.centerIn: parent
                running: pane.msg.loading ?? false
                visible: running
            }
        }
    }

    FileDialog {
        id: saveDialog
        property int attachmentIndex: -1
        fileMode: FileDialog.SaveFile
        currentFolder: StandardPaths.writableLocation(StandardPaths.DownloadLocation)
        onAccepted: App.saveAttachment(pane.msg.id, attachmentIndex, selectedFile)
    }
}

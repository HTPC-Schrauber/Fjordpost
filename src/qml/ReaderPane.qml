import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import QtWebEngine
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Lesebereich: Aktionsleiste, Betreff, Absenderkarte, Inhalt, Anhänge, Schnellantwort.
Rectangle {
    id: pane

    readonly property var msg: App.currentMessage
    readonly property bool hasMessage: msg.id !== undefined
    property int position: -1
    property int total: 0
    property bool drawerVisible: true

    signal replyRequested(string mode)
    signal navigate(int delta)
    signal toggleDrawer()
    signal notify(string message)

    color: Theme.reading

    // Klartext-Mails im Theme darstellen
    function pushReaderColors() {
        App.setReaderColors(String(Theme.reading), String(Theme.textPrimary), String(Theme.accent), String(Theme.textMuted));
    }
    readonly property string readerColorKey: [Theme.reading, Theme.textPrimary, Theme.accent, Theme.textMuted].join()
    onReaderColorKeyChanged: pushReaderColors()
    Component.onCompleted: pushReaderColors()

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Aktionsleiste
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: Theme.reading
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 4
                enabled: pane.hasMessage

                IconButton {
                    text: i18n("Reply")
                    display: QQC2.AbstractButton.TextBesideIcon
                    iconName: "mail-reply-sender"
                    iconSize: 14
                    tooltip: i18n("Reply (Ctrl+R)")
                    onClicked: pane.replyRequested("reply")
                }
                IconButton {
                    iconName: "mail-reply-all"
                    tooltip: i18n("Reply All (Ctrl+Shift+R)")
                    onClicked: pane.replyRequested("replyAll")
                }
                IconButton {
                    iconName: "mail-forward"
                    tooltip: i18n("Forward (Ctrl+L)")
                    onClicked: pane.replyRequested("forward")
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 16
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    color: Theme.border
                }
                IconButton {
                    iconName: "archive-insert"
                    hoverColor: Theme.success
                    tooltip: i18n("Archive (E)")
                    onClicked: App.archiveMessage(pane.msg.id)
                }
                IconButton {
                    iconName: "chronometer"
                    hoverColor: Theme.warning
                    tooltip: i18n("Snooze (coming in a later version)")
                    onClicked: pane.notify(i18n("Snoozing will be available in a later version."))
                }
                IconButton {
                    iconName: "edit-delete"
                    hoverColor: Theme.danger
                    tooltip: i18n("Delete (Del)")
                    onClicked: App.deleteMessage(pane.msg.id)
                }
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 16
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    color: Theme.border
                }
                IconButton {
                    iconName: "mail-mark-unread"
                    tooltip: i18n("Mark as unread (U)")
                    onClicked: App.setRead(pane.msg.id, false)
                }
                IconButton {
                    iconName: "mail-mark-junk"
                    tooltip: i18n("Move to spam")
                    onClicked: App.moveToRole(pane.msg.id, "junk")
                }

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Label {
                    visible: pane.position >= 0
                    text: i18nc("position in list", "%1 of %2", pane.position + 1, pane.total)
                    font.family: "monospace"
                    font.pointSize: Theme.fontXxs
                    color: Theme.textMuted
                }
                Rectangle {
                    implicitWidth: navRow.implicitWidth + 2
                    implicitHeight: 28
                    radius: Theme.radius
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.border
                    Row {
                        id: navRow
                        anchors.centerIn: parent
                        IconButton {
                            iconName: "go-up"
                            iconSize: 14
                            tooltip: i18n("Previous message (K)")
                            onClicked: pane.navigate(-1)
                        }
                        IconButton {
                            iconName: "go-down"
                            iconSize: 14
                            tooltip: i18n("Next message (J)")
                            onClicked: pane.navigate(1)
                        }
                    }
                }
                IconButton {
                    enabled: true
                    iconName: "view-split-left-right"
                    active: pane.drawerVisible
                    tooltip: i18n("Toggle contact sidebar")
                    onClicked: pane.toggleDrawer()
                }
            }
        }

        // Platzhalter ohne Auswahl
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !pane.hasMessage
            Item {
                Layout.fillHeight: true
            }
            Kirigami.Icon {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                source: "mail-unread"
                isMask: true
                color: Theme.textMuted
            }
            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                text: i18n("No message selected")
                font.pointSize: Theme.fontSm
                font.weight: Font.Medium
                color: Theme.textSecondary
            }
            QQC2.Label {
                Layout.alignment: Qt.AlignHCenter
                text: i18n("Select a message from the list to read it.")
                font.pointSize: Theme.fontXs
                color: Theme.textMuted
            }
            Item {
                Layout.fillHeight: true
            }
        }

        // Kopfbereich der Nachricht
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.topMargin: 24
            spacing: 12
            visible: pane.hasMessage

            RowLayout {
                Layout.fillWidth: true
                spacing: 16
                QQC2.Label {
                    Layout.fillWidth: true
                    text: pane.msg.subject ?? ""
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.pointSize: Theme.fontLg
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                IconButton {
                    Layout.alignment: Qt.AlignTop
                    iconName: (pane.msg.flagged ?? false) ? "starred-symbolic" : "non-starred-symbolic"
                    iconColor: (pane.msg.flagged ?? false) ? Theme.star : Theme.textMuted
                    hoverColor: Theme.star
                    tooltip: (pane.msg.flagged ?? false) ? i18n("Remove flag (S)") : i18n("Flag (S)")
                    onClicked: App.toggleFlagged(pane.msg.id)
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8
                Tag {
                    text: pane.msg.folderName ?? ""
                    visible: text.length > 0
                    outlined: false
                }
                Tag {
                    text: pane.msg.accountName ?? ""
                    tint: pane.msg.accountColor ?? Theme.accent
                }
            }

            // Absenderkarte
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: senderRow.implicitHeight + 32
                radius: Theme.radiusXl
                color: Qt.alpha(Theme.card, 0.6)
                border.width: 1
                border.color: Theme.border

                RowLayout {
                    id: senderRow
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 14

                    InitialsAvatar {
                        Layout.alignment: Qt.AlignTop
                        name: pane.msg.fromName ?? ""
                        size: 40
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            QQC2.Label {
                                text: pane.msg.fromName ?? ""
                                font.pointSize: Theme.fontSm
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: implicitWidth
                                Layout.fillWidth: true
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: "<%1>".arg(pane.msg.fromEmail ?? "")
                                textFormat: Text.PlainText
                                font.family: "monospace"
                                font.pointSize: Theme.fontXxs
                                color: Theme.textMuted
                                elide: Text.ElideRight
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("To: %1", pane.msg.to ?? "")
                            textFormat: Text.PlainText
                            font.pointSize: Theme.fontXs
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            visible: (pane.msg.cc ?? "").length > 0
                            text: i18n("Cc: %1", pane.msg.cc ?? "")
                            textFormat: Text.PlainText
                            font.pointSize: Theme.fontXs
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                    }
                    QQC2.Label {
                        Layout.alignment: Qt.AlignTop
                        text: pane.msg.dateText ?? ""
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.textSecondary
                    }
                }
            }

            // Hinweis auf blockierte externe Inhalte
            Rectangle {
                Layout.fillWidth: true
                visible: (pane.msg.hasRemoteContent ?? false) && !App.remoteContentAllowed
                implicitHeight: remoteRow.implicitHeight + 16
                radius: Theme.radius
                color: Qt.alpha(Theme.warning, 0.1)
                border.width: 1
                border.color: Qt.alpha(Theme.warning, 0.3)
                RowLayout {
                    id: remoteRow
                    anchors.fill: parent
                    anchors.margins: 8
                    anchors.leftMargin: 12
                    Kirigami.Icon {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        source: "security-medium"
                        isMask: true
                        color: Theme.warning
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: i18n("Remote images were blocked to protect your privacy.")
                        font.pointSize: Theme.fontXs
                        color: Theme.textPrimary
                        wrapMode: Text.Wrap
                    }
                    IconButton {
                        text: i18n("Load images")
                        display: QQC2.AbstractButton.TextOnly
                        onClicked: App.allowRemoteContent()
                    }
                }
            }
        }

        // Nachrichtentext
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            visible: pane.hasMessage

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
                backgroundColor: Theme.reading

                // Links nie in der Nachrichtenansicht öffnen, sondern im Browser.
                onNavigationRequested: request => {
                    if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation) {
                        Qt.openUrlExternally(request.url);
                        request.reject();
                    }
                }
                onNewWindowRequested: request => Qt.openUrlExternally(request.requestedUrl)
            }

            Connections {
                target: App
                function onCurrentMessageChanged() {
                    if (pane.hasMessage && !(pane.msg.loading ?? false))
                        web.loadHtml(pane.msg.html ?? "", "about:blank");
                }
            }

            QQC2.BusyIndicator {
                anchors.centerIn: parent
                running: pane.msg.loading ?? false
                visible: running
            }
        }

        // Anhänge
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 28
            Layout.rightMargin: 28
            Layout.bottomMargin: 12
            visible: pane.hasMessage && attachmentRepeater.count > 0
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.border
            }
            RowLayout {
                Layout.fillWidth: true
                SectionLabel {
                    Layout.fillWidth: true
                    text: i18np("%1 attachment", "%1 attachments", attachmentRepeater.count)
                }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: pane.width > 700 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                Repeater {
                    id: attachmentRepeater
                    model: pane.msg.attachments ?? []

                    QQC2.AbstractButton {
                        id: attachment
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 52
                        hoverEnabled: true
                        onClicked: App.openAttachment(pane.msg.id, modelData.index)

                        background: Rectangle {
                            radius: Theme.radiusLg
                            color: attachment.hovered ? Theme.card : Qt.alpha(Theme.card, 0.7)
                            border.width: 1
                            border.color: Theme.border
                        }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                Layout.leftMargin: 10
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 32
                                radius: Theme.radius
                                color: Qt.alpha(Theme.accent, 0.15)
                                Kirigami.Icon {
                                    anchors.centerIn: parent
                                    width: 16
                                    height: 16
                                    source: "mail-attachment"
                                    isMask: true
                                    color: Theme.accent
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: attachment.modelData.name
                                    elide: Text.ElideMiddle
                                    font.pointSize: Theme.fontXs
                                    font.weight: Font.Medium
                                    color: attachment.hovered ? Theme.accent : Theme.textPrimary
                                }
                                QQC2.Label {
                                    text: attachment.modelData.size
                                    font.family: "monospace"
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textMuted
                                }
                            }
                            IconButton {
                                Layout.rightMargin: 8
                                iconName: "document-save"
                                iconSize: 14
                                tooltip: i18n("Save attachment")
                                onClicked: {
                                    saveDialog.attachmentIndex = attachment.modelData.index;
                                    saveDialog.selectedFile = saveDialog.currentFolder + "/" + attachment.modelData.name;
                                    saveDialog.open();
                                }
                            }
                        }
                    }
                }
            }
        }

        // Schnellantwort
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: quickBox.implicitHeight + 32
            visible: pane.hasMessage
            color: Theme.reading
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }

            Rectangle {
                id: quickBox
                anchors.fill: parent
                anchors.margins: 16
                implicitHeight: quickColumn.implicitHeight + 24
                radius: Theme.radiusXl
                color: Qt.alpha(Theme.card, 0.9)
                border.width: 1
                border.color: quickText.activeFocus ? Qt.alpha(Theme.accent, 0.7) : Theme.border

                ColumnLayout {
                    id: quickColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            source: "mail-reply-sender"
                            isMask: true
                            color: Theme.accent
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: i18n("Quick reply to <b>%1</b>", String(pane.msg.fromName ?? "").replace(/</g, "&lt;"))
                            textFormat: Text.StyledText
                            font.pointSize: Theme.fontXs
                            color: Theme.textMuted
                            elide: Text.ElideRight
                        }
                        IconButton {
                            iconName: "view-fullscreen"
                            iconSize: 14
                            tooltip: i18n("Open in composer")
                            onClicked: pane.replyRequested("reply")
                        }
                    }

                    QQC2.TextArea {
                        id: quickText
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(Math.max(44, implicitHeight), 160)
                        placeholderText: i18n("Write a message… (Ctrl+Enter to send)")
                        placeholderTextColor: Theme.textMuted
                        color: Theme.textPrimary
                        wrapMode: TextEdit.Wrap
                        font.pointSize: Theme.fontXs
                        padding: 0
                        background: Item {}
                        Keys.onPressed: event => {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                                sendButton.clicked();
                                event.accepted = true;
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.border
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        IconButton {
                            iconName: "mail-attachment"
                            iconSize: 14
                            tooltip: i18n("Attach a file (opens the composer)")
                            onClicked: pane.replyRequested("reply")
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        AccentButton {
                            id: sendButton
                            text: i18n("Send")
                            iconName: "document-send"
                            iconAfterText: true
                            enabled: quickText.text.trim().length > 0
                            onClicked: {
                                const error = App.sendQuickReply(quickText.text);
                                if (error.length > 0) {
                                    pane.notify(error);
                                    return;
                                }
                                quickText.clear();
                                pane.notify(i18n("Reply queued for sending."));
                            }
                        }
                    }
                }
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

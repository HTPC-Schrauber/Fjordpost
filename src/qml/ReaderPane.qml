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
    signal newLabelRequested(var messageId)

    function openLabelMenu() {
        if (hasMessage)
            labelMenu.openFor(msg.id, labelButton);
    }

    LabelMenu {
        id: labelMenu
        onNewLabelRequested: id => pane.newLabelRequested(id)
    }

    color: Theme.reading

    // Klartext-Mails im Theme darstellen
    // Nur für --dev-action thread-history
    function showWholeConversation() {
        conversation.showAll = true;
    }

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
                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 16
                    Layout.leftMargin: 6
                    Layout.rightMargin: 6
                    color: Theme.border
                }
                IconButton {
                    id: labelButton
                    iconName: "tag"
                    tooltip: i18n("Labels (L)")
                    onClicked: pane.openLabelMenu()
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
                Repeater {
                    model: pane.msg.labels ?? []
                    Tag {
                        required property var modelData
                        text: modelData.name
                        tint: modelData.color.length > 0 ? modelData.color : Theme.textSecondary
                    }
                }
            }

            // Konversation: alle Nachrichten kompakt, ein Klick zeigt die jeweilige an.
            // Lange Verläufe zeigen nur Anfang, Ende und die aktuelle Nachricht.
            ColumnLayout {
                id: conversation
                readonly property var entries: pane.msg.thread ?? []
                readonly property int maxRows: 6
                property bool showAll: false
                // Neue Konversation: wieder zusammengefasst beginnen
                readonly property var threadKey: entries.length > 0 ? entries[0].id : 0
                onThreadKeyChanged: showAll = false

                readonly property var items: {
                    const e = entries;
                    if (e.length <= 4)
                        return e.map(entry => ({ entry: entry, hidden: 0 }));
                    if (showAll) {
                        // Einklappen an derselben Stelle, an der vorher "N weitere" stand
                        const all = e.map(entry => ({ entry: entry, hidden: 0 }));
                        all.splice(1, 0, { entry: null, hidden: 0 });
                        return all;
                    }
                    const current = e.findIndex(entry => entry.current);
                    const keep = [0, e.length - 2, e.length - 1, current];
                    const result = [];
                    let hidden = 0;
                    for (let i = 0; i < e.length; ++i) {
                        if (!keep.includes(i)) {
                            ++hidden;
                            continue;
                        }
                        if (hidden > 0)
                            result.push({ entry: null, hidden: hidden });
                        hidden = 0;
                        result.push({ entry: e[i], hidden: 0 });
                    }
                    return result;
                }

                Layout.fillWidth: true
                visible: entries.length > 1
                spacing: 6

                QQC2.AbstractButton {
                    id: conversationHeader
                    hoverEnabled: true
                    onClicked: Prefs.view.threadPanelOpen = !Prefs.view.threadPanelOpen
                    contentItem: Row {
                        spacing: 6
                        SectionLabel {
                            anchors.verticalCenter: parent.verticalCenter
                            text: i18np("Conversation · %1 message", "Conversation · %1 messages", conversation.entries.length)
                            color: conversationHeader.hovered ? Theme.textPrimary : Theme.textMuted
                        }
                        Kirigami.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10
                            height: 10
                            source: Prefs.view.threadPanelOpen ? "arrow-up" : "arrow-down"
                            isMask: true
                            color: conversationHeader.hovered ? Theme.textPrimary : Theme.textMuted
                        }
                    }
                    background: Item {}
                }

                ListView {
                    id: threadList
                    readonly property real rowHeight: 34
                    visible: Prefs.view.threadPanelOpen
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(count, conversation.maxRows) * rowHeight
                    clip: true
                    interactive: count > conversation.maxRows
                    boundsBehavior: Flickable.StopAtBounds
                    model: conversation.items
                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                        id: threadScrollBar
                        policy: threadList.count > conversation.maxRows ? QQC2.ScrollBar.AsNeeded : QQC2.ScrollBar.AlwaysOff
                    }
                    // Beim Auf-/Zuklappen oben bleiben, damit der Knopf sichtbar bleibt;
                    // nur bei einer anderen Nachricht zur aktuellen scrollen.
                    property var positionedFor: undefined
                    onModelChanged: {
                        const id = pane.msg.id;
                        if (id === positionedFor) {
                            positionViewAtBeginning();
                            return;
                        }
                        positionedFor = id;
                        const current = conversation.items.findIndex(item => item.entry && item.entry.current);
                        if (current >= 0)
                            positionViewAtIndex(current, ListView.Contain);
                    }

                    delegate: QQC2.ItemDelegate {
                        id: entry
                        required property var modelData
                        readonly property var data_: modelData.entry
                        readonly property bool isMore: data_ === null
                        readonly property bool isCurrent: !isMore && data_.current
                        // Platz für die Scrollleiste lassen, sonst verdeckt sie das Datum
                        width: ListView.view.width - (threadScrollBar.visible ? threadScrollBar.width + 4 : 0)
                        height: threadList.rowHeight
                        hoverEnabled: true
                        leftPadding: 10
                        rightPadding: 10
                        topPadding: 0
                        bottomPadding: 0
                        onClicked: {
                            if (isMore)
                                conversation.showAll = !conversation.showAll;
                            else if (!isCurrent)
                                App.openMessage(data_.id);
                        }
                        background: Rectangle {
                            radius: Theme.radius
                            color: entry.isCurrent ? Qt.alpha(Theme.accent, Theme.dark ? 0.11 : 0.08) : (entry.hovered ? Qt.alpha(Theme.card, 0.6) : "transparent")
                            Rectangle {
                                visible: entry.isCurrent
                                width: 3
                                height: parent.height - 12
                                anchors.verticalCenter: parent.verticalCenter
                                radius: 1.5
                                color: Theme.accent
                            }
                        }
                        contentItem: Item {
                            // "N weitere Nachrichten"
                            RowLayout {
                                anchors.fill: parent
                                visible: entry.isMore
                                spacing: 8
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: Theme.border
                                }
                                Rectangle {
                                    implicitWidth: moreLabel.implicitWidth + 20
                                    implicitHeight: moreLabel.implicitHeight + 6
                                    radius: height / 2
                                    color: entry.hovered ? Qt.alpha(Theme.accent, 0.18) : Theme.card
                                    border.width: 1
                                    border.color: Theme.border
                                    QQC2.Label {
                                        id: moreLabel
                                        anchors.centerIn: parent
                                        text: conversation.showAll ? i18n("Show fewer messages") : i18np("%1 more message", "%1 more messages", entry.modelData.hidden)
                                        font.pointSize: Theme.fontXxs
                                        color: entry.hovered ? Theme.accent : Theme.textSecondary
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: Theme.border
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                visible: !entry.isMore
                                spacing: 10
                                InitialsAvatar {
                                    name: entry.isMore ? "" : entry.data_.fromName
                                    size: 22
                                }
                                QQC2.Label {
                                    Layout.preferredWidth: Math.min(Math.ceil(implicitWidth) + 1, entry.width * 0.3)
                                    text: entry.isMore ? "" : entry.data_.fromName
                                    elide: Text.ElideRight
                                    font.pointSize: Theme.fontXs
                                    font.weight: !entry.isMore && (entry.data_.unread || entry.isCurrent) ? Font.DemiBold : Font.Medium
                                    color: !entry.isMore && (entry.isCurrent || entry.data_.unread) ? Theme.textPrimary : Theme.textSecondary
                                }
                                Kirigami.Icon {
                                    visible: !entry.isMore && entry.data_.sent
                                    Layout.preferredWidth: 12
                                    Layout.preferredHeight: 12
                                    source: "mail-sent"
                                    isMask: true
                                    color: Theme.textMuted
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: entry.isMore ? "" : entry.data_.snippet
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textMuted
                                }
                                Rectangle {
                                    visible: !entry.isMore && entry.data_.unread
                                    Layout.preferredWidth: 6
                                    Layout.preferredHeight: 6
                                    radius: 3
                                    color: Theme.accent
                                }
                                QQC2.Label {
                                    text: entry.isMore ? "" : entry.data_.dateText
                                    font.family: "monospace"
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
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
                                // aufrunden, sonst wird der Name wegen Pixelbruchteilen gekürzt
                                Layout.maximumWidth: Math.ceil(implicitWidth) + 1
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

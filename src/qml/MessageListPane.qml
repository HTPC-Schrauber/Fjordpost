import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import Qt.labs.qmlmodels
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Mittlere Spalte: Titel, Suche, Filter-Pills und Nachrichtenkarten.
Rectangle {
    id: pane

    readonly property alias list: messageList

    function openFirst() {
        messageList.currentIndex = 0;
    }

    // Nur für --dev-action open-thread/expand-thread: größte Konversation öffnen
    function openFirstConversation(expand) {
        let best = -1;
        for (let i = 0; i < messageList.count; ++i) {
            const item = messageList.itemAtIndex(i);
            if (item && item.threadCount > 1 && (best < 0 || item.threadCount > messageList.itemAtIndex(best).threadCount))
                best = i;
        }
        if (best < 0)
            return;
        messageList.currentIndex = best;
        if (expand)
            App.messages.toggleThread(messageList.currentItem.messageId);
    }

    function focusSearch() {
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function step(delta) {
        const next = messageList.currentIndex + delta;
        if (next >= 0 && next < messageList.count)
            messageList.currentIndex = next;
    }

    color: Theme.list

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                QQC2.Label {
                    Layout.maximumWidth: pane.width - 140
                    text: App.searchText.length > 0 ? i18n("Search results") : App.selectionTitle
                    font.pointSize: Theme.fontSm
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                Rectangle {
                    implicitWidth: countLabel.implicitWidth + 10
                    implicitHeight: countLabel.implicitHeight + 4
                    radius: 4
                    color: Theme.card
                    QQC2.Label {
                        id: countLabel
                        anchors.centerIn: parent
                        text: messageList.count
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.textSecondary
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    iconName: "mail-mark-read"
                    iconSize: 14
                    tooltip: i18n("Mark all as read")
                    onClicked: App.markAllRead()
                }
            }

            SearchBox {
                id: searchField
                Layout.fillWidth: true
                placeholderText: App.selectedAccountId === 0 && App.selectedFolderId === 0 ? i18n("Search all mail…") : i18n("Search in %1…", App.selectionTitle)
                text: App.searchText
                onSearchRequested: query => App.searchText = query
                Keys.onDownPressed: messageList.forceActiveFocus()
                Keys.onEscapePressed: {
                    clear();
                    App.searchText = "";
                    messageList.forceActiveFocus();
                }
            }

            RowLayout {
                spacing: 6
                Pill {
                    text: i18n("All")
                    selected: App.quickFilter === ""
                    onClicked: App.quickFilter = ""
                }
                Pill {
                    text: i18n("Unread")
                    selected: App.quickFilter === "unread"
                    onClicked: App.quickFilter = "unread"
                }
                Pill {
                    text: i18n("Flagged")
                    selected: App.quickFilter === "flagged"
                    onClicked: App.quickFilter = "flagged"
                }
                Pill {
                    text: i18n("Attachment")
                    iconName: "mail-attachment"
                    selected: App.quickFilter === "attachments"
                    onClicked: App.quickFilter = "attachments"
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.border
        }

        ListView {
            id: messageList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: App.messages
            clip: true
            currentIndex: -1
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            highlightMoveDuration: 0

            QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                width: 6
                contentItem: Rectangle {
                    implicitWidth: 5
                    radius: 3
                    color: Qt.alpha(Theme.textMuted, parent.hovered ? 0.4 : 0.2)
                }
                background: Item {}
            }

            delegate: DelegateChooser {
                role: "isChild"
                DelegateChoice {
                    roleValue: false
                    MessageDelegate {
                        width: ListView.view.width
                    }
                }
                DelegateChoice {
                    roleValue: true
                    ThreadChildDelegate {
                        width: ListView.view.width
                    }
                }
            }

            section.property: "dateGroup"
            section.criteria: ViewSection.FullString
            section.labelPositioning: ViewSection.InlineLabels | ViewSection.CurrentLabelAtStart
            section.delegate: Rectangle {
                // Kein "required property section": Abschnittsköpfe werden wiederverwendet,
                // eine required property behielte dann den alten Wert.
                readonly property string sectionName: section
                width: ListView.view.width
                height: 26
                color: Qt.alpha(Theme.sidebar, 0.95)
                SectionLabel {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.sectionName
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Theme.border
                }
            }

            onCurrentIndexChanged: {
                // Verschiebt sich nur die Position (neue Mails oben), nicht neu öffnen.
                if (currentItem && currentItem.messageId !== App.currentMessage.id)
                    App.openMessage(currentItem.messageId);
            }

            Connections {
                target: App
                function onSelectionChanged() {
                    messageList.currentIndex = -1;
                }
                function onSearchTextChanged() {
                    messageList.currentIndex = -1;
                }
                function onQuickFilterChanged() {
                    messageList.currentIndex = -1;
                }
            }

            Keys.onDeletePressed: {
                if (currentItem)
                    App.deleteMessage(currentItem.messageId);
            }
            Keys.onPressed: event => {
                if (event.key === Qt.Key_J) {
                    pane.step(1);
                    event.accepted = true;
                    return;
                }
                if (event.key === Qt.Key_K) {
                    pane.step(-1);
                    event.accepted = true;
                    return;
                }
                if (!currentItem)
                    return;
                if (event.key === Qt.Key_E) {
                    App.archiveMessage(currentItem.messageId);
                    event.accepted = true;
                } else if (event.key === Qt.Key_S) {
                    App.toggleFlagged(currentItem.messageId);
                    event.accepted = true;
                } else if (event.key === Qt.Key_U) {
                    App.setRead(currentItem.messageId, false);
                    event.accepted = true;
                }
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 60
                visible: messageList.count === 0
                spacing: 6
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: App.searchText.length > 0 || App.quickFilter.length > 0 ? i18n("No messages found") : i18n("No messages here")
                    font.pointSize: Theme.fontXs
                    font.weight: Font.Medium
                    color: Theme.textSecondary
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    text: App.searchText.length > 0 || App.quickFilter.length > 0 ? i18n("Adjust your search or filter.") : i18n("This folder is empty.")
                    font.pointSize: Theme.fontXxs
                    color: Theme.textMuted
                }
            }
        }
    }
}

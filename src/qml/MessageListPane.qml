import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Mittlere Spalte: Titel, Suche und Nachrichtenliste.
QQC2.Pane {
    id: pane

    function openFirst() {
        messageList.currentIndex = 0;
    }

    function focusSearch() {
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    padding: 0
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: App.searchText.length > 0 ? i18n("Search results") : App.selectionTitle
                    elide: Text.ElideRight
                }
                QQC2.Label {
                    text: i18np("%1 message", "%1 messages", messageList.count)
                    color: Kirigami.Theme.disabledTextColor
                }
            }

            Kirigami.SearchField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: i18n("Search all mail…")
                delaySearch: true
                text: App.searchText
                onAccepted: App.searchText = text
                Keys.onDownPressed: messageList.forceActiveFocus()
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: messageList
                model: App.messages
                clip: true
                currentIndex: -1
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                reuseItems: true

                delegate: MessageDelegate {
                    width: ListView.view.width
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
                }

                Keys.onDeletePressed: {
                    if (currentItem)
                        App.deleteMessage(currentItem.messageId);
                }
                Keys.onPressed: event => {
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

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: messageList.count === 0
                    icon.name: App.searchText.length > 0 ? "edit-find" : "mail-folder-inbox"
                    text: App.searchText.length > 0 ? i18n("No messages found") : i18n("No messages here")
                    explanation: App.searchText.length > 0 ? "" : i18n("This folder is empty.")
                }
            }
        }
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import de.fjordblau.fjordpost.core
import de.fjordblau.fjordpost.app

// Kalender, Aufgabenlisten oder Adressbücher mit Sichtbarkeitsschalter.
ColumnLayout {
    id: list

    property string kind: "events" // "events", "todos", "contacts"
    property string title
    readonly property var entries: {
        App.pim.revision;
        return App.pim.collections(kind);
    }

    function create() {
        collectionDialog.openCreate(Theme.labelColors[entries.length % Theme.labelColors.length]);
    }
    function edit(entry) {
        collectionDialog.openEdit(entry.id, entry.name, entry.ownColor);
    }
    function remove(entry) {
        collectionDialog.openDelete(entry.id, entry.name);
    }

    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        spacing: 4
        SectionLabel {
            Layout.fillWidth: true
            text: list.title
        }
        IconButton {
            visible: App.pim.hasDavAccounts
            implicitWidth: 22
            implicitHeight: 22
            iconName: "list-add"
            iconSize: 14
            tooltip: list.kind === "contacts" ? i18n("New Address Book…") : list.kind === "todos" ? i18n("New Task List…") : i18n("New Calendar…")
            onClicked: list.create()
        }
    }

    Repeater {
        model: list.entries
        delegate: QQC2.AbstractButton {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: 28
            hoverEnabled: true
            Accessible.name: modelData.name
            onClicked: App.pim.setCollectionVisible(modelData.id, !modelData.visible)
            TapHandler {
                acceptedButtons: Qt.RightButton
                enabled: !modelData.readOnly
                onTapped: entryMenu.openFor(modelData)
            }
            background: Rectangle {
                radius: Theme.radiusSm
                color: parent.hovered ? Theme.card : "transparent"
            }
            contentItem: RowLayout {
                spacing: 8
                Rectangle {
                    Layout.leftMargin: 6
                    implicitWidth: 13
                    implicitHeight: 13
                    radius: 4
                    color: modelData.visible ? modelData.color : "transparent"
                    border.width: 2
                    border.color: modelData.color
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: modelData.name
                    elide: Text.ElideRight
                    font.pointSize: Theme.fontXs
                    color: modelData.visible ? Theme.textPrimary : Theme.textMuted
                }
                IconButton {
                    visible: !modelData.readOnly && (parent.parent.hovered || entryMenu.visible && entryMenu.entry === modelData)
                    implicitWidth: 22
                    implicitHeight: 22
                    iconName: "overflow-menu"
                    iconSize: 14
                    tooltip: i18n("More Actions")
                    onClicked: entryMenu.openFor(modelData)
                }
            }
        }
    }

    QQC2.Menu {
        id: entryMenu
        property var entry: null
        function openFor(e) {
            entry = e;
            popup();
        }
        QQC2.MenuItem {
            text: i18n("Edit…")
            icon.name: "edit-rename"
            onTriggered: list.edit(entryMenu.entry)
        }
        QQC2.MenuItem {
            text: i18n("Delete…")
            icon.name: "edit-delete"
            onTriggered: collectionDialog.openDelete(entryMenu.entry.id, entryMenu.entry.name)
        }
    }

    CollectionDialog {
        id: collectionDialog
        parent: QQC2.Overlay.overlay
        kind: list.kind
    }
}

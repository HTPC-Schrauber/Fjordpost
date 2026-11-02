import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import de.fjordblau.fjordpost.app

// Kalender, Aufgabenliste oder Adressbuch anlegen, bearbeiten oder löschen.
// Das Ergebnis kommt vom Server; bei einer Ablehnung bleibt der Dialog mit der Meldung offen.
ThemedDialog {
    id: dialog

    property string kind: "events" // "events", "todos", "contacts"
    property string mode: "create" // "create", "edit", "delete"
    property var collectionId: 0
    property string collectionName
    property string selectedColor
    // Eine Farbe von einem anderen Client bleibt wählbar.
    readonly property var colors: {
        const known = Theme.labelColors.some(c => c.toLowerCase() === selectedColor.toLowerCase());
        return known || selectedColor.length === 0 ? Theme.labelColors : [selectedColor].concat(Theme.labelColors);
    }
    property var accounts: []
    property bool busy: false

    function prepare(newMode, id, name, color) {
        mode = newMode;
        collectionId = id;
        collectionName = name;
        selectedColor = color;
        busy = false;
        errorMessage.visible = false;
        accounts = App.pim.davAccounts();
        nameField.text = name;
        open();
    }
    function openCreate(color) {
        prepare("create", 0, "", color);
    }
    function openEdit(id, name, color) {
        prepare("edit", id, name, color);
    }
    function openDelete(id, name) {
        prepare("delete", id, name, "");
    }

    function accept() {
        const name = nameField.text.trim();
        if (mode !== "delete" && name.length === 0)
            return;
        busy = true;
        errorMessage.visible = false;
        if (mode === "create")
            App.pim.createCollection(accountBox.currentValue ?? 0, kind, name, selectedColor);
        else if (mode === "edit")
            App.pim.updateCollection(collectionId, name, selectedColor);
        else
            App.pim.deleteCollection(collectionId);
    }

    Connections {
        target: App.pim
        // Jede Liste hat einen eigenen Dialog; nur der wartende reagiert.
        function onCollectionResult(error) {
            if (!dialog.busy)
                return;
            dialog.busy = false;
            if (error.length > 0) {
                errorMessage.text = error;
                errorMessage.visible = true;
                return;
            }
            dialog.close();
        }
    }

    title: {
        if (mode === "create")
            return kind === "contacts" ? i18n("New Address Book") : kind === "todos" ? i18n("New Task List") : i18n("New Calendar");
        if (mode === "edit")
            return kind === "contacts" ? i18n("Edit Address Book") : kind === "todos" ? i18n("Edit Task List") : i18n("Edit Calendar");
        return kind === "contacts" ? i18n("Delete Address Book") : kind === "todos" ? i18n("Delete Task List") : i18n("Delete Calendar");
    }
    preferredWidth: Kirigami.Units.gridUnit * 24
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    onOpened: {
        if (mode !== "delete")
            nameField.forceActiveFocus();
    }

    customFooterActions: [
        Kirigami.Action {
            text: dialog.mode === "create" ? i18n("Create") : dialog.mode === "edit" ? i18n("Save") : i18n("Delete")
            icon.name: dialog.mode === "delete" ? "edit-delete" : "dialog-ok"
            enabled: !dialog.busy && (dialog.mode === "delete" || nameField.text.trim().length > 0)
            onTriggered: dialog.accept()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            enabled: !dialog.busy
            onTriggered: dialog.close()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.mode === "delete"
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            text: i18n("Delete \"%1\" with everything in it? It is removed from the server and from all your devices. This cannot be undone.", dialog.collectionName)
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: dialog.mode !== "delete"
            QQC2.ComboBox {
                id: accountBox
                Kirigami.FormData.label: i18n("Account:")
                Layout.fillWidth: true
                visible: dialog.mode === "create" && dialog.accounts.length > 1
                model: dialog.accounts
                textRole: "name"
                valueRole: "id"
            }
            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: i18n("Name:")
                Layout.fillWidth: true
                color: Theme.textPrimary
                onAccepted: {
                    if (!dialog.busy)
                        dialog.accept();
                }
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Color:")
                spacing: 8
                Repeater {
                    model: dialog.colors
                    QQC2.AbstractButton {
                        id: swatch
                        required property string modelData
                        readonly property bool selected: modelData.toLowerCase() === dialog.selectedColor.toLowerCase()
                        implicitWidth: 22
                        implicitHeight: 22
                        hoverEnabled: true
                        checkable: true
                        checked: selected
                        Accessible.name: modelData
                        onClicked: dialog.selectedColor = modelData
                        contentItem: Rectangle {
                            radius: 11
                            color: swatch.modelData
                            border.width: swatch.selected ? 2 : 0
                            border.color: Theme.textPrimary
                            scale: swatch.hovered ? 1.1 : 1
                        }
                    }
                }
            }
        }
    }
}

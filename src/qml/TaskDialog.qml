import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Aufgabe anlegen oder bearbeiten.
ThemedDialog {
    id: dialog

    property var itemId: 0
    property var collectionId: 0
    property bool readOnly: false
    // Erst beim Öffnen lesen: als Bindung bliebe die Liste leer, wenn das Konto später dazukommt,
    // und mit App.pim.revision würde ein Abgleich die Auswahl beim Bearbeiten zurücksetzen.
    property var lists: []

    signal notify(string message)

    // Vorbelegung, z. B. aus einer Mail
    function openNew(summary, description) {
        lists = App.pim.collections("todos").filter(c => !c.readOnly);
        itemId = 0;
        load({ summary: summary, description: description, dueMs: 0, priority: 0, reminderMinutes: -1 });
        open();
    }

    function openEdit(id) {
        lists = App.pim.collections("todos").filter(c => !c.readOnly);
        itemId = id;
        const task = App.pim.task(id);
        collectionId = task.collectionId;
        readOnly = task.readOnly;
        load(task);
        open();
    }

    function load(t) {
        titleField.text = t.summary;
        descriptionField.text = t.description;
        dueField.value = t.dueMs > 0 ? new Date(t.dueMs) : null;
        priorityBox.currentIndex = Math.max(0, priorityBox.indexOfValue(t.priority));
        reminderBox.currentIndex = Math.max(0, reminderBox.indexOfValue(t.reminderMinutes));
        listBox.currentIndex = t.collectionId !== undefined ? Math.max(0, listBox.indexOfValue(t.collectionId)) : 0;
        errorMessage.visible = false;
    }

    function accept() {
        dueField.commit();
        const error = App.pim.saveTask({
            itemId: itemId,
            collectionId: listBox.currentValue ?? 0,
            summary: titleField.text.trim(),
            description: descriptionField.text,
            dueMs: dueField.value ? dueField.value.getTime() : 0,
            priority: priorityBox.currentValue,
            reminderMinutes: reminderBox.currentValue
        });
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
    }

    title: itemId > 0 ? i18n("Edit Task") : i18n("New Task")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    onOpened: titleField.forceActiveFocus()

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Save")
            icon.name: "dialog-ok"
            enabled: titleField.text.trim().length > 0
            onTriggered: dialog.accept()
        },
        Kirigami.Action {
            text: i18n("Move or Copy…")
            icon.name: "go-jump"
            visible: dialog.itemId > 0
            onTriggered: transfer.openFor(dialog.itemId, titleField.text, dialog.collectionId, dialog.readOnly)
        },
        Kirigami.Action {
            text: i18n("Delete")
            icon.name: "edit-delete"
            visible: dialog.itemId > 0
            onTriggered: {
                const error = App.pim.deleteTask(dialog.itemId);
                if (error.length > 0)
                    dialog.notify(error);
                dialog.close();
            }
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    TransferDialog {
        id: transfer
        parent: QQC2.Overlay.overlay
        kind: "todos"
        onDone: dialog.close()
    }

    Kirigami.FormLayout {
        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }
        QQC2.TextField {
            id: titleField
            Kirigami.FormData.label: i18n("Title:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.ComboBox {
            id: listBox
            Kirigami.FormData.label: i18n("List:")
            Layout.fillWidth: true
            visible: dialog.itemId === 0
            model: dialog.lists
            textRole: "name"
            valueRole: "id"
        }
        DateTimeField {
            id: dueField
            Kirigami.FormData.label: i18n("Due:")
            Layout.fillWidth: true
            optional: true
        }
        QQC2.ComboBox {
            id: priorityBox
            Kirigami.FormData.label: i18n("Priority:")
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            // Werte nach RFC 5545: 1 hoch, 5 mittel, 9 niedrig, 0 keine
            model: [
                { value: 0, text: i18n("None") },
                { value: 1, text: i18n("High") },
                { value: 5, text: i18n("Medium") },
                { value: 9, text: i18n("Low") }
            ]
        }
        QQC2.ComboBox {
            id: reminderBox
            Kirigami.FormData.label: i18n("Reminder:")
            Layout.fillWidth: true
            enabled: dueField.value !== null
            textRole: "text"
            valueRole: "value"
            model: [
                { value: -1, text: i18n("None") },
                { value: 0, text: i18n("When due") },
                { value: 60, text: i18np("%1 hour before", "%1 hours before", 1) },
                { value: 1440, text: i18np("%1 day before", "%1 days before", 1) }
            ]
        }
        QQC2.TextArea {
            id: descriptionField
            Kirigami.FormData.label: i18n("Notes:")
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            color: Theme.textPrimary
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
        }
    }
}

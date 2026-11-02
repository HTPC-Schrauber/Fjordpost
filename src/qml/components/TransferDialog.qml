import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import de.fjordblau.fjordpost.app

// Termin, Aufgabe oder Kontakt in eine andere Sammlung verschieben oder kopieren.
ThemedDialog {
    id: dialog

    property string kind: "events" // "events", "todos", "contacts"
    property var itemId: 0
    property string itemName
    property bool sourceReadOnly: false
    property var targets: []

    // moved: das Original ist weg, Ansichten sollen es nicht mehr zeigen
    signal done(bool moved)

    function openFor(id, name, collectionId, readOnly) {
        itemId = id;
        itemName = name;
        sourceReadOnly = readOnly;
        targets = App.pim.collections(kind).filter(c => !c.readOnly && c.id !== collectionId);
        moveButton.checked = !readOnly;
        copyButton.checked = readOnly;
        targetBox.currentIndex = 0;
        errorMessage.visible = false;
        open();
    }

    function accept() {
        const move = moveButton.checked;
        const error = App.pim.transferItem(itemId, targetBox.currentValue ?? 0, move);
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
        done(move);
    }

    title: i18n("Move or Copy")
    preferredWidth: Kirigami.Units.gridUnit * 24
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton

    customFooterActions: [
        Kirigami.Action {
            text: moveButton.checked ? i18n("Move") : i18n("Copy")
            icon.name: moveButton.checked ? "go-jump" : "edit-copy"
            enabled: dialog.targets.length > 0
            onTriggered: dialog.accept()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
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
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.textPrimary
            font.weight: Font.Medium
            text: dialog.itemName
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.targets.length === 0
            wrapMode: Text.Wrap
            color: Theme.textSecondary
            text: dialog.kind === "contacts" ? i18n("There is no other writable address book.") : dialog.kind === "todos" ? i18n("There is no other writable task list.") : i18n("There is no other writable calendar.")
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: dialog.targets.length > 0
            QQC2.ComboBox {
                id: targetBox
                Kirigami.FormData.label: i18n("Destination:")
                Layout.fillWidth: true
                model: dialog.targets
                textRole: "name"
                valueRole: "id"
            }
            QQC2.RadioButton {
                id: moveButton
                Kirigami.FormData.label: i18n("Action:")
                text: i18n("Move")
                enabled: !dialog.sourceReadOnly
            }
            QQC2.RadioButton {
                id: copyButton
                text: i18n("Copy")
            }
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: copyButton.checked && dialog.kind === "events" && dialog.targets.length > 0
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
            text: i18n("The copy has no guests, so nobody is invited again.")
        }
    }
}

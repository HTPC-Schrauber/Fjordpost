import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Kontakt anlegen oder bearbeiten. Mehrere Adressen und Nummern zeilenweise.
ThemedDialog {
    id: dialog

    property var itemId: 0
    // Erst beim Öffnen lesen: als Bindung bliebe die Liste leer, wenn das Konto später dazukommt,
    // und mit App.pim.revision würde ein Abgleich die Auswahl beim Bearbeiten zurücksetzen.
    property var books: []

    function openNew(name, email) {
        books = App.pim.collections("contacts").filter(c => !c.readOnly);
        itemId = 0;
        load({ name: name, organization: "", jobTitle: "", emails: email.length > 0 ? [email] : [], phones: [], address: "", birthdayMs: 0, note: "" });
        open();
    }

    function openEdit(id) {
        books = App.pim.collections("contacts").filter(c => !c.readOnly);
        itemId = id;
        load(App.pim.contact(id));
        open();
    }

    function load(c) {
        nameField.text = c.name;
        organizationField.text = c.organization;
        jobField.text = c.jobTitle;
        emailsField.text = c.emails.join("\n");
        phonesField.text = c.phones.join("\n");
        addressField.text = c.address;
        birthdayField.value = c.birthdayMs > 0 ? new Date(c.birthdayMs) : null;
        noteField.text = c.note;
        bookBox.currentIndex = c.collectionId !== undefined ? Math.max(0, bookBox.indexOfValue(c.collectionId)) : 0;
        errorMessage.visible = false;
    }

    function lines(text) {
        return text.split("\n").map(l => l.trim()).filter(l => l.length > 0);
    }

    function accept() {
        const error = App.pim.saveContact({
            itemId: itemId,
            collectionId: bookBox.currentValue ?? 0,
            name: nameField.text,
            organization: organizationField.text,
            jobTitle: jobField.text,
            emails: lines(emailsField.text),
            phones: lines(phonesField.text),
            address: addressField.text,
            birthdayMs: birthdayField.value ? birthdayField.value.getTime() : 0,
            note: noteField.text
        });
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
    }

    title: itemId > 0 ? i18n("Edit Contact") : i18n("New Contact")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    onOpened: nameField.forceActiveFocus()

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Save")
            icon.name: "dialog-ok"
            enabled: nameField.text.trim().length > 0
            onTriggered: dialog.accept()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    Kirigami.FormLayout {
        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }
        QQC2.TextField {
            id: nameField
            Kirigami.FormData.label: i18n("Name:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.ComboBox {
            id: bookBox
            Kirigami.FormData.label: i18n("Address book:")
            Layout.fillWidth: true
            visible: dialog.itemId === 0 && count > 1
            model: dialog.books
            textRole: "name"
            valueRole: "id"
        }
        QQC2.TextField {
            id: organizationField
            Kirigami.FormData.label: i18n("Organization:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.TextField {
            id: jobField
            Kirigami.FormData.label: i18n("Job title:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.TextArea {
            id: emailsField
            Kirigami.FormData.label: i18n("Email:")
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            color: Theme.textPrimary
            textFormat: TextEdit.PlainText
            placeholderText: i18n("One address per line")
            placeholderTextColor: Theme.textMuted
        }
        QQC2.TextArea {
            id: phonesField
            Kirigami.FormData.label: i18n("Phone:")
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            color: Theme.textPrimary
            textFormat: TextEdit.PlainText
            placeholderText: i18n("One number per line")
            placeholderTextColor: Theme.textMuted
        }
        QQC2.TextArea {
            id: addressField
            Kirigami.FormData.label: i18n("Address:")
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            color: Theme.textPrimary
            textFormat: TextEdit.PlainText
        }
        DateTimeField {
            id: birthdayField
            Kirigami.FormData.label: i18n("Birthday:")
            Layout.fillWidth: true
            showTime: false
            optional: true
        }
        QQC2.TextArea {
            id: noteField
            Kirigami.FormData.label: i18n("Note:")
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 3
            color: Theme.textPrimary
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
        }
    }
}

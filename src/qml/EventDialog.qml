import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Termin anlegen oder bearbeiten. Serien werden immer als Ganzes geändert;
// beim Löschen lässt sich auch nur ein Vorkommnis oder der Rest der Serie wählen.
ThemedDialog {
    id: dialog

    property var itemId: 0
    property bool readOnly: false
    property var originalRepeat: "none"
    property var collectionId: 0
    // Beginn des angeklickten Vorkommnisses (0 = unbekannt, dann nur ganze Serie)
    property var occurrenceMs: 0
    // Erst beim Öffnen lesen: als Bindung bliebe die Liste leer, wenn das Konto später dazukommt,
    // und mit App.pim.revision würde ein Abgleich die Auswahl beim Bearbeiten zurücksetzen.
    property var calendars: []

    signal notify(string message)

    function openNew(start, allDay) {
        calendars = App.pim.collections("events").filter(c => !c.readOnly);
        itemId = 0;
        readOnly = false;
        const begin = new Date(start);
        // Aus der Monatsansicht kommt reine Tageszeit 0: nächste volle Stunde vorschlagen.
        if (begin.getHours() === 0 && !allDay) {
            const now = new Date();
            begin.setHours(Math.min(23, now.getHours() + 1), 0, 0, 0);
        }
        load({ summary: "", location: "", description: "", allDay: allDay, repeat: "none", reminderMinutes: -1,
               startMs: begin.getTime(), endMs: begin.getTime() + 3600000, attendees: [] });
        open();
    }

    function openEdit(id, startMs) {
        calendars = App.pim.collections("events").filter(c => !c.readOnly);
        const event = App.pim.event(id);
        itemId = id;
        occurrenceMs = startMs ?? 0;
        readOnly = event.readOnly;
        collectionId = event.collectionId;
        load(event);
        open();
    }

    function load(e) {
        titleField.text = e.summary;
        locationField.text = e.location;
        descriptionField.text = e.description;
        allDayBox.checked = e.allDay;
        startField.value = new Date(e.startMs);
        endField.value = new Date(e.endMs);
        attendeesField.text = (e.attendees ?? []).join(", ");
        originalRepeat = e.repeat;
        repeatBox.currentIndex = Math.max(0, repeatBox.indexOfValue(e.repeat));
        reminderBox.currentIndex = Math.max(0, reminderBox.indexOfValue(e.reminderMinutes));
        calendarBox.currentIndex = e.collectionId !== undefined ? Math.max(0, calendarBox.indexOfValue(e.collectionId)) : 0;
        errorMessage.visible = false;
    }

    function confirmRemove() {
        if (originalRepeat !== "none" && occurrenceMs > 0)
            deleteScope.open();
        else
            remove("all");
    }

    function remove(scope) {
        const error = App.pim.deleteEvent(itemId, scope, occurrenceMs);
        if (error.length > 0)
            notify(error);
        close();
    }

    function accept() {
        startField.commit();
        endField.commit();
        const attendees = attendeesField.text.split(/[,;]/).map(a => a.trim()).filter(a => a.length > 0);
        const error = App.pim.saveEvent({
            itemId: itemId,
            collectionId: calendarBox.currentValue ?? 0,
            summary: titleField.text.trim(),
            location: locationField.text,
            description: descriptionField.text,
            allDay: allDayBox.checked,
            startMs: startField.value.getTime(),
            endMs: endField.value.getTime(),
            repeat: repeatBox.currentValue,
            reminderMinutes: reminderBox.currentValue,
            attendees: attendees
        });
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
    }

    title: itemId > 0 ? i18n("Edit Event") : i18n("New Event")
    preferredWidth: Kirigami.Units.gridUnit * 28
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    onOpened: titleField.forceActiveFocus()

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Save")
            icon.name: "dialog-ok"
            enabled: !dialog.readOnly && titleField.text.trim().length > 0
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
            visible: dialog.itemId > 0 && !dialog.readOnly
            onTriggered: dialog.confirmRemove()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    // Nachfrage bei Serien
    ThemedDialog {
        id: deleteScope
        parent: QQC2.Overlay.overlay
        title: i18n("Delete Recurring Event")
        preferredWidth: Kirigami.Units.gridUnit * 22
        padding: Kirigami.Units.largeSpacing * 2
        standardButtons: Kirigami.Dialog.NoButton
        onOpened: thisOnly.checked = true

        customFooterActions: [
            Kirigami.Action {
                text: i18n("Delete")
                icon.name: "edit-delete"
                onTriggered: {
                    deleteScope.close();
                    dialog.remove(thisOnly.checked ? "this" : following.checked ? "following" : "all");
                }
            },
            Kirigami.Action {
                text: i18n("Cancel")
                icon.name: "dialog-cancel"
                onTriggered: deleteScope.close()
            }
        ]

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: i18n("\"%1\" repeats. Which events do you want to delete?", titleField.text)
            }
            QQC2.RadioButton {
                id: thisOnly
                text: i18n("Only this event (%1)", new Date(dialog.occurrenceMs).toLocaleDateString(Qt.locale(), Locale.LongFormat))
            }
            QQC2.RadioButton {
                id: following
                text: i18n("This and all following events")
            }
            QQC2.RadioButton {
                text: i18n("All events in the series")
            }
        }
    }

    TransferDialog {
        id: transfer
        parent: QQC2.Overlay.overlay
        kind: "events"
        onDone: dialog.close()
    }

    Kirigami.FormLayout {
        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }
        QQC2.Label {
            visible: dialog.readOnly
            text: i18n("This calendar is read-only.")
            color: Theme.textSecondary
        }
        QQC2.TextField {
            id: titleField
            Kirigami.FormData.label: i18n("Title:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.ComboBox {
            id: calendarBox
            Kirigami.FormData.label: i18n("Calendar:")
            Layout.fillWidth: true
            visible: dialog.itemId === 0
            model: dialog.calendars
            textRole: "name"
            valueRole: "id"
        }
        QQC2.CheckBox {
            id: allDayBox
            Kirigami.FormData.label: i18n("All day:")
        }
        DateTimeField {
            id: startField
            Kirigami.FormData.label: i18n("Starts:")
            Layout.fillWidth: true
            showTime: !allDayBox.checked
            onEdited: {
                // Dauer beibehalten: das Ende rückt mit, wenn es sonst davor läge
                if (endField.value < value)
                    endField.value = new Date(value.getTime() + (allDayBox.checked ? 0 : 3600000));
            }
        }
        DateTimeField {
            id: endField
            Kirigami.FormData.label: i18n("Ends:")
            Layout.fillWidth: true
            showTime: !allDayBox.checked
        }
        QQC2.TextField {
            id: locationField
            Kirigami.FormData.label: i18n("Location:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.ComboBox {
            id: repeatBox
            Kirigami.FormData.label: i18n("Repeat:")
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            // Regeln, die das Formular nicht abbilden kann, bleiben als "Custom" unangetastet.
            model: {
                const options = [
                    { value: "none", text: i18n("Never") },
                    { value: "daily", text: i18n("Daily") },
                    { value: "weekly", text: i18n("Weekly") },
                    { value: "monthly", text: i18n("Monthly") },
                    { value: "yearly", text: i18n("Yearly") }
                ];
                if (dialog.originalRepeat === "custom")
                    options.push({ value: "custom", text: i18n("Custom") });
                return options;
            }
        }
        QQC2.ComboBox {
            id: reminderBox
            Kirigami.FormData.label: i18n("Reminder:")
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: [
                { value: -1, text: i18n("None") },
                { value: 0, text: i18n("At start") },
                { value: 10, text: i18np("%1 minute before", "%1 minutes before", 10) },
                { value: 30, text: i18np("%1 minute before", "%1 minutes before", 30) },
                { value: 60, text: i18np("%1 hour before", "%1 hours before", 1) },
                { value: 1440, text: i18np("%1 day before", "%1 days before", 1) }
            ]
        }
        QQC2.TextField {
            id: attendeesField
            Kirigami.FormData.label: i18n("Guests:")
            Layout.fillWidth: true
            color: Theme.textPrimary
            placeholderText: i18n("Email addresses, separated by commas")
            placeholderTextColor: Theme.textMuted
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

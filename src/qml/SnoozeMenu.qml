import QtQuick
import QtQuick.Controls as QQC2
import de.fjordblau.fjordpost.core

// Zeitpunkt zum Zurückstellen wählen. Ist die Nachricht schon zurückgestellt,
// lässt sie sich hier auch vorzeitig zurückholen.
QQC2.Menu {
    id: snoozeMenu

    property var messageId: 0
    property bool snoozed: false
    property var options: []

    signal pickTimeRequested(var messageId)

    function openFor(id, anchor, isSnoozed) {
        messageId = id;
        snoozed = isSnoozed;
        options = App.snoozeOptions();
        popup(anchor, 0, anchor.height);
    }

    Instantiator {
        model: snoozeMenu.options
        delegate: QQC2.MenuItem {
            required property var modelData
            text: i18nc("snooze option and its time", "%1 (%2)", modelData.text, modelData.hint)
            onTriggered: App.snoozeMessage(snoozeMenu.messageId, modelData.time)
        }
        onObjectAdded: (index, object) => snoozeMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => snoozeMenu.removeItem(object)
    }

    QQC2.MenuSeparator {}

    QQC2.MenuItem {
        text: i18n("Pick Date and Time…")
        icon.name: "view-calendar-day"
        onTriggered: snoozeMenu.pickTimeRequested(snoozeMenu.messageId)
    }

    QQC2.MenuItem {
        visible: snoozeMenu.snoozed
        height: visible ? implicitHeight : 0
        text: i18n("Move Back to Inbox Now")
        icon.name: "mail-folder-inbox"
        onTriggered: App.unsnoozeMessage(snoozeMenu.messageId)
    }
}

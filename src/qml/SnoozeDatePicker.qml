import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Freie Weckzeit. Eigenes DateTimeField statt DatePopup aus kirigamiaddons: Das meldet beim
// Öffnen massenhaft "Unable to assign double to QDateTime" und ist im Theme schlecht lesbar.
ThemedDialog {
    id: dialog

    property var messageId: 0
    readonly property bool valid: whenField.value !== null && whenField.value.getTime() > Date.now()

    function openFor(id) {
        messageId = id;
        const tomorrow = new Date();
        tomorrow.setDate(tomorrow.getDate() + 1);
        tomorrow.setHours(8, 0, 0, 0);
        whenField.value = tomorrow;
        open();
    }

    function accept() {
        whenField.commit();
        if (!valid)
            return;
        App.snoozeMessage(messageId, whenField.value.getTime());
        close();
    }

    parent: QQC2.Overlay.overlay
    title: i18n("Snooze Until")
    preferredWidth: Kirigami.Units.gridUnit * 20
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Snooze")
            icon.name: "dialog-ok"
            enabled: dialog.valid
            onTriggered: dialog.accept()
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing
        DateTimeField {
            id: whenField
            Layout.fillWidth: true
        }
        QQC2.Label {
            Layout.fillWidth: true
            visible: !dialog.valid
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
            text: i18n("Please choose a time in the future.")
        }
    }
}

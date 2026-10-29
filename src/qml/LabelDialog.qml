import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Label anlegen oder bearbeiten (Name und Farbe).
ThemedDialog {
    id: dialog

    property var labelId: 0     // > 0: bearbeiten
    property var assignTo: 0    // neues Label gleich dieser Nachricht geben
    property string chosenColor: Theme.labelColors[0]

    function openNew(messageId) {
        labelId = 0;
        assignTo = messageId ?? 0;
        nameField.text = "";
        chosenColor = Theme.labelColors[App.labels.count % Theme.labelColors.length];
        errorMessage.visible = false;
        open();
    }

    function openEdit(id, name, color) {
        labelId = id;
        assignTo = 0;
        nameField.text = name;
        chosenColor = color.length > 0 ? color : Theme.labelColors[0];
        errorMessage.visible = false;
        open();
    }

    function accept() {
        const error = labelId > 0 ? App.updateLabel(labelId, nameField.text, chosenColor) : App.createLabel(nameField.text, chosenColor, assignTo);
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
    }

    title: labelId > 0 ? i18n("Edit Label") : i18n("New Label")
    preferredWidth: Kirigami.Units.gridUnit * 22
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    onOpened: nameField.forceActiveFocus()

    customFooterActions: [
        Kirigami.Action {
            text: dialog.labelId > 0 ? i18n("Save") : i18n("Create")
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

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: i18n("Name:")
                placeholderText: i18n("e.g. Invoices")
                onAccepted: dialog.accept()
            }

            RowLayout {
                Kirigami.FormData.label: i18n("Color:")
                spacing: 8
                Repeater {
                    model: Theme.labelColors
                    QQC2.AbstractButton {
                        required property string modelData
                        implicitWidth: 22
                        implicitHeight: 22
                        hoverEnabled: true
                        onClicked: dialog.chosenColor = modelData
                        contentItem: Rectangle {
                            radius: 11
                            color: parent.modelData
                            border.width: dialog.chosenColor === parent.modelData ? 2 : 0
                            border.color: Theme.textPrimary
                            scale: parent.hovered ? 1.1 : 1
                        }
                    }
                }
            }
        }
    }
}

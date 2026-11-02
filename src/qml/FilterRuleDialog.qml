import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Filterregel anlegen oder bearbeiten.
ThemedDialog {
    id: dialog

    signal saved()

    property var ruleId: 0
    property bool ruleEnabled: true
    property var conditions: []
    property var actions: []

    readonly property var fieldChoices: [
        { value: "from", text: i18n("From") },
        { value: "to", text: i18n("To or Cc") },
        { value: "subject", text: i18n("Subject") },
        { value: "any", text: i18n("Any of these") }
    ]
    readonly property var opChoices: [
        { value: "contains", text: i18n("contains") },
        { value: "notContains", text: i18n("does not contain") },
        { value: "is", text: i18n("is") },
        { value: "startsWith", text: i18n("starts with") },
        { value: "endsWith", text: i18n("ends with") }
    ]
    // "In Ordner verschieben" nur für ein bestimmtes Konto: Ordner gibt es je Konto
    readonly property var actionChoices: {
        const choices = [
            { value: "read", text: i18n("Mark as read") },
            { value: "flag", text: i18n("Flag") },
            { value: "label", text: i18n("Add label") },
            { value: "archive", text: i18n("Archive") },
            { value: "junk", text: i18n("Move to spam") },
            { value: "trash", text: i18n("Move to trash") }
        ];
        if (accountBox.currentValue > 0)
            choices.push({ value: "move", text: i18n("Move to folder") });
        return choices;
    }

    function openFor(rule) {
        ruleId = rule ? rule.id : 0;
        ruleEnabled = rule ? rule.enabled : true;
        nameField.text = rule ? rule.name : "";
        accountBox.model = [{ accountId: 0, text: i18n("All accounts") }].concat(App.identities());
        accountBox.currentIndex = Math.max(0, accountBox.indexOfValue(rule ? rule.accountId : 0));
        matchBox.currentIndex = rule && !rule.matchAll ? 1 : 0;
        stopBox.checked = rule ? rule.stop : false;
        conditions = rule ? rule.conditions.slice() : [{ field: "from", op: "contains", value: "" }];
        actions = rule ? rule.actions.slice() : [{ type: "read", argument: "" }];
        errorMessage.visible = false;
        open();
    }

    function changed(list, index, key, value) {
        const copy = list.slice();
        copy[index] = Object.assign({}, copy[index]);
        copy[index][key] = value;
        return copy;
    }

    // Eingabefelder melden jeden Tastendruck; das Modell neu zu setzen nähme ihnen den Fokus.
    function setConditionValue(index, value) {
        conditions[index].value = value;
    }

    function accept() {
        const error = App.saveFilterRule({
            id: ruleId,
            name: nameField.text,
            enabled: ruleEnabled,
            accountId: accountBox.currentValue,
            matchAll: matchBox.currentIndex === 0,
            stop: stopBox.checked,
            conditions: conditions,
            actions: actions
        });
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        close();
        saved();
    }

    title: ruleId > 0 ? i18n("Edit Rule") : i18n("New Rule")
    preferredWidth: Kirigami.Units.gridUnit * 36
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
                placeholderText: i18n("e.g. Newsletters")
            }
            QQC2.ComboBox {
                id: accountBox
                Kirigami.FormData.label: i18n("Applies to:")
                textRole: "text"
                valueRole: "accountId"
            }
            QQC2.ComboBox {
                id: matchBox
                Kirigami.FormData.label: i18n("Match:")
                model: [i18n("All conditions"), i18n("At least one condition")]
            }
        }

        SectionLabel {
            text: i18n("Conditions")
        }
        Repeater {
            model: dialog.conditions
            delegate: RowLayout {
                id: conditionRow
                required property var modelData
                required property int index
                Layout.fillWidth: true
                QQC2.ComboBox {
                    model: dialog.fieldChoices
                    textRole: "text"
                    valueRole: "value"
                    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(conditionRow.modelData.field))
                    onActivated: dialog.conditions = dialog.changed(dialog.conditions, conditionRow.index, "field", currentValue)
                }
                QQC2.ComboBox {
                    model: dialog.opChoices
                    textRole: "text"
                    valueRole: "value"
                    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(conditionRow.modelData.op))
                    onActivated: dialog.conditions = dialog.changed(dialog.conditions, conditionRow.index, "op", currentValue)
                }
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: conditionRow.modelData.value
                    onTextEdited: dialog.setConditionValue(conditionRow.index, text)
                }
                IconButton {
                    iconName: "list-remove"
                    iconSize: 14
                    enabled: dialog.conditions.length > 1
                    tooltip: i18n("Remove")
                    onClicked: {
                        const copy = dialog.conditions.slice();
                        copy.splice(conditionRow.index, 1);
                        dialog.conditions = copy;
                    }
                }
            }
        }
        QQC2.Button {
            text: i18n("Add Condition")
            icon.name: "list-add"
            onClicked: dialog.conditions = dialog.conditions.concat([{ field: "subject", op: "contains", value: "" }])
        }

        SectionLabel {
            text: i18n("Actions")
        }
        Repeater {
            model: dialog.actions
            delegate: RowLayout {
                id: actionRow
                required property var modelData
                required property int index
                readonly property bool needsArgument: modelData.type === "label" || modelData.type === "move"
                readonly property var argumentChoices: modelData.type === "label" ? App.labelChoices() : (modelData.type === "move" ? App.foldersOfAccount(accountBox.currentValue) : [])
                Layout.fillWidth: true
                QQC2.ComboBox {
                    model: dialog.actionChoices
                    textRole: "text"
                    valueRole: "value"
                    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(actionRow.modelData.type))
                    onActivated: {
                        const next = dialog.changed(dialog.actions, actionRow.index, "type", currentValue);
                        next[actionRow.index].argument = "";
                        dialog.actions = next;
                    }
                }
                QQC2.ComboBox {
                    id: argumentBox
                    Layout.fillWidth: true
                    visible: actionRow.needsArgument
                    model: actionRow.argumentChoices
                    textRole: "text"
                    valueRole: "value"
                    // Vorauswahl übernehmen, sonst gäbe es ohne Klick kein Argument
                    function sync() {
                        const index = indexOfValue(actionRow.modelData.argument);
                        currentIndex = index >= 0 ? index : (count > 0 ? 0 : -1);
                        if (index < 0 && count > 0)
                            dialog.actions[actionRow.index].argument = actionRow.argumentChoices[0].value;
                    }
                    onCountChanged: sync()
                    Component.onCompleted: sync()
                    onActivated: dialog.actions[actionRow.index].argument = currentValue
                }
                Item {
                    Layout.fillWidth: true
                    visible: !actionRow.needsArgument
                }
                IconButton {
                    iconName: "list-remove"
                    iconSize: 14
                    enabled: dialog.actions.length > 1
                    tooltip: i18n("Remove")
                    onClicked: {
                        const copy = dialog.actions.slice();
                        copy.splice(actionRow.index, 1);
                        dialog.actions = copy;
                    }
                }
            }
        }
        QQC2.Button {
            text: i18n("Add Action")
            icon.name: "list-add"
            onClicked: dialog.actions = dialog.actions.concat([{ type: "flag", argument: "" }])
        }

        QQC2.CheckBox {
            id: stopBox
            text: i18n("Stop here: do not apply any further rules")
        }
    }
}

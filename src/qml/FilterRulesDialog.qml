import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Liste der Filterregeln. Sie laufen von oben nach unten auf neuen Nachrichten im Posteingang.
ThemedDialog {
    id: dialog

    property var rules: []

    function reload() {
        rules = App.filterRules();
    }

    function editRule(rule) {
        editor.openFor(rule);
    }

    title: i18n("Filter Rules")
    preferredWidth: Kirigami.Units.gridUnit * 34
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.Close
    onAboutToShow: reload()

    customFooterActions: [
        Kirigami.Action {
            text: i18n("New Rule")
            icon.name: "list-add"
            onTriggered: dialog.editRule(null)
        }
    ]

    FilterRuleDialog {
        id: editor
        parent: dialog.parent
        onSaved: dialog.reload()
    }

    ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        QQC2.Label {
            Layout.fillWidth: true
            text: i18n("Rules run from top to bottom on new messages in the inbox. They look at sender, recipients and subject.")
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
        }

        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            visible: dialog.rules.length === 0
            horizontalAlignment: Text.AlignHCenter
            text: i18n("No rules yet.")
            font.pointSize: Theme.fontXs
            color: Theme.textSecondary
        }

        Repeater {
            model: dialog.rules
            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index

                Layout.fillWidth: true
                implicitHeight: entryRow.implicitHeight + 12
                radius: Theme.radius
                color: Theme.card
                opacity: modelData.enabled ? 1 : 0.6

                RowLayout {
                    id: entryRow
                    anchors.fill: parent
                    anchors.margins: 6
                    anchors.leftMargin: 10
                    spacing: 2

                    QQC2.CheckBox {
                        checked: entry.modelData.enabled
                        onToggled: {
                            App.setFilterRuleEnabled(entry.modelData.id, checked);
                            dialog.reload();
                        }
                        QQC2.ToolTip.text: i18n("Rule is active")
                        QQC2.ToolTip.visible: hovered
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: entry.modelData.name
                        elide: Text.ElideRight
                        font.pointSize: Theme.fontXs
                        font.weight: Font.Medium
                        color: Theme.textPrimary
                    }
                    IconButton {
                        iconName: "media-playback-start"
                        iconSize: 14
                        tooltip: i18n("Apply to messages already in the inbox")
                        onClicked: App.runFilterRule(entry.modelData.id)
                    }
                    IconButton {
                        iconName: "go-up"
                        iconSize: 14
                        enabled: entry.index > 0
                        tooltip: i18n("Move up")
                        onClicked: {
                            App.moveFilterRule(entry.modelData.id, -1);
                            dialog.reload();
                        }
                    }
                    IconButton {
                        iconName: "go-down"
                        iconSize: 14
                        enabled: entry.index < dialog.rules.length - 1
                        tooltip: i18n("Move down")
                        onClicked: {
                            App.moveFilterRule(entry.modelData.id, 1);
                            dialog.reload();
                        }
                    }
                    IconButton {
                        iconName: "document-edit"
                        iconSize: 14
                        tooltip: i18n("Edit")
                        onClicked: dialog.editRule(entry.modelData)
                    }
                    IconButton {
                        iconName: "edit-delete"
                        iconSize: 14
                        hoverColor: Theme.danger
                        tooltip: i18n("Delete")
                        onClicked: {
                            App.deleteFilterRule(entry.modelData.id);
                            dialog.reload();
                        }
                    }
                }
            }
        }
    }
}

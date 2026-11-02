import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Einstellungen. Änderungen wirken sofort und werden automatisch gespeichert.
ThemedDialog {
    id: dialog

    signal filterRulesRequested()
    signal addDavAccountRequested()

    property var davAccounts: []

    title: i18n("Settings")
    preferredWidth: Kirigami.Units.gridUnit * 30
    standardButtons: Kirigami.Dialog.Close
    padding: Kirigami.Units.largeSpacing * 2

    // Konten neu einlesen, damit neue Konten und gespeicherte Signaturen erscheinen
    onAboutToShow: {
        signatureAccount.model = App.identities();
        signatureAccount.currentIndex = Math.max(0, signatureAccount.indexOfValue(App.selectedAccountId));
        signatureText.load();
        davAccounts = App.pim.davAccounts();
    }
    onClosed: signatureText.save()

    Kirigami.FormLayout {
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("General")
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Closing the window:")
            text: i18n("Keep running in the system tray")
            checked: Prefs.general.closeToTray
            onToggled: Prefs.general.closeToTray = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Appearance")
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Colors:")
            text: i18n("Use KDE color scheme")
            checked: Theme.settings.useSystemColors
            onToggled: Theme.settings.useSystemColors = checked
        }

        QQC2.CheckBox {
            text: i18n("Dark mode")
            enabled: !Theme.system
            checked: Theme.settings.dark
            onToggled: Theme.settings.dark = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Accent color:")
            spacing: 8
            enabled: !Theme.system
            opacity: enabled ? 1 : 0.4
            Repeater {
                model: Theme.accentNames
                QQC2.AbstractButton {
                    required property string modelData
                    implicitWidth: 22
                    implicitHeight: 22
                    hoverEnabled: true
                    onClicked: Theme.settings.accent = modelData
                    QQC2.ToolTip.text: modelData
                    QQC2.ToolTip.visible: hovered
                    contentItem: Rectangle {
                        radius: 11
                        color: Theme.accents[parent.modelData][0]
                        border.width: Theme.settings.accent === parent.modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        scale: parent.hovered ? 1.1 : 1
                    }
                }
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Message List")
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Conversations:")
            text: i18n("Group messages into conversations")
            checked: Prefs.view.conversations
            onToggled: Prefs.view.conversations = checked
        }

        QQC2.Button {
            Kirigami.FormData.label: i18n("Incoming mail:")
            text: i18n("Filter Rules…")
            icon.name: "view-filter"
            onClicked: {
                dialog.close();
                dialog.filterRulesRequested();
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Composing")
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18n("Open new messages:")
            text: i18n("In the main window")
            checked: Prefs.compose.openIn === "inline"
            onToggled: Prefs.compose.openIn = "inline"
        }

        QQC2.RadioButton {
            text: i18n("In a separate window")
            checked: Prefs.compose.openIn === "window"
            onToggled: Prefs.compose.openIn = "window"
        }

        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: i18n("Applies to new messages, replies and forwards. A message opened in the main window can be moved to its own window at any time.")
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
        }

        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Undo send:")
            model: [
                { value: 0, text: i18n("Off") },
                { value: 5, text: i18np("%1 second", "%1 seconds", 5) },
                { value: 10, text: i18np("%1 second", "%1 seconds", 10) },
                { value: 20, text: i18np("%1 second", "%1 seconds", 20) },
                { value: 30, text: i18np("%1 second", "%1 seconds", 30) }
            ]
            textRole: "text"
            valueRole: "value"
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(Prefs.compose.undoSendSeconds))
            onActivated: Prefs.compose.undoSendSeconds = currentValue
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Calendars and Contacts")
        }

        // Repeater direkt im FormLayout würde seine Zeilen ans Ende sortieren.
        ColumnLayout {
            Kirigami.FormData.label: i18n("Accounts:")
            visible: dialog.davAccounts.length > 0
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: dialog.davAccounts
                RowLayout {
                    id: davRow
                    required property var modelData
                    Layout.fillWidth: true
                    QQC2.Label {
                        text: davRow.modelData.name
                        color: Theme.textPrimary
                    }
                    QQC2.ComboBox {
                        Layout.fillWidth: true
                        QQC2.ToolTip.text: i18n("Invitations are sent from this address")
                        QQC2.ToolTip.visible: hovered
                        model: [{ accountId: 0, text: i18n("No invitations") }].concat(App.identities())
                        textRole: "text"
                        valueRole: "accountId"
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(davRow.modelData.mailAccountId))
                        onActivated: App.pim.setDavMailAccount(davRow.modelData.id, currentValue)
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        QQC2.ToolTip.text: i18n("Remove account")
                        QQC2.ToolTip.visible: hovered
                        onClicked: {
                            App.pim.removeDavAccount(davRow.modelData.id);
                            dialog.davAccounts = App.pim.davAccounts();
                        }
                    }
                }
            }
        }

        QQC2.Button {
            Kirigami.FormData.label: dialog.davAccounts.length === 0 ? i18n("Accounts:") : " "
            text: i18n("Connect Account…")
            icon.name: "list-add"
            onClicked: {
                dialog.close();
                dialog.addDavAccountRequested();
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Signatures")
            visible: signatureAccount.count > 0
        }

        QQC2.ComboBox {
            id: signatureAccount
            Kirigami.FormData.label: i18n("Account:")
            visible: count > 0
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "accountId"
            onActivated: signatureText.load()
        }

        Rectangle {
            visible: signatureAccount.count > 0
            Layout.fillWidth: true
            // FormLayout richtet sich nach der impliziten Größe, nicht nach Layout.preferredHeight
            implicitWidth: Kirigami.Units.gridUnit * 18
            implicitHeight: Kirigami.Units.gridUnit * 5
            radius: Theme.radiusSm
            color: Theme.card
            border.width: 1
            border.color: signatureText.activeFocus ? Theme.accent : Theme.border

            QQC2.TextArea {
                id: signatureText
                anchors.fill: parent
                background: Item {}
                color: Theme.textPrimary
                placeholderTextColor: Theme.textMuted
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                placeholderText: i18n("No signature")

                property bool loading: false
                function load() {
                    saveTimer.stop();
                    loading = true;
                    const identity = signatureAccount.model[signatureAccount.currentIndex];
                    text = identity ? identity.signature : "";
                    loading = false;
                }
                function save() {
                    saveTimer.stop();
                    const identity = signatureAccount.model[signatureAccount.currentIndex];
                    if (!identity || identity.signature === text)
                        return;
                    if (App.setSignature(identity.accountId, text))
                        identity.signature = text;
                }
                onTextChanged: {
                    if (!loading)
                        saveTimer.restart();
                }
                onActiveFocusChanged: {
                    if (!activeFocus)
                        save();
                }
                Timer {
                    id: saveTimer
                    interval: 600
                    onTriggered: signatureText.save()
                }
            }
        }

        QQC2.Label {
            visible: signatureAccount.count > 0
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: i18n("Inserted automatically into new messages, replies and forwards, separated by \"-- \".")
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
        }
    }
}

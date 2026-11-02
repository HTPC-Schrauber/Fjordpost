import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// CalDAV/CardDAV-Konto verbinden. Der Server wird automatisch erkannt.
ThemedDialog {
    id: dialog

    property bool busy: false
    readonly property var identities: [{ accountId: 0, text: i18n("None") }].concat(App.identities())

    function prepare() {
        addressField.text = "";
        userField.text = "";
        passwordField.text = "";
        errorMessage.visible = false;
        busy = false;
        mailBox.currentIndex = dialog.identities.length > 1 ? 1 : 0;
        const identity = dialog.identities[mailBox.currentIndex];
        if (identity.accountId > 0 && identity.email)
            userField.text = identity.email;
    }

    title: i18n("Connect Calendar and Contacts")
    preferredWidth: Kirigami.Units.gridUnit * 26
    padding: Kirigami.Units.largeSpacing * 2
    standardButtons: Kirigami.Dialog.NoButton
    closePolicy: busy ? QQC2.Popup.NoAutoClose : (QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutside)
    onAboutToShow: prepare()
    onOpened: addressField.forceActiveFocus()

    customFooterActions: [
        Kirigami.Action {
            text: dialog.busy ? i18n("Connecting…") : i18n("Connect")
            icon.name: "dialog-ok"
            enabled: !dialog.busy && addressField.text.trim().length > 0 && userField.text.trim().length > 0
            onTriggered: {
                dialog.busy = true;
                errorMessage.visible = false;
                App.pim.addDavAccount(addressField.text.trim(), userField.text.trim(), passwordField.text, mailBox.currentValue ?? 0);
            }
        },
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            enabled: !dialog.busy
            onTriggered: dialog.close()
        }
    ]

    Connections {
        target: App.pim
        function onDavAccountResult(error) {
            dialog.busy = false;
            if (error.length > 0) {
                errorMessage.text = error;
                errorMessage.visible = true;
            } else {
                dialog.close();
            }
        }
    }

    Kirigami.FormLayout {
        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
        }
        QQC2.TextField {
            id: addressField
            Kirigami.FormData.label: i18n("Server:")
            Layout.fillWidth: true
            color: Theme.textPrimary
            placeholderText: "https://cloud.example.org"
            placeholderTextColor: Theme.textMuted
        }
        QQC2.TextField {
            id: userField
            Kirigami.FormData.label: i18n("User name:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        Kirigami.PasswordField {
            id: passwordField
            Kirigami.FormData.label: i18n("Password:")
            Layout.fillWidth: true
            color: Theme.textPrimary
        }
        QQC2.ComboBox {
            id: mailBox
            Kirigami.FormData.label: i18n("Send invitations from:")
            Layout.fillWidth: true
            model: dialog.identities
            textRole: "text"
            valueRole: "accountId"
        }
        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: i18n("Enter the address of your Nextcloud or other CalDAV/CardDAV server. If your provider offers app passwords, use one here.")
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
        }
    }
}

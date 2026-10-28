import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Konto-Assistent: Schritt 1 Adresse/Passwort, Schritt 2 Servereinstellungen prüfen.
Kirigami.Dialog {
    id: dialog

    readonly property var setup: App.accountSetup
    property int step: 0

    title: i18n("Add Email Account")
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing * 2
    closePolicy: setup.busy ? QQC2.Popup.NoAutoClose : QQC2.Popup.CloseOnEscape

    onOpened: {
        setup.reset();
        step = 0;
        passwordField.text = "";
        emailField.forceActiveFocus();
    }
    onClosed: setup.cancel()

    Connections {
        target: dialog.setup
        function onDiscovered() {
            dialog.step = 1;
        }
        function onSaved(accountId) {
            dialog.close();
            App.selectAccount(accountId);
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.setup.errorText.length > 0
            type: Kirigami.MessageType.Error
            text: dialog.setup.errorText
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.step === 1 && dialog.setup.needsOAuth
            type: Kirigami.MessageType.Warning
            text: i18n("This provider may require signing in through the browser (OAuth). Support for this follows in a later version; an app password may work instead.")
        }

        // Schritt 1: Zugangsdaten
        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: i18n("Your name:")
                placeholderText: i18n("Max Mustermann")
                text: dialog.setup.displayName
                onTextEdited: dialog.setup.displayName = text
            }
            QQC2.TextField {
                id: emailField
                Kirigami.FormData.label: i18n("Email address:")
                placeholderText: "name@example.com"
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                text: dialog.setup.email
                enabled: dialog.step === 0
                onTextEdited: dialog.setup.email = text
                onAccepted: passwordField.forceActiveFocus()
            }
            Kirigami.PasswordField {
                id: passwordField
                Kirigami.FormData.label: i18n("Password:")
                onAccepted: dialog.step === 0 ? dialog.setup.discover(emailField.text) : dialog.setup.testAndSave(text)
            }
        }

        // Schritt 2: Servereinstellungen
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: dialog.step === 1
            enabled: !dialog.setup.busy

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Server Settings")
            }
            QQC2.Label {
                Kirigami.FormData.label: i18n("Found via:")
                text: dialog.setup.source
                color: Kirigami.Theme.disabledTextColor
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Account name:")
                text: dialog.setup.name
                onTextEdited: dialog.setup.name = text
            }
            QQC2.TextField {
                Kirigami.FormData.label: i18n("Username:")
                text: dialog.setup.username
                onTextEdited: dialog.setup.username = text
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Incoming Mail")
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: i18n("Protocol:")
                model: ["IMAP", "POP3"]
                currentIndex: dialog.setup.pop3 ? 1 : 0
                onActivated: index => dialog.setup.pop3 = index === 1
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Server:")
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: dialog.setup.incomingHost
                    onTextEdited: dialog.setup.incomingHost = text
                }
                QQC2.SpinBox {
                    from: 1
                    to: 65535
                    editable: true
                    value: dialog.setup.incomingPort
                    textFromValue: value => String(value)
                    onValueModified: dialog.setup.incomingPort = value
                }
            }
            SecurityBox {
                Kirigami.FormData.label: i18n("Security:")
                currentIndex: dialog.setup.incomingSecurity
                onActivated: index => dialog.setup.incomingSecurity = index
            }

            Kirigami.Separator {
                Kirigami.FormData.isSection: true
                Kirigami.FormData.label: i18n("Outgoing Mail (SMTP)")
            }
            RowLayout {
                Kirigami.FormData.label: i18n("Server:")
                QQC2.TextField {
                    Layout.fillWidth: true
                    text: dialog.setup.outgoingHost
                    onTextEdited: dialog.setup.outgoingHost = text
                }
                QQC2.SpinBox {
                    from: 1
                    to: 65535
                    editable: true
                    value: dialog.setup.outgoingPort
                    textFromValue: value => String(value)
                    onValueModified: dialog.setup.outgoingPort = value
                }
            }
            SecurityBox {
                Kirigami.FormData.label: i18n("Security:")
                currentIndex: dialog.setup.outgoingSecurity
                onActivated: index => dialog.setup.outgoingSecurity = index
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: dialog.setup.busy
            QQC2.BusyIndicator {
                running: dialog.setup.busy
                Layout.preferredHeight: Kirigami.Units.gridUnit * 1.5
                Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
            }
            QQC2.Label {
                Layout.fillWidth: true
                text: dialog.setup.status
                wrapMode: Text.Wrap
            }
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Back")
            icon.name: "go-previous"
            visible: dialog.step === 1
            enabled: !dialog.setup.busy
            onTriggered: dialog.step = 0
        },
        Kirigami.Action {
            text: dialog.step === 0 ? i18n("Continue") : i18n("Sign In")
            icon.name: dialog.step === 0 ? "go-next" : "dialog-ok"
            enabled: !dialog.setup.busy && emailField.text.length > 0 && passwordField.text.length > 0
            onTriggered: dialog.step === 0 ? dialog.setup.discover(emailField.text) : dialog.setup.testAndSave(passwordField.text)
        }
    ]

    component SecurityBox: QQC2.ComboBox {
        model: [i18n("None"), i18n("STARTTLS"), i18n("SSL/TLS")]
    }
}

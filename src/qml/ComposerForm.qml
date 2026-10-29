import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Inhalt des Composers (neu, Antwort, Weiterleitung). Wird entweder im Hauptfenster
// anstelle des Lesebereichs (inline) oder in einem eigenen ComposerWindow angezeigt.
Rectangle {
    id: composer

    // Vorbelegung aus App.composeData() bzw. collect() beim Herauslösen
    property var draft: ({})
    property var attachments: draft.attachments ?? []
    property bool dirty: false
    // Im Hauptfenster: Knopf zum Herauslösen in ein eigenes Fenster anzeigen
    property bool inline: false
    // Aktuell eingefügter Signaturblock; wird beim Wechsel des Absenders ersetzt.
    property string signatureBlock: draft.signatureBlock ?? ""

    readonly property string title: subjectField.text.length > 0 ? subjectField.text : i18n("New Message")

    // Senden oder Verwerfen abgeschlossen: der Gastgeber schließt den Composer.
    signal finished()
    signal popOutRequested(var state)
    signal notify(string message)

    color: Theme.elevated

    function collect() {
        return {
            mode: draft.mode ?? "new",
            referenceId: draft.referenceId ?? 0,
            accountId: fromBox.currentValue,
            to: toField.text,
            cc: ccField.text,
            bcc: bccField.text,
            subject: subjectField.text,
            body: bodyArea.text,
            attachments: composer.attachments,
            showCc: ccRow.visible,
            dirty: composer.dirty,
            inReplyTo: draft.inReplyTo ?? "",
            references: draft.references ?? [],
            signatureApplied: true,
            signatureBlock: composer.signatureBlock
        };
    }

    function signatureOf(accountId) {
        const identity = fromBox.model.find(i => i.accountId === accountId);
        const text = identity ? identity.signature : "";
        // Trenner "-- " nach RFC 3676, damit Mailprogramme die Signatur erkennen
        return text.trim().length > 0 ? "\n\n-- \n" + text : "";
    }

    // Signatur des neuen Absenders einsetzen: den alten Block ersetzen oder,
    // falls keiner (mehr) da ist, vor dem Zitat bzw. am Ende einfügen.
    function switchSignature(accountId) {
        const block = signatureOf(accountId);
        const body = bodyArea.text;
        const old = signatureBlock;
        if (old.length > 0 && body.indexOf(old) >= 0) {
            bodyArea.text = body.replace(old, () => block);
        } else if (block.length > 0) {
            const quote = draft.body ?? "";
            const at = quote.length > 0 && body.endsWith(quote) ? body.length - quote.length : body.length;
            bodyArea.text = body.slice(0, at) + block + body.slice(at);
        }
        signatureBlock = block;
    }

    function send() {
        const error = App.sendMessage(collect());
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        dirty = false;
        finished();
    }

    function saveDraft() {
        const error = App.saveDraft(collect());
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return false;
        }
        dirty = false;
        notify(i18n("Draft saved."));
        return true;
    }

    // Schließen anfordern; bei ungesicherten Änderungen erst nachfragen.
    // Liefert true, wenn sofort geschlossen wird.
    function requestClose() {
        if (!dirty) {
            finished();
            return true;
        }
        discardDialog.open();
        return false;
    }

    function discard() {
        dirty = false;
        finished();
    }

    Component.onCompleted: {
        if ((draft.to ?? "").length > 0)
            bodyArea.forceActiveFocus();
        else
            toField.forceActiveFocus();
        if (!(draft.signatureApplied ?? false)) {
            signatureBlock = signatureOf(draft.accountId ?? fromBox.currentValue);
            bodyArea.text = signatureBlock + bodyArea.text;
        }
        bodyArea.cursorPosition = 0;
        dirty = draft.dirty ?? false;
    }

    Keys.onEscapePressed: requestClose()

    function hasFocusWithin() {
        for (let item = Window.activeFocusItem; item; item = item.parent) {
            if (item === composer)
                return true;
        }
        return false;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
            color: Qt.alpha(Theme.dock, 0.6)
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 12
                spacing: 8
                Rectangle {
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: Theme.accent
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: (composer.draft.mode ?? "new") === "new" ? i18n("Compose new message") : (composer.draft.mode === "forward" ? i18n("Forward message") : i18n("Reply"))
                    font.pointSize: Theme.fontXs
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                }
                IconButton {
                    text: i18n("Cc/Bcc")
                    display: QQC2.AbstractButton.TextOnly
                    active: ccRow.visible
                    onClicked: ccRow.visible = bccRow.visible = !ccRow.visible
                }
                IconButton {
                    visible: composer.inline
                    iconName: "window-new"
                    tooltip: i18n("Open in separate window")
                    onClicked: composer.popOutRequested(composer.collect())
                }
                IconButton {
                    visible: composer.inline
                    iconName: "window-close"
                    tooltip: i18n("Close (Esc)")
                    onClicked: composer.requestClose()
                }
            }
        }

        Kirigami.InlineMessage {
            id: errorMessage
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            showCloseButton: true
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            columns: 2
            columnSpacing: 12
            rowSpacing: 4

            FieldLabel {
                text: i18n("From:")
            }
            QQC2.ComboBox {
                id: fromBox
                Layout.fillWidth: true
                model: App.identities()
                textRole: "text"
                valueRole: "accountId"
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(composer.draft.accountId))
                onActivated: composer.switchSignature(currentValue)
            }

            FieldLabel {
                text: i18n("To:")
            }
            Field {
                id: toField
                Layout.fillWidth: true
                text: composer.draft.to ?? ""
                placeholderText: i18n("Recipients, separated by commas")
                onTextEdited: composer.dirty = true
            }

            FieldLabel {
                text: i18n("Cc:")
                visible: ccRow.visible
            }
            Field {
                id: ccField
                Layout.fillWidth: true
                visible: ccRow.visible
                text: composer.draft.cc ?? ""
                onTextEdited: composer.dirty = true
            }

            FieldLabel {
                text: i18n("Bcc:")
                visible: bccRow.visible
            }
            Field {
                id: bccField
                Layout.fillWidth: true
                visible: bccRow.visible
                text: composer.draft.bcc ?? ""
                onTextEdited: composer.dirty = true
            }

            FieldLabel {
                text: i18n("Subject:")
            }
            Field {
                id: subjectField
                Layout.fillWidth: true
                text: composer.draft.subject ?? ""
                onTextEdited: composer.dirty = true
            }
        }

        // Sichtbarkeit der Cc/Bcc-Zeilen (zu Beginn nur, wenn vorbelegt)
        QtObject {
            id: ccRow
            property bool visible: composer.draft.showCc ?? ((composer.draft.cc ?? "").length > 0 || (composer.draft.bcc ?? "").length > 0)
        }
        QtObject {
            id: bccRow
            property bool visible: ccRow.visible
        }

        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: composer.attachments.length > 0
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: composer.attachments
                Kirigami.Chip {
                    required property var modelData
                    required property int index
                    text: String(modelData).split("/").pop()
                    icon.name: "mail-attachment"
                    closable: true
                    checkable: false
                    onRemoved: {
                        const list = composer.attachments.slice();
                        list.splice(index, 1);
                        composer.attachments = list;
                        composer.dirty = true;
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            QQC2.TextArea {
                id: bodyArea
                color: Theme.textPrimary
                placeholderText: i18n("Write your message…")
                placeholderTextColor: Theme.textMuted
                text: composer.draft.body ?? ""
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                leftPadding: Kirigami.Units.gridUnit
                rightPadding: Kirigami.Units.gridUnit
                topPadding: Kirigami.Units.largeSpacing
                background: Item {}
                onTextChanged: composer.dirty = true
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        composer.send();
                        event.accepted = true;
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: Qt.alpha(Theme.dock, 0.4)
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 16
                spacing: 4
                IconButton {
                    iconName: "mail-attachment"
                    tooltip: i18n("Attach files")
                    onClicked: fileDialog.open()
                }
                IconButton {
                    iconName: "document-save"
                    tooltip: i18n("Save draft")
                    onClicked: composer.saveDraft()
                }
                IconButton {
                    iconName: "object-locked"
                    tooltip: i18n("Encryption (OpenPGP/S/MIME) follows in a later version")
                    enabled: false
                }
                Item {
                    Layout.fillWidth: true
                }
                IconButton {
                    text: i18n("Discard")
                    display: QQC2.AbstractButton.TextOnly
                    onClicked: composer.discard()
                }
                AccentButton {
                    text: i18n("Send")
                    iconName: "document-send"
                    iconAfterText: true
                    onClicked: composer.send()
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        fileMode: FileDialog.OpenFiles
        title: i18n("Attach Files")
        onAccepted: {
            const list = composer.attachments.slice();
            for (const url of selectedFiles)
                list.push(url);
            composer.attachments = list;
            composer.dirty = true;
        }
    }

    Kirigami.PromptDialog {
        id: discardDialog
        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.backgroundColor: Theme.elevated
        Kirigami.Theme.textColor: Theme.textPrimary
        Kirigami.Theme.highlightColor: Theme.accent
        Kirigami.Theme.highlightedTextColor: Theme.accentText
        Kirigami.Theme.hoverColor: Theme.accent
        Kirigami.Theme.focusColor: Theme.accent
        title: i18n("Discard message?")
        subtitle: i18n("The message has not been sent. Do you want to save it as a draft?")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        onAccepted: {
            if (composer.saveDraft())
                composer.finished();
        }
        onDiscarded: composer.discard()
    }

    component FieldLabel: QQC2.Label {
        Layout.preferredWidth: 64
        font.pointSize: Theme.fontXs
        color: Theme.textMuted
    }

    component Field: QQC2.TextField {
        font.pointSize: Theme.fontXs
        color: Theme.textPrimary
        placeholderTextColor: Theme.textMuted
        leftPadding: 0
        background: Item {
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.border
            }
        }
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        onActivated: {
            if (composer.hasFocusWithin())
                composer.send();
        }
    }
}

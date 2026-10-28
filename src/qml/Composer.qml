import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Fenster zum Verfassen einer Nachricht (neu, Antwort, Weiterleitung).
Kirigami.AbstractApplicationWindow {
    id: composer

    // Vorbelegung aus App.composeData()
    property var data: ({})
    property var attachments: data.attachments ?? []
    property bool dirty: false
    property bool closingConfirmed: false

    title: subjectField.text.length > 0 ? subjectField.text : i18n("New Message")
    width: Kirigami.Units.gridUnit * 42
    height: Kirigami.Units.gridUnit * 34
    minimumWidth: Kirigami.Units.gridUnit * 25
    minimumHeight: Kirigami.Units.gridUnit * 20

    function collect() {
        return {
            mode: data.mode ?? "new",
            referenceId: data.referenceId ?? 0,
            accountId: fromBox.currentValue,
            to: toField.text,
            cc: ccField.text,
            bcc: bccField.text,
            subject: subjectField.text,
            body: bodyArea.text,
            attachments: composer.attachments,
            inReplyTo: data.inReplyTo ?? "",
            references: data.references ?? []
        };
    }

    function send() {
        const error = App.sendMessage(collect());
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return;
        }
        closingConfirmed = true;
        close();
    }

    function saveDraft() {
        const error = App.saveDraft(collect());
        if (error.length > 0) {
            errorMessage.text = error;
            errorMessage.visible = true;
            return false;
        }
        dirty = false;
        showPassiveNotification(i18n("Draft saved."));
        return true;
    }

    onClosing: close => {
        if (dirty && !closingConfirmed) {
            close.accepted = false;
            discardDialog.open();
        }
    }

    Component.onCompleted: {
        if ((data.to ?? "").length > 0)
            bodyArea.forceActiveFocus();
        else
            toField.forceActiveFocus();
        bodyArea.cursorPosition = 0;
        dirty = false;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            RowLayout {
                anchors.fill: parent
                QQC2.Button {
                    text: i18n("Send")
                    icon.name: "document-send"
                    highlighted: true
                    onClicked: composer.send()
                }
                QQC2.ToolButton {
                    text: i18n("Attach")
                    icon.name: "mail-attachment"
                    onClicked: fileDialog.open()
                }
                QQC2.ToolButton {
                    text: i18n("Save Draft")
                    icon.name: "document-save"
                    onClicked: composer.saveDraft()
                }
                Item {
                    Layout.fillWidth: true
                }
                QQC2.ToolButton {
                    text: i18n("Cc/Bcc")
                    checkable: true
                    checked: ccRow.visible
                    onToggled: ccRow.visible = bccRow.visible = checked
                }
                QQC2.ToolButton {
                    icon.name: "edit-delete"
                    text: i18n("Discard")
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        composer.closingConfirmed = true;
                        composer.close();
                    }
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
            Layout.margins: Kirigami.Units.largeSpacing
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing

            QQC2.Label {
                text: i18n("From:")
                Layout.alignment: Qt.AlignRight
            }
            QQC2.ComboBox {
                id: fromBox
                Layout.fillWidth: true
                model: App.identities()
                textRole: "text"
                valueRole: "accountId"
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(composer.data.accountId))
            }

            QQC2.Label {
                text: i18n("To:")
                Layout.alignment: Qt.AlignRight
            }
            QQC2.TextField {
                id: toField
                Layout.fillWidth: true
                text: composer.data.to ?? ""
                placeholderText: i18n("Recipients, separated by commas")
                onTextEdited: composer.dirty = true
            }

            QQC2.Label {
                text: i18n("Cc:")
                Layout.alignment: Qt.AlignRight
                visible: ccRow.visible
            }
            QQC2.TextField {
                id: ccField
                Layout.fillWidth: true
                visible: ccRow.visible
                text: composer.data.cc ?? ""
                onTextEdited: composer.dirty = true
            }

            QQC2.Label {
                text: i18n("Bcc:")
                Layout.alignment: Qt.AlignRight
                visible: bccRow.visible
            }
            QQC2.TextField {
                id: bccField
                Layout.fillWidth: true
                visible: bccRow.visible
                onTextEdited: composer.dirty = true
            }

            QQC2.Label {
                text: i18n("Subject:")
                Layout.alignment: Qt.AlignRight
            }
            QQC2.TextField {
                id: subjectField
                Layout.fillWidth: true
                text: composer.data.subject ?? ""
                onTextEdited: composer.dirty = true
            }
        }

        // Sichtbarkeit der Cc/Bcc-Zeilen (zu Beginn nur, wenn vorbelegt)
        QtObject {
            id: ccRow
            property bool visible: (composer.data.cc ?? "").length > 0
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
                text: composer.data.body ?? ""
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                font.family: "monospace"
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
        title: i18n("Discard message?")
        subtitle: i18n("The message has not been sent. Do you want to save it as a draft?")
        standardButtons: Kirigami.Dialog.Save | Kirigami.Dialog.Discard | Kirigami.Dialog.Cancel
        onAccepted: {
            if (composer.saveDraft()) {
                composer.closingConfirmed = true;
                composer.close();
            }
        }
        onDiscarded: {
            composer.closingConfirmed = true;
            composer.close();
        }
    }

    Shortcut {
        sequence: "Ctrl+Return"
        onActivated: composer.send()
    }
}

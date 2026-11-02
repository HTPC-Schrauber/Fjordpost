import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Adressbuch: Liste mit Suche, rechts die Kontaktkarte.
Rectangle {
    id: page

    property var selectedId: 0
    readonly property var contact: {
        App.pim.revision;
        return selectedId > 0 ? App.pim.contact(selectedId) : ({});
    }
    readonly property bool hasContact: contact.itemId !== undefined

    function openTransfer() {
        transfer.openFor(selectedId, contact.name, contact.collectionId, contact.readOnly);
    }
    // Nur für --dev-action contact-transfer; die Liste ist nach dem Seitenwechsel noch nicht gezeichnet.
    function devTransfer() {
        devTimer.start();
    }
    Timer {
        id: devTimer
        interval: 500
        onTriggered: {
            page.selectedId = contactList.itemAtIndex(0).itemId;
            page.openTransfer();
        }
    }

    signal newContactRequested()
    signal editContactRequested(var itemId)
    signal composeRequested(string address)
    signal addAccountRequested()

    color: Theme.reading

    TransferDialog {
        id: transfer
        parent: QQC2.Overlay.overlay
        kind: "contacts"
        onDone: moved => {
            if (moved)
                page.selectedId = 0;
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            color: Theme.list

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                AccentButton {
                    Layout.fillWidth: true
                    text: i18n("New Contact")
                    iconName: "contact-new"
                    enabled: App.pim.hasDavAccounts
                    onClicked: page.newContactRequested()
                }
                SearchBox {
                    Layout.fillWidth: true
                    placeholderText: i18n("Search contacts…")
                    onSearchRequested: text => App.pim.contacts.search = text
                }
                CollectionList {
                    Layout.fillWidth: true
                    visible: App.pim.hasDavAccounts
                    kind: "contacts"
                    title: i18n("Address Books")
                }
                ListView {
                    id: contactList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: App.pim.contacts
                    section.property: "letter"
                    section.delegate: QQC2.Label {
                        width: ListView.view.width
                        topPadding: 8
                        bottomPadding: 2
                        leftPadding: 4
                        text: section
                        font.pointSize: Theme.fontXxs
                        font.weight: Font.DemiBold
                        color: Theme.textSecondary
                    }
                    delegate: QQC2.AbstractButton {
                        required property var itemId
                        required property string name
                        required property string subtitle
                        required property string email
                        required property string bookColor
                        required property string bookName
                        width: ListView.view.width
                        implicitHeight: 46
                        hoverEnabled: true
                        onClicked: page.selectedId = itemId
                        background: Rectangle {
                            radius: Theme.radius
                            color: page.selectedId === itemId ? Qt.alpha(Theme.accent, 0.18) : (parent.hovered ? Theme.card : "transparent")
                        }
                        QQC2.ToolTip.text: bookName
                        QQC2.ToolTip.visible: hovered && bookName.length > 0
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        contentItem: RowLayout {
                            spacing: 8
                            // Farbe des Adressbuchs, wie bei Terminen im Kalender
                            Rectangle {
                                Layout.leftMargin: 2
                                implicitWidth: 3
                                implicitHeight: 30
                                radius: 2
                                color: bookColor
                            }
                            InitialsAvatar {
                                size: 32
                                name: parent.parent.name
                                initials: Theme.initials(parent.parent.name)
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    text: name
                                    elide: Text.ElideRight
                                    font.pointSize: Theme.fontXs
                                    font.weight: Font.Medium
                                    color: Theme.textPrimary
                                }
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: email.length > 0 ? email : subtitle
                                    elide: Text.ElideRight
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textSecondary
                                }
                            }
                        }
                    }
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    visible: contactList.count === 0 && App.pim.hasDavAccounts
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("No contacts found")
                    font.pointSize: Theme.fontXs
                    color: Theme.textMuted
                }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        // Kontaktkarte
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                visible: !App.pim.hasDavAccounts
                spacing: 12
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("No address book connected yet")
                    font.pointSize: Theme.fontBase * 1.1
                    color: Theme.textPrimary
                }
                AccentButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Connect Account…")
                    onClicked: page.addAccountRequested()
                }
            }

            QQC2.Label {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: App.pim.hasDavAccounts && !page.hasContact
                text: i18n("Select a contact")
                font.pointSize: Theme.fontXs
                color: Theme.textMuted
            }

            QQC2.ScrollView {
                anchors.fill: parent
                visible: page.hasContact
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: 28
                        Layout.bottomMargin: 0
                        spacing: 16
                        InitialsAvatar {
                            size: 72
                            name: page.contact.name ?? ""
                            initials: Theme.initials(page.contact.name)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: page.contact.name ?? ""
                                font.pointSize: Theme.fontLg
                                font.weight: Font.DemiBold
                                color: Theme.textPrimary
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: [page.contact.jobTitle, page.contact.organization].filter(t => t && t.length > 0).join(" · ")
                                font.pointSize: Theme.fontSm
                                color: Theme.textSecondary
                                elide: Text.ElideRight
                            }
                            RowLayout {
                                spacing: 6
                                Rectangle {
                                    implicitWidth: 10
                                    implicitHeight: 10
                                    radius: 5
                                    color: page.contact.bookColor ?? "transparent"
                                }
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    text: page.contact.bookName ?? ""
                                    font.pointSize: Theme.fontXs
                                    color: Theme.textMuted
                                }
                            }
                        }
                        IconButton {
                            iconName: "go-jump"
                            tooltip: i18n("Move or Copy…")
                            onClicked: page.openTransfer()
                        }
                        IconButton {
                            iconName: "document-edit"
                            tooltip: i18n("Edit")
                            visible: !page.contact.readOnly
                            onClicked: page.editContactRequested(page.selectedId)
                        }
                        IconButton {
                            iconName: "edit-delete"
                            tooltip: i18n("Delete")
                            hoverColor: Theme.danger
                            visible: !page.contact.readOnly
                            onClicked: {
                                App.pim.deleteContact(page.selectedId);
                                page.selectedId = 0;
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 28
                        Layout.rightMargin: 28
                        Layout.bottomMargin: 28
                        spacing: 12

                        DetailRow {
                            label: i18n("Email")
                            visible: (page.contact.emails ?? []).length > 0
                            Repeater {
                                model: page.contact.emails ?? []
                                QQC2.Label {
                                    required property string modelData
                                    text: "<a href=\"#\">" + modelData.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</a>"
                                    textFormat: Text.StyledText
                                    linkColor: Theme.accent
                                    font.pointSize: Theme.fontSm
                                    onLinkActivated: page.composeRequested(modelData)
                                    HoverHandler {
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                }
                            }
                        }
                        DetailRow {
                            label: i18n("Phone")
                            visible: (page.contact.phones ?? []).length > 0
                            Repeater {
                                model: page.contact.phones ?? []
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    required property string modelData
                                    text: modelData
                                    font.pointSize: Theme.fontSm
                                    color: Theme.textPrimary
                                }
                            }
                        }
                        DetailRow {
                            label: i18n("Address")
                            visible: (page.contact.address ?? "").length > 0
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                text: page.contact.address ?? ""
                                wrapMode: Text.Wrap
                                font.pointSize: Theme.fontSm
                                color: Theme.textPrimary
                            }
                        }
                        DetailRow {
                            label: i18n("Birthday")
                            visible: (page.contact.birthdayMs ?? 0) > 0
                            QQC2.Label {
                                textFormat: Text.PlainText
                                text: new Date(page.contact.birthdayMs ?? 0).toLocaleDateString(Qt.locale(), Locale.LongFormat)
                                font.pointSize: Theme.fontSm
                                color: Theme.textPrimary
                            }
                        }
                        DetailRow {
                            label: i18n("Note")
                            visible: (page.contact.note ?? "").length > 0
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: page.contact.note ?? ""
                                wrapMode: Text.Wrap
                                textFormat: Text.PlainText
                                font.pointSize: Theme.fontSm
                                color: Theme.textPrimary
                            }
                        }
                    }
                }
            }
        }
    }

    component DetailRow: RowLayout {
        property string label
        default property alias content: valueColumn.data
        Layout.fillWidth: true
        spacing: 16
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.preferredWidth: 90
            Layout.alignment: Qt.AlignTop
            text: parent.label
            font.pointSize: Theme.fontXs
            color: Theme.textSecondary
        }
        ColumnLayout {
            id: valueColumn
            Layout.fillWidth: true
            spacing: 4
        }
    }
}

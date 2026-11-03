import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Rechte Seitenleiste (Mailbird-Stil): Kontakt, Agenda, Aufgaben.
Rectangle {
    id: drawer

    readonly property var msg: App.currentMessage
    readonly property bool hasMessage: msg.id !== undefined

    // Bekannter Kontakt zum Absender (leer, wenn nicht im Adressbuch)
    readonly property var contact: {
        App.pim.revision;
        return hasMessage ? App.pim.contactByEmail(msg.fromEmail) : ({});
    }
    readonly property bool hasContact: contact.itemId !== undefined
    readonly property var agenda: {
        App.pim.revision;
        const start = new Date();
        start.setHours(0, 0, 0, 0);
        return App.pim.occurrences(start.getTime(), start.getTime() + 86400000);
    }
    readonly property var openTasks: {
        App.pim.revision;
        return App.pim.tasks(0, false).slice(0, 5);
    }

    signal closeRequested()
    signal newTaskRequested(string summary, string description)
    signal newContactRequested(string name, string email)
    signal editEventRequested(var itemId, var startMs)
    signal editTaskRequested(var itemId)
    signal notify(string message)

    color: Theme.sidebar

    Rectangle {
        width: 1
        height: parent.height
        color: Theme.border
    }

    QQC2.ScrollView {
        anchors.fill: parent
        anchors.leftMargin: 1
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            width: drawer.width - 1
            spacing: 20

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.leftMargin: 16
                Layout.rightMargin: 12
                SectionLabel {
                    Layout.fillWidth: true
                    text: i18n("Contact & Context")
                }
                IconButton {
                    iconName: "window-close"
                    iconSize: 14
                    tooltip: i18n("Close sidebar")
                    onClicked: drawer.closeRequested()
                }
            }

            // Kontaktkarte
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                implicitHeight: contactColumn.implicitHeight + 32
                radius: Theme.radiusXl
                color: Qt.alpha(Theme.card, 0.7)
                border.width: 1
                border.color: Theme.border

                ColumnLayout {
                    id: contactColumn
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    InitialsAvatar {
                        Layout.alignment: Qt.AlignHCenter
                        size: 56
                        name: drawer.hasMessage ? (drawer.msg.fromName ?? "") : ""
                        initials: drawer.hasMessage ? Theme.initials(drawer.msg.fromName) : "–"
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: drawer.hasMessage ? drawer.msg.fromName : i18n("No message selected")
                        font.pointSize: Theme.fontSm
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: drawer.hasMessage
                        horizontalAlignment: Text.AlignHCenter
                        text: drawer.msg.fromEmail ?? ""
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.textMuted
                        elide: Text.ElideMiddle
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: text.length > 0
                        horizontalAlignment: Text.AlignHCenter
                        text: drawer.hasContact ? [drawer.contact.jobTitle, drawer.contact.organization].filter(t => t && t.length > 0).join(" · ") : ""
                        font.pointSize: Theme.fontXxs
                        color: Theme.textSecondary
                        wrapMode: Text.Wrap
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: drawer.hasContact && (drawer.contact.phones ?? []).length > 0
                        horizontalAlignment: Text.AlignHCenter
                        text: (drawer.contact.phones ?? [""])[0] ?? ""
                        font.pointSize: Theme.fontXxs
                        color: Theme.textSecondary
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        visible: drawer.hasMessage
                        spacing: 8
                        IconButton {
                            visible: !drawer.hasContact && App.pim.hasDavAccounts
                            iconName: "contact-new"
                            iconSize: 14
                            tooltip: i18n("Add to contacts")
                            onClicked: drawer.newContactRequested(drawer.msg.fromName, drawer.msg.fromEmail)
                        }
                        IconButton {
                            display: QQC2.AbstractButton.TextBesideIcon
                            iconName: "view-history"
                            iconSize: 12
                            text: i18np("%1 mail", "%1 mails", drawer.hasMessage ? App.messageCountFrom(drawer.msg.fromEmail) : 0)
                            tooltip: i18n("Show all messages from this contact")
                            onClicked: App.showMessagesFrom(drawer.msg.fromEmail)
                        }
                    }
                }
            }

            // Agenda
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                spacing: 8
                RowLayout {
                    SectionLabel {
                        Layout.fillWidth: true
                        text: i18n("Today's Agenda")
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        text: i18n("Today")
                        font.family: "monospace"
                        font.pointSize: Theme.fontXxs
                        color: Theme.accent
                    }
                }
                Placeholder {
                    visible: !App.pim.hasDavAccounts
                    text: i18n("Your appointments appear here once a calendar (CalDAV) is connected.")
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    visible: App.pim.hasDavAccounts && drawer.agenda.length === 0
                    text: i18n("Nothing scheduled today")
                    font.pointSize: Theme.fontXs
                    color: Theme.textMuted
                }
                Repeater {
                    model: drawer.agenda
                    delegate: QQC2.AbstractButton {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 38
                        hoverEnabled: true
                        onClicked: drawer.editEventRequested(modelData.itemId, modelData.startMs)
                        background: Rectangle {
                            radius: Theme.radiusSm
                            color: parent.hovered ? Theme.card : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Rectangle {
                                implicitWidth: 3
                                Layout.fillHeight: true
                                Layout.topMargin: 3
                                Layout.bottomMargin: 3
                                radius: 2
                                color: modelData.color
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    elide: Text.ElideRight
                                    font.pointSize: Theme.fontXs
                                    color: Theme.textPrimary
                                }
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    text: modelData.timeText
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textSecondary
                                }
                            }
                        }
                    }
                }
            }

            // Aufgaben
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.bottomMargin: 16
                spacing: 8
                RowLayout {
                    SectionLabel {
                        Layout.fillWidth: true
                        text: i18n("Tasks")
                    }
                    IconButton {
                        visible: App.pim.hasDavAccounts
                        iconName: "list-add"
                        iconSize: 12
                        tooltip: drawer.hasMessage ? i18n("New task from this message") : i18n("New task")
                        onClicked: drawer.newTaskRequested(drawer.hasMessage ? drawer.msg.subject : "", "")
                    }
                }
                Placeholder {
                    visible: !App.pim.hasDavAccounts
                    text: i18n("Your tasks appear here once a calendar (CalDAV) is connected.")
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    visible: App.pim.hasDavAccounts && drawer.openTasks.length === 0
                    text: i18n("Nothing to do")
                    font.pointSize: Theme.fontXs
                    color: Theme.textMuted
                }
                Repeater {
                    model: drawer.openTasks
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 4
                        TaskCheck {
                            checked: false
                            onToggled: {
                                const error = App.pim.setTaskCompleted(parent.modelData.itemId, true);
                                if (error.length > 0)
                                    drawer.notify(error);
                            }
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: parent.modelData.title
                            elide: Text.ElideRight
                            font.pointSize: Theme.fontXs
                            color: Theme.textPrimary
                            TapHandler {
                                onTapped: drawer.editTaskRequested(parent.parent.modelData.itemId)
                            }
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            visible: text.length > 0
                            text: parent.modelData.dueText
                            font.pointSize: Theme.fontXxs
                            color: parent.modelData.overdue ? Theme.danger : Theme.textSecondary
                        }
                    }
                }
            }
        }
    }

    component Placeholder: Rectangle {
        property alias text: label.text
        Layout.fillWidth: true
        implicitHeight: label.implicitHeight + 20
        radius: Theme.radiusLg
        color: Qt.alpha(Theme.card, 0.5)
        border.width: 1
        border.color: Theme.border
        QQC2.Label {
            textFormat: Text.PlainText
            id: label
            anchors.fill: parent
            anchors.margins: 10
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }
    }
}

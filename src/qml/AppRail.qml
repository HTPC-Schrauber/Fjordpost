import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core
import "components"

// Schmale Leiste ganz links: Konten, Module, Darstellung, Einstellungen.
Rectangle {
    id: rail

    property string section: "mail"
    signal sectionRequested(string name)
    signal settingsRequested()
    signal addAccountRequested()

    implicitWidth: 56
    color: Theme.dock

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 10

        // Logo
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 36
            Layout.preferredHeight: 36
            radius: Theme.radiusLg
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.accentHover }
            }
            QQC2.Label {
                anchors.centerIn: parent
                text: "M"
                color: "white"
                font.weight: Font.ExtraBold
                font.pointSize: Theme.fontSm
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 28
            Layout.preferredHeight: 1
            color: Theme.border
        }

        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("All Inboxes")
            iconName: "mail-folder-inbox"
            filled: true
            selected: rail.section === "mail" && App.selectedAccountId === 0
            badge: App.totalUnread
            onClicked: {
                rail.sectionRequested("mail");
                App.selectAccount(0);
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 40
            model: App.accounts
            spacing: 10
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            delegate: RailButton {
                required property var accountId
                required property string name
                required property string email
                required property color accountColor
                required property int unreadCount

                width: ListView.view.width
                text: name + "\n" + email
                initials: Theme.initials(name)
                tint: accountColor
                selected: rail.section === "mail" && App.selectedAccountId === accountId
                badge: unreadCount
                onClicked: {
                    rail.sectionRequested("mail");
                    App.selectAccount(accountId);
                }
            }
            footer: Item {
                width: ListView.view.width
                height: 38
                IconButton {
                    anchors.centerIn: parent
                    anchors.verticalCenterOffset: 5
                    iconName: "list-add"
                    tooltip: i18n("Add account")
                    onClicked: rail.addAccountRequested()
                }
            }
        }

        // Module
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: modules.implicitHeight + 24
            color: "transparent"
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Rectangle { width: parent.width; height: 1; color: Theme.border; anchors.bottom: parent.bottom }

            ColumnLayout {
                id: modules
                anchors.centerIn: parent
                spacing: 6
                RailButton {
                    subtle: true
                    text: i18n("Mail (Ctrl+1)")
                    iconName: "mail-message"
                    selected: rail.section === "mail"
                    onClicked: rail.sectionRequested("mail")
                }
                RailButton {
                    subtle: true
                    text: i18n("Calendar (Ctrl+2)")
                    iconName: "view-calendar"
                    selected: rail.section === "calendar"
                    onClicked: rail.sectionRequested("calendar")
                }
                RailButton {
                    subtle: true
                    text: i18n("Contacts (Ctrl+3)")
                    iconName: "view-pim-contacts"
                    selected: rail.section === "contacts"
                    onClicked: rail.sectionRequested("contacts")
                }
                RailButton {
                    subtle: true
                    text: i18n("Tasks (Ctrl+4)")
                    iconName: "view-task"
                    selected: rail.section === "tasks"
                    onClicked: rail.sectionRequested("tasks")
                }
                RailButton {
                    subtle: true
                    text: i18n("Messages with attachments (Ctrl+5)")
                    iconName: "mail-attachment"
                    selected: rail.section === "mail" && App.quickFilter === "attachments"
                    onClicked: {
                        rail.sectionRequested("mail");
                        App.quickFilter = App.quickFilter === "attachments" ? "" : "attachments";
                    }
                }
            }
        }

        // Darstellung & Einstellungen
        RailButton {
            id: accentButton
            subtle: true
            text: i18n("Appearance")
            iconName: ""
            initials: ""
            onClicked: appearancePopup.open()
            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 16
                radius: 8
                color: Theme.accent
                border.width: 3
                border.color: Qt.alpha(Theme.accent, 0.3)
            }

            QQC2.Popup {
                id: appearancePopup
                x: parent.width + 4
                y: parent.height - height
                padding: 12
                background: Rectangle {
                    radius: Theme.radiusLg
                    color: Theme.elevated
                    border.width: 1
                    border.color: Theme.border
                }
                contentItem: ColumnLayout {
                    spacing: 10
                    SectionLabel {
                        text: i18n("Accent color")
                    }
                    RowLayout {
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
                    QQC2.CheckBox {
                        text: i18n("Use KDE color scheme")
                        checked: Theme.settings.useSystemColors
                        onToggled: Theme.settings.useSystemColors = checked
                    }
                }
            }
        }
        RailButton {
            subtle: true
            text: Theme.dark ? i18n("Switch to light mode") : i18n("Switch to dark mode")
            iconName: Theme.dark ? "weather-clear" : "weather-clear-night"
            enabled: !Theme.system
            onClicked: Theme.settings.dark = !Theme.settings.dark
        }
        RailButton {
            subtle: true
            text: i18n("Settings")
            iconName: "configure"
            onClicked: rail.settingsRequested()
        }
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Schmale, dunkle Leiste ganz links (Mailbird-Stil): Unified Inbox,
// Konten, Bereichswechsel (Mail/Kalender/Kontakte) und Einstellungen.
Rectangle {
    id: rail

    property string section: "mail"
    signal sectionRequested(string name)
    signal settingsRequested()
    signal addAccountRequested()

    implicitWidth: Kirigami.Units.gridUnit * 3.5
    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Complementary
    color: Kirigami.Theme.backgroundColor

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Kirigami.Units.largeSpacing
        anchors.bottomMargin: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("All Inboxes")
            iconName: "mail-folder-inbox"
            selected: rail.section === "mail" && App.selectedAccountId === 0
            badge: App.totalUnread
            onClicked: {
                rail.sectionRequested("mail");
                App.selectAccount(0);
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                model: App.accounts
                spacing: Kirigami.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds
                delegate: RailButton {
                    required property var accountId
                    required property string name
                    required property string email
                    required property color accountColor
                    required property int unreadCount

                    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                    text: name + "\n" + email
                    avatarColor: accountColor
                    selected: rail.section === "mail" && App.selectedAccountId === accountId
                    badge: unreadCount
                    onClicked: {
                        rail.sectionRequested("mail");
                        App.selectAccount(accountId);
                    }
                }
                footer: RailButton {
                    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                    text: i18n("Add account")
                    iconName: "list-add"
                    onClicked: rail.addAccountRequested()
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
        }

        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Mail")
            iconName: "mail-message"
            onClicked: rail.sectionRequested("mail")
        }
        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Calendar")
            iconName: "view-calendar"
            selected: rail.section === "calendar"
            onClicked: rail.sectionRequested("calendar")
        }
        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Contacts")
            iconName: "view-pim-contacts"
            selected: rail.section === "contacts"
            onClicked: rail.sectionRequested("contacts")
        }
        RailButton {
            Layout.alignment: Qt.AlignHCenter
            text: i18n("Settings")
            iconName: "configure"
            onClicked: rail.settingsRequested()
        }
    }
}

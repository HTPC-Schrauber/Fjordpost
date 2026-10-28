import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

Kirigami.AbstractApplicationWindow {
    id: root

    title: App.totalUnread > 0 ? i18nc("@title:window", "MopMail (%1 unread)", App.totalUnread) : "MopMail"
    width: Kirigami.Units.gridUnit * 70
    height: Kirigami.Units.gridUnit * 44
    minimumWidth: Kirigami.Units.gridUnit * 40
    minimumHeight: Kirigami.Units.gridUnit * 25

    // Aktiver Bereich der App-Leiste: "mail", "calendar", "contacts"
    property string section: "mail"

    function notImplemented(feature) {
        showPassiveNotification(i18n("%1 is coming in a later milestone.", feature));
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        AppRail {
            Layout.fillHeight: true
            section: root.section
            onSectionRequested: name => {
                if (name === "mail")
                    root.section = name;
                else
                    root.notImplemented(name === "calendar" ? i18n("Calendar") : i18n("Contacts"));
            }
            onSettingsRequested: root.notImplemented(i18n("Settings"))
            onAddAccountRequested: root.notImplemented(i18n("Account setup"))
        }

        QQC2.SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            FolderPane {
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 12
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 9
                onComposeRequested: root.notImplemented(i18n("Composing messages"))
            }

            MessageListPane {
                id: messageList
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 22
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 15
            }

            ReaderPane {
                QQC2.SplitView.fillWidth: true
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 18
                onReplyRequested: root.notImplemented(i18n("Replying"))
            }
        }
    }

    // Globale Tastenkürzel (an Mailbird/Gmail angelehnt)
    Shortcut {
        sequences: [StandardKey.Find, "/"]
        onActivated: messageList.focusSearch()
    }
    Shortcut {
        sequence: StandardKey.New
        onActivated: root.notImplemented(i18n("Composing messages"))
    }
}

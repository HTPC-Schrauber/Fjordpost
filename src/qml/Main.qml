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

    function compose(messageId, mode) {
        const window = composerComponent.createObject(null, {
            data: App.composeData(messageId, mode)
        });
        window.closing.connect(() => window.destroy());
        window.show();
    }

    Component {
        id: composerComponent
        Composer {}
    }

    AccountSetupDialog {
        id: accountSetup
        parent: root.overlay
    }

    Connections {
        target: App
        function onNotify(message) {
            root.showPassiveNotification(message);
        }
    }

    // Nur für Entwicklung/Tests (--dev-action)
    function devAction(name) {
        if (name === "open-first" || name === "reply") {
            messageList.openFirst();
            if (name === "reply")
                compose(App.currentMessage.id, "reply");
        } else if (name === "compose") {
            compose(0, "new");
        } else if (name === "setup") {
            accountSetup.open();
        }
    }

    Component.onCompleted: {
        if (!App.hasAccounts)
            accountSetup.open();
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
            onAddAccountRequested: accountSetup.open()
        }

        QQC2.SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            FolderPane {
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 12
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 9
                onComposeRequested: root.compose(0, "new")
            }

            MessageListPane {
                id: messageList
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 22
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 15
            }

            ReaderPane {
                QQC2.SplitView.fillWidth: true
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 18
                onReplyRequested: mode => root.compose(App.currentMessage.id, mode)
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
        onActivated: root.compose(0, "new")
    }
    Shortcut {
        sequences: [StandardKey.Refresh, "F5"]
        onActivated: App.syncNow()
    }
}

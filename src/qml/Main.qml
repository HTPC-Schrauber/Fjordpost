import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core
import "components"

Kirigami.AbstractApplicationWindow {
    id: root

    title: App.totalUnread > 0 ? i18nc("@title:window", "MopMail (%1 unread)", App.totalUnread) : "MopMail"
    width: 1500
    height: 900
    minimumWidth: 1000
    minimumHeight: 560
    color: Theme.reading

    // Bedienelemente (Dialoge, Eingabefelder) an das MopMail-Theme anpassen
    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.backgroundColor: Theme.reading
    Kirigami.Theme.alternateBackgroundColor: Theme.card
    Kirigami.Theme.textColor: Theme.textPrimary
    Kirigami.Theme.disabledTextColor: Theme.textMuted
    Kirigami.Theme.highlightColor: Theme.accent
    Kirigami.Theme.highlightedTextColor: Theme.accentText
    Kirigami.Theme.hoverColor: Theme.accent
    Kirigami.Theme.focusColor: Theme.accent
    Kirigami.Theme.linkColor: Theme.accent

    // Aktiver Bereich der App-Leiste: "mail", "calendar", "contacts", "tasks"
    property string section: "mail"

    function notify(message) {
        toast.show(message);
    }

    function compose(messageId, mode) {
        const window = composerComponent.createObject(null, {
            data: App.composeData(messageId, mode)
        });
        window.closing.connect(() => window.destroy());
        window.show();
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
        } else if (name === "light") {
            Theme.settings.dark = false;
            messageList.openFirst();
        }
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
            root.notify(message);
        }
    }

    Component.onCompleted: {
        if (!App.hasAccounts)
            accountSetup.open();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        HeaderBar {
            Layout.fillWidth: true
            onSearchRequested: messageList.focusSearch()
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            AppRail {
                Layout.fillHeight: true
                section: root.section
                onSectionRequested: name => {
                    if (name === "mail")
                        root.section = name;
                    else
                        root.notify(i18n("%1 will be available with CalDAV/CardDAV support.", name === "calendar" ? i18n("The calendar") : name === "contacts" ? i18n("The address book") : i18n("The task list")));
                }
                onSettingsRequested: root.notify(i18n("Settings are coming in a later version."))
                onAddAccountRequested: accountSetup.open()
            }

            QQC2.SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                handle: Item {
                    implicitWidth: 3
                }

                FolderPane {
                    QQC2.SplitView.preferredWidth: 240
                    QQC2.SplitView.minimumWidth: 180
                    onComposeRequested: root.compose(0, "new")
                }

                MessageListPane {
                    id: messageList
                    QQC2.SplitView.preferredWidth: 370
                    QQC2.SplitView.minimumWidth: 280
                }

                ReaderPane {
                    QQC2.SplitView.fillWidth: true
                    QQC2.SplitView.minimumWidth: 380
                    position: messageList.list.currentIndex
                    total: messageList.list.count
                    drawerVisible: Theme.settings.contextDrawer
                    onReplyRequested: mode => root.compose(App.currentMessage.id, mode)
                    onNavigate: delta => messageList.step(delta)
                    onToggleDrawer: Theme.settings.contextDrawer = !Theme.settings.contextDrawer
                    onNotify: message => root.notify(message)
                }

                ContextDrawer {
                    visible: Theme.settings.contextDrawer
                    QQC2.SplitView.preferredWidth: 288
                    QQC2.SplitView.minimumWidth: 220
                    onCloseRequested: Theme.settings.contextDrawer = false
                }
            }
        }
    }

    Toast {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        z: 100
    }

    // Globale Tastenkürzel (an Mailbird/Gmail angelehnt)
    Shortcut {
        sequences: [StandardKey.Find, "Ctrl+K", "/"]
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
    Shortcut {
        sequence: "Ctrl+R"
        enabled: App.currentMessage.id !== undefined
        onActivated: root.compose(App.currentMessage.id, "reply")
    }
    Shortcut {
        sequence: "Ctrl+Shift+R"
        enabled: App.currentMessage.id !== undefined
        onActivated: root.compose(App.currentMessage.id, "replyAll")
    }
    Shortcut {
        sequence: "Ctrl+L"
        enabled: App.currentMessage.id !== undefined
        onActivated: root.compose(App.currentMessage.id, "forward")
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: root.section = "mail"
    }
}

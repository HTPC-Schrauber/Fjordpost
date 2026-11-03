import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

Kirigami.AbstractApplicationWindow {
    id: root

    title: App.totalUnread > 0 ? i18nc("@title:window", "Fjordpost (%1 unread)", App.totalUnread) : "Fjordpost"
    width: 1500
    height: 900
    minimumWidth: 1000
    minimumHeight: 560
    color: Theme.reading

    // Das Tray-Symbol hält die App am Leben, sie endet also nicht von selbst mit dem letzten Fenster.
    onClosing: close => {
        if (Prefs.general.closeToTray)
            return;
        close.accepted = confirmQuit();
        if (close.accepted)
            Qt.quit();
    }

    // Entwürfe gehen bei IMAP asynchron zum Server: Beim Beenden lässt sich der Text nicht mehr sicher ablegen.
    function confirmQuit(): bool {
        if (!inlineComposer.item || !inlineComposer.item.dirty)
            return true;
        root.show();
        root.raise();
        root.notify(i18n("Your message is still open: send it, save it as a draft or discard it."));
        return false;
    }

    // Bedienelemente (Dialoge, Eingabefelder) an das Fjordpost-Theme anpassen
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

    // Anwendungsweites KDE-Farbschema aus den Theme-Farben, damit auch Dialoge,
    // Menüs und Auswahllisten passen. Mit "KDE-Farbschema verwenden" die Systemfarben.
    function applyColorScheme() {
        if (Theme.system) {
            App.applyColorScheme({});
            return;
        }
        App.applyColorScheme({
            view: String(Theme.elevated),
            viewAlternate: String(Theme.card),
            window: String(Theme.sidebar),
            button: String(Theme.card),
            header: String(Theme.dock),
            text: String(Theme.textPrimary),
            textInactive: String(Theme.textMuted),
            accent: String(Theme.accent),
            accentText: String(Theme.accentText),
            negative: String(Theme.danger),
            neutral: String(Theme.warning),
            positive: String(Theme.success)
        });
    }
    readonly property string colorSchemeKey: Theme.system ? "system" : [Theme.dark, Theme.settings.accent].join()
    onColorSchemeKeyChanged: applyColorScheme()

    function notify(message) {
        toast.show(message);
    }

    function compose(messageId, mode) {
        openComposer(App.composeData(messageId, mode), false);
    }

    // Öffnet den Composer je nach Einstellung im Hauptfenster oder in einem eigenen
    // Fenster. Ist im Hauptfenster schon eine angefangene Nachricht offen, bekommt
    // die neue ein eigenes Fenster, damit nichts verloren geht.
    function openComposer(data, forceWindow) {
        const inlineBusy = inlineComposer.item !== null && inlineComposer.item.dirty;
        if (!forceWindow && Prefs.compose.openIn === "inline" && !inlineBusy) {
            inlineComposer.active = false;
            inlineComposer.composeData = data;
            inlineComposer.openedFor = App.currentMessage.id;
            inlineComposer.active = true;
            inlineComposer.item.forceActiveFocus();
            return;
        }
        const window = composerWindowComponent.createObject(null, {
            draft: data
        });
        window.notify.connect(root.notify);
        window.closing.connect(close => {
            if (!close.accepted)
                return;
            window.destroy();
            // Das Beenden kann an der Rückfrage dieses Fensters hängen geblieben sein.
            if (!root.visible && !Prefs.general.closeToTray)
                Qt.quit();
        });
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
        } else if (name === "compose-window") {
            openComposer(App.composeData(0, "new"), true);
        } else if (name === "open-thread" || name === "expand-thread" || name === "thread-history") {
            messageList.openFirstConversation(name === "expand-thread");
            if (name === "thread-history")
                readerPane.showWholeConversation();
        } else if (name === "label-menu") {
            messageList.openFirst();
            readerPane.openLabelMenu();
        } else if (name === "label-dialog") {
            labelDialog.openNew(0);
        } else if (name === "label-filter") {
            const first = App.labels.index(0, 0);
            App.selectLabel(App.labels.data(first, Qt.UserRole + 1), App.labels.data(first, Qt.UserRole + 2));
        } else if (name === "snooze-menu") {
            messageList.openFirst();
            readerPane.openSnoozeMenu();
        } else if (name === "snooze-date") {
            messageList.openFirst();
            readerPane.openSnoozePicker();
        } else if (name === "snoozed") {
            messageList.openFirst();
            App.snoozeMessage(App.currentMessage.id, App.snoozeOptions()[0].time);
            App.selectFolder(0, "snoozed", i18n("Snoozed"));
        } else if (name === "filter-rules") {
            filterRules.open();
        } else if (name === "filter-rule") {
            filterRules.open();
            filterRules.editRule(null);
        } else if (name === "undo-send") {
            messageList.openFirst();
            App.sendQuickReply("Test");
        } else if (name === "compose-completion") {
            compose(0, "new");
            inlineComposer.item.showCompletionPreview();
        } else if (name === "calendar") {
            root.section = "calendar";
        } else if (name === "calendar-week") {
            root.section = "calendar";
            calendarPage.view = "week";
        } else if (name === "calendar-event") {
            root.section = "calendar";
            eventDialog.openNew(new Date(), false);
        } else if (name === "event-delete-series") {
            // zweites Vorkommnis des wöchentlichen Demo-Termins "Lauftreff"
            root.section = "calendar";
            const now = new Date();
            const runs = App.pim.occurrences(now.getTime(), now.getTime() + 21 * 86400000).filter(o => o.recurring && o.title === "Lauftreff");
            eventDialog.openEdit(runs[1].itemId, runs[1].startMs);
            eventDialog.confirmRemove();
        } else if (name === "collection-new") {
            root.section = "calendar";
            calendarPage.collections.create();
        } else if (name === "collection-edit") {
            root.section = "calendar";
            calendarPage.collections.edit(calendarPage.collections.entries[0]);
        } else if (name === "collection-delete") {
            root.section = "calendar";
            calendarPage.collections.remove(calendarPage.collections.entries[0]);
        } else if (name === "contacts") {
            root.section = "contacts";
            contactsPage.selectedId = App.pim.contacts.data(App.pim.contacts.index(0, 0), Qt.UserRole + 1);
        } else if (name === "contact-transfer") {
            root.section = "contacts";
            contactsPage.devTransfer();
        } else if (name === "contact-edit") {
            root.section = "contacts";
            contactDialog.openNew("Eva Muster", "eva@example.org");
        } else if (name === "tasks") {
            root.section = "tasks";
        } else if (name === "task-edit") {
            root.section = "tasks";
            taskDialog.openNew("Neue Aufgabe", "");
        } else if (name === "dav-account") {
            davAccountDialog.open();
        } else if (name === "invitation") {
            messageList.openFirst();
            messageList.step(1);
        } else if (name === "setup") {
            accountSetup.open();
        } else if (name === "setup-oauth") {
            accountSetup.devEmail = "max.mustermann@gmail.com";
            accountSetup.open();
        } else if (name === "settings") {
            settingsDialog.open();
        } else if (name === "light") {
            Theme.settings.dark = false;
            messageList.openFirst();
        }
    }

    Component {
        id: composerWindowComponent
        ComposerWindow {}
    }

    SettingsDialog {
        id: settingsDialog
        parent: root.overlay
        onFilterRulesRequested: filterRules.open()
        onAddDavAccountRequested: davAccountDialog.open()
    }

    EventDialog {
        id: eventDialog
        parent: root.overlay
        onNotify: message => root.notify(message)
    }

    TaskDialog {
        id: taskDialog
        parent: root.overlay
        onNotify: message => root.notify(message)
    }

    ContactDialog {
        id: contactDialog
        parent: root.overlay
    }

    DavAccountDialog {
        id: davAccountDialog
        parent: root.overlay
    }

    FilterRulesDialog {
        id: filterRules
        parent: root.overlay
    }

    LabelDialog {
        id: labelDialog
        parent: root.overlay
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
        function onMessageQueued(outboxId, delaySeconds, draft) {
            if (delaySeconds <= 0) {
                root.notify(i18n("Message queued for sending."));
                return;
            }
            toast.show(i18np("Sending message in %1 second…", "Sending message in %1 seconds…", delaySeconds), {
                actionText: i18n("Undo"),
                timeout: delaySeconds * 1000,
                onAction: () => {
                    if (!App.cancelQueued(outboxId)) {
                        root.notify(i18n("The message has already been sent."));
                        return;
                    }
                    const data = Object.assign({}, draft);
                    data.dirty = true;
                    root.openComposer(data, false);
                }
            });
        }
    }

    Binding {
        target: App
        property: "undoSendSeconds"
        value: Prefs.compose.undoSendSeconds
    }
    Binding {
        target: App
        property: "conversationView"
        value: Prefs.view.conversations
    }

    Component.onCompleted: {
        applyColorScheme();
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
                onSectionRequested: name => root.section = name
                onSettingsRequested: settingsDialog.open()
                onAddAccountRequested: accountSetup.open()
            }

            // Die Mailansicht bleibt geladen, damit Lesebereich und Composer erhalten bleiben.
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: ["mail", "calendar", "contacts", "tasks"].indexOf(root.section)

                QQC2.SplitView {
                    handle: Item {
                        implicitWidth: 3
                    }

                    FolderPane {
                        QQC2.SplitView.preferredWidth: 240
                        QQC2.SplitView.minimumWidth: 180
                        onComposeRequested: root.compose(0, "new")
                        onNewLabelRequested: labelDialog.openNew(0)
                        onEditLabelRequested: (id, name, color) => labelDialog.openEdit(id, name, color)
                        onFilterRulesRequested: filterRules.open()
                    }

                    MessageListPane {
                        id: messageList
                        QQC2.SplitView.preferredWidth: 370
                        QQC2.SplitView.minimumWidth: 280
                    }

                    // Lesebereich; ein Composer im Hauptfenster liegt an derselben Stelle darüber.
                    Item {
                        QQC2.SplitView.fillWidth: true
                        QQC2.SplitView.minimumWidth: 380

                        ReaderPane {
                            id: readerPane
                            anchors.fill: parent
                            visible: !inlineComposer.active
                            position: messageList.list.currentIndex
                            total: messageList.list.count
                            drawerVisible: Theme.settings.contextDrawer
                            onReplyRequested: mode => root.compose(App.currentMessage.id, mode)
                            onNavigate: delta => messageList.step(delta)
                            onToggleDrawer: Theme.settings.contextDrawer = !Theme.settings.contextDrawer
                            onNotify: message => root.notify(message)
                            onNewLabelRequested: id => labelDialog.openNew(id)
                        }

                        Loader {
                            id: inlineComposer
                            property var composeData: ({})
                            // Beim Öffnen angezeigte Nachricht; nur ein Wechsel schließt den Composer.
                            property var openedFor
                            anchors.fill: parent
                            active: false
                            sourceComponent: ComposerForm {
                                inline: true
                                draft: inlineComposer.composeData
                                onFinished: inlineComposer.active = false
                                onNotify: message => root.notify(message)
                                onPopOutRequested: state => {
                                    inlineComposer.active = false;
                                    root.openComposer(state, true);
                                }
                            }
                        }

                        // Wird eine andere Nachricht geöffnet, weicht ein unveränderter Composer.
                        Connections {
                            target: App
                            function onCurrentMessageChanged() {
                                if (!inlineComposer.item || App.currentMessage.id === undefined || App.currentMessage.id === inlineComposer.openedFor)
                                    return;
                                inlineComposer.openedFor = App.currentMessage.id;
                                if (inlineComposer.item.dirty)
                                    root.notify(i18n("Your message is still open: send it, save it as a draft or discard it."));
                                else
                                    inlineComposer.active = false;
                            }
                        }
                    }

                    ContextDrawer {
                        visible: Theme.settings.contextDrawer
                        QQC2.SplitView.preferredWidth: 288
                        QQC2.SplitView.minimumWidth: 220
                        onCloseRequested: Theme.settings.contextDrawer = false
                    onNewTaskRequested: (summary, description) => taskDialog.openNew(summary, description)
                    onNewContactRequested: (name, email) => contactDialog.openNew(name, email)
                    onEditEventRequested: (id, startMs) => eventDialog.openEdit(id, startMs)
                    onEditTaskRequested: id => taskDialog.openEdit(id)
                    onNotify: message => root.notify(message)
                    }
                }

                CalendarPage {
                    id: calendarPage
                    onNewEventRequested: (start, allDay) => eventDialog.openNew(start, allDay)
                    onEditEventRequested: (id, startMs) => eventDialog.openEdit(id, startMs)
                    onAddAccountRequested: davAccountDialog.open()
                }

                ContactsPage {
                    id: contactsPage
                    onNewContactRequested: contactDialog.openNew("", "")
                    onEditContactRequested: id => contactDialog.openEdit(id)
                    onAddAccountRequested: davAccountDialog.open()
                    onComposeRequested: address => {
                        const data = App.composeData(0, "new");
                        data.to = address;
                        root.section = "mail";
                        root.openComposer(data, false);
                    }
                }

                TasksPage {
                    onEditTaskRequested: id => taskDialog.openEdit(id)
                    onAddAccountRequested: davAccountDialog.open()
                    onNotify: message => root.notify(message)
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
        sequence: "L"
        enabled: root.section === "mail" && App.currentMessage.id !== undefined && !inlineComposer.active
        onActivated: readerPane.openLabelMenu()
    }
    Shortcut {
        sequence: "B"
        enabled: root.section === "mail" && App.currentMessage.id !== undefined && !inlineComposer.active
        onActivated: readerPane.openSnoozeMenu()
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: root.section = "mail"
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: root.section = "calendar"
    }
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: root.section = "contacts"
    }
    Shortcut {
        sequence: "Ctrl+4"
        onActivated: root.section = "tasks"
    }
}

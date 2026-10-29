import QtQuick
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Eigenes Fenster für den Composer (Einstellung "In separatem Fenster" oder herausgelöst).
Kirigami.AbstractApplicationWindow {
    id: window

    property alias draft: form.draft
    property bool closingConfirmed: false

    title: form.title
    width: Kirigami.Units.gridUnit * 42
    height: Kirigami.Units.gridUnit * 34
    minimumWidth: Kirigami.Units.gridUnit * 25
    minimumHeight: Kirigami.Units.gridUnit * 20
    color: Theme.elevated

    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.backgroundColor: Theme.elevated
    Kirigami.Theme.alternateBackgroundColor: Theme.card
    Kirigami.Theme.textColor: Theme.textPrimary
    Kirigami.Theme.disabledTextColor: Theme.textMuted
    Kirigami.Theme.highlightColor: Theme.accent
    Kirigami.Theme.highlightedTextColor: Theme.accentText
    Kirigami.Theme.hoverColor: Theme.accent
    Kirigami.Theme.focusColor: Theme.accent

    signal notify(string message)

    onClosing: close => {
        if (closingConfirmed || !form.dirty)
            return;
        close.accepted = false;
        form.requestClose(); // fragt nach und meldet dann finished()
    }

    ComposerForm {
        id: form
        anchors.fill: parent
        focus: true
        onFinished: {
            window.closingConfirmed = true;
            window.close();
        }
        onNotify: message => {
            window.showPassiveNotification(message);
            window.notify(message);
        }
    }
}

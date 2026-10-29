import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core

// Labels einer Nachricht (bzw. Konversation) setzen und entfernen.
QQC2.Menu {
    id: labelMenu

    property var messageId: 0
    property var assigned: []

    signal newLabelRequested(var messageId)

    // anchor: Knopf, unter dem das Menü erscheint (auch beim Öffnen per Taste L)
    function openFor(id, anchor) {
        messageId = id;
        assigned = App.labelIdsOf(id);
        if (anchor)
            popup(anchor, 0, anchor.height);
        else
            popup();
    }

    Instantiator {
        model: App.labels
        delegate: QQC2.MenuItem {
            required property var labelId
            required property string name
            required property string labelColor
            text: name
            checkable: true
            checked: labelMenu.assigned.includes(labelId)
            icon.name: "tag"
            icon.color: labelColor
            onTriggered: App.toggleLabel(labelMenu.messageId, labelId)
        }
        onObjectAdded: (index, object) => labelMenu.insertItem(index, object)
        onObjectRemoved: (index, object) => labelMenu.removeItem(object)
    }

    QQC2.MenuSeparator {
        visible: App.labels.count > 0
    }

    QQC2.MenuItem {
        text: i18n("New Label…")
        icon.name: "tag-new"
        onTriggered: labelMenu.newLabelRequested(labelMenu.messageId)
    }
}

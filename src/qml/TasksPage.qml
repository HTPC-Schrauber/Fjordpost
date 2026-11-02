import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Aufgaben aus den CalDAV-Kalendern.
Rectangle {
    id: page

    property bool showCompleted: false
    property var targetList: 0
    readonly property var writableLists: {
        App.pim.revision;
        return App.pim.collections("todos").filter(c => !c.readOnly);
    }
    readonly property var tasks: {
        App.pim.revision;
        return App.pim.tasks(0, showCompleted);
    }

    signal editTaskRequested(var itemId)
    signal notify(string message)
    signal addAccountRequested()

    color: Theme.reading

    function quickAdd() {
        const title = quickField.text.trim();
        if (title.length === 0)
            return;
        const list = listBox.currentValue ?? 0;
        const error = App.pim.saveTask({ summary: title, collectionId: list, dueMs: 0, priority: 0 });
        if (error.length > 0)
            page.notify(error);
        else
            quickField.clear();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14
                CollectionList {
                    Layout.fillWidth: true
                    kind: "todos"
                    title: i18n("Task Lists")
                }
                QQC2.CheckBox {
                    text: i18n("Show completed")
                    checked: page.showCompleted
                    font.pointSize: Theme.fontXs
                    onToggled: page.showCompleted = checked
                }
                Item { Layout.fillHeight: true }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.border
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            spacing: 12

            QQC2.Label {
                textFormat: Text.PlainText
                text: i18n("Tasks")
                font.pointSize: Theme.fontBase * 1.25
                font.weight: Font.DemiBold
                color: Theme.textPrimary
            }

            ColumnLayout {
                visible: !App.pim.hasDavAccounts
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                Item { Layout.fillHeight: true }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("No task list connected yet")
                    font.pointSize: Theme.fontBase * 1.1
                    color: Theme.textPrimary
                }
                AccentButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Connect Account…")
                    onClicked: page.addAccountRequested()
                }
                Item { Layout.fillHeight: true }
            }

            RowLayout {
                visible: App.pim.hasDavAccounts
                Layout.fillWidth: true
                spacing: 8
                QQC2.TextField {
                    id: quickField
                    Layout.fillWidth: true
                    placeholderText: i18n("Add a task and press Enter")
                    color: Theme.textPrimary
                    placeholderTextColor: Theme.textMuted
                    onAccepted: page.quickAdd()
                }
                QQC2.ComboBox {
                    id: listBox
                    visible: page.writableLists.length > 1
                    model: page.writableLists
                    textRole: "name"
                    valueRole: "id"
                }
            }

            ListView {
                id: taskList
                visible: App.pim.hasDavAccounts
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: page.tasks
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: ListView.view.width
                    implicitHeight: 44
                    radius: Theme.radius
                    color: rowHover.hovered ? Theme.card : Qt.alpha(Theme.card, 0.45)
                    HoverHandler {
                        id: rowHover
                    }
                    TapHandler {
                        onTapped: page.editTaskRequested(row.modelData.itemId)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 12
                        spacing: 10
                        TaskCheck {
                            checked: row.modelData.completed
                            onToggled: {
                                const error = App.pim.setTaskCompleted(row.modelData.itemId, checked);
                                if (error.length > 0)
                                    page.notify(error);
                            }
                        }
                        Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: row.modelData.color
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: row.modelData.title
                            elide: Text.ElideRight
                            font.pointSize: Theme.fontSm
                            font.strikeout: row.modelData.completed
                            color: row.modelData.completed ? Theme.textMuted : Theme.textPrimary
                        }
                        Kirigami.Icon {
                            visible: row.modelData.priority > 0 && row.modelData.priority < 5
                            implicitWidth: 14
                            implicitHeight: 14
                            source: "flag"
                            isMask: true
                            color: Theme.danger
                        }
                        QQC2.Label {
                            textFormat: Text.PlainText
                            visible: row.modelData.dueText.length > 0
                            text: row.modelData.dueText
                            font.pointSize: Theme.fontXs
                            color: row.modelData.overdue ? Theme.danger : Theme.textSecondary
                        }
                    }
                }
            }
            QQC2.Label {
                textFormat: Text.PlainText
                visible: App.pim.hasDavAccounts && taskList.count === 0
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 40
                text: i18n("Nothing to do")
                font.pointSize: Theme.fontXs
                color: Theme.textMuted
            }
        }
    }
}

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Ordnerspalte: Kontotitel, "Neue E-Mail", Ordner, Status und Cache.
Rectangle {
    id: pane

    signal composeRequested()
    signal newLabelRequested()
    signal editLabelRequested(var labelId, string name, string color)

    color: Theme.sidebar

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.rightMargin: 1
        spacing: 0

        // Kopf
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: App.selectedAccountId === 0 ? i18n("All Mailboxes") : App.selectionTitle
                        font.pointSize: Theme.fontSm
                        font.weight: Font.DemiBold
                        color: Theme.textPrimary
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: App.selectionSubtitle
                        font.pointSize: Theme.fontXxs
                        color: Theme.textMuted
                        elide: Text.ElideRight
                    }
                }
                IconButton {
                    iconName: "view-refresh"
                    iconSize: 14
                    tooltip: i18n("Synchronize now (F5)")
                    enabled: !App.syncing
                    onClicked: App.syncNow()
                    RotationAnimation on rotation {
                        running: App.syncing
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: 1200
                        onStopped: parent.rotation = 0
                    }
                }
            }

            AccentButton {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                fillWidth: true
                text: i18n("New Email")
                iconName: "document-edit"
                shortcutHint: i18nc("keyboard shortcut", "Ctrl+N")
                onClicked: pane.composeRequested()
            }
        }

        // Ordner
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: folderList
                model: App.folders
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                leftMargin: 10
                rightMargin: 10
                spacing: 2
                header: SectionLabel {
                    leftPadding: 8
                    bottomPadding: 6
                    text: i18n("Mailbox")
                }

                delegate: QQC2.AbstractButton {
                    id: folder

                    required property var folderId
                    required property string name
                    required property string folderRole
                    required property string iconName
                    required property int unreadCount

                    readonly property bool current: App.searchText.length === 0
                        && (folderId > 0 ? App.selectedFolderId === folderId : App.selectedFolderId === 0 && App.selectedRole === folderRole)

                    width: ListView.view.width - 20
                    implicitHeight: 32
                    hoverEnabled: true
                    onClicked: App.selectFolder(folderId, folderRole, name)

                    background: Rectangle {
                        radius: Theme.radius
                        color: folder.current ? Qt.alpha(Theme.accent, 0.15) : (folder.hovered ? Theme.card : "transparent")
                    }

                    contentItem: RowLayout {
                        spacing: 10
                        Item {
                            Layout.preferredWidth: 2
                        }
                        Kirigami.Icon {
                            Layout.preferredWidth: 16
                            Layout.preferredHeight: 16
                            source: folder.folderRole === "flagged" ? "starred-symbolic" : folder.iconName
                            isMask: true
                            color: folder.current ? Theme.accent : (folder.folderRole === "flagged" ? Theme.star : (folder.hovered ? Theme.textPrimary : Theme.textSecondary))
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: folder.name
                            elide: Text.ElideRight
                            font.pointSize: Theme.fontXs
                            font.weight: folder.current ? Font.Medium : Font.Normal
                            color: folder.current ? Theme.accent : (folder.hovered ? Theme.textPrimary : Theme.textSecondary)
                        }
                        Rectangle {
                            visible: folder.unreadCount > 0
                            Layout.preferredHeight: 16
                            Layout.preferredWidth: Math.max(16, countLabel.implicitWidth + 10)
                            Layout.rightMargin: 6
                            radius: 8
                            color: folder.current ? Theme.accent : "transparent"
                            QQC2.Label {
                                id: countLabel
                                anchors.centerIn: parent
                                text: folder.unreadCount
                                font.pointSize: Theme.fontXxs
                                font.weight: Font.DemiBold
                                color: folder.current ? Theme.accentText : (folder.folderRole === "drafts" ? Theme.warning : Theme.textMuted)
                            }
                        }
                    }
                }

                footer: ColumnLayout {
                    width: folderList.width - 20
                    spacing: 6
                    Item {
                        Layout.preferredHeight: 14
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        SectionLabel {
                            Layout.fillWidth: true
                            leftPadding: 8
                            text: i18n("Labels & Filters")
                        }
                        IconButton {
                            implicitWidth: 22
                            implicitHeight: 20
                            iconName: "list-add"
                            iconSize: 12
                            tooltip: i18n("New label")
                            onClicked: pane.newLabelRequested()
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: App.labels.count === 0
                        leftPadding: 8
                        text: i18n("Labels sort messages across all accounts. Create one with + or via the tag button above a message.")
                        font.pointSize: Theme.fontXxs
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                    Repeater {
                        model: App.labels
                        delegate: QQC2.AbstractButton {
                            id: labelEntry

                            required property var labelId
                            required property string name
                            required property string labelColor
                            required property int messageCount

                            readonly property bool current: App.searchText.length === 0 && App.selectedLabelId === labelId

                            Layout.fillWidth: true
                            implicitHeight: 32
                            hoverEnabled: true
                            onClicked: App.selectLabel(labelId, name)

                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: labelContextMenu.openFor(labelEntry.labelId, labelEntry.name, labelEntry.labelColor)
                            }

                            background: Rectangle {
                                radius: Theme.radius
                                color: labelEntry.current ? Qt.alpha(Theme.accent, 0.15) : (labelEntry.hovered ? Theme.card : "transparent")
                            }
                            contentItem: RowLayout {
                                spacing: 10
                                Item {
                                    Layout.preferredWidth: 5
                                }
                                Rectangle {
                                    Layout.preferredWidth: 8
                                    Layout.preferredHeight: 8
                                    radius: 4
                                    color: labelEntry.labelColor.length > 0 ? labelEntry.labelColor : Theme.textMuted
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 3
                                    text: labelEntry.name
                                    elide: Text.ElideRight
                                    font.pointSize: Theme.fontXs
                                    font.weight: labelEntry.current ? Font.Medium : Font.Normal
                                    color: labelEntry.current ? Theme.accent : (labelEntry.hovered ? Theme.textPrimary : Theme.textSecondary)
                                }
                                QQC2.Label {
                                    Layout.rightMargin: 8
                                    visible: labelEntry.messageCount > 0
                                    text: labelEntry.messageCount
                                    font.family: "monospace"
                                    font.pointSize: Theme.fontXxs
                                    color: Theme.textMuted
                                }
                            }
                        }
                    }
                    }
                }
            }
        }

        // Fehlermeldung
        Rectangle {
            Layout.fillWidth: true
            Layout.margins: 10
            visible: App.lastError.length > 0
            implicitHeight: errorRow.implicitHeight + 16
            radius: Theme.radius
            color: Qt.alpha(Theme.danger, 0.12)
            border.width: 1
            border.color: Qt.alpha(Theme.danger, 0.3)
            RowLayout {
                id: errorRow
                anchors.fill: parent
                anchors.margins: 8
                QQC2.Label {
                    Layout.fillWidth: true
                    text: App.lastError
                    wrapMode: Text.Wrap
                    font.pointSize: Theme.fontXxs
                    color: Theme.textPrimary
                }
                IconButton {
                    iconName: "window-close"
                    iconSize: 12
                    tooltip: i18n("Dismiss")
                    onClicked: App.dismissError()
                }
            }
        }

        // Lokaler Cache
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 40
            color: Qt.alpha(Theme.dock, 0.3)
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Local cache")
                    font.pointSize: Theme.fontXxs
                    font.weight: Font.Medium
                    color: Theme.textSecondary
                }
                QQC2.Label {
                    id: cacheLabel
                    font.pointSize: Theme.fontXxs
                    font.family: "monospace"
                    color: Theme.textMuted
                    text: App.cacheSize()
                    Connections {
                        target: App
                        function onSyncingChanged() {
                            if (!App.syncing)
                                cacheLabel.text = App.cacheSize();
                        }
                    }
                }
            }
        }
    }

    QQC2.Menu {
        id: labelContextMenu
        property var labelId: 0
        property string labelName
        property string labelColor
        function openFor(id, name, color) {
            labelId = id;
            labelName = name;
            labelColor = color;
            popup();
        }
        QQC2.MenuItem {
            text: i18n("Edit…")
            icon.name: "document-edit"
            onTriggered: pane.editLabelRequested(labelContextMenu.labelId, labelContextMenu.labelName, labelContextMenu.labelColor)
        }
        QQC2.MenuItem {
            text: i18n("Delete")
            icon.name: "edit-delete"
            onTriggered: deleteLabelDialog.open()
        }
    }

    Kirigami.PromptDialog {
        id: deleteLabelDialog
        parent: QQC2.Overlay.overlay
        Kirigami.Theme.inherit: false
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.backgroundColor: Theme.elevated
        Kirigami.Theme.textColor: Theme.textPrimary
        Kirigami.Theme.highlightColor: Theme.accent
        Kirigami.Theme.highlightedTextColor: Theme.accentText
        title: i18n("Delete label \"%1\"?", labelContextMenu.labelName)
        subtitle: i18n("The label is removed from all messages. The messages themselves are kept.")
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: App.deleteLabel(labelContextMenu.labelId)
    }
}

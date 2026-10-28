import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core
import "components"

// Ordnerspalte: Kontotitel, "Neue E-Mail", Ordner, Status und Cache.
Rectangle {
    id: pane

    signal composeRequested()

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
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        leftPadding: 8
                        text: i18n("Labels will be available in a later version.")
                        font.pointSize: Theme.fontXxs
                        color: Theme.textMuted
                        wrapMode: Text.Wrap
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
}

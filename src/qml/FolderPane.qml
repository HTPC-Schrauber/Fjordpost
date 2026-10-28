import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.mopmail.core

// Ordnerspalte: "Neue E-Mail"-Schaltfläche und Ordnerliste des gewählten
// Kontos bzw. die Sammelordner der Unified Inbox.
QQC2.Pane {
    id: pane

    signal composeRequested()

    padding: 0
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    background: Rectangle {
        color: Kirigami.Theme.alternateBackgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QQC2.Button {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            Layout.preferredHeight: Kirigami.Units.gridUnit * 2.2
            text: i18n("New Email")
            icon.name: "mail-message-new"
            highlighted: true
            onClicked: pane.composeRequested()
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: folderList
                model: App.folders
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                delegate: QQC2.ItemDelegate {
                    id: folderDelegate

                    required property var folderId
                    required property string name
                    required property string folderRole
                    required property string iconName
                    required property int unreadCount

                    width: ListView.view.width
                    highlighted: App.searchText.length === 0
                                 && (folderId > 0 ? App.selectedFolderId === folderId
                                                  : App.selectedFolderId === 0 && App.selectedRole === folderRole)
                    onClicked: App.selectFolder(folderId, folderRole, name)

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        Kirigami.Icon {
                            source: folderDelegate.iconName
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: folderDelegate.name
                            elide: Text.ElideRight
                            font.bold: folderDelegate.unreadCount > 0
                        }
                        QQC2.Label {
                            visible: folderDelegate.unreadCount > 0
                            text: folderDelegate.unreadCount
                            color: Kirigami.Theme.highlightColor
                            font.bold: true
                        }
                    }
                }
            }
        }
    }
}

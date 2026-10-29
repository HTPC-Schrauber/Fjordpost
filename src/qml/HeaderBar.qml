import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Schmale Kopfleiste im Fenster (Inhalt der Titelleiste aus dem Entwurf);
// die eigentliche Fensterleiste zeichnet KWin.
Rectangle {
    id: bar

    signal searchRequested()

    implicitHeight: 36
    color: Theme.dock

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 8

        Rectangle {
            Layout.preferredWidth: 20
            Layout.preferredHeight: 20
            radius: 7
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Qt.lighter(Theme.accent, 1.3) }
            }
            QQC2.Label {
                anchors.centerIn: parent
                text: "F"
                color: "white"
                font.bold: true
                font.pixelSize: 10
            }
        }
        QQC2.Label {
            text: "Fjordpost"
            font.pointSize: Theme.fontXs
            font.bold: true
            color: Theme.textPrimary
        }

        Item {
            Layout.fillWidth: true
        }

        // Befehl / globale Suche
        QQC2.AbstractButton {
            id: searchButton
            Layout.preferredWidth: 300
            Layout.preferredHeight: 24
            hoverEnabled: true
            onClicked: bar.searchRequested()
            background: Rectangle {
                radius: Theme.radiusSm
                color: searchButton.hovered ? Theme.card : Qt.alpha(Theme.card, 0.7)
                border.width: 1
                border.color: Theme.border
            }
            contentItem: RowLayout {
                spacing: 6
                Item {
                    Layout.preferredWidth: 4
                }
                Kirigami.Icon {
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    source: "search"
                    isMask: true
                    color: Theme.textMuted
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: i18n("Search all mail…")
                    font.pointSize: Theme.fontXs
                    color: Theme.textSecondary
                }
                Kbd {
                    text: i18nc("keyboard shortcut", "Ctrl+K")
                }
                Item {
                    Layout.preferredWidth: 2
                }
            }
        }

        Item {
            Layout.fillWidth: true
        }

        // Synchronisationsstatus
        Item {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            Rectangle {
                id: ping
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: statusDot.color
                SequentialAnimation on scale {
                    running: App.syncing
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 2.2; duration: 900 }
                    NumberAnimation { to: 1; duration: 0 }
                }
                opacity: App.syncing ? 2.2 - scale : 0
            }
            Rectangle {
                id: statusDot
                anchors.centerIn: parent
                width: 8
                height: 8
                radius: 4
                color: App.lastError.length > 0 ? Theme.danger : (App.hasAccounts ? Theme.success : Theme.textMuted)
            }
        }
        QQC2.Label {
            text: App.lastError.length > 0 ? i18n("Connection problem") : (App.syncing ? i18n("Synchronizing…") : (App.hasAccounts ? i18n("Up to date") : i18n("No account")))
            font.pointSize: Theme.fontXxs
            color: Theme.textMuted
        }
    }
}

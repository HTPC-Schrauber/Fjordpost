import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Einstellungen. Änderungen wirken sofort und werden automatisch gespeichert.
ThemedDialog {
    id: dialog

    title: i18n("Settings")
    preferredWidth: Kirigami.Units.gridUnit * 30
    standardButtons: Kirigami.Dialog.Close
    padding: Kirigami.Units.largeSpacing * 2

    Kirigami.FormLayout {
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Appearance")
        }

        QQC2.CheckBox {
            Kirigami.FormData.label: i18n("Colors:")
            text: i18n("Use KDE color scheme")
            checked: Theme.settings.useSystemColors
            onToggled: Theme.settings.useSystemColors = checked
        }

        QQC2.CheckBox {
            text: i18n("Dark mode")
            enabled: !Theme.system
            checked: Theme.settings.dark
            onToggled: Theme.settings.dark = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Accent color:")
            spacing: 8
            enabled: !Theme.system
            opacity: enabled ? 1 : 0.4
            Repeater {
                model: Theme.accentNames
                QQC2.AbstractButton {
                    required property string modelData
                    implicitWidth: 22
                    implicitHeight: 22
                    hoverEnabled: true
                    onClicked: Theme.settings.accent = modelData
                    QQC2.ToolTip.text: modelData
                    QQC2.ToolTip.visible: hovered
                    contentItem: Rectangle {
                        radius: 11
                        color: Theme.accents[parent.modelData][0]
                        border.width: Theme.settings.accent === parent.modelData ? 2 : 0
                        border.color: Theme.textPrimary
                        scale: parent.hovered ? 1.1 : 1
                    }
                }
            }
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Composing")
        }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18n("Open new messages:")
            text: i18n("In the main window")
            checked: Prefs.compose.openIn === "inline"
            onToggled: Prefs.compose.openIn = "inline"
        }

        QQC2.RadioButton {
            text: i18n("In a separate window")
            checked: Prefs.compose.openIn === "window"
            onToggled: Prefs.compose.openIn = "window"
        }

        QQC2.Label {
            Layout.maximumWidth: Kirigami.Units.gridUnit * 18
            text: i18n("Applies to new messages, replies and forwards. A message opened in the main window can be moved to its own window at any time.")
            wrapMode: Text.Wrap
            font.pointSize: Theme.fontXs
            color: Theme.textMuted
        }
    }
}

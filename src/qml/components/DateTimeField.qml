import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.app

// Datum und (optional) Uhrzeit; value ist ein JavaScript-Date oder null.
RowLayout {
    id: field

    property var value: null
    property bool showTime: true
    property bool optional: false
    signal edited()

    spacing: 6

    // Getippte Uhrzeit übernehmen. Ein Klick auf eine Dialog-Schaltfläche nimmt dem Feld nicht
    // immer den Fokus, editingFinished käme dann zu spät.
    function commit() {
        if (timeField.visible && timeField.acceptableInput && field.value) {
            const next = new Date(field.value);
            next.setHours(parseInt(timeField.text.slice(0, 2)), parseInt(timeField.text.slice(3, 5)), 0, 0);
            if (next.getTime() !== field.value.getTime()) {
                field.value = next;
                field.edited();
            }
        }
    }

    function setDate(date) {
        const next = new Date(date);
        const old = value ?? new Date(2000, 0, 1, 9, 0);
        next.setHours(old.getHours(), old.getMinutes(), 0, 0);
        value = next;
        edited();
    }

    QQC2.Button {
        Layout.fillWidth: true
        text: field.value ? field.value.toLocaleDateString(Qt.locale(), Locale.ShortFormat) : i18n("No date")
        icon.name: "view-calendar-day"
        onClicked: {
            grid.month = (field.value ?? new Date()).getMonth();
            grid.year = (field.value ?? new Date()).getFullYear();
            popup.open();
        }
    }

    QQC2.TextField {
        id: timeField
        visible: field.showTime && field.value !== null
        Layout.preferredWidth: 70
        horizontalAlignment: TextInput.AlignHCenter
        inputMask: "99:99"
        color: Theme.textPrimary
        text: field.value ? Qt.formatTime(field.value, "hh:mm") : ""
        validator: RegularExpressionValidator {
            regularExpression: /([01]\d|2[0-3]):[0-5]\d/
        }
        onEditingFinished: field.commit()
    }

    QQC2.ToolButton {
        visible: field.optional && field.value !== null
        icon.name: "edit-clear"
        onClicked: {
            field.value = null;
            field.edited();
        }
    }

    QQC2.Popup {
        id: popup
        y: field.height + 4
        padding: 10
        modal: false
        background: Rectangle {
            radius: Theme.radius
            color: Theme.elevated
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ColumnLayout {
            spacing: 6
            RowLayout {
                QQC2.ToolButton {
                    icon.name: "go-previous"
                    onClicked: {
                        const d = new Date(grid.year, grid.month - 1, 1);
                        grid.month = d.getMonth();
                        grid.year = d.getFullYear();
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: Qt.locale().standaloneMonthName(grid.month) + " " + grid.year
                    color: Theme.textPrimary
                    font.weight: Font.DemiBold
                }
                QQC2.ToolButton {
                    icon.name: "go-next"
                    onClicked: {
                        const d = new Date(grid.year, grid.month + 1, 1);
                        grid.month = d.getMonth();
                        grid.year = d.getFullYear();
                    }
                }
            }
            QQC2.DayOfWeekRow {
                Layout.fillWidth: true
                locale: grid.locale
                delegate: QQC2.Label {
                    required property string shortName
                    horizontalAlignment: Text.AlignHCenter
                    text: shortName
                    color: Theme.textSecondary
                    font.pointSize: Theme.fontXs
                }
            }
            QQC2.MonthGrid {
                id: grid
                Layout.preferredWidth: 252
                Layout.preferredHeight: 200
                locale: Qt.locale()
                delegate: Rectangle {
                    required property var model
                    readonly property bool selected: field.value !== null && field.value.getDate() === model.day && field.value.getMonth() === model.month
                        && field.value.getFullYear() === model.year
                    radius: Theme.radiusSm
                    color: selected ? Theme.accent : (dayMouse.containsMouse ? Theme.card : "transparent")
                    QQC2.Label {
                        anchors.centerIn: parent
                        text: parent.model.day
                        color: parent.selected ? Theme.accentText : (parent.model.month === grid.month ? Theme.textPrimary : Theme.textMuted)
                        font.weight: parent.model.today ? Font.Bold : Font.Normal
                    }
                    MouseArea {
                        id: dayMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            field.setDate(parent.model.date);
                            popup.close();
                        }
                    }
                }
            }
        }
    }
}

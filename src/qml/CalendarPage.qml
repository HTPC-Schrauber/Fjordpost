import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Kalender: Monats- und Wochenansicht, links Kalenderliste und Tagesübersicht.
Rectangle {
    id: page

    property string view: "month" // "month", "week"
    property date anchorDate: new Date()
    property date selectedDate: new Date()
    // Tag → Vorkommnisse im sichtbaren Zeitraum
    property var byDay: ({})

    property alias collections: calendarCollections

    signal newEventRequested(date start, bool allDay)
    // startMs: angeklicktes Vorkommnis (bei Serien für "nur diesen Termin")
    signal editEventRequested(var itemId, var startMs)
    signal addAccountRequested()

    color: Theme.reading

    function dayKey(d) {
        return d.getFullYear() + "-" + d.getMonth() + "-" + d.getDate();
    }
    function sameDay(a, b) {
        return dayKey(a) === dayKey(b);
    }
    function addDays(d, n) {
        return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n);
    }
    function startOfWeek(d) {
        const offset = (d.getDay() - Qt.locale().firstDayOfWeek + 7) % 7;
        return addDays(d, -offset);
    }

    readonly property date rangeStart: view === "week" ? startOfWeek(anchorDate) : startOfWeek(new Date(anchorDate.getFullYear(), anchorDate.getMonth(), 1))
    readonly property int rangeDays: view === "week" ? 7 : 42
    readonly property string title: view === "week"
        ? rangeStart.toLocaleDateString(Qt.locale(), Locale.ShortFormat) + " – " + addDays(rangeStart, 6).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
        : Qt.locale().standaloneMonthName(anchorDate.getMonth()) + " " + anchorDate.getFullYear()

    function reload() {
        const end = addDays(rangeStart, rangeDays);
        const list = App.pim.occurrences(rangeStart.getTime(), end.getTime());
        const map = {};
        for (const o of list) {
            // Mehrtägige Termine erscheinen an jedem Tag.
            let day = new Date(o.startMs);
            day = new Date(day.getFullYear(), day.getMonth(), day.getDate());
            const last = o.allDay ? o.endMs : Math.max(o.startMs, o.endMs - 1);
            while (day.getTime() <= last && day < end) {
                const key = dayKey(day);
                (map[key] = map[key] ?? []).push(o);
                day = addDays(day, 1);
            }
        }
        byDay = map;
    }

    onRangeStartChanged: reload()
    onViewChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: App.pim
        function onRevisionChanged() {
            page.reload();
        }
    }

    function step(delta) {
        anchorDate = view === "week" ? addDays(anchorDate, 7 * delta) : new Date(anchorDate.getFullYear(), anchorDate.getMonth() + delta, 1);
    }
    function goToday() {
        anchorDate = new Date();
        selectedDate = new Date();
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Seitenleiste
        Rectangle {
            Layout.preferredWidth: 240
            Layout.fillHeight: true
            color: Theme.sidebar

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 16

                AccentButton {
                    Layout.fillWidth: true
                    text: i18n("New Event")
                    iconName: "appointment-new"
                    enabled: App.pim.hasDavAccounts
                    onClicked: page.newEventRequested(page.selectedDate, false)
                }

                CollectionList {
                    id: calendarCollections
                    Layout.fillWidth: true
                    kind: "events"
                    title: i18n("Calendars")
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 6
                    SectionLabel {
                        text: page.selectedDate.toLocaleDateString(Qt.locale(), "dddd, d. MMMM")
                    }
                    QQC2.Label {
                        textFormat: Text.PlainText
                        visible: dayList.count === 0
                        text: i18n("No events")
                        font.pointSize: Theme.fontXs
                        color: Theme.textMuted
                    }
                    ListView {
                        id: dayList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: page.byDay[page.dayKey(page.selectedDate)] ?? []
                        delegate: QQC2.AbstractButton {
                            required property var modelData
                            width: ListView.view.width
                            implicitHeight: 42
                            hoverEnabled: true
                            onClicked: page.editEventRequested(modelData.itemId, modelData.startMs)
                            background: Rectangle {
                                radius: Theme.radiusSm
                                color: parent.hovered ? Theme.card : "transparent"
                            }
                            contentItem: RowLayout {
                                spacing: 8
                                Rectangle {
                                    implicitWidth: 3
                                    Layout.fillHeight: true
                                    Layout.topMargin: 4
                                    Layout.bottomMargin: 4
                                    radius: 2
                                    color: modelData.color
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    QQC2.Label {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        elide: Text.ElideRight
                                        font.pointSize: Theme.fontXs
                                        color: Theme.textPrimary
                                    }
                                    QQC2.Label {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        text: modelData.location.length > 0 ? modelData.timeText + " · " + modelData.location : modelData.timeText
                                        elide: Text.ElideRight
                                        font.pointSize: Theme.fontXxs
                                        color: Theme.textSecondary
                                    }
                                }
                            }
                        }
                    }
                }
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
            spacing: 0

            // Kopf
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 12
                spacing: 6
                IconButton {
                    iconName: "go-previous"
                    tooltip: i18n("Previous")
                    onClicked: page.step(-1)
                }
                IconButton {
                    iconName: "go-next"
                    tooltip: i18n("Next")
                    onClicked: page.step(1)
                }
                IconButton {
                    display: QQC2.AbstractButton.TextBesideIcon
                    iconName: "go-jump-today"
                    text: i18n("Today")
                    onClicked: page.goToday()
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    text: page.title
                    font.pointSize: Theme.fontBase * 1.25
                    font.weight: Font.DemiBold
                    color: Theme.textPrimary
                    elide: Text.ElideRight
                }
                Pill {
                    text: i18n("Month")
                    selected: page.view === "month"
                    onClicked: page.view = "month"
                }
                Pill {
                    text: i18n("Week")
                    selected: page.view === "week"
                    onClicked: page.view = "week"
                }
            }

            // Noch kein Kalender verbunden
            ColumnLayout {
                visible: !App.pim.hasDavAccounts
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12
                Item { Layout.fillHeight: true }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("No calendar connected yet")
                    font.pointSize: Theme.fontBase * 1.1
                    color: Theme.textPrimary
                }
                QQC2.Label {
                    textFormat: Text.PlainText
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Connect a CalDAV/CardDAV account, for example Nextcloud.")
                    font.pointSize: Theme.fontXs
                    color: Theme.textSecondary
                }
                AccentButton {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18n("Connect Account…")
                    onClicked: page.addAccountRequested()
                }
                Item { Layout.fillHeight: true }
            }

            // Wochentage (Monatsansicht)
            RowLayout {
                visible: App.pim.hasDavAccounts && page.view === "month"
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: 7
                    QQC2.Label {
                        textFormat: Text.PlainText
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.bottomMargin: 4
                        horizontalAlignment: Text.AlignHCenter
                        text: Qt.locale().dayName((Qt.locale().firstDayOfWeek + index) % 7, Locale.ShortFormat)
                        font.pointSize: Theme.fontXs
                        color: Theme.textSecondary
                    }
                }
            }

            // Monatsraster
            GridLayout {
                id: monthGrid
                visible: App.pim.hasDavAccounts && page.view === "month"
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 7
                rowSpacing: 0
                columnSpacing: 0
                Repeater {
                    model: 42
                    Rectangle {
                        id: cell
                        required property int index
                        readonly property date day: page.addDays(page.rangeStart, index)
                        readonly property var events: page.byDay[page.dayKey(day)] ?? []
                        readonly property bool inMonth: day.getMonth() === page.anchorDate.getMonth()
                        readonly property bool today: page.sameDay(day, new Date())
                        readonly property bool selected: page.sameDay(day, page.selectedDate)
                        // Platz für Datum (22) und Mehr-Zeile; ein Eintrag braucht 19.
                        readonly property int capacity: Math.max(0, Math.floor((height - 24) / 19))
                        readonly property int shown: events.length > capacity ? Math.max(0, capacity - 1) : events.length
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 1
                        color: selected ? Qt.alpha(Theme.accent, 0.1) : (inMonth ? "transparent" : Qt.alpha(Theme.sidebar, 0.5))
                        border.width: 1
                        border.color: Theme.border
                        clip: true

                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.selectedDate = cell.day
                            onDoubleClicked: page.newEventRequested(cell.day, false)
                        }

                        Column {
                            anchors.fill: parent
                            anchors.margins: 3
                            spacing: 2
                            Rectangle {
                                anchors.right: parent.right
                                width: 20
                                height: 20
                                radius: 10
                                color: cell.today ? Theme.accent : "transparent"
                                QQC2.Label {
                                    textFormat: Text.PlainText
                                    anchors.centerIn: parent
                                    text: cell.day.getDate()
                                    font.pointSize: Theme.fontXs
                                    font.weight: cell.today ? Font.Bold : Font.Normal
                                    color: cell.today ? Theme.accentText : (cell.inMonth ? Theme.textPrimary : Theme.textMuted)
                                }
                            }
                            Repeater {
                                model: cell.events.slice(0, cell.shown)
                                delegate: QQC2.AbstractButton {
                                    required property var modelData
                                    width: parent.width
                                    height: 17
                                    hoverEnabled: true
                                    onClicked: page.editEventRequested(modelData.itemId, modelData.startMs)
                                    QQC2.ToolTip.text: modelData.timeText + " " + modelData.title
                                    QQC2.ToolTip.visible: hovered
                                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                    background: Rectangle {
                                        radius: 4
                                        color: Qt.alpha(modelData.color, parent.hovered ? 0.45 : (modelData.allDay ? 0.35 : 0.2))
                                    }
                                    contentItem: QQC2.Label {
                                        textFormat: Text.PlainText
                                        leftPadding: 4
                                        rightPadding: 2
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData.allDay ? modelData.title : modelData.timeText + " " + modelData.title
                                        elide: Text.ElideRight
                                        font.pointSize: Theme.fontXxs
                                        color: Theme.textPrimary
                                    }
                                }
                            }
                            QQC2.Label {
                                textFormat: Text.PlainText
                                visible: cell.events.length > cell.shown
                                leftPadding: 4
                                text: i18n("+%1 more", cell.events.length - cell.shown)
                                font.pointSize: Theme.fontXxs
                                color: Theme.textSecondary
                            }
                        }
                    }
                }
            }

            // Wochenansicht
            ColumnLayout {
                visible: App.pim.hasDavAccounts && page.view === "week"
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Köpfe und ganztägige Termine
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Item { implicitWidth: 52 }
                    Repeater {
                        model: 7
                        ColumnLayout {
                            required property int index
                            readonly property date day: page.addDays(page.rangeStart, index)
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            Layout.alignment: Qt.AlignTop
                            spacing: 2
                            QQC2.Label {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                Layout.bottomMargin: 2
                                horizontalAlignment: Text.AlignHCenter
                                text: parent.day.toLocaleDateString(Qt.locale(), "ddd d.")
                                font.pointSize: Theme.fontXs
                                font.weight: page.sameDay(parent.day, new Date()) ? Font.Bold : Font.Normal
                                color: page.sameDay(parent.day, new Date()) ? Theme.accent : Theme.textSecondary
                            }
                            Repeater {
                                model: (page.byDay[page.dayKey(parent.day)] ?? []).filter(o => o.allDay)
                                QQC2.AbstractButton {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 2
                                    Layout.rightMargin: 2
                                    implicitHeight: 18
                                    onClicked: page.editEventRequested(modelData.itemId, modelData.startMs)
                                    background: Rectangle {
                                        radius: 4
                                        color: Qt.alpha(modelData.color, 0.35)
                                    }
                                    contentItem: QQC2.Label {
                                        textFormat: Text.PlainText
                                        leftPadding: 4
                                        verticalAlignment: Text.AlignVCenter
                                        text: modelData.title
                                        elide: Text.ElideRight
                                        font.pointSize: Theme.fontXxs
                                        color: Theme.textPrimary
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: Theme.border
                }

                Flickable {
                    id: weekScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: 24 * 44
                    boundsBehavior: Flickable.StopAtBounds
                    contentY: 7 * 44 // morgens beginnen
                    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

                    Repeater {
                        model: 24
                        Item {
                            required property int index
                            y: index * 44
                            width: weekScroll.width
                            height: 44
                            QQC2.Label {
                                textFormat: Text.PlainText
                                x: 8
                                y: -height / 2
                                visible: parent.index > 0
                                text: String(parent.index).padStart(2, "0") + ":00"
                                font.pointSize: Theme.fontXxs
                                color: Theme.textMuted
                            }
                            Rectangle {
                                x: 52
                                width: parent.width - 52
                                height: 1
                                color: Theme.border
                            }
                        }
                    }

                    Repeater {
                        model: 7
                        Item {
                            id: dayColumn
                            required property int index
                            readonly property date day: page.addDays(page.rangeStart, index)
                            readonly property var timed: layoutDay((page.byDay[page.dayKey(day)] ?? []).filter(o => !o.allDay))
                            x: 52 + index * (weekScroll.width - 52) / 7
                            width: (weekScroll.width - 52) / 7
                            height: weekScroll.contentHeight

                            // Überschneidungen nebeneinander legen: Spalte und Spaltenzahl je Termin
                            function layoutDay(list) {
                                const result = [];
                                const ends = [];
                                for (const o of list) {
                                    let column = ends.findIndex(e => e <= o.startMs);
                                    if (column < 0)
                                        column = ends.length;
                                    ends[column] = o.endMs;
                                    result.push({ event: o, column: column });
                                }
                                for (const r of result)
                                    r.columns = ends.length;
                                return result;
                            }

                            Rectangle {
                                width: 1
                                height: parent.height
                                color: Theme.border
                            }
                            MouseArea {
                                anchors.fill: parent
                                onDoubleClicked: mouse => {
                                    const start = new Date(dayColumn.day);
                                    start.setHours(Math.floor(mouse.y / 44), 0, 0, 0);
                                    page.newEventRequested(start, false);
                                }
                                onClicked: page.selectedDate = dayColumn.day
                            }
                            Repeater {
                                model: dayColumn.timed
                                QQC2.AbstractButton {
                                    required property var modelData
                                    readonly property date dayStart: dayColumn.day
                                    readonly property real startHours: Math.max(0, (modelData.event.startMs - dayStart.getTime()) / 3600000)
                                    readonly property real endHours: Math.min(24, (modelData.event.endMs - dayStart.getTime()) / 3600000)
                                    x: 2 + modelData.column * (dayColumn.width - 4) / modelData.columns
                                    y: startHours * 44
                                    width: (dayColumn.width - 4) / modelData.columns - 1
                                    height: Math.max(20, (endHours - startHours) * 44 - 1)
                                    hoverEnabled: true
                                    onClicked: page.editEventRequested(modelData.event.itemId, modelData.event.startMs)
                                    background: Rectangle {
                                        radius: 4
                                        color: Qt.alpha(parent.modelData.event.color, parent.hovered ? 0.5 : 0.28)
                                        Rectangle {
                                            width: 3
                                            height: parent.height
                                            radius: 2
                                            color: parent.parent.modelData.event.color
                                        }
                                    }
                                    contentItem: QQC2.Label {
                                        textFormat: Text.PlainText
                                        leftPadding: 7
                                        topPadding: 2
                                        verticalAlignment: Text.AlignTop
                                        wrapMode: Text.Wrap
                                        clip: true
                                        text: modelData.event.timeText + " " + modelData.event.title
                                        font.pointSize: Theme.fontXxs
                                        color: Theme.textPrimary
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

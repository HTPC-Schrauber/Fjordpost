import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigamiaddons.dateandtime as DateAndTime
import de.fjordblau.fjordpost.core

// Freie Weckzeit: erst das Datum, dann die Uhrzeit. Der Loader lädt die Popups erst
// beim ersten Gebrauch, sonst meldet der Datumswähler schon beim Programmstart Warnungen.
Item {
    id: picker

    property var messageId: 0

    function openFor(id) {
        messageId = id;
        loader.active = true;
        loader.item.start();
    }

    Loader {
        id: loader
        active: false
        sourceComponent: Item {
            function start() {
                const tomorrow = new Date();
                tomorrow.setDate(tomorrow.getDate() + 1);
                datePopup.value = tomorrow;
                datePopup.open();
            }

            DateAndTime.DatePopup {
                id: datePopup
                parent: QQC2.Overlay.overlay
                anchors.centerIn: parent
                minimumDate: new Date()
                onAccepted: {
                    const time = new Date();
                    time.setHours(8, 0, 0, 0);
                    timePopup.value = time;
                    timePopup.open();
                }
            }

            DateAndTime.TimePopup {
                id: timePopup
                parent: QQC2.Overlay.overlay
                anchors.centerIn: parent
                onAccepted: {
                    const when = new Date(datePopup.value);
                    when.setHours(value.getHours(), value.getMinutes(), 0, 0);
                    App.snoozeMessage(picker.messageId, when.getTime());
                }
            }
        }
    }
}

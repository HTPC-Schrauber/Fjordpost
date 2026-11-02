pragma Singleton

import QtQuick
import QtCore

// Allgemeine Einstellungen (Darstellung liegt in Theme.settings).
QtObject {
    readonly property Settings general: Settings {
        category: "Behavior"
        // Fenster schließen lässt die App im Tray weiterlaufen, sonst wird sie beendet
        property bool closeToTray: true
    }

    readonly property Settings view: Settings {
        category: "View"
        // Nachrichten zu Konversationen zusammenfassen
        property bool conversations: true
        // Verlauf der Konversation im Lesebereich aufgeklappt
        property bool threadPanelOpen: true
    }

    readonly property Settings compose: Settings {
        category: "Compose"
        // "inline": im Hauptfenster anstelle des Lesebereichs, "window": eigenes Fenster
        property string openIn: "inline"
        // Wartezeit für "Senden rückgängig" in Sekunden (0 = aus)
        property int undoSendSeconds: 10
    }
}

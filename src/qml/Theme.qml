pragma Singleton

import QtQuick
import QtCore

// Design-Tokens aus dem Fjordpost-Entwurf (mopmail_ui.html). Mit
// "useSystemColors" werden stattdessen die Farben des KDE-Farbschemas verwendet.
QtObject {
    id: theme

    readonly property Settings settings: Settings {
        category: "Appearance"
        property string accent: "indigo"
        property bool dark: true
        property bool useSystemColors: false
        property bool contextDrawer: true
    }

    readonly property SystemPalette palette: SystemPalette {
        colorGroup: SystemPalette.Active
    }

    readonly property bool system: settings.useSystemColors
    readonly property bool dark: system ? (palette.window.hslLightness < 0.5) : settings.dark

    readonly property var accents: ({
            indigo: ["#6366f1", "#4f46e5"],
            emerald: ["#10b981", "#059669"],
            rose: ["#f43f5e", "#e11d48"],
            amber: ["#f59e0b", "#d97706"],
            cyan: ["#06b6d4", "#0891b2"]
        })
    readonly property var accentNames: ["indigo", "emerald", "rose", "amber", "cyan"]

    readonly property color accent: system ? palette.highlight : accents[settings.accent][0]
    readonly property color accentHover: system ? Qt.darker(palette.highlight, 1.15) : accents[settings.accent][1]
    readonly property color accentText: "white"

    // Flächen
    readonly property color dock: system ? Qt.darker(palette.window, dark ? 1.25 : 1.05) : (dark ? "#07090e" : "#f1f5f9")
    readonly property color sidebar: system ? palette.window : (dark ? "#0c0f17" : "#f8fafc")
    readonly property color list: system ? palette.base : (dark ? "#10141f" : "#ffffff")
    readonly property color reading: system ? palette.base : (dark ? "#141926" : "#ffffff")
    readonly property color card: system ? palette.alternateBase : (dark ? "#1c2333" : "#f1f5f9")
    readonly property color elevated: system ? palette.base : (dark ? "#1e2638" : "#ffffff")
    readonly property color border: dark ? Qt.rgba(1, 1, 1, 0.06) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 0.08)

    // Text
    readonly property color textPrimary: system ? palette.text : (dark ? "#f1f5f9" : "#0f172a")
    readonly property color textSecondary: system ? Qt.alpha(palette.text, 0.72) : (dark ? "#94a3b8" : "#475569")
    readonly property color textMuted: system ? palette.placeholderText : (dark ? "#64748b" : "#94a3b8")

    // Statusfarben
    readonly property color success: "#10b981"
    readonly property color warning: "#f59e0b"
    readonly property color danger: "#f43f5e"
    readonly property color star: "#fbbf24"

    // Typografie (relativ zur Systemschrift, damit KDE-Schriftgrößen respektiert werden)
    readonly property real fontBase: Qt.application.font.pointSize > 0 ? Qt.application.font.pointSize : 10
    readonly property real fontXxs: fontBase * 0.75
    readonly property real fontXs: fontBase * 0.85
    readonly property real fontSm: fontBase * 0.95
    readonly property real fontLg: fontBase * 1.5

    // Radien
    readonly property real radiusSm: 6
    readonly property real radius: 8
    readonly property real radiusLg: 12
    readonly property real radiusXl: 16

    // Farben für Avatare (stabil pro Name)
    readonly property var avatarColors: ["#818cf8", "#fb7185", "#fbbf24", "#34d399", "#22d3ee", "#a78bfa", "#38bdf8", "#f472b6"]

    function avatarColor(name) {
        let hash = 0;
        const s = String(name ?? "");
        for (let i = 0; i < s.length; ++i)
            hash = (hash * 31 + s.charCodeAt(i)) | 0;
        return avatarColors[Math.abs(hash) % avatarColors.length];
    }

    function initials(name) {
        // Nur Wortanfänge, die Buchstaben sind (Groß- und Kleinschreibung unterscheidbar)
        const parts = String(name ?? "").split(/[\s.@_<>"'()-]+/).filter(p => p.length > 0 && p[0].toUpperCase() !== p[0].toLowerCase());
        const result = parts.slice(0, 2).map(p => p[0].toUpperCase()).join("");
        return result.length > 0 ? result : "?";
    }
}

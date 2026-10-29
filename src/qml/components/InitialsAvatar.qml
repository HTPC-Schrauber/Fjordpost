import QtQuick
import QtQuick.Controls as QQC2
import de.fjordblau.fjordpost.app

// Abgerundetes Quadrat mit Initialen (wie im Entwurf).
Rectangle {
    id: avatar
    property string name
    property string initials: Theme.initials(name)
    property color tint: Theme.avatarColor(name)
    property real size: 40

    implicitWidth: size
    implicitHeight: size
    radius: size * 0.28
    color: Qt.alpha(tint, 0.2)

    QQC2.Label {
        anchors.centerIn: parent
        text: avatar.initials
        color: avatar.tint
        font.weight: Font.DemiBold
        font.pixelSize: avatar.size * 0.34
    }
}

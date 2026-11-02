import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import de.fjordblau.fjordpost.core
import "components"

// Einladung in einer Mail: Zusage, Vorbehalt, Absage; Antworten und Absagen eintragen.
Rectangle {
    id: card

    property var messageId: 0
    readonly property var invitation: {
        App.pim.revision;
        return messageId > 0 ? App.pim.invitation(messageId) : ({});
    }
    readonly property bool valid: invitation.summary !== undefined
    readonly property bool isRequest: invitation.method === "REQUEST" || invitation.method === "PUBLISH"

    signal notify(string message)

    function statusText(status) {
        return status === "ACCEPTED" ? i18n("accepted") : status === "TENTATIVE" ? i18n("tentatively accepted") : status === "DECLINED" ? i18n("declined") : i18n("not answered");
    }

    function respond(status) {
        const error = App.pim.respondToInvitation(messageId, status);
        notify(error.length > 0 ? error : i18n("Your reply was saved."));
    }

    visible: valid
    implicitHeight: valid ? content.implicitHeight + 24 : 0
    radius: Theme.radiusLg
    color: Qt.alpha(Theme.accent, 0.08)
    border.width: 1
    border.color: Qt.alpha(Theme.accent, 0.35)

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Kirigami.Icon {
                implicitWidth: 20
                implicitHeight: 20
                source: "view-calendar"
                isMask: true
                color: Theme.accent
            }
            QQC2.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: card.invitation.summary ?? ""
                font.pointSize: Theme.fontSm
                font.weight: Font.DemiBold
                font.strikeout: card.invitation.method === "CANCEL"
                color: Theme.textPrimary
                elide: Text.ElideRight
            }
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: card.invitation.whenText ?? ""
            font.pointSize: Theme.fontXs
            color: Theme.textPrimary
            wrapMode: Text.Wrap
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: (card.invitation.location ?? "").length > 0
            text: card.invitation.location ?? ""
            font.pointSize: Theme.fontXs
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: text.length > 0
            text: card.invitation.method === "REPLY" ? i18n("%1 has %2.", card.invitation.replyFrom, card.statusText(card.invitation.replyStatus))
                : card.invitation.method === "CANCEL" ? i18n("This event was cancelled.")
                : (card.invitation.organizer ?? "").length > 0 ? i18n("Organizer: %1", card.invitation.organizer) : ""
            font.pointSize: Theme.fontXs
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: card.isRequest && (card.invitation.ownStatus ?? "").length > 0
            text: i18n("You have %1.", card.statusText(card.invitation.ownStatus))
            font.pointSize: Theme.fontXs
            color: Theme.textSecondary
        }
        QQC2.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            visible: !(card.invitation.canSchedule ?? true)
            text: i18n("Connect a calendar to answer this invitation.")
            font.pointSize: Theme.fontXs
            color: Theme.warning
        }

        RowLayout {
            Layout.topMargin: 4
            spacing: 8
            visible: card.isRequest && (card.invitation.canSchedule ?? false)
            AccentButton {
                text: i18n("Accept")
                iconName: "dialog-ok"
                onClicked: card.respond("ACCEPTED")
            }
            IconButton {
                display: QQC2.AbstractButton.TextBesideIcon
                iconName: "question"
                text: i18n("Maybe")
                onClicked: card.respond("TENTATIVE")
            }
            IconButton {
                display: QQC2.AbstractButton.TextBesideIcon
                iconName: "dialog-cancel"
                text: i18n("Decline")
                onClicked: card.respond("DECLINED")
            }
        }

        IconButton {
            visible: (card.invitation.method === "REPLY" || card.invitation.method === "CANCEL") && (card.invitation.inCalendar ?? false)
            display: QQC2.AbstractButton.TextBesideIcon
            iconName: "view-calendar"
            text: card.invitation.method === "REPLY" ? i18n("Update calendar") : i18n("Remove from calendar")
            onClicked: {
                const error = App.pim.applyInvitation(card.messageId);
                card.notify(error.length > 0 ? error : i18n("Calendar updated."));
            }
        }
    }
}

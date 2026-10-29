#include "notifier.h"

#include "storage/database.h"

#include <KLocalizedString>
#include <KNotification>
#include <KStatusNotifierItem>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QSqlQuery>

namespace Fjordpost {

Notifier::Notifier(QObject *parent)
    : QObject(parent)
    , m_tray(new KStatusNotifierItem(QStringLiteral("fjordpost"), this))
{
    m_tray->setCategory(KStatusNotifierItem::Communications);
    m_tray->setTitle(QStringLiteral("Fjordpost"));
    m_tray->setIconByName(QStringLiteral("mail-client"));
    m_tray->setStandardActionsEnabled(true);
    // Fenster selbst ein-/ausblenden statt der Standard-Logik von KStatusNotifierItem
    m_tray->setAssociatedWindow(nullptr);
    connect(m_tray, &KStatusNotifierItem::activateRequested, this, &Notifier::activateRequested);
    setUnreadCount(0);
}

void Notifier::setUnreadCount(int count)
{
    if (count == m_unread)
        return;
    m_unread = count;
    m_tray->setStatus(count > 0 ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
    m_tray->setIconByName(count > 0 ? QStringLiteral("mail-unread-new") : QStringLiteral("mail-client"));
    m_tray->setToolTip(QStringLiteral("mail-client"), QStringLiteral("Fjordpost"),
                       count > 0 ? i18np("%1 unread message", "%1 unread messages", count) : i18n("No unread messages"));
    updateLauncherBadge(count);
}

void Notifier::updateLauncherBadge(int count)
{
    // Unity-LauncherEntry-Protokoll, das auch die Plasma-Taskleiste versteht.
    QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/de/fjordblau/Fjordpost"), QStringLiteral("com.canonical.Unity.LauncherEntry"),
                                                     QStringLiteral("Update"));
    signal << QStringLiteral("application://de.fjordblau.Fjordpost.desktop");
    signal << QVariantMap{{QStringLiteral("count"), qint64(count)}, {QStringLiteral("count-visible"), count > 0}};
    QDBusConnection::sessionBus().send(signal);
}

void Notifier::notifyNewMessages(const QList<qint64> &messageIds)
{
    if (messageIds.isEmpty())
        return;

    auto *notification = new KNotification(QStringLiteral("newMail"));
    notification->setIconName(QStringLiteral("mail-unread-new"));

    if (messageIds.size() == 1) {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT from_name, from_email, subject, snippet FROM messages WHERE id = ?"));
        q.addBindValue(messageIds.first());
        if (!q.exec() || !q.next()) {
            delete notification;
            return;
        }
        const QString from = q.value(0).toString().isEmpty() ? q.value(1).toString() : q.value(0).toString();
        notification->setTitle(from);
        notification->setText(QStringLiteral("<b>%1</b><br>%2").arg(q.value(2).toString().toHtmlEscaped(), q.value(3).toString().toHtmlEscaped()));
        const qint64 id = messageIds.first();
        KNotificationAction *open = notification->addDefaultAction(i18n("Open"));
        connect(open, &KNotificationAction::activated, this, [this, id] {
            Q_EMIT activateRequested();
            Q_EMIT openMessageRequested(id);
        });
    } else {
        notification->setTitle(i18n("New mail"));
        notification->setText(i18np("You have %1 new message.", "You have %1 new messages.", messageIds.size()));
        KNotificationAction *open = notification->addDefaultAction(i18n("Show"));
        connect(open, &KNotificationAction::activated, this, &Notifier::activateRequested);
    }
    notification->sendEvent();
}

} // namespace Fjordpost

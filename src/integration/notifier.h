#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>

class KStatusNotifierItem;

namespace Fjordpost {

// Desktop-Integration für neue Nachrichten: Benachrichtigungen,
// Tray-Symbol und Zähler am Programmsymbol in der Plasma-Taskleiste.
class Notifier : public QObject
{
    Q_OBJECT
public:
    explicit Notifier(QObject *parent = nullptr);

    void setUnreadCount(int count);
    // Wird vor "Beenden" im Tray-Menü gefragt; false bricht das Beenden ab.
    void setQuitGuard(std::function<bool()> guard);
    void notifyNewMessages(const QList<qint64> &messageIds);
    void notifyReminder(const QString &title, const QDateTime &when);

Q_SIGNALS:
    void activateRequested();
    void openMessageRequested(qint64 messageId);

private:
    void updateLauncherBadge(int count);

    KStatusNotifierItem *m_tray;
    std::function<bool()> m_quitGuard;
    int m_unread = -1;
};

} // namespace Fjordpost

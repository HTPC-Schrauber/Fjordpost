#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

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
    void notifyNewMessages(const QList<qint64> &messageIds);

Q_SIGNALS:
    void activateRequested();
    void openMessageRequested(qint64 messageId);

private:
    void updateLauncherBadge(int count);

    KStatusNotifierItem *m_tray;
    int m_unread = -1;
};

} // namespace Fjordpost

#pragma once

#include "core/account.h"

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <functional>

namespace KIMAP {
class LoginJob;
class Session;
}

namespace Fjordpost {

// Eine angemeldete IMAP-Verbindung mit serieller Aufgabenwarteschlange.
// Jede Aufgabe bekommt die Session und ruft done() auf, wenn sie fertig ist;
// erst dann startet die nächste. Die Anmeldung erfolgt bei Bedarf automatisch.
class ImapConnection : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void()>;
    using Task = std::function<void(KIMAP::Session *session, Done done)>;

    ImapConnection(const Account &account, const QString &name, QObject *parent = nullptr);
    ~ImapConnection() override;

    void setCredentials(const Account &account, const QString &secret);
    void enqueue(Task task, bool urgent = false);
    bool isBusy() const { return m_running || !m_queue.isEmpty(); }

    QStringList capabilities() const { return m_capabilities; }
    bool hasCapability(const QString &cap) const { return m_capabilities.contains(cap, Qt::CaseInsensitive); }

    // Aktuell ausgewählter Ordner (zur Vermeidung unnötiger SELECTs).
    QString selectedMailbox() const { return m_selected; }
    void setSelectedMailbox(const QString &path) { m_selected = path; }

    // Prüft, ob die Session nach einem Jobfehler noch verbunden ist
    // (Unterscheidung Serverablehnung vs. Verbindungsabbruch).
    bool isConnected() const;

    void disconnectFromServer();

    static KIMAP::LoginJob *createLoginJob(KIMAP::Session *session, const Account &account, const QString &secret);

Q_SIGNALS:
    void loginFailed(const QString &message);
    void connectionLost();

private:
    void runNext();
    void login(std::function<void(bool)> then);
    void resetSession();

    Account m_account;
    QString m_secret;
    QString m_name;
    QPointer<KIMAP::Session> m_session;
    bool m_loggedIn = false;
    bool m_running = false;
    QList<Task> m_queue;
    QStringList m_capabilities;
    QString m_selected;
};

} // namespace Fjordpost

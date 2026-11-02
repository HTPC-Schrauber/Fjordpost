#pragma once

#include "core/account.h"

#include <QList>
#include <QObject>

namespace Fjordpost {

class LineSocket;

struct Pop3Entry {
    int number = 0;
    QByteArray uidl;
};

// Eine POP3-Sitzung (RFC 1939, STLS nach RFC 2595). Befehle laufen einzeln: der
// nächste darf erst nach dem Signal des vorherigen kommen.
class Pop3Client : public QObject
{
    Q_OBJECT
public:
    explicit Pop3Client(QObject *parent = nullptr);

    void connectAndLogin(const Account &account, const QString &secret);
    void listUids();
    void retrieve(int number);
    // Wirksam erst mit quit(): bricht die Verbindung vorher ab, bleibt die Nachricht liegen.
    void remove(int number);
    void quit();
    void abort();

Q_SIGNALS:
    void loggedIn();
    void uidsListed(const QList<Fjordpost::Pop3Entry> &entries);
    void messageRetrieved(int number, const QByteArray &raw);
    void messageRemoved(int number);
    void finished();
    void failed(const QString &message);

private:
    enum class Step { Idle, Greeting, Stls, User, Pass, Uidl, Retr, Dele, Quit };

    void onLine(const QByteArray &line);
    void onStatus(bool ok, const QString &text);
    void onDataLine(const QByteArray &line);
    void onDataEnd();
    void sendUser();
    void send(Step step, const QByteArray &command);
    void fail(const QString &message);

    LineSocket *m_socket;
    Account m_account;
    QString m_secret;
    Step m_step = Step::Idle;
    bool m_inData = false;
    int m_number = 0;
    QByteArray m_data;
    QList<Pop3Entry> m_entries;
};

// Prüft die Zugangsdaten; ruft callback genau einmal auf.
void testPop3Login(const Account &account, const QString &secret, QObject *parent, std::function<void(bool ok, const QString &error)> callback);

} // namespace Fjordpost

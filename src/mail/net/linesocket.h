#pragma once

#include "core/account.h"

#include <QByteArray>
#include <QObject>
#include <QTimer>

class QSslSocket;

namespace MopMail {

// Zeilenorientierte TCP/TLS-Verbindung für textbasierte Protokolle (SMTP, POP3).
// Antworten werden gepuffert und nur als vollständige Zeilen gemeldet: Server
// dürfen eine Zeile auf mehrere TCP-Pakete verteilen.
class LineSocket : public QObject
{
    Q_OBJECT
public:
    explicit LineSocket(QObject *parent = nullptr);

    // Security::Tls verschlüsselt sofort; StartTls erst nach startTls().
    void connectToHost(const QString &host, quint16 port, Security security);
    void startTls();
    void writeLine(const QByteArray &line);
    void write(const QByteArray &data);
    void close();

    bool isEncrypted() const;
    // Adressliteral der lokalen Seite für EHLO, z. B. "[192.0.2.1]".
    QByteArray localAddressLiteral() const;

    void setTimeout(int seconds);

Q_SIGNALS:
    void ready();     // verbunden (bei TLS: nach dem Handshake)
    void encrypted(); // nach startTls()
    void lineReceived(const QByteArray &line); // ohne Zeilenende
    void failed(const QString &message);

private:
    void readLines();
    void fail(const QString &message);

    QSslSocket *m_socket;
    QTimer m_timeout;
    QString m_host;
    bool m_startTlsPending = false;
    bool m_failed = false;
};

} // namespace MopMail

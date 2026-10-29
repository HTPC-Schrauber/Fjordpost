#include "linesocket.h"

#include <KLocalizedString>

#include <QHostAddress>
#include <QSslSocket>

namespace MopMail {

LineSocket::LineSocket(QObject *parent)
    : QObject(parent)
    , m_socket(new QSslSocket(this))
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(60 * 1000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        fail(i18n("The server %1 did not respond in time.", m_host));
    });

    connect(m_socket, &QSslSocket::connected, this, [this] {
        m_timeout.start();
        if (m_socket->mode() == QSslSocket::UnencryptedMode)
            Q_EMIT ready();
    });
    connect(m_socket, &QSslSocket::encrypted, this, [this] {
        m_timeout.start();
        if (m_startTlsPending) {
            m_startTlsPending = false;
            Q_EMIT encrypted();
        } else {
            Q_EMIT ready();
        }
    });
    connect(m_socket, &QSslSocket::readyRead, this, &LineSocket::readLines);
    connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        QStringList messages;
        for (const QSslError &e : errors)
            messages << e.errorString();
        fail(i18n("The secure connection to %1 could not be verified: %2", m_host, messages.join(QStringLiteral("; "))));
    });
    connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        if (error == QAbstractSocket::RemoteHostClosedError)
            fail(i18n("The server %1 closed the connection.", m_host));
        else
            fail(m_socket->errorString());
    });
}

void LineSocket::connectToHost(const QString &host, quint16 port, Security security)
{
    m_host = host;
    m_failed = false;
    m_timeout.start();
    if (security == Security::Tls)
        m_socket->connectToHostEncrypted(host, port);
    else
        m_socket->connectToHost(host, port);
}

void LineSocket::startTls()
{
    m_startTlsPending = true;
    m_socket->startClientEncryption();
}

void LineSocket::writeLine(const QByteArray &line)
{
    write(line + "\r\n");
}

void LineSocket::write(const QByteArray &data)
{
    m_timeout.start();
    m_socket->write(data);
}

void LineSocket::close()
{
    m_timeout.stop();
    m_socket->disconnect(this);
    m_socket->abort();
}

bool LineSocket::isEncrypted() const
{
    return m_socket->isEncrypted();
}

QByteArray LineSocket::localAddressLiteral() const
{
    const QHostAddress address = m_socket->localAddress();
    if (address.protocol() == QAbstractSocket::IPv6Protocol)
        return "[IPv6:" + address.toString().toLatin1() + "]";
    return "[" + address.toString().toLatin1() + "]";
}

void LineSocket::setTimeout(int seconds)
{
    m_timeout.setInterval(seconds * 1000);
}

void LineSocket::readLines()
{
    m_timeout.start();
    while (!m_failed && m_socket->canReadLine()) {
        QByteArray line = m_socket->readLine();
        while (line.endsWith('\n') || line.endsWith('\r'))
            line.chop(1);
        Q_EMIT lineReceived(line);
    }
}

void LineSocket::fail(const QString &message)
{
    if (m_failed)
        return;
    m_failed = true;
    m_timeout.stop();
    Q_EMIT failed(message);
}

} // namespace MopMail

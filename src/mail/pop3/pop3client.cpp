#include "pop3client.h"

#include "mail/net/linesocket.h"

#include <KLocalizedString>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPop3, "fjordpost.pop3")

namespace Fjordpost {

Pop3Client::Pop3Client(QObject *parent)
    : QObject(parent)
    , m_socket(new LineSocket(this))
{
    connect(m_socket, &LineSocket::lineReceived, this, &Pop3Client::onLine);
    connect(m_socket, &LineSocket::failed, this, [this](const QString &message) {
        // Nach QUIT trennt der Server die Verbindung von sich aus.
        if (m_step == Step::Quit) {
            m_step = Step::Idle;
            Q_EMIT finished();
            return;
        }
        fail(message);
    });
    connect(m_socket, &LineSocket::encrypted, this, &Pop3Client::sendUser);
}

void Pop3Client::connectAndLogin(const Account &account, const QString &secret)
{
    m_account = account;
    m_secret = secret;
    if (account.incomingHost.isEmpty()) {
        fail(i18n("No incoming mail server is configured."));
        return;
    }
    m_step = Step::Greeting;
    m_socket->connectToHost(account.incomingHost, quint16(account.incomingPort), account.incomingSecurity);
}

void Pop3Client::listUids()
{
    m_entries.clear();
    send(Step::Uidl, "UIDL");
}

void Pop3Client::retrieve(int number)
{
    m_number = number;
    m_data.clear();
    send(Step::Retr, "RETR " + QByteArray::number(number));
}

void Pop3Client::remove(int number)
{
    m_number = number;
    send(Step::Dele, "DELE " + QByteArray::number(number));
}

void Pop3Client::quit()
{
    send(Step::Quit, "QUIT");
}

void Pop3Client::abort()
{
    m_step = Step::Idle;
    m_socket->close();
}

void Pop3Client::send(Step step, const QByteArray &command)
{
    m_step = step;
    m_socket->writeLine(command);
}

void Pop3Client::sendUser()
{
    const QString user = m_account.username.isEmpty() ? m_account.email : m_account.username;
    send(Step::User, "USER " + user.toUtf8());
}

void Pop3Client::onLine(const QByteArray &line)
{
    if (m_inData) {
        onDataLine(line);
        return;
    }
    const bool ok = line.startsWith("+OK");
    if (!ok && !line.startsWith("-ERR")) {
        fail(i18n("The server sent an unexpected response: %1", QString::fromUtf8(line.left(80))));
        return;
    }
    const qsizetype space = line.indexOf(' ');
    onStatus(ok, space > 0 ? QString::fromUtf8(line.mid(space + 1)).trimmed() : QString());
}

void Pop3Client::onStatus(bool ok, const QString &text)
{
    switch (m_step) {
    case Step::Greeting:
        if (!ok)
            return fail(i18n("The server refused the connection: %1", text));
        if (m_account.incomingSecurity == Security::StartTls && !m_socket->isEncrypted())
            send(Step::Stls, "STLS");
        else
            sendUser();
        return;

    case Step::Stls:
        if (!ok)
            return fail(i18n("The server does not support STARTTLS. Choose a different security setting."));
        m_socket->startTls();
        return;

    case Step::User:
        if (!ok)
            return fail(i18n("Login failed: %1", text));
        send(Step::Pass, "PASS " + m_secret.toUtf8());
        return;

    case Step::Pass:
        if (!ok)
            return fail(i18n("Login failed: %1", text));
        m_step = Step::Idle;
        Q_EMIT loggedIn();
        return;

    case Step::Uidl:
        if (!ok)
            return fail(i18n("The server does not support message IDs (UIDL), so new messages cannot be recognized: %1", text));
        m_inData = true;
        return;

    case Step::Retr:
        if (!ok)
            return fail(i18n("The message could not be retrieved: %1", text));
        m_inData = true;
        return;

    case Step::Dele:
        if (!ok)
            return fail(i18n("The message could not be deleted from the server: %1", text));
        m_step = Step::Idle;
        Q_EMIT messageRemoved(m_number);
        return;

    case Step::Quit:
        m_step = Step::Idle;
        m_socket->close();
        Q_EMIT finished();
        return;

    case Step::Idle:
        return;
    }
}

void Pop3Client::onDataLine(const QByteArray &line)
{
    if (line == ".") {
        m_inData = false;
        onDataEnd();
        return;
    }
    // Verdoppelter Punkt am Zeilenanfang (RFC 1939, 3)
    const QByteArray content = line.startsWith('.') ? line.mid(1) : line;
    if (m_step == Step::Uidl) {
        const qsizetype space = content.indexOf(' ');
        if (space > 0)
            m_entries << Pop3Entry{content.left(space).toInt(), content.mid(space + 1).trimmed()};
        return;
    }
    m_data += content;
    m_data += '\n';
}

void Pop3Client::onDataEnd()
{
    const Step step = m_step;
    m_step = Step::Idle;
    if (step == Step::Uidl)
        Q_EMIT uidsListed(m_entries);
    else
        Q_EMIT messageRetrieved(m_number, std::exchange(m_data, {}));
}

void Pop3Client::fail(const QString &message)
{
    qCWarning(lcPop3) << "POP3-Fehler:" << message;
    m_step = Step::Idle;
    m_inData = false;
    m_socket->close();
    Q_EMIT failed(message);
}

void testPop3Login(const Account &account, const QString &secret, QObject *parent, std::function<void(bool, const QString &)> callback)
{
    auto *client = new Pop3Client(parent);
    QObject::connect(client, &Pop3Client::loggedIn, client, [client, callback] {
        client->quit();
        callback(true, QString());
    });
    QObject::connect(client, &Pop3Client::finished, client, &QObject::deleteLater);
    QObject::connect(client, &Pop3Client::failed, client, [client, callback](const QString &message) {
        client->deleteLater();
        callback(false, message);
    });
    client->connectAndLogin(account, secret);
}

} // namespace Fjordpost

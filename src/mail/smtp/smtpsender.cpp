#include "smtpsender.h"

#include "mail/net/linesocket.h"

#include <KLocalizedString>

#include <QLoggingCategory>
#include <QSet>

Q_LOGGING_CATEGORY(lcSmtp, "fjordpost.smtp")

namespace Fjordpost {

namespace {

// Zeilenenden auf CRLF bringen und Zeilen mit führendem Punkt verdoppeln (RFC 5321, 4.5.2).
QByteArray dotStuffed(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() + data.size() / 50 + 8);
    bool lineStart = true;
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data.at(i);
        if (lineStart && c == '.')
            out += '.';
        if (c == '\n') {
            if (!out.endsWith('\r'))
                out += '\r';
            out += '\n';
            lineStart = true;
            continue;
        }
        out += c;
        lineStart = false;
    }
    if (!out.endsWith("\r\n"))
        out += "\r\n";
    return out;
}

// Eine SMTP-Sitzung: Verbinden, EHLO, ggf. STARTTLS, Anmelden, optional Senden, QUIT.
class SmtpSession : public QObject
{
public:
    SmtpSession(const Account &account, const QString &secret, const QString &from, const QStringList &recipients, const QByteArray &data,
                bool sendMessage, SmtpSender::Callback callback, QObject *parent)
        : QObject(parent)
        , m_account(account)
        , m_secret(secret)
        , m_from(from)
        , m_recipients(recipients)
        , m_data(data)
        , m_sendMessage(sendMessage)
        , m_callback(std::move(callback))
        , m_socket(new LineSocket(this))
    {
        connect(m_socket, &LineSocket::lineReceived, this, &SmtpSession::onLine);
        connect(m_socket, &LineSocket::failed, this, [this](const QString &message) {
            finish(false, message);
        });
        connect(m_socket, &LineSocket::encrypted, this, [this] {
            sendEhlo();
        });
    }

    void start()
    {
        if (m_account.outgoingHost.isEmpty()) {
            finish(false, i18n("No outgoing mail server is configured."));
            return;
        }
        m_step = Step::Greeting;
        m_socket->connectToHost(m_account.outgoingHost, quint16(m_account.outgoingPort), m_account.outgoingSecurity);
    }

private:
    enum class Step { Greeting, Ehlo, Helo, StartTls, Auth, AuthLoginUser, AuthLoginPassword, MailFrom, RcptTo, Data, Body, Quit };

    void onLine(const QByteArray &line)
    {
        const int code = line.left(3).toInt();
        m_reply << QString::fromUtf8(line.mid(4));
        // Mehrzeilige Antwort: "250-..." bis zur Zeile "250 ..."
        if (line.size() > 3 && line.at(3) == '-')
            return;
        const QStringList reply = std::exchange(m_reply, {});
        handle(code, reply);
    }

    void handle(int code, const QStringList &reply)
    {
        const QString text = reply.join(QLatin1Char(' ')).trimmed();
        if (code == 421 && m_step != Step::Quit) {
            finish(false, i18n("The server is not available: %1", text));
            return;
        }

        switch (m_step) {
        case Step::Greeting:
            if (code != 220)
                return finish(false, i18n("The server refused the connection: %1", text));
            sendEhlo();
            return;

        case Step::Ehlo:
            if (code != 250) {
                // Sehr alte Server kennen kein EHLO.
                m_step = Step::Helo;
                m_socket->writeLine("HELO " + m_socket->localAddressLiteral());
                return;
            }
            parseCapabilities(reply.mid(1));
            if (m_account.outgoingSecurity == Security::StartTls && !m_socket->isEncrypted()) {
                if (!m_capabilities.contains(QStringLiteral("STARTTLS")))
                    return finish(false, i18n("The server does not support STARTTLS. Choose a different security setting."));
                m_step = Step::StartTls;
                m_socket->writeLine("STARTTLS");
                return;
            }
            authenticate();
            return;

        case Step::Helo:
            if (code != 250)
                return finish(false, i18n("The server rejected the greeting: %1", text));
            authenticate();
            return;

        case Step::StartTls:
            if (code != 220)
                return finish(false, i18n("The server could not start encryption: %1", text));
            m_socket->startTls();
            return;

        case Step::AuthLoginUser:
            if (code != 334)
                return finish(false, i18n("Login failed: %1", text));
            m_step = Step::AuthLoginPassword;
            m_socket->writeLine(userName().toUtf8().toBase64());
            return;

        case Step::AuthLoginPassword:
            if (code != 334)
                return finish(false, i18n("Login failed: %1", text));
            m_step = Step::Auth;
            m_socket->writeLine(m_secret.toUtf8().toBase64());
            return;

        case Step::Auth:
            if (code == 334) {
                // XOAUTH2 meldet Fehler als Challenge; leere Antwort beendet den Versuch.
                m_socket->writeLine(QByteArray());
                return;
            }
            if (code != 235)
                return finish(false, i18n("Login failed: %1", text));
            startTransaction();
            return;

        case Step::MailFrom:
            if (code != 250)
                return finish(false, i18n("The server rejected the sender %1: %2", m_from, text));
            m_nextRecipient = 0;
            nextRecipient();
            return;

        case Step::RcptTo:
            if (code != 250 && code != 251)
                return finish(false, i18n("The server rejected the recipient %1: %2", m_recipients.value(m_nextRecipient - 1), text));
            nextRecipient();
            return;

        case Step::Data:
            if (code != 354)
                return finish(false, i18n("The server refused the message: %1", text));
            m_step = Step::Body;
            m_socket->write(dotStuffed(m_data) + ".\r\n");
            return;

        case Step::Body:
            if (code != 250)
                return finish(false, i18n("The server refused the message: %1", text));
            quit();
            return;

        case Step::Quit:
            return;
        }
    }

    void sendEhlo()
    {
        m_step = Step::Ehlo;
        m_capabilities.clear();
        m_authMechanisms.clear();
        m_socket->writeLine("EHLO " + m_socket->localAddressLiteral());
    }

    void parseCapabilities(const QStringList &lines)
    {
        for (const QString &line : lines) {
            const QStringList words = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (words.isEmpty())
                continue;
            const QString keyword = words.first().toUpper();
            // Ältere Server melden "AUTH=PLAIN LOGIN".
            if (keyword == QLatin1String("AUTH") || keyword.startsWith(QLatin1String("AUTH="))) {
                QStringList mechanisms = words.mid(1);
                if (keyword.startsWith(QLatin1String("AUTH=")))
                    mechanisms.prepend(keyword.mid(5));
                for (const QString &m : std::as_const(mechanisms))
                    m_authMechanisms.insert(m.toUpper());
                m_capabilities.insert(QStringLiteral("AUTH"));
            } else {
                m_capabilities.insert(keyword);
            }
        }
    }

    QString userName() const
    {
        return m_account.username.isEmpty() ? m_account.email : m_account.username;
    }

    void authenticate()
    {
        if (m_secret.isEmpty() || !m_capabilities.contains(QStringLiteral("AUTH"))) {
            // Ohne Zugangsdaten bzw. bei Servern ohne Anmeldung (lokale Relays) direkt senden.
            startTransaction();
            return;
        }
        m_step = Step::Auth;
        if (m_account.authMethod == AuthMethod::OAuth2) {
            const QByteArray token = "user=" + userName().toUtf8() + "\x01" "auth=Bearer " + m_secret.toUtf8() + "\x01\x01";
            m_socket->writeLine("AUTH XOAUTH2 " + token.toBase64());
        } else if (m_authMechanisms.contains(QStringLiteral("PLAIN"))) {
            const QByteArray credentials = QByteArray(1, '\0') + userName().toUtf8() + QByteArray(1, '\0') + m_secret.toUtf8();
            m_socket->writeLine("AUTH PLAIN " + credentials.toBase64());
        } else if (m_authMechanisms.contains(QStringLiteral("LOGIN"))) {
            m_step = Step::AuthLoginUser;
            m_socket->writeLine("AUTH LOGIN");
        } else {
            finish(false, i18n("The server offers no supported login method (%1).", QStringList(m_authMechanisms.values()).join(QStringLiteral(", "))));
        }
    }

    void startTransaction()
    {
        if (!m_sendMessage) {
            finish(true, QString());
            quit();
            return;
        }
        m_step = Step::MailFrom;
        QByteArray command = "MAIL FROM:<" + m_from.toUtf8() + ">";
        if (m_capabilities.contains(QStringLiteral("SIZE")))
            command += " SIZE=" + QByteArray::number(m_data.size());
        m_socket->writeLine(command);
    }

    void nextRecipient()
    {
        if (m_nextRecipient >= m_recipients.size()) {
            m_step = Step::Data;
            m_socket->writeLine("DATA");
            return;
        }
        m_step = Step::RcptTo;
        m_socket->writeLine("RCPT TO:<" + m_recipients.at(m_nextRecipient++).toUtf8() + ">");
    }

    void quit()
    {
        finish(true, QString());
        m_step = Step::Quit;
        m_socket->writeLine("QUIT");
        // Auf die Antwort kommt es nicht mehr an.
        QTimer::singleShot(2000, this, [this] {
            m_socket->close();
            deleteLater();
        });
    }

    void finish(bool ok, const QString &error)
    {
        if (m_finished)
            return;
        m_finished = true;
        if (!ok) {
            qCWarning(lcSmtp) << "SMTP-Fehler:" << error;
            m_socket->close();
            deleteLater();
        }
        m_callback(ok, error);
    }

    Account m_account;
    QString m_secret;
    QString m_from;
    QStringList m_recipients;
    QByteArray m_data;
    bool m_sendMessage;
    SmtpSender::Callback m_callback;
    LineSocket *m_socket;

    Step m_step = Step::Greeting;
    QStringList m_reply;
    QSet<QString> m_capabilities;
    QSet<QString> m_authMechanisms;
    int m_nextRecipient = 0;
    bool m_finished = false;
};

} // namespace

SmtpSender::SmtpSender(QObject *parent)
    : QObject(parent)
{
}

void SmtpSender::send(const Account &account, const QString &secret, const QString &from, const QStringList &recipients, const QByteArray &data, Callback callback)
{
    if (recipients.isEmpty()) {
        callback(false, i18n("The message has no recipients."));
        return;
    }
    auto *session = new SmtpSession(account, secret, from, recipients, data, true, std::move(callback), this);
    session->start();
}

void SmtpSender::testLogin(const Account &account, const QString &secret, Callback callback)
{
    auto *session = new SmtpSession(account, secret, {}, {}, {}, false, std::move(callback), this);
    session->start();
}

} // namespace Fjordpost

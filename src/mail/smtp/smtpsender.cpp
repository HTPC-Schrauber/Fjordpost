#include "smtpsender.h"

#include <KLocalizedString>
#include <KSMTP/LoginJob>
#include <KSMTP/SendJob>
#include <KSMTP/Session>

#include <QLoggingCategory>
#include <QPointer>
#include <memory>

Q_LOGGING_CATEGORY(lcSmtp, "mopmail.smtp")

namespace MopMail {

namespace {

KSmtp::Session::EncryptionMode encryptionFor(Security s)
{
    switch (s) {
    case Security::None:
        return KSmtp::Session::Unencrypted;
    case Security::StartTls:
        return KSmtp::Session::STARTTLS;
    case Security::Tls:
        break;
    }
    return KSmtp::Session::TLS;
}

// Ablauf: Verbinden -> (Anmelden) -> optional Senden -> QUIT.
void run(QObject *context, const Account &account, const QString &secret, std::function<void(KSmtp::Session *, SmtpSender::Callback)> afterLogin,
         SmtpSender::Callback callback)
{
    auto *session = new KSmtp::Session(account.outgoingHost, quint16(account.outgoingPort), context);
    session->setEncryptionMode(encryptionFor(account.outgoingSecurity));
    session->setSocketTimeout(60 * 1000);

    // Callback genau einmal aufrufen und die Session danach aufräumen.
    auto finished = std::make_shared<bool>(false);
    auto finish = [session, finished, callback](bool ok, const QString &error) {
        if (*finished)
            return;
        *finished = true;
        if (!ok)
            qCWarning(lcSmtp) << "SMTP-Fehler:" << error;
        callback(ok, error);
        if (session->state() != KSmtp::Session::Disconnected)
            session->quit();
        session->deleteLater();
    };

    QObject::connect(session, &KSmtp::Session::connectionError, context, [finish](const QString &error) {
        finish(false, error);
    });
    QObject::connect(session, &KSmtp::Session::stateChanged, context, [=](KSmtp::Session::State state) {
        if (state == KSmtp::Session::Disconnected) {
            finish(false, i18n("Connection to %1 was closed.", account.outgoingHost));
            return;
        }
        if (state != KSmtp::Session::NotAuthenticated)
            return;

        if (secret.isEmpty()) {
            afterLogin(session, finish);
            return;
        }
        auto *login = new KSmtp::LoginJob(session);
        login->setUserName(account.username.isEmpty() ? account.email : account.username);
        login->setPassword(secret);
        login->setPreferedAuthMode(account.authMethod == AuthMethod::OAuth2 ? KSmtp::LoginJob::XOAuth2 : KSmtp::LoginJob::Plain);
        QObject::connect(login, &KJob::result, context, [=](KJob *job) {
            if (job->error()) {
                finish(false, job->errorString());
                return;
            }
            afterLogin(session, finish);
        });
        login->start();
    });
    session->open();
}

} // namespace

SmtpSender::SmtpSender(QObject *parent)
    : QObject(parent)
{
}

void SmtpSender::send(const Account &account, const QString &secret, const QString &from, const QStringList &recipients, const QByteArray &data, Callback callback)
{
    run(this, account, secret,
        [this, from, recipients, data](KSmtp::Session *session, Callback finish) {
            auto *job = new KSmtp::SendJob(session);
            job->setFrom(from);
            job->setTo(recipients);
            job->setData(data);
            connect(job, &KJob::result, this, [finish](KJob *j) {
                finish(!j->error(), j->errorString());
            });
            job->start();
        },
        std::move(callback));
}

void SmtpSender::testLogin(const Account &account, const QString &secret, Callback callback)
{
    run(this, account, secret,
        [](KSmtp::Session *, Callback finish) {
            finish(true, QString());
        },
        std::move(callback));
}

} // namespace MopMail

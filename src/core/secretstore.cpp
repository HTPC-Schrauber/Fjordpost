#include "secretstore.h"

#include <QLoggingCategory>
#include <qt6keychain/keychain.h>

Q_LOGGING_CATEGORY(lcSecrets, "mopmail.secrets")

namespace MopMail {

SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
{
}

QString SecretStore::serviceName()
{
    return QStringLiteral("MopMail");
}

void SecretStore::read(const QString &key, ReadCallback callback)
{
    auto *job = new QKeychain::ReadPasswordJob(serviceName(), this);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [job, callback = std::move(callback)] {
        const bool ok = job->error() == QKeychain::NoError;
        if (!ok && job->error() != QKeychain::EntryNotFound)
            qCWarning(lcSecrets) << "Lesen fehlgeschlagen:" << job->errorString();
        if (callback)
            callback(ok, ok ? job->textData() : QString());
    });
    job->start();
}

void SecretStore::write(const QString &key, const QString &secret, WriteCallback callback)
{
    auto *job = new QKeychain::WritePasswordJob(serviceName(), this);
    job->setKey(key);
    job->setTextData(secret);
    connect(job, &QKeychain::Job::finished, this, [job, callback = std::move(callback)] {
        const bool ok = job->error() == QKeychain::NoError;
        if (!ok)
            qCWarning(lcSecrets) << "Schreiben fehlgeschlagen:" << job->errorString();
        if (callback)
            callback(ok);
    });
    job->start();
}

void SecretStore::remove(const QString &key, WriteCallback callback)
{
    auto *job = new QKeychain::DeletePasswordJob(serviceName(), this);
    job->setKey(key);
    connect(job, &QKeychain::Job::finished, this, [job, callback = std::move(callback)] {
        const bool ok = job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound;
        if (callback)
            callback(ok);
    });
    job->start();
}

} // namespace MopMail

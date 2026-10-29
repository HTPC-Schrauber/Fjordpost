#pragma once

#include <QObject>
#include <functional>

namespace Fjordpost {

// Asynchroner Zugriff auf Passwörter und OAuth-Tokens über QtKeychain.
// Unter KDE landet alles in KWallet, sonst im Secret Service.
class SecretStore : public QObject
{
    Q_OBJECT
public:
    using ReadCallback = std::function<void(bool ok, const QString &secret)>;
    using WriteCallback = std::function<void(bool ok)>;

    explicit SecretStore(QObject *parent = nullptr);

    // virtuell, damit Tests ohne KWallet/Secret Service auskommen
    virtual void read(const QString &key, ReadCallback callback);
    void write(const QString &key, const QString &secret, WriteCallback callback = {});
    void remove(const QString &key, WriteCallback callback = {});

private:
    static QString serviceName();
};

} // namespace Fjordpost

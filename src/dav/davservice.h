#pragma once

#include "auth/oauthclient.h"
#include "storage/davstore.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <functional>

namespace Fjordpost {

class DavClient;
class DavDiscovery;
class DavSync;
class OAuthTokens;
class SecretStore;

// Koordiniert alle DAV-Konten: Abgleich, Einrichtung, Erinnerungen.
class DavService : public QObject
{
    Q_OBJECT
public:
    explicit DavService(SecretStore *secrets, QObject *parent = nullptr);

    // Ohne Tokens (Tests) können OAuth-Konten keine Verbindung aufbauen.
    void setTokens(OAuthTokens *tokens) { m_tokens = tokens; }

    void start();
    void syncAll();
    // Nach lokalen Änderungen aufrufen; ohne Start (--offline) folgenlos.
    void flush(qint64 accountId);

    // Prüft die Zugangsdaten, legt das Konto an und gleicht es ab.
    // done erhält eine Fehlermeldung oder einen leeren String.
    void addAccount(const QString &address, const QString &username, const QString &password, qint64 mailAccountId, std::function<void(const QString &error)> done);
    // Nach der Anmeldung im Browser: Kalender und Adressbücher des Anbieters suchen und anlegen.
    void addOAuthAccount(const OAuthProvider &provider, const QString &email, const OAuthClient::Result &tokens, qint64 mailAccountId, std::function<void(const QString &error)> done);
    void removeAccount(qint64 id);

    // Direkt auf dem Server; done erhält eine Fehlermeldung oder einen leeren String.
    using Done = std::function<void(const QString &error)>;
    void createCollection(qint64 accountId, const QString &kind, const QString &name, const QString &color, Done done);
    void updateCollection(qint64 collectionId, const QString &name, const QString &color, Done done);
    void removeCollection(qint64 collectionId, Done done);

    static constexpr int syncIntervalMinutes = 15;

Q_SIGNALS:
    void dataChanged();
    void errorOccurred(const QString &message);
    void conflict(const QString &title);
    void reminder(const QString &title, const QDateTime &when);

private:
    void attach(const DavAccount &account, const QString &password);
    void saveAndAttach(DavAccount account, const QString &secret, std::function<void(const QString &error)> done);
    void attachWithSecret(const DavAccount &account);
    void checkAlarms();

    SecretStore *m_secrets;
    OAuthTokens *m_tokens = nullptr;
    bool m_started = false;
    QHash<qint64, DavSync *> m_accounts;
    QTimer m_syncTimer;
    QTimer m_alarmTimer;
    QDateTime m_alarmsChecked;
};

} // namespace Fjordpost

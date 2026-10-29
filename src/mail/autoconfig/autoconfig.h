#pragma once

#include "core/account.h"

#include <QList>
#include <QObject>
#include <QStringList>

class QNetworkAccessManager;

namespace Fjordpost {

struct ServerSettings {
    AccountType type = AccountType::Imap; // nur für Eingangsserver relevant
    QString host;
    int port = 0;
    Security security = Security::Tls;
    QString username;
    bool oauth = false;
};

struct DiscoveredSettings {
    QList<ServerSettings> incoming; // bevorzugte zuerst
    QList<ServerSettings> outgoing;
    QString providerName;
    QString source; // woher die Daten stammen (für die Anzeige)
    bool guessed = false;

    bool isValid() const { return !incoming.isEmpty() && !outgoing.isEmpty(); }
};

// Ermittelt Servereinstellungen zu einer E-Mail-Adresse (wie Thunderbird):
// Anbieter-Autoconfig, .well-known, ISPDB, MX-Eintrag, zuletzt Raten.
class AutoConfig : public QObject
{
    Q_OBJECT
public:
    explicit AutoConfig(QObject *parent = nullptr);

    void discover(const QString &email);
    void abort();

    // Parser für das Thunderbird-Autoconfig-Format (öffentlich für Tests).
    static DiscoveredSettings parse(const QByteArray &xml, const QString &email);
    // Fallback ohne Netzwerk: übliche Namen raten.
    static DiscoveredSettings guess(const QString &email);

Q_SIGNALS:
    void finished(const Fjordpost::DiscoveredSettings &settings);
    void progress(const QString &message);

private:
    void tryNext();
    void lookupMx();

    QNetworkAccessManager *m_network;
    QString m_email;
    QString m_domain;
    QStringList m_urls;
    QStringList m_sources;
    bool m_mxDone = false;
    int m_generation = 0;
};

} // namespace Fjordpost

Q_DECLARE_METATYPE(Fjordpost::DiscoveredSettings)

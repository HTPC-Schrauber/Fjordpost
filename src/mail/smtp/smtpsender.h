#pragma once

#include "core/account.h"

#include <QObject>
#include <functional>

namespace Fjordpost {

// Versendet eine fertige Nachricht über den SMTP-Server eines Kontos.
// Pro Versand eine eigene Verbindung (SMTP-Sitzungen sind kurzlebig).
class SmtpSender : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(bool ok, const QString &error)>;

    explicit SmtpSender(QObject *parent = nullptr);

    void send(const Account &account, const QString &secret, const QString &from, const QStringList &recipients, const QByteArray &data, Callback callback);

    // Nur Verbindung und Anmeldung prüfen (Kontoeinrichtung).
    void testLogin(const Account &account, const QString &secret, Callback callback);
};

} // namespace Fjordpost

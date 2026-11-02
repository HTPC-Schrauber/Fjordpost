#pragma once

#include "dav/davclient.h"

#include <QObject>
#include <QUrl>

#include <functional>

namespace Fjordpost {

// Ermittelt aus einer Eingabe (URL, Hostname oder Mailadresse) die Heimat-
// verzeichnisse für Kalender und Adressbücher (RFC 6764, RFC 4791, RFC 6352).
class DavDiscovery : public QObject
{
    Q_OBJECT
public:
    struct Result {
        QString error;   // leer bei Erfolg
        QUrl baseUrl;
        QString calendarHome;
        QString addressbookHome;
        bool autoSchedule = false;
    };
    using Callback = std::function<void(const Result &result)>;

    DavDiscovery(DavClient *client, QObject *parent = nullptr);

    void start(const QString &input, Callback callback);
    // Für Tests öffentlich: mögliche Einstiegspunkte einer Eingabe
    static QList<QUrl> candidates(const QString &input);

private:
    void tryNext();
    void probe(const QUrl &url);
    void readPrincipal(const QUrl &principal);
    void checkSchedule();
    void finish();

    DavClient *m_client;
    Callback m_callback;
    QList<QUrl> m_queue;
    Result m_result;
    QString m_lastError;
};

} // namespace Fjordpost

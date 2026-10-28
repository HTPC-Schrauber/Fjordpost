#pragma once

#include <QWebEngineUrlRequestInterceptor>
#include <atomic>

namespace MopMail {

// Blockiert in der Nachrichtenansicht alle Netzwerkzugriffe (Tracking-Pixel,
// externe Bilder), solange der Nutzer sie nicht ausdrücklich erlaubt.
class RemoteContentBlocker : public QWebEngineUrlRequestInterceptor
{
    Q_OBJECT
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void setAllowed(bool allowed) { m_allowed = allowed; }
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;

private:
    std::atomic_bool m_allowed = false;
};

} // namespace MopMail

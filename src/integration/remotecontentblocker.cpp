#include "remotecontentblocker.h"

namespace Fjordpost {

void RemoteContentBlocker::interceptRequest(QWebEngineUrlRequestInfo &info)
{
    const QString scheme = info.requestUrl().scheme();
    const bool remote = scheme != QLatin1String("data") && scheme != QLatin1String("about") && scheme != QLatin1String("qrc");
    // Seitennavigation (Link-Klicks) wird in QML abgefangen und extern geöffnet.
    if (remote && !m_allowed && info.navigationType() != QWebEngineUrlRequestInfo::NavigationTypeLink)
        info.block(true);
}

} // namespace Fjordpost

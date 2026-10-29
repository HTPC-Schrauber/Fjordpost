#include "autoconfig.h"

#include <KLocalizedString>

#include <QDnsLookup>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(lcAutoconfig, "fjordpost.autoconfig")

namespace Fjordpost {

namespace {

constexpr int requestTimeoutMs = 8000;

QString expandPlaceholders(QString value, const QString &email)
{
    value.replace(QLatin1String("%EMAILADDRESS%"), email);
    value.replace(QLatin1String("%EMAILLOCALPART%"), email.section(QLatin1Char('@'), 0, 0));
    value.replace(QLatin1String("%EMAILDOMAIN%"), email.section(QLatin1Char('@'), 1));
    return value;
}

Security securityFromSocketType(const QString &socketType)
{
    if (socketType.compare(QLatin1String("SSL"), Qt::CaseInsensitive) == 0)
        return Security::Tls;
    if (socketType.compare(QLatin1String("STARTTLS"), Qt::CaseInsensitive) == 0)
        return Security::StartTls;
    return Security::None;
}

// Registrierbare Domain aus einem Hostnamen (vereinfachte Heuristik ohne Public-Suffix-Liste).
QString baseDomain(const QString &host)
{
    QStringList labels = host.toLower().split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() <= 2)
        return labels.join(QLatin1Char('.'));
    // z. B. mx.example.co.uk -> example.co.uk
    const QString secondLevel = labels.at(labels.size() - 2);
    const int keep = (secondLevel.size() <= 3 && labels.last().size() == 2) ? 3 : 2;
    return labels.mid(labels.size() - keep).join(QLatin1Char('.'));
}

} // namespace

AutoConfig::AutoConfig(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    qRegisterMetaType<DiscoveredSettings>();
}

void AutoConfig::discover(const QString &email)
{
    ++m_generation;
    m_email = email.trimmed();
    m_domain = m_email.section(QLatin1Char('@'), 1).toLower();
    m_mxDone = false;
    m_urls.clear();
    m_sources.clear();
    if (m_domain.isEmpty()) {
        Q_EMIT finished({});
        return;
    }

    const QString encoded = QString::fromUtf8(QUrl::toPercentEncoding(m_email));
    m_urls << QStringLiteral("https://autoconfig.%1/mail/config-v1.1.xml?emailaddress=%2").arg(m_domain, encoded)
           << QStringLiteral("https://%1/.well-known/autoconfig/mail/config-v1.1.xml?emailaddress=%2").arg(m_domain, encoded)
           << QStringLiteral("https://autoconfig.thunderbird.net/v1.1/%1").arg(m_domain);
    m_sources << i18n("Provider configuration") << i18n("Provider configuration") << i18n("Thunderbird provider database");
    tryNext();
}

void AutoConfig::abort()
{
    ++m_generation;
}

void AutoConfig::tryNext()
{
    if (m_urls.isEmpty()) {
        if (!m_mxDone) {
            lookupMx();
            return;
        }
        Q_EMIT finished(guess(m_email));
        return;
    }

    const QUrl url(m_urls.takeFirst());
    const QString source = m_sources.takeFirst();
    Q_EMIT progress(i18n("Looking up settings at %1…", url.host()));

    QNetworkRequest request(url);
    request.setTransferTimeout(requestTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, source, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        if (reply->error() == QNetworkReply::NoError) {
            DiscoveredSettings s = parse(reply->readAll(), m_email);
            if (s.isValid()) {
                s.source = source;
                qCInfo(lcAutoconfig) << "Einstellungen gefunden über" << reply->url().host();
                Q_EMIT finished(s);
                return;
            }
        }
        tryNext();
    });
}

void AutoConfig::lookupMx()
{
    m_mxDone = true;
    Q_EMIT progress(i18n("Checking mail server of %1…", m_domain));
    auto *dns = new QDnsLookup(QDnsLookup::MX, m_domain, this);
    const int generation = m_generation;
    connect(dns, &QDnsLookup::finished, this, [this, dns, generation] {
        dns->deleteLater();
        if (generation != m_generation)
            return;
        const auto records = dns->mailExchangeRecords();
        if (dns->error() == QDnsLookup::NoError && !records.isEmpty()) {
            const QString mxDomain = baseDomain(records.first().exchange());
            if (!mxDomain.isEmpty() && mxDomain != m_domain) {
                // z. B. eigene Domain bei Google Workspace (aspmx.l.google.com -> google.com)
                m_urls << QStringLiteral("https://autoconfig.thunderbird.net/v1.1/%1").arg(mxDomain);
                m_sources << i18n("Thunderbird provider database (via mail server)");
            }
        }
        tryNext();
    });
    dns->lookup();
}

DiscoveredSettings AutoConfig::parse(const QByteArray &xml, const QString &email)
{
    DiscoveredSettings result;
    QXmlStreamReader reader(xml);
    ServerSettings current;
    bool inIncoming = false;
    bool inOutgoing = false;

    while (!reader.atEnd()) {
        reader.readNext();
        const auto name = reader.name();
        if (reader.isStartElement()) {
            if (name == QLatin1String("incomingServer") || name == QLatin1String("outgoingServer")) {
                current = {};
                const auto type = reader.attributes().value(QLatin1String("type"));
                inIncoming = name == QLatin1String("incomingServer") && (type == QLatin1String("imap") || type == QLatin1String("pop3"));
                inOutgoing = name == QLatin1String("outgoingServer") && type == QLatin1String("smtp");
                current.type = type == QLatin1String("pop3") ? AccountType::Pop3 : AccountType::Imap;
            } else if (name == QLatin1String("displayName") && result.providerName.isEmpty() && !inIncoming && !inOutgoing) {
                result.providerName = reader.readElementText();
            } else if (inIncoming || inOutgoing) {
                if (name == QLatin1String("hostname"))
                    current.host = expandPlaceholders(reader.readElementText().trimmed(), email);
                else if (name == QLatin1String("port"))
                    current.port = reader.readElementText().toInt();
                else if (name == QLatin1String("socketType"))
                    current.security = securityFromSocketType(reader.readElementText().trimmed());
                else if (name == QLatin1String("username"))
                    current.username = expandPlaceholders(reader.readElementText().trimmed(), email);
                else if (name == QLatin1String("authentication") && reader.readElementText().trimmed() == QLatin1String("OAuth2"))
                    current.oauth = true;
            }
        } else if (reader.isEndElement()) {
            if (name == QLatin1String("incomingServer") && inIncoming) {
                if (!current.host.isEmpty() && current.port > 0)
                    result.incoming << current;
                inIncoming = false;
            } else if (name == QLatin1String("outgoingServer") && inOutgoing) {
                if (!current.host.isEmpty() && current.port > 0)
                    result.outgoing << current;
                inOutgoing = false;
            }
        }
    }
    if (reader.hasError())
        return {};

    // IMAP vor POP3, verschlüsselt vor unverschlüsselt, stabile Reihenfolge sonst.
    auto rank = [](const ServerSettings &s) {
        return (s.type == AccountType::Imap ? 0 : 10) + (s.security == Security::None ? 5 : 0);
    };
    std::stable_sort(result.incoming.begin(), result.incoming.end(), [&](const auto &a, const auto &b) {
        return rank(a) < rank(b);
    });
    std::stable_sort(result.outgoing.begin(), result.outgoing.end(), [&](const auto &a, const auto &b) {
        return rank(a) < rank(b);
    });
    return result;
}

DiscoveredSettings AutoConfig::guess(const QString &email)
{
    const QString domain = email.section(QLatin1Char('@'), 1).toLower();
    DiscoveredSettings s;
    s.guessed = true;
    s.source = i18n("Guessed from the email address");
    if (domain.isEmpty())
        return s;
    s.incoming << ServerSettings{AccountType::Imap, QStringLiteral("imap.") + domain, 993, Security::Tls, email, false}
               << ServerSettings{AccountType::Imap, QStringLiteral("mail.") + domain, 993, Security::Tls, email, false};
    s.outgoing << ServerSettings{AccountType::Imap, QStringLiteral("smtp.") + domain, 465, Security::Tls, email, false}
               << ServerSettings{AccountType::Imap, QStringLiteral("smtp.") + domain, 587, Security::StartTls, email, false}
               << ServerSettings{AccountType::Imap, QStringLiteral("mail.") + domain, 465, Security::Tls, email, false};
    return s;
}

} // namespace Fjordpost

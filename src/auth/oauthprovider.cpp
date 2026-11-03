#include "oauthprovider.h"

#include <QSettings>

namespace Fjordpost {

namespace {

OAuthProvider google()
{
    OAuthProvider p;
    p.id = QStringLiteral("google");
    p.name = QStringLiteral("Google");
    p.authorizationUrl = QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth"));
    p.tokenUrl = QUrl(QStringLiteral("https://oauth2.googleapis.com/token"));
    p.mailScopes = {QStringLiteral("https://mail.google.com/"), QStringLiteral("openid"), QStringLiteral("email")};
    p.davScopes = {QStringLiteral("https://www.googleapis.com/auth/calendar"), QStringLiteral("https://www.googleapis.com/auth/carddav")};
    // Ohne access_type=offline gibt es kein Refresh-Token, ohne prompt=consent nur beim allerersten Mal.
    p.extraParameters = {{QStringLiteral("access_type"), QStringLiteral("offline")}, {QStringLiteral("prompt"), QStringLiteral("consent")}};
    p.imapHosts = {QStringLiteral("imap.gmail.com"), QStringLiteral("imap.googlemail.com")};
    p.smtpHost = QStringLiteral("smtp.gmail.com");
    p.smtpPort = 465;
    p.caldavUrl = QUrl(QStringLiteral("https://apidata.googleusercontent.com/caldav/v2/"));
    p.carddavUrl = QUrl(QStringLiteral("https://www.googleapis.com/.well-known/carddav"));
    return p;
}

OAuthProvider microsoft()
{
    OAuthProvider p;
    p.id = QStringLiteral("microsoft");
    p.name = QStringLiteral("Microsoft");
    // "common" nimmt private Konten (Outlook.com) und Firmenkonten (Microsoft 365) an.
    p.authorizationUrl = QUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"));
    p.tokenUrl = QUrl(QStringLiteral("https://login.microsoftonline.com/common/oauth2/v2.0/token"));
    p.mailScopes = {QStringLiteral("https://outlook.office.com/IMAP.AccessAsUser.All"), QStringLiteral("https://outlook.office.com/SMTP.Send"),
                    QStringLiteral("offline_access"), QStringLiteral("openid"), QStringLiteral("email")};
    p.extraParameters = {{QStringLiteral("prompt"), QStringLiteral("select_account")}};
    p.imapHosts = {QStringLiteral("outlook.office365.com"), QStringLiteral("imap-mail.outlook.com")};
    p.smtpHost = QStringLiteral("smtp.office365.com");
    p.smtpPort = 587;
    p.smtpStartTls = true;
    return p;
}

} // namespace

OAuthProvider OAuthProvider::forId(const QString &id)
{
    OAuthProvider p;
    if (id == QLatin1String("google"))
        p = google();
    else if (id == QLatin1String("microsoft"))
        p = microsoft();
    else
        return {};

    QSettings settings;
    settings.beginGroup(QStringLiteral("OAuth"));
    settings.beginGroup(p.id);
    p.clientId = settings.value(QStringLiteral("clientId")).toString().trimmed();
    p.clientSecret = settings.value(QStringLiteral("clientSecret")).toString().trimmed();
    return p;
}

QString OAuthProvider::idForImapHost(const QString &host)
{
    const QString h = host.trimmed().toLower();
    for (const OAuthProvider &p : {google(), microsoft()}) {
        if (p.imapHosts.contains(h))
            return p.id;
    }
    return {};
}

} // namespace Fjordpost

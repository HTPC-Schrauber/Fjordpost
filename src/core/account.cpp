#include "account.h"

namespace MopMail {

QString accountTypeToString(AccountType t)
{
    return t == AccountType::Pop3 ? QStringLiteral("pop3") : QStringLiteral("imap");
}

QString securityToString(Security s)
{
    switch (s) {
    case Security::None:
        return QStringLiteral("none");
    case Security::StartTls:
        return QStringLiteral("starttls");
    case Security::Tls:
        break;
    }
    return QStringLiteral("tls");
}

QString authMethodToString(AuthMethod a)
{
    return a == AuthMethod::OAuth2 ? QStringLiteral("oauth2") : QStringLiteral("password");
}

AccountType accountTypeFromString(const QString &s)
{
    return s == QLatin1String("pop3") ? AccountType::Pop3 : AccountType::Imap;
}

Security securityFromString(const QString &s)
{
    if (s == QLatin1String("none"))
        return Security::None;
    if (s == QLatin1String("starttls"))
        return Security::StartTls;
    return Security::Tls;
}

AuthMethod authMethodFromString(const QString &s)
{
    return s == QLatin1String("oauth2") ? AuthMethod::OAuth2 : AuthMethod::Password;
}

} // namespace MopMail

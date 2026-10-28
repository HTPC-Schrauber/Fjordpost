#pragma once

#include <QColor>
#include <QMetaType>
#include <QString>

namespace MopMail {

enum class AccountType { Imap, Pop3 };
enum class Security { None, StartTls, Tls };
enum class AuthMethod { Password, OAuth2 };

// Ein Mailkonto mit Eingangs- und Ausgangsserver. Passwörter/Tokens liegen
// nicht hier, sondern im SecretStore (KWallet / Secret Service).
struct Account {
    qint64 id = 0;
    QString name;          // Anzeigename des Kontos, z. B. "Privat"
    QString displayName;   // Absendername
    QString email;
    QColor color;
    AccountType type = AccountType::Imap;

    QString incomingHost;
    int incomingPort = 993;
    Security incomingSecurity = Security::Tls;

    QString outgoingHost;
    int outgoingPort = 465;
    Security outgoingSecurity = Security::Tls;

    QString username;
    AuthMethod authMethod = AuthMethod::Password;
    bool enabled = true;
    int sortOrder = 0;

    bool isValid() const { return id > 0; }
    QString secretKey() const { return QStringLiteral("account-%1").arg(id); }
};

QString accountTypeToString(AccountType t);
QString securityToString(Security s);
QString authMethodToString(AuthMethod a);
AccountType accountTypeFromString(const QString &s);
Security securityFromString(const QString &s);
AuthMethod authMethodFromString(const QString &s);

} // namespace MopMail

Q_DECLARE_METATYPE(MopMail::Account)

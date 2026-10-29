#pragma once

#include "mail/autoconfig/autoconfig.h"

#include <QObject>

namespace Fjordpost {

class AccountManager;
class SecretStore;
class SmtpSender;

// Ablaufsteuerung für den Konto-Assistenten in QML:
// 1. discover(email) ermittelt Servereinstellungen,
// 2. der Nutzer prüft/ändert sie,
// 3. testAndSave(password) prüft die Anmeldung und legt das Konto an.
class AccountSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

    // Editierbare Einstellungen (Security: 0 = keine, 1 = STARTTLS, 2 = SSL/TLS)
    Q_PROPERTY(QString name MEMBER m_name NOTIFY settingsChanged)
    Q_PROPERTY(QString displayName MEMBER m_displayName NOTIFY settingsChanged)
    Q_PROPERTY(QString email MEMBER m_email NOTIFY settingsChanged)
    Q_PROPERTY(QString username MEMBER m_username NOTIFY settingsChanged)
    Q_PROPERTY(bool pop3 MEMBER m_pop3 NOTIFY settingsChanged)
    Q_PROPERTY(QString incomingHost MEMBER m_incomingHost NOTIFY settingsChanged)
    Q_PROPERTY(int incomingPort MEMBER m_incomingPort NOTIFY settingsChanged)
    Q_PROPERTY(int incomingSecurity MEMBER m_incomingSecurity NOTIFY settingsChanged)
    Q_PROPERTY(QString outgoingHost MEMBER m_outgoingHost NOTIFY settingsChanged)
    Q_PROPERTY(int outgoingPort MEMBER m_outgoingPort NOTIFY settingsChanged)
    Q_PROPERTY(int outgoingSecurity MEMBER m_outgoingSecurity NOTIFY settingsChanged)
    Q_PROPERTY(QString source READ source NOTIFY settingsChanged)
    Q_PROPERTY(bool needsOAuth READ needsOAuth NOTIFY settingsChanged)

public:
    AccountSetup(AccountManager *accounts, SecretStore *secrets, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString errorText() const { return m_error; }
    QString source() const { return m_settings.source; }
    bool needsOAuth() const { return m_needsOAuth; }

    Q_INVOKABLE void reset();
    Q_INVOKABLE void discover(const QString &email);
    Q_INVOKABLE void testAndSave(const QString &password);
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void busyChanged();
    void statusChanged();
    void errorTextChanged();
    void settingsChanged();
    void discovered();
    void saved(qint64 accountId);

private:
    void setBusy(bool busy);
    void setStatus(const QString &status);
    void setError(const QString &error);
    void applyCandidate(int incomingIndex, int outgoingIndex);
    Account currentAccount() const;
    void testIncoming(const QString &password);
    void testOutgoing(const QString &password);
    void save(const QString &password);

    AccountManager *m_accounts;
    SecretStore *m_secrets;
    AutoConfig *m_autoconfig;
    SmtpSender *m_smtp;
    DiscoveredSettings m_settings;
    int m_incomingCandidate = 0;
    int m_outgoingCandidate = 0;
    int m_generation = 0;

    bool m_busy = false;
    QString m_status;
    QString m_error;
    bool m_needsOAuth = false;

    QString m_name;
    QString m_displayName;
    QString m_email;
    QString m_username;
    bool m_pop3 = false;
    QString m_incomingHost;
    int m_incomingPort = 993;
    int m_incomingSecurity = 2;
    QString m_outgoingHost;
    int m_outgoingPort = 465;
    int m_outgoingSecurity = 2;
};

} // namespace Fjordpost

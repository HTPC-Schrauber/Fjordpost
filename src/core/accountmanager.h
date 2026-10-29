#pragma once

#include "account.h"

#include <QList>
#include <QObject>

namespace Fjordpost {

// Verwaltet die Konten (Persistenz in der Datenbank) und benachrichtigt
// Modelle und Sync-Dienste über Änderungen.
class AccountManager : public QObject
{
    Q_OBJECT
public:
    explicit AccountManager(QObject *parent = nullptr);

    void load();
    QList<Account> accounts() const { return m_accounts; }
    Account account(qint64 id) const;

    // Legt ein neues Konto an (id wird vergeben) bzw. aktualisiert ein bestehendes.
    Account save(Account account);
    bool remove(qint64 id);

Q_SIGNALS:
    void accountAdded(const Fjordpost::Account &account);
    void accountChanged(const Fjordpost::Account &account);
    void accountRemoved(qint64 id);

private:
    QList<Account> m_accounts;
};

} // namespace Fjordpost

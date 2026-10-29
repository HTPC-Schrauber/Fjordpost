#include "accountmanager.h"

#include "storage/database.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcAccounts, "fjordpost.accounts")

namespace Fjordpost {

namespace {

Account accountFromQuery(const QSqlQuery &q)
{
    Account a;
    a.id = q.value(QStringLiteral("id")).toLongLong();
    a.name = q.value(QStringLiteral("name")).toString();
    a.displayName = q.value(QStringLiteral("display_name")).toString();
    a.email = q.value(QStringLiteral("email")).toString();
    a.color = QColor(q.value(QStringLiteral("color")).toString());
    a.type = accountTypeFromString(q.value(QStringLiteral("type")).toString());
    a.incomingHost = q.value(QStringLiteral("incoming_host")).toString();
    a.incomingPort = q.value(QStringLiteral("incoming_port")).toInt();
    a.incomingSecurity = securityFromString(q.value(QStringLiteral("incoming_security")).toString());
    a.outgoingHost = q.value(QStringLiteral("outgoing_host")).toString();
    a.outgoingPort = q.value(QStringLiteral("outgoing_port")).toInt();
    a.outgoingSecurity = securityFromString(q.value(QStringLiteral("outgoing_security")).toString());
    a.username = q.value(QStringLiteral("username")).toString();
    a.authMethod = authMethodFromString(q.value(QStringLiteral("auth_method")).toString());
    a.enabled = q.value(QStringLiteral("enabled")).toBool();
    a.sortOrder = q.value(QStringLiteral("sort_order")).toInt();
    a.signature = q.value(QStringLiteral("signature")).toString();
    return a;
}

// Ein nicht gesetzter QString würde als SQL-NULL gebunden und die
// NOT-NULL-Spalten verletzen; daher immer einen leeren String binden.
QString nonNull(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

void bindAccount(QSqlQuery &q, const Account &a)
{
    q.bindValue(QStringLiteral(":name"), nonNull(a.name));
    q.bindValue(QStringLiteral(":display_name"), nonNull(a.displayName));
    q.bindValue(QStringLiteral(":email"), nonNull(a.email));
    q.bindValue(QStringLiteral(":color"), a.color.isValid() ? a.color.name() : QStringLiteral(""));
    q.bindValue(QStringLiteral(":type"), accountTypeToString(a.type));
    q.bindValue(QStringLiteral(":incoming_host"), nonNull(a.incomingHost));
    q.bindValue(QStringLiteral(":incoming_port"), a.incomingPort);
    q.bindValue(QStringLiteral(":incoming_security"), securityToString(a.incomingSecurity));
    q.bindValue(QStringLiteral(":outgoing_host"), nonNull(a.outgoingHost));
    q.bindValue(QStringLiteral(":outgoing_port"), a.outgoingPort);
    q.bindValue(QStringLiteral(":outgoing_security"), securityToString(a.outgoingSecurity));
    q.bindValue(QStringLiteral(":username"), nonNull(a.username));
    q.bindValue(QStringLiteral(":auth_method"), authMethodToString(a.authMethod));
    q.bindValue(QStringLiteral(":enabled"), a.enabled);
    q.bindValue(QStringLiteral(":sort_order"), a.sortOrder);
    q.bindValue(QStringLiteral(":signature"), nonNull(a.signature));
}

} // namespace

AccountManager::AccountManager(QObject *parent)
    : QObject(parent)
{
}

void AccountManager::load()
{
    m_accounts.clear();
    QSqlQuery q(Database::connection());
    if (!q.exec(QStringLiteral("SELECT * FROM accounts ORDER BY sort_order, id"))) {
        qCWarning(lcAccounts) << q.lastError().text();
        return;
    }
    while (q.next())
        m_accounts << accountFromQuery(q);
}

Account AccountManager::account(qint64 id) const
{
    for (const Account &a : m_accounts) {
        if (a.id == id)
            return a;
    }
    return {};
}

Account AccountManager::save(Account account)
{
    QSqlQuery q(Database::connection());
    const bool isNew = account.id <= 0;
    if (isNew) {
        q.prepare(QStringLiteral(
            "INSERT INTO accounts (name, display_name, email, color, type, incoming_host, incoming_port, "
            "incoming_security, outgoing_host, outgoing_port, outgoing_security, username, auth_method, enabled, sort_order, signature) "
            "VALUES (:name, :display_name, :email, :color, :type, :incoming_host, :incoming_port, "
            ":incoming_security, :outgoing_host, :outgoing_port, :outgoing_security, :username, :auth_method, :enabled, :sort_order, :signature)"));
    } else {
        q.prepare(QStringLiteral(
            "UPDATE accounts SET name=:name, display_name=:display_name, email=:email, color=:color, type=:type, "
            "incoming_host=:incoming_host, incoming_port=:incoming_port, incoming_security=:incoming_security, "
            "outgoing_host=:outgoing_host, outgoing_port=:outgoing_port, outgoing_security=:outgoing_security, "
            "username=:username, auth_method=:auth_method, enabled=:enabled, sort_order=:sort_order, signature=:signature WHERE id=:id"));
        q.bindValue(QStringLiteral(":id"), account.id);
    }
    bindAccount(q, account);
    if (!q.exec()) {
        qCWarning(lcAccounts) << "Konto konnte nicht gespeichert werden:" << q.lastError().text();
        return {};
    }

    if (isNew) {
        account.id = q.lastInsertId().toLongLong();
        m_accounts << account;
        Q_EMIT accountAdded(account);
    } else {
        for (Account &a : m_accounts) {
            if (a.id == account.id)
                a = account;
        }
        Q_EMIT accountChanged(account);
    }
    return account;
}

bool AccountManager::setSignature(qint64 id, const QString &signature)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE accounts SET signature = :signature WHERE id = :id"));
    q.bindValue(QStringLiteral(":signature"), nonNull(signature));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    for (Account &a : m_accounts) {
        if (a.id == id)
            a.signature = signature;
    }
    return true;
}

bool AccountManager::remove(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE id = :id"));
    q.bindValue(QStringLiteral(":id"), id);
    if (!q.exec() || q.numRowsAffected() == 0)
        return false;
    m_accounts.removeIf([id](const Account &a) {
        return a.id == id;
    });
    Q_EMIT accountRemoved(id);
    return true;
}

} // namespace Fjordpost

#include "appcontroller.h"

#include "core/accountmanager.h"
#include "models/accountmodel.h"
#include "models/foldermodel.h"
#include "models/messagelistmodel.h"
#include "storage/database.h"
#include "storage/messageflags.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QLocale>
#include <QSqlQuery>

namespace MopMail {

AppController::AppController(AccountManager *accounts, QObject *parent)
    : QObject(parent)
    , m_accountManager(accounts)
    , m_accountModel(new AccountModel(accounts, this))
    , m_folderModel(new FolderModel(this))
    , m_messageModel(new MessageListModel(this))
{
    m_role = QString::fromLatin1(FolderRole::Inbox);
    m_title = i18n("All Inboxes");
    applyFilter();
    refreshCounts();
}

QObject *AppController::accountsObject() const
{
    return m_accountModel;
}

QObject *AppController::foldersObject() const
{
    return m_folderModel;
}

QObject *AppController::messagesObject() const
{
    return m_messageModel;
}

void AppController::setSearchText(const QString &text)
{
    if (m_search == text)
        return;
    m_search = text;
    Q_EMIT searchTextChanged();
    applyFilter();
}

void AppController::selectAccount(qint64 accountId)
{
    m_accountId = accountId;
    m_folderModel->setAccountId(accountId);

    // Beim Kontowechsel immer im Posteingang landen
    m_role = QString::fromLatin1(FolderRole::Inbox);
    m_folderId = 0;
    if (accountId > 0) {
        QSqlQuery q(Database::connection());
        q.prepare(QStringLiteral("SELECT id, name FROM folders WHERE account_id = ? AND role = 'inbox'"));
        q.addBindValue(accountId);
        q.exec();
        if (q.next()) {
            m_folderId = q.value(0).toLongLong();
            m_title = q.value(1).toString();
        }
    } else {
        m_title = i18n("All Inboxes");
    }
    Q_EMIT selectionChanged();
    applyFilter();
}

void AppController::selectFolder(qint64 folderId, const QString &role, const QString &name)
{
    m_folderId = folderId;
    m_role = role;
    m_title = name;
    if (!m_search.isEmpty()) {
        m_search.clear();
        Q_EMIT searchTextChanged();
    }
    Q_EMIT selectionChanged();
    applyFilter();
}

void AppController::applyFilter()
{
    MessageListModel::Filter f;
    f.accountId = m_accountId;
    f.folderId = m_folderId;
    f.role = m_role;
    f.search = m_search;
    m_messageModel->setFilter(f);

    if (!m_current.isEmpty()) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
    }
}

int AppController::flagsOf(qint64 messageId) const
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT flags FROM messages WHERE id = ?"));
    q.addBindValue(messageId);
    q.exec();
    return q.next() ? q.value(0).toInt() : 0;
}

void AppController::setFlags(qint64 messageId, int flags)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE messages SET flags = ? WHERE id = ?"));
    q.addBindValue(flags);
    q.addBindValue(messageId);
    q.exec();
    // TODO(M1): Änderung in die Sync-Queue legen, damit sie zum Server gelangt.
    m_messageModel->updateFlags(messageId, flags);
    if (m_current.value(QStringLiteral("id")).toLongLong() == messageId) {
        m_current[QStringLiteral("flagged")] = (flags & MessageFlag::Flagged) != 0;
        m_current[QStringLiteral("unread")] = (flags & MessageFlag::Seen) == 0;
        Q_EMIT currentMessageChanged();
    }
    refreshCounts();
}

void AppController::openMessage(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "SELECT m.id, m.subject, m.from_name, m.from_email, m.to_list, m.cc_list, m.date, m.flags, m.body_text, "
        "a.name, a.color FROM messages m JOIN accounts a ON a.id = m.account_id WHERE m.id = ?"));
    q.addBindValue(messageId);
    q.exec();
    if (!q.next()) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
        return;
    }

    const int flags = q.value(7).toInt();
    const QDateTime date = QDateTime::fromSecsSinceEpoch(q.value(6).toLongLong());
    m_current = {
        {QStringLiteral("id"), q.value(0)},
        {QStringLiteral("subject"), q.value(1).toString().isEmpty() ? i18n("(No subject)") : q.value(1).toString()},
        {QStringLiteral("fromName"), q.value(2).toString().isEmpty() ? q.value(3) : q.value(2)},
        {QStringLiteral("fromEmail"), q.value(3)},
        {QStringLiteral("to"), q.value(4)},
        {QStringLiteral("cc"), q.value(5)},
        {QStringLiteral("dateText"), QLocale().toString(date, QLocale::LongFormat)},
        {QStringLiteral("body"), q.value(8)},
        {QStringLiteral("accountName"), q.value(9)},
        {QStringLiteral("accountColor"), q.value(10)},
        {QStringLiteral("flagged"), (flags & MessageFlag::Flagged) != 0},
        {QStringLiteral("unread"), false},
    };
    Q_EMIT currentMessageChanged();

    if ((flags & MessageFlag::Seen) == 0)
        setFlags(messageId, flags | MessageFlag::Seen);
}

void AppController::setRead(qint64 messageId, bool read)
{
    const int flags = flagsOf(messageId);
    setFlags(messageId, read ? (flags | MessageFlag::Seen) : (flags & ~MessageFlag::Seen));
}

void AppController::toggleFlagged(qint64 messageId)
{
    setFlags(messageId, flagsOf(messageId) ^ MessageFlag::Flagged);
}

void AppController::moveToRole(qint64 messageId, const QString &role)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral(
        "UPDATE messages SET folder_id = (SELECT f.id FROM folders f WHERE f.account_id = messages.account_id AND f.role = ?) "
        "WHERE id = ? AND EXISTS (SELECT 1 FROM folders f WHERE f.account_id = messages.account_id AND f.role = ?)"));
    q.addBindValue(role);
    q.addBindValue(messageId);
    q.addBindValue(role);
    q.exec();
    // TODO(M1): Verschieben auf dem Server (IMAP MOVE) einreihen.
    m_messageModel->removeMessage(messageId);
    if (m_current.value(QStringLiteral("id")).toLongLong() == messageId) {
        m_current.clear();
        Q_EMIT currentMessageChanged();
    }
    refreshCounts();
}

void AppController::deleteMessage(qint64 messageId)
{
    if (m_role == QLatin1String(FolderRole::Trash)) {
        // Endgültig löschen ist erst mit Server-Sync sinnvoll (M1); bis dahin nur markieren.
        setFlags(messageId, flagsOf(messageId) | MessageFlag::Deleted);
        m_messageModel->removeMessage(messageId);
        return;
    }
    moveToRole(messageId, QString::fromLatin1(FolderRole::Trash));
}

void AppController::archiveMessage(qint64 messageId)
{
    moveToRole(messageId, QString::fromLatin1(FolderRole::Archive));
}

void AppController::refreshCounts()
{
    m_accountModel->reload();
    m_folderModel->reload();

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM messages m JOIN folders f ON f.id = m.folder_id "
                             "WHERE f.role = 'inbox' AND (m.flags & ?) = 0"));
    q.addBindValue(int(MessageFlag::Seen));
    q.exec();
    const int unread = q.next() ? q.value(0).toInt() : 0;
    if (unread != m_totalUnread) {
        m_totalUnread = unread;
        Q_EMIT totalUnreadChanged();
    }
}

} // namespace MopMail

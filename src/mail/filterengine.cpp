#include "filterengine.h"

#include "storage/database.h"
#include "storage/labelstore.h"
#include "storage/mailstore.h"
#include "storage/messageflags.h"

#include <QLoggingCategory>
#include <QSqlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcFilter, "fjordpost.filter")

namespace Fjordpost::FilterEngine {

namespace {

bool matchesValue(const QString &text, const FilterCondition &c)
{
    if (c.op == QLatin1String("notContains"))
        return !text.contains(c.value, Qt::CaseInsensitive);
    if (c.op == QLatin1String("is"))
        return text.compare(c.value, Qt::CaseInsensitive) == 0;
    if (c.op == QLatin1String("startsWith"))
        return text.startsWith(c.value, Qt::CaseInsensitive);
    if (c.op == QLatin1String("endsWith"))
        return text.endsWith(c.value, Qt::CaseInsensitive);
    return text.contains(c.value, Qt::CaseInsensitive);
}

bool matchesCondition(const FilterCondition &c, const FilterFields &f)
{
    // Eine leere Bedingung darf nicht auf alles zutreffen.
    if (c.value.isEmpty())
        return false;
    QStringList texts;
    if (c.field == QLatin1String("from"))
        texts = {f.fromName, f.fromEmail};
    else if (c.field == QLatin1String("to"))
        texts = {f.to, f.cc};
    else if (c.field == QLatin1String("subject"))
        texts = {f.subject};
    else
        texts = {f.fromName, f.fromEmail, f.to, f.cc, f.subject};

    // Bei Verneinung darf der Text in keinem der Felder stehen.
    const auto hit = [&c](const QString &text) {
        return matchesValue(text, c);
    };
    return c.op == QLatin1String("notContains") ? std::all_of(texts.cbegin(), texts.cend(), hit) : std::any_of(texts.cbegin(), texts.cend(), hit);
}

FilterFields loadFields(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT from_name, from_email, to_list, cc_list, subject FROM messages WHERE id = ?"));
    q.addBindValue(messageId);
    if (!q.exec() || !q.next())
        return {};
    return {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(), q.value(4).toString()};
}

// Ziel eines Verschiebens für dieses Konto; 0, wenn es keins gibt.
qint64 targetFolder(const FilterAction &action, qint64 accountId)
{
    if (action.type == QLatin1String("move"))
        return MailStore::folderIdForPath(accountId, action.argument);
    return MailStore::ensureFolderForRole(accountId, action.type == QLatin1String("archive") ? QString::fromLatin1(FolderRole::Archive)
                                                  : action.type == QLatin1String("junk")      ? QString::fromLatin1(FolderRole::Junk)
                                                                                              : QString::fromLatin1(FolderRole::Trash));
}

} // namespace

bool matches(const FilterRule &rule, const FilterFields &fields)
{
    if (rule.conditions.isEmpty())
        return false;
    for (const FilterCondition &c : rule.conditions) {
        const bool hit = matchesCondition(c, fields);
        if (hit && !rule.matchAll)
            return true;
        if (!hit && rule.matchAll)
            return false;
    }
    return rule.matchAll;
}

FilterResult apply(const QList<qint64> &messageIds, const QList<FilterRule> &rules)
{
    FilterResult result;
    for (qint64 id : messageIds) {
        const MessageLocation loc = MailStore::location(id);
        if (loc.id == 0)
            continue;
        const FilterFields fields = loadFields(id);

        int flags = loc.flags;
        qint64 target = 0;
        bool hit = false;

        auto queueFlag = [&](const QString &change) {
            if (loc.uid > 0) {
                MailStore::enqueueOp(loc.accountId, QStringLiteral("flags"), loc.folderPath, loc.uid, change);
                result.accounts.insert(loc.accountId);
            }
        };

        for (const FilterRule &rule : rules) {
            if (!rule.enabled || (rule.accountId > 0 && rule.accountId != loc.accountId) || !matches(rule, fields))
                continue;
            hit = true;
            for (const FilterAction &action : rule.actions) {
                if (action.type == QLatin1String("read")) {
                    if (!(flags & MessageFlag::Seen))
                        queueFlag(QStringLiteral("+\\Seen"));
                    flags |= MessageFlag::Seen;
                } else if (action.type == QLatin1String("flag")) {
                    if (!(flags & MessageFlag::Flagged))
                        queueFlag(QStringLiteral("+\\Flagged"));
                    flags |= MessageFlag::Flagged;
                } else if (action.type == QLatin1String("label")) {
                    const Label label = LabelStore::label(action.argument.toLongLong());
                    if (label.id > 0 && LabelStore::setLabel(id, label.id, true) && MailStore::folder(loc.folderId).keywords)
                        queueFlag(QLatin1Char('+') + label.keyword);
                } else if (target == 0) {
                    // Die erste Verschiebung gewinnt; sie läuft unten nach allen Flag-Änderungen,
                    // denn die brauchen noch die UID im Ursprungsordner.
                    target = targetFolder(action, loc.accountId);
                    if (target == 0)
                        qCWarning(lcFilter) << "Regel" << rule.name << ": kein Zielordner für" << action.type;
                }
            }
            if (rule.stop)
                break;
        }

        if (!hit) {
            if (!(flags & MessageFlag::Seen))
                result.untouched << id;
            continue;
        }

        ++result.matched;
        if (flags != loc.flags)
            MailStore::updateFlags(id, flags);
        result.folders.insert(loc.folderId);
        if (target > 0 && target != loc.folderId) {
            MailStore::moveLocally(id, target);
            result.folders.insert(target);
            if (loc.uid > 0) {
                MailStore::enqueueOp(loc.accountId, QStringLiteral("move"), loc.folderPath, loc.uid, MailStore::folder(target).path);
                result.accounts.insert(loc.accountId);
            }
        } else if (!(flags & MessageFlag::Seen)) {
            result.untouched << id;
        }
    }
    return result;
}

} // namespace Fjordpost::FilterEngine

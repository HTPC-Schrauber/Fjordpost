#include "filterstore.h"

#include "database.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>

Q_DECLARE_LOGGING_CATEGORY(lcStore)

namespace Fjordpost::FilterStore {

namespace {

constexpr auto columns = "id, name, enabled, account_id, match_all, conditions, actions, stop, sort_order";

QString conditionsToJson(const QList<FilterCondition> &conditions)
{
    QJsonArray array;
    for (const FilterCondition &c : conditions)
        array << QJsonObject{{QStringLiteral("field"), c.field}, {QStringLiteral("op"), c.op}, {QStringLiteral("value"), c.value}};
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString actionsToJson(const QList<FilterAction> &actions)
{
    QJsonArray array;
    for (const FilterAction &a : actions)
        array << QJsonObject{{QStringLiteral("type"), a.type}, {QStringLiteral("argument"), a.argument}};
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

FilterRule ruleFromQuery(const QSqlQuery &q)
{
    FilterRule r;
    r.id = q.value(0).toLongLong();
    r.name = q.value(1).toString();
    r.enabled = q.value(2).toBool();
    r.accountId = q.value(3).toLongLong();
    r.matchAll = q.value(4).toBool();
    const auto conditions = QJsonDocument::fromJson(q.value(5).toString().toUtf8()).array();
    for (const QJsonValue &v : conditions) {
        const QJsonObject o = v.toObject();
        r.conditions << FilterCondition{o.value(QStringLiteral("field")).toString(), o.value(QStringLiteral("op")).toString(), o.value(QStringLiteral("value")).toString()};
    }
    const auto actions = QJsonDocument::fromJson(q.value(6).toString().toUtf8()).array();
    for (const QJsonValue &v : actions) {
        const QJsonObject o = v.toObject();
        r.actions << FilterAction{o.value(QStringLiteral("type")).toString(), o.value(QStringLiteral("argument")).toString()};
    }
    r.stop = q.value(7).toBool();
    r.sortOrder = q.value(8).toInt();
    return r;
}

} // namespace

QList<FilterRule> rules()
{
    QSqlQuery q(Database::connection());
    q.exec(QStringLiteral("SELECT %1 FROM filter_rules ORDER BY sort_order, id").arg(QLatin1String(columns)));
    QList<FilterRule> result;
    while (q.next())
        result << ruleFromQuery(q);
    return result;
}

FilterRule rule(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT %1 FROM filter_rules WHERE id = ?").arg(QLatin1String(columns)));
    q.addBindValue(id);
    return q.exec() && q.next() ? ruleFromQuery(q) : FilterRule{};
}

qint64 save(FilterRule rule)
{
    QSqlQuery q(Database::connection());
    if (rule.id > 0) {
        q.prepare(QStringLiteral("UPDATE filter_rules SET name = ?, enabled = ?, account_id = ?, match_all = ?, conditions = ?, actions = ?, stop = ? WHERE id = ?"));
    } else {
        // Neue Regeln laufen zuletzt.
        QSqlQuery next(Database::connection());
        next.exec(QStringLiteral("SELECT COALESCE(MAX(sort_order), 0) + 1 FROM filter_rules"));
        rule.sortOrder = next.next() ? next.value(0).toInt() : 1;
        q.prepare(QStringLiteral("INSERT INTO filter_rules (name, enabled, account_id, match_all, conditions, actions, stop, sort_order) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    }
    q.addBindValue(rule.name.isNull() ? QStringLiteral("") : rule.name);
    q.addBindValue(rule.enabled);
    q.addBindValue(rule.accountId);
    q.addBindValue(rule.matchAll);
    q.addBindValue(conditionsToJson(rule.conditions));
    q.addBindValue(actionsToJson(rule.actions));
    q.addBindValue(rule.stop);
    q.addBindValue(rule.id > 0 ? rule.id : qint64(rule.sortOrder));
    if (!q.exec()) {
        qCWarning(lcStore) << q.lastError().text();
        return 0;
    }
    return rule.id > 0 ? rule.id : q.lastInsertId().toLongLong();
}

bool remove(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM filter_rules WHERE id = ?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

void move(qint64 id, int delta)
{
    QList<FilterRule> all = rules();
    const auto it = std::find_if(all.begin(), all.end(), [id](const FilterRule &r) {
        return r.id == id;
    });
    if (it == all.end())
        return;
    const int from = int(it - all.begin());
    const int to = from + delta;
    if (to < 0 || to >= all.size())
        return;
    all.swapItemsAt(from, to);

    // Durchnummerieren: gleiche sort_order-Werte ließen sich nicht tauschen.
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE filter_rules SET sort_order = ? WHERE id = ?"));
    for (int i = 0; i < all.size(); ++i) {
        q.addBindValue(i + 1);
        q.addBindValue(all.at(i).id);
        q.exec();
    }
}

void removeForAccount(qint64 accountId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM filter_rules WHERE account_id = ?"));
    q.addBindValue(accountId);
    q.exec();
}

} // namespace Fjordpost::FilterStore

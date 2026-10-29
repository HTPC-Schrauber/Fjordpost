#include "labelstore.h"

#include "database.h"

#include <QLoggingCategory>
#include <QSqlError>
#include <QSqlQuery>

Q_LOGGING_CATEGORY(lcLabels, "fjordpost.labels")

namespace Fjordpost::LabelStore {

namespace {

bool exec(QSqlQuery &q)
{
    if (!q.exec()) {
        qCWarning(lcLabels) << q.lastError().text() << q.lastQuery();
        return false;
    }
    return true;
}

QString nonNull(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

Label labelFromQuery(const QSqlQuery &q)
{
    return {q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(), q.value(4).toInt()};
}

bool keywordTaken(const QString &keyword)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT 1 FROM labels WHERE keyword = ?"));
    q.addBindValue(keyword);
    return exec(q) && q.next();
}

// Vergebener Name ist ein Bedienfehler, kein Datenbankfehler: vorher prüfen
bool nameTaken(const QString &name, qint64 exceptId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT 1 FROM labels WHERE name = ? COLLATE NOCASE AND id != ?"));
    q.addBindValue(name);
    q.addBindValue(exceptId);
    return exec(q) && q.next();
}

} // namespace

QString keywordFor(const QString &name)
{
    // Zeichen, die in einem IMAP-Atom nicht vorkommen dürfen, durch "_" ersetzen;
    // Umlaute über die Zerlegung (ä → a) bzw. ß → ss.
    const QString decomposed = QString(name).replace(QChar(0x00DF), QStringLiteral("ss")).normalized(QString::NormalizationForm_KD);
    QString out;
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        const char16_t u = c.unicode();
        const bool safe = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'-' || u == u'_' || u == u'.'
            || u == u'+';
        if (safe)
            out += c.toLower();
        else if (!out.endsWith(QLatin1Char('_')))
            out += QLatin1Char('_');
    }
    while (out.startsWith(QLatin1Char('_')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('_')))
        out.chop(1);
    return out.isEmpty() ? QStringLiteral("label") : out;
}

QList<Label> labels()
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, name, color, keyword, sort_order FROM labels ORDER BY sort_order, name COLLATE NOCASE"));
    QList<Label> result;
    if (exec(q)) {
        while (q.next())
            result << labelFromQuery(q);
    }
    return result;
}

Label label(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT id, name, color, keyword, sort_order FROM labels WHERE id = ?"));
    q.addBindValue(id);
    return exec(q) && q.next() ? labelFromQuery(q) : Label{};
}

qint64 create(const QString &name, const QString &color)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || nameTaken(trimmed, 0))
        return 0;
    const QString base = keywordFor(trimmed);
    QString keyword = base;
    for (int n = 2; keywordTaken(keyword); ++n)
        keyword = base + QLatin1Char('_') + QString::number(n);

    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("INSERT INTO labels (name, color, keyword, sort_order) VALUES (?, ?, ?, (SELECT COALESCE(MAX(sort_order), 0) + 1 FROM labels))"));
    q.addBindValue(trimmed);
    q.addBindValue(nonNull(color));
    q.addBindValue(keyword);
    return exec(q) ? q.lastInsertId().toLongLong() : 0;
}

bool update(qint64 id, const QString &name, const QString &color)
{
    // Das Schlüsselwort bleibt, sonst gingen die Zuordnungen auf dem Server verloren.
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || nameTaken(trimmed, id))
        return false;
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("UPDATE labels SET name = ?, color = ? WHERE id = ?"));
    q.addBindValue(trimmed);
    q.addBindValue(nonNull(color));
    q.addBindValue(id);
    return exec(q) && q.numRowsAffected() > 0;
}

bool remove(qint64 id)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("DELETE FROM labels WHERE id = ?"));
    q.addBindValue(id);
    return exec(q) && q.numRowsAffected() > 0;
}

QHash<qint64, int> counts()
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT ml.label_id, COUNT(*) FROM message_labels ml JOIN messages m ON m.id = ml.message_id "
                             "JOIN folders f ON f.id = m.folder_id WHERE f.role NOT IN ('trash', 'junk') GROUP BY ml.label_id"));
    QHash<qint64, int> result;
    if (exec(q)) {
        while (q.next())
            result.insert(q.value(0).toLongLong(), q.value(1).toInt());
    }
    return result;
}

QList<qint64> labelsOf(qint64 messageId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT ml.label_id FROM message_labels ml JOIN labels l ON l.id = ml.label_id WHERE ml.message_id = ? "
                             "ORDER BY l.sort_order, l.name COLLATE NOCASE"));
    q.addBindValue(messageId);
    QList<qint64> result;
    if (exec(q)) {
        while (q.next())
            result << q.value(0).toLongLong();
    }
    return result;
}

bool setLabel(qint64 messageId, qint64 labelId, bool on)
{
    QSqlQuery q(Database::connection());
    q.prepare(on ? QStringLiteral("INSERT OR IGNORE INTO message_labels (message_id, label_id) VALUES (?, ?)")
                 : QStringLiteral("DELETE FROM message_labels WHERE message_id = ? AND label_id = ?"));
    q.addBindValue(messageId);
    q.addBindValue(labelId);
    return exec(q) && q.numRowsAffected() > 0;
}

QList<qint64> messagesWith(qint64 labelId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT message_id FROM message_labels WHERE label_id = ?"));
    q.addBindValue(labelId);
    QList<qint64> result;
    if (exec(q)) {
        while (q.next())
            result << q.value(0).toLongLong();
    }
    return result;
}

QHash<qint64, QSet<QString>> keywordsInFolder(qint64 folderId)
{
    QSqlQuery q(Database::connection());
    q.prepare(QStringLiteral("SELECT m.id, l.keyword FROM messages m JOIN message_labels ml ON ml.message_id = m.id "
                             "JOIN labels l ON l.id = ml.label_id WHERE m.folder_id = ?"));
    q.addBindValue(folderId);
    QHash<qint64, QSet<QString>> result;
    if (exec(q)) {
        while (q.next())
            result[q.value(0).toLongLong()].insert(q.value(1).toString());
    }
    return result;
}

void applyKeywords(qint64 messageId, const QSet<QString> &keywords)
{
    QHash<QString, qint64> known;
    const auto all = labels();
    for (const Label &l : all)
        known.insert(l.keyword, l.id);

    QSet<qint64> wanted;
    for (const QString &k : keywords) {
        if (const qint64 id = known.value(k))
            wanted.insert(id);
    }
    const QList<qint64> current = labelsOf(messageId);
    for (qint64 id : current) {
        if (!wanted.remove(id))
            setLabel(messageId, id, false);
    }
    for (qint64 id : std::as_const(wanted))
        setLabel(messageId, id, true);
}

QSet<QString> keywordsFromFlags(const QList<QByteArray> &flags)
{
    QSet<QString> result;
    for (const QByteArray &f : flags) {
        // Systemflags (\Seen …) und Schlüsselwörter mit "$" (Junk-Erkennung,
        // $Forwarded, $MDNSent) sind keine Labels.
        if (f.isEmpty() || f.startsWith('\\') || f.startsWith('$'))
            continue;
        const QString keyword = QString::fromUtf8(f).toLower();
        if (keyword != QLatin1String("junk") && keyword != QLatin1String("nonjunk"))
            result.insert(keyword);
    }
    return result;
}

} // namespace Fjordpost::LabelStore

#include "mbox.h"

#include "storage/messageflags.h"

#include <KMime/Util>

#include <QIODevice>
#include <QLocale>
#include <QRegularExpression>

namespace Fjordpost::Mbox {

namespace {

// "From addr Mon Jan  1 12:00:00 2024". Alte mbox-Varianten maskieren "From " im Text
// nicht immer; nur eine vollständige Trennzeile beginnt daher eine neue Nachricht.
bool isSeparator(const QByteArray &line)
{
    if (!line.startsWith("From "))
        return false;
    static const QRegularExpression pattern(QStringLiteral("^From \\S+\\s+[A-Z][a-z]{2} [A-Z][a-z]{2}\\s+\\d{1,2} \\d{2}:\\d{2}(:\\d{2})? "));
    return pattern.match(QString::fromLatin1(line)).hasMatch();
}

// ">From " und ">>From " (mboxrd)
bool isQuotedFrom(const QByteArray &line)
{
    qsizetype i = 0;
    while (i < line.size() && line.at(i) == '>')
        ++i;
    return i > 0 && line.mid(i).startsWith("From ");
}

bool headerValue(const QByteArray &head, const QByteArray &name, QByteArray *value)
{
    const auto lines = head.split('\n');
    for (const QByteArray &line : lines) {
        if (line.size() > name.size() && line.at(name.size()) == ':' && qstrnicmp(line.constData(), name.constData(), name.size()) == 0) {
            *value = line.mid(name.size() + 1).trimmed();
            return true;
        }
    }
    return false;
}

} // namespace

QByteArray entry(const QByteArray &raw, const QString &sender, const QDateTime &date, int flags)
{
    const QByteArray text = KMime::CRLFtoLF(raw);
    const qsizetype headEnd = text.indexOf("\n\n");
    const QByteArray head = headEnd < 0 ? text : text.left(headEnd + 1);

    // Die Trennzeile ist immer englisch und in UTC.
    const QDateTime utc = (date.isValid() ? date : QDateTime::fromSecsSinceEpoch(0)).toUTC();
    const QString stamp = QLocale::c().toString(utc, QStringLiteral("ddd MMM d HH:mm:ss yyyy"));
    QByteArray out = "From " + (sender.isEmpty() ? QByteArray("-") : sender.toUtf8()) + ' ' + stamp.toLatin1() + '\n';

    QByteArray ignored;
    if (!headerValue(head, "Status", &ignored)) {
        // Auch "ungelesen" ausdrücklich angeben: ohne Status gilt eine Nachricht beim Import als gelesen.
        out += (flags & MessageFlag::Seen) ? "Status: RO\n" : "Status: O\n";
        if (flags & MessageFlag::Flagged)
            out += "X-Status: F\n";
    }

    for (qsizetype pos = 0; pos < text.size();) {
        qsizetype end = text.indexOf('\n', pos);
        if (end < 0)
            end = text.size();
        const QByteArray line = text.mid(pos, end - pos);
        if (line.startsWith("From ") || isQuotedFrom(line))
            out += '>';
        out += line;
        out += '\n';
        pos = end + 1;
    }
    out += '\n';
    return out;
}

int flagsFromHeaders(const QByteArray &raw)
{
    const QByteArray text = KMime::CRLFtoLF(raw);
    const qsizetype headEnd = text.indexOf("\n\n");
    const QByteArray head = headEnd < 0 ? text : text.left(headEnd + 1);

    QByteArray value;
    if (headerValue(head, "X-Mozilla-Status", &value)) {
        bool ok = false;
        const int bits = value.toInt(&ok, 16);
        if (ok)
            return ((bits & 0x0001) ? int(MessageFlag::Seen) : 0) | ((bits & 0x0004) ? int(MessageFlag::Flagged) : 0);
    }
    int flags = 0;
    bool known = false;
    if (headerValue(head, "Status", &value)) {
        known = true;
        if (value.contains('R'))
            flags |= MessageFlag::Seen;
    }
    if (headerValue(head, "X-Status", &value) && value.contains('F'))
        flags |= MessageFlag::Flagged;
    return known ? flags : (flags | MessageFlag::Seen);
}

Reader::Reader(QIODevice *device)
    : m_device(device)
{
}

QByteArray Reader::next()
{
    QByteArray message;
    while (!m_device->atEnd()) {
        // peek statt read: die Trennzeile der nächsten Nachricht bleibt für deren Aufruf liegen.
        const QByteArray peeked = m_device->peek(256);
        const qsizetype nl = peeked.indexOf('\n');
        const QByteArray start = nl >= 0 ? peeked.left(nl) : peeked;
        if (isSeparator(start)) {
            if (m_started && !message.isEmpty())
                break;
            m_device->readLine();
            m_started = true;
            continue;
        }
        QByteArray line = m_device->readLine();
        if (line.isEmpty())
            break;
        m_started = true;
        if (line.startsWith('>') && isQuotedFrom(line))
            line.remove(0, 1);
        message += line;
    }
    // Die Leerzeile vor der nächsten Trennzeile gehört nicht zur Nachricht.
    if (message.endsWith("\r\n\r\n"))
        message.chop(2);
    else if (message.endsWith("\n\n"))
        message.chop(1);
    return message;
}

} // namespace Fjordpost::Mbox

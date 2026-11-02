#pragma once

#include "mail/mime/messageparser.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

namespace Fjordpost {

struct FolderState {
    qint64 id = 0;
    QString path;
    QString role;
    qint64 uidValidity = 0;
    qint64 uidNext = 0;
    quint64 highestModSeq = 0;
    bool keywords = true; // Server speichert eigene Schlüsselwörter (Labels)
};

struct LocalMessage {
    qint64 id = 0;
    int flags = 0;
};

struct MessageLocation {
    qint64 id = 0;
    qint64 accountId = 0;
    qint64 folderId = 0;
    QString folderPath;
    QString folderRole;
    qint64 uid = 0;
    int flags = 0;
    QString blobPath;
    bool bodyFetched = false;
    QByteArray messageId;
};

struct PendingOp {
    qint64 id = 0;
    qint64 accountId = 0;
    QString op; // "flags", "move", "expunge", "create"
    QString folderPath;
    qint64 uid = 0;
    QString argument;
};

// Datenbankzugriffe für die Synchronisation (IMAP, später POP3).
// Alle Funktionen arbeiten auf der Verbindung des aufrufenden Threads.
namespace MailStore {

// Ordner
qint64 upsertFolder(qint64 accountId, const QString &path, const QString &name, const QString &role, const QString &delimiter, int sortOrder);
void removeFoldersExcept(qint64 accountId, const QStringList &paths);
QList<FolderState> folders(qint64 accountId);
FolderState folder(qint64 folderId);
qint64 folderIdForRole(qint64 accountId, const QString &role);
qint64 folderIdForPath(qint64 accountId, const QString &path);
// Liefert den Ordner für eine Rolle (Gesendet, Entwürfe, Papierkorb, Archiv, Spam). Fehlt er,
// wird er lokal angelegt und eine "create"-Operation für den Server eingereiht.
qint64 ensureFolderForRole(qint64 accountId, const QString &role);
// Pfade, deren Anlage auf dem Server noch aussteht (dürfen beim Abgleich nicht verschwinden).
QStringList pendingFolderCreations(qint64 accountId);
int roleSortOrder(const QString &role);
void saveFolderState(const FolderState &state);
void clearFolder(qint64 folderId);

// Snooze (rein lokal: auf dem Server bleibt die Nachricht unverändert)
void setSnooze(const QList<qint64> &messageIds, qint64 until);
// Hebt abgelaufene Zurückstellungen auf und liefert die betroffenen Nachrichten.
QList<qint64> wakeSnoozed(qint64 now);
// Weckt zurückgestellte Nachrichten, deren Konversation Post bekommen hat, und liefert sie.
// Eigene Mails und in Papierkorb/Spam gefilterte Nachrichten wecken nicht.
QList<qint64> wakeRepliedThreads(const QList<qint64> &arrivedIds, const QString &ownEmail);
// Sekunden seit 1970 oder 0
qint64 snoozeUntil(qint64 messageId);
int snoozedCount(qint64 accountId);

// Nachrichten
QHash<qint64, LocalMessage> localMessages(qint64 folderId, qint64 minUid = 0);
qint64 minUid(qint64 folderId);
qint64 insertMessage(qint64 accountId, qint64 folderId, qint64 uid, int flags, qint64 size, const ParsedMessage &headers);
// Vollständige Nachricht ohne Server-Kopie ablegen (POP3, Import, lokale Ordner).
qint64 insertRaw(qint64 accountId, qint64 folderId, const QByteArray &raw, int flags, const QString &blobCategory);
bool hasMessageId(qint64 folderId, const QByteArray &messageId);
void updateFlags(qint64 messageId, int flags);
void removeMessages(const QList<qint64> &messageIds);
void storeBody(qint64 messageId, const QString &blobPath, const ParsedMessage &parsed);
MessageLocation location(qint64 messageId);
QList<qint64> messagesWithoutBody(qint64 folderId, int limit, qint64 maxSize);
// Lokal verschoben: neuer Ordner, UID unbekannt bis zur nächsten Synchronisation.
void moveLocally(qint64 messageId, qint64 targetFolderId);
// Nachrichten der Konversation (chronologisch), ohne Papierkorb/Spam – es sei denn,
// die Nachricht liegt selbst dort. sameFolder: nur aus ihrem Ordner.
QList<qint64> threadMessages(qint64 messageId, bool sameFolder = false);
// Ordnet einmalig alle Nachrichten neu den Konversationen zu (Bestände aus älteren
// Versionen); danach nur, wenn sich das Verfahren ändert. Liefert die Zahl geänderter Nachrichten.
int rebuildThreads();

// Bereits abgerufene POP3-Nachrichten
QSet<QByteArray> pop3KnownUids(qint64 accountId);
void rememberPop3Uid(qint64 accountId, const QByteArray &uidl);
void forgetPop3Uids(qint64 accountId, const QSet<QByteArray> &uidls);

// Offline-Operationen
void enqueueOp(qint64 accountId, const QString &op, const QString &folderPath, qint64 uid, const QString &argument = {});
QList<PendingOp> pendingOps(qint64 accountId);
void removeOp(qint64 opId);
// UIDs eines Ordners mit noch ausstehenden Operationen; deren lokaler
// Zustand darf beim Abgleich nicht vom (veralteten) Serverzustand überschrieben werden.
QSet<qint64> uidsWithPendingOps(qint64 accountId, const QString &folderPath);

// Umrechnung IMAP-Flags <-> MessageFlag-Bitmaske
int flagsFromImap(const QList<QByteArray> &flags);
QList<QByteArray> imapFlagsFor(int mask);

// Ordnerrolle aus SPECIAL-USE-Attributen bzw. üblichen Namen
QString roleFor(const QString &path, const QList<QByteArray> &attributes);

} // namespace MailStore
} // namespace Fjordpost

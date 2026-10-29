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
// Liefert den Ordner für eine Rolle (Gesendet, Entwürfe, Papierkorb, Archiv). Fehlt er,
// wird er lokal angelegt und eine "create"-Operation für den Server eingereiht.
qint64 ensureFolderForRole(qint64 accountId, const QString &role);
// Pfade, deren Anlage auf dem Server noch aussteht (dürfen beim Abgleich nicht verschwinden).
QStringList pendingFolderCreations(qint64 accountId);
int roleSortOrder(const QString &role);
void saveFolderState(const FolderState &state);
void clearFolder(qint64 folderId);

// Nachrichten
QHash<qint64, LocalMessage> localMessages(qint64 folderId, qint64 minUid = 0);
qint64 minUid(qint64 folderId);
qint64 insertMessage(qint64 accountId, qint64 folderId, qint64 uid, int flags, qint64 size, const ParsedMessage &headers);
void updateFlags(qint64 messageId, int flags);
void removeMessages(const QList<qint64> &messageIds);
void storeBody(qint64 messageId, const QString &blobPath, const ParsedMessage &parsed);
MessageLocation location(qint64 messageId);
QList<qint64> messagesWithoutBody(qint64 folderId, int limit, qint64 maxSize);
// Lokal verschoben: neuer Ordner, UID unbekannt bis zur nächsten Synchronisation.
void moveLocally(qint64 messageId, qint64 targetFolderId);

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

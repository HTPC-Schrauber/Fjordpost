#include "imapaccount.h"

#include "imapconnection.h"
#include "mail/mime/messageparser.h"
#include "storage/blobstore.h"
#include "storage/database.h"
#include "storage/messageflags.h"

#include <KIMAP/AppendJob>
#include <KIMAP/CopyJob>
#include <KIMAP/CreateJob>
#include <KIMAP/ExpungeJob>
#include <KIMAP/FetchJob>
#include <KIMAP/IdleJob>
#include <KIMAP/ListJob>
#include <KIMAP/LoginJob>
#include <KIMAP/MoveJob>
#include <KIMAP/SelectJob>
#include <KIMAP/Session>
#include <KIMAP/StoreJob>
#include <KIMAP/SubscribeJob>
#include <KLocalizedString>

#include <QLoggingCategory>
#include <QSqlDatabase>
#include <memory>

Q_DECLARE_LOGGING_CATEGORY(lcImap)

namespace Fjordpost {

namespace {

KIMAP::ImapSet uidSet(qint64 uid)
{
    KIMAP::ImapSet set;
    set.add(KIMAP::ImapSet::Id(uid));
    return set;
}

// Kleine RAII-Hilfe für Schreibtransaktionen beim Abgleich.
struct Transaction {
    Transaction()
        : db(Database::connection())
    {
        db.transaction();
    }
    ~Transaction() { db.commit(); }
    QSqlDatabase db;
};

} // namespace

ImapAccount::ImapAccount(const Account &account, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_sync(new ImapConnection(account, QStringLiteral("sync-%1").arg(account.id), this))
    , m_interactive(new ImapConnection(account, QStringLiteral("ui-%1").arg(account.id), this))
{
    auto onLoginFailed = [this](const QString &message) {
        m_syncQueued = false;
        m_opsQueued = false;
        setSyncing(false);
        Q_EMIT errorOccurred(i18n("Login to %1 failed: %2", m_account.incomingHost, message));
    };
    connect(m_sync, &ImapConnection::loginFailed, this, onLoginFailed);
    connect(m_interactive, &ImapConnection::loginFailed, this, onLoginFailed);

    m_idleRetry.setSingleShot(true);
    connect(&m_idleRetry, &QTimer::timeout, this, &ImapAccount::startIdle);
}

ImapAccount::~ImapAccount()
{
    shutdown();
}

void ImapAccount::setCredentials(const Account &account, const QString &secret)
{
    m_account = account;
    m_secret = secret;
    m_sync->setCredentials(account, secret);
    m_interactive->setCredentials(account, secret);
}

void ImapAccount::setSyncing(bool syncing)
{
    if (m_syncing == syncing)
        return;
    m_syncing = syncing;
    Q_EMIT syncingChanged(syncing);
}

void ImapAccount::shutdown()
{
    m_shuttingDown = true;
    m_idleRetry.stop();
    if (m_idleJob)
        m_idleJob->stop();
    if (m_idleSession) {
        m_idleSession->close();
        m_idleSession->deleteLater();
    }
    m_sync->disconnectFromServer();
    m_interactive->disconnectFromServer();
}

// ---------------------------------------------------------------- Abgleich

void ImapAccount::sync()
{
    if (m_syncQueued || m_shuttingDown)
        return;
    m_syncQueued = true;
    setSyncing(true);

    // Lokale Änderungen zuerst losschicken (läuft parallel auf der interaktiven Verbindung).
    flushPendingOps();

    m_sync->enqueue([this](KIMAP::Session *session, Done done) {
        m_syncQueued = false;
        listFolders(session, [this, session, done] {
            syncFolders(session, MailStore::folders(m_account.id), [this, done] {
                setSyncing(false);
                done();
            });
        });
    });
}

void ImapAccount::syncFolder(qint64 folderId)
{
    if (m_shuttingDown)
        return;
    m_sync->enqueue([this, folderId](KIMAP::Session *session, Done done) {
        const FolderState folder = MailStore::folder(folderId);
        if (folder.id == 0) {
            done();
            return;
        }
        syncOneFolder(session, folder, done);
    });
}

void ImapAccount::listFolders(KIMAP::Session *session, Done done)
{
    auto *job = new KIMAP::ListJob(session);
    job->setOption(KIMAP::ListJob::IncludeUnsubscribed);

    auto descriptors = std::make_shared<QList<QPair<KIMAP::MailBoxDescriptor, QList<QByteArray>>>>();
    connect(job, &KIMAP::ListJob::mailBoxesReceived, this,
            [descriptors](const QList<KIMAP::MailBoxDescriptor> &boxes, const QList<QList<QByteArray>> &flags) {
                for (int i = 0; i < boxes.size(); ++i)
                    descriptors->append({boxes.at(i), flags.value(i)});
            });
    connect(job, &KJob::result, this, [this, job, descriptors, done](KJob *) {
        if (job->error()) {
            qCWarning(lcImap) << "LIST fehlgeschlagen:" << job->errorString();
            done();
            return;
        }

        QStringList paths;
        {
            Transaction tx;
            for (const auto &[box, flags] : std::as_const(*descriptors)) {
                const bool selectable = !flags.contains("\\noselect") && !flags.contains("\\NoSelect") && !flags.contains("\\NonExistent");
                if (!selectable)
                    continue;
                const QString role = MailStore::roleFor(box.name, flags);
                QString name = box.separator.isNull() ? box.name : box.name.section(box.separator, -1);
                if (role == QLatin1String(FolderRole::Inbox))
                    name = i18n("Inbox");
                MailStore::upsertFolder(m_account.id, box.name, name, role, box.separator.isNull() ? QString() : QString(box.separator), MailStore::roleSortOrder(role));
                paths << box.name;
            }
            paths << MailStore::pendingFolderCreations(m_account.id);
            MailStore::removeFoldersExcept(m_account.id, paths);
        }
        Q_EMIT foldersChanged();
        done();
    });
    job->start();
}

void ImapAccount::syncFolders(KIMAP::Session *session, QList<FolderState> folders, Done done)
{
    if (folders.isEmpty() || m_shuttingDown) {
        done();
        return;
    }
    const FolderState folder = folders.takeFirst();
    syncOneFolder(session, folder, [this, session, folders, done] {
        syncFolders(session, folders, done);
    });
}

void ImapAccount::syncOneFolder(KIMAP::Session *session, const FolderState &folder, Done done)
{
    auto *select = new KIMAP::SelectJob(session);
    select->setMailBox(folder.path);
    const bool condstore = m_sync->hasCapability(QStringLiteral("CONDSTORE"));
    select->setCondstoreEnabled(condstore);

    connect(select, &KJob::result, this, [this, session, select, folder, condstore, done](KJob *) {
        if (select->error()) {
            qCWarning(lcImap) << "SELECT" << folder.path << "fehlgeschlagen:" << select->errorString();
            done();
            return;
        }
        m_sync->setSelectedMailbox(folder.path);

        FolderState state = folder;
        bool firstSync = folder.uidNext == 0;
        if (folder.uidValidity != 0 && folder.uidValidity != select->uidValidity()) {
            qCInfo(lcImap) << folder.path << "UIDVALIDITY geändert, Ordner wird neu geladen";
            MailStore::clearFolder(folder.id);
            firstSync = true;
            state.uidNext = 0;
        }
        state.uidValidity = select->uidValidity();

        const int exists = select->messageCount();
        const qint64 serverUidNext = select->nextUid();
        const quint64 serverModSeq = select->highestModSequence();

        auto finish = [this, state, serverUidNext, serverModSeq, folder, session, done]() mutable {
            state.uidNext = serverUidNext;
            if (state.uidNext <= 0) {
                // Manche Server melden kein UIDNEXT
                const auto local = MailStore::localMessages(state.id);
                qint64 maxUid = 0;
                for (auto it = local.cbegin(); it != local.cend(); ++it)
                    maxUid = std::max(maxUid, it.key());
                state.uidNext = maxUid + 1;
            }
            state.highestModSeq = serverModSeq;
            MailStore::saveFolderState(state);
            Q_EMIT folderUpdated(folder.id);
            if (folder.role == QLatin1String(FolderRole::Inbox))
                prefetchBodies(session, state, done);
            else
                done();
        };

        if (exists == 0) {
            MailStore::clearFolder(folder.id);
            finish();
            return;
        }

        const bool hasNew = firstSync || serverUidNext <= 0 || serverUidNext > folder.uidNext;
        const bool unchanged = !firstSync && condstore && folder.highestModSeq != 0 && folder.highestModSeq == serverModSeq
            && !hasNew && MailStore::localMessages(folder.id).size() == exists;

        auto afterHeaders = [this, session, state, firstSync, unchanged, finish]() mutable {
            if (firstSync || unchanged)
                finish();
            else
                syncFlags(session, state, finish);
        };

        if (hasNew)
            fetchNewHeaders(session, state, exists, firstSync, afterHeaders);
        else
            afterHeaders();
    });
    select->start();
}

void ImapAccount::fetchNewHeaders(KIMAP::Session *session, FolderState folder, int exists, bool firstSync, Done done)
{
    auto *job = new KIMAP::FetchJob(session);
    KIMAP::ImapSet set;
    if (firstSync || folder.uidNext <= 0) {
        // Erstabgleich: nur die neuesten Nachrichten über Sequenznummern holen.
        const qint64 start = std::max<qint64>(1, exists - initialHeaderLimit + 1);
        set.add(KIMAP::ImapInterval(start, 0));
        job->setUidBased(false);
    } else {
        set.add(KIMAP::ImapInterval(folder.uidNext, 0));
        job->setUidBased(true);
    }
    job->setSequenceSet(set);
    KIMAP::FetchJob::FetchScope scope;
    scope.mode = KIMAP::FetchJob::FetchScope::FullHeaders;
    job->setScope(scope);

    auto known = std::make_shared<QHash<qint64, LocalMessage>>(MailStore::localMessages(folder.id));
    auto newIds = std::make_shared<QList<qint64>>();
    const bool notify = !firstSync && folder.role == QLatin1String(FolderRole::Inbox);

    connect(job, &KIMAP::FetchJob::messagesAvailable, this, [this, folder, firstSync, known, newIds, notify](const QMap<qint64, KIMAP::Message> &messages) {
        Transaction tx;
        for (const KIMAP::Message &m : messages) {
            if (m.uid <= 0 || known->contains(m.uid))
                continue;
            // "n:*" liefert immer die letzte Nachricht, auch wenn deren UID kleiner ist.
            if (!firstSync && folder.uidNext > 0 && m.uid < folder.uidNext)
                continue;
            if (!m.message)
                continue;
            // KIMAP liefert CRLF; KMime erwartet LF, daher neu aus dem Rohkopf parsen.
            const ParsedMessage headers = MessageParser::parseHeaders(m.message->head());
            const int flags = MailStore::flagsFromImap(m.flags);
            const qint64 id = MailStore::insertMessage(m_account.id, folder.id, m.uid, flags, m.size, headers);
            known->insert(m.uid, {id, flags});
            if (notify && id > 0 && !(flags & MessageFlag::Seen))
                newIds->append(id);
        }
    });
    connect(job, &KJob::result, this, [this, job, folder, newIds, done](KJob *) {
        if (job->error())
            qCWarning(lcImap) << "FETCH (Kopfzeilen)" << folder.path << "fehlgeschlagen:" << job->errorString();
        if (!newIds->isEmpty())
            Q_EMIT newMessages(*newIds);
        done();
    });
    job->start();
}

void ImapAccount::syncFlags(KIMAP::Session *session, const FolderState &folder, Done done)
{
    const qint64 minUid = MailStore::minUid(folder.id);
    if (minUid <= 0) {
        done();
        return;
    }

    auto *job = new KIMAP::FetchJob(session);
    KIMAP::ImapSet set;
    set.add(KIMAP::ImapInterval(minUid, 0));
    job->setSequenceSet(set);
    job->setUidBased(true);
    KIMAP::FetchJob::FetchScope scope;
    scope.mode = KIMAP::FetchJob::FetchScope::Flags;
    job->setScope(scope);

    auto server = std::make_shared<QHash<qint64, int>>();
    connect(job, &KIMAP::FetchJob::messagesAvailable, this, [server](const QMap<qint64, KIMAP::Message> &messages) {
        for (const KIMAP::Message &m : messages)
            server->insert(m.uid, MailStore::flagsFromImap(m.flags));
    });
    connect(job, &KJob::result, this, [this, job, folder, minUid, server, done](KJob *) {
        if (job->error()) {
            qCWarning(lcImap) << "FETCH (Flags)" << folder.path << "fehlgeschlagen:" << job->errorString();
            done();
            return;
        }
        const QHash<qint64, LocalMessage> local = MailStore::localMessages(folder.id, minUid);
        const QSet<qint64> pending = MailStore::uidsWithPendingOps(m_account.id, folder.path);
        QList<qint64> removed;
        {
            Transaction tx;
            for (auto it = local.cbegin(); it != local.cend(); ++it) {
                if (pending.contains(it.key()))
                    continue;
                const auto serverIt = server->constFind(it.key());
                if (serverIt == server->constEnd())
                    removed << it.value().id;
                else if (serverIt.value() != it.value().flags)
                    MailStore::updateFlags(it.value().id, serverIt.value());
            }
            MailStore::removeMessages(removed);
        }
        done();
    });
    job->start();
}

void ImapAccount::prefetchBodies(KIMAP::Session *session, const FolderState &folder, Done done)
{
    QHash<qint64, qint64> uidToMessage;
    const auto ids = MailStore::messagesWithoutBody(folder.id, prefetchCount, prefetchMaxSize);
    for (qint64 id : ids) {
        const MessageLocation loc = MailStore::location(id);
        if (loc.uid > 0)
            uidToMessage.insert(loc.uid, id);
    }
    fetchBodies(session, folder.path, uidToMessage, [this, folder, done] {
        Q_EMIT folderUpdated(folder.id);
        done();
    });
}

void ImapAccount::fetchBodies(KIMAP::Session *session, const QString &mailbox, const QHash<qint64, qint64> &uidToMessage, Done done)
{
    if (uidToMessage.isEmpty()) {
        done();
        return;
    }
    auto *job = new KIMAP::FetchJob(session);
    KIMAP::ImapSet set;
    for (auto it = uidToMessage.cbegin(); it != uidToMessage.cend(); ++it)
        set.add(KIMAP::ImapSet::Id(it.key()));
    job->setSequenceSet(set);
    job->setUidBased(true);
    KIMAP::FetchJob::FetchScope scope;
    scope.mode = KIMAP::FetchJob::FetchScope::Full;
    job->setScope(scope);

    auto received = std::make_shared<QSet<qint64>>();
    connect(job, &KIMAP::FetchJob::messagesAvailable, this, [this, uidToMessage, received](const QMap<qint64, KIMAP::Message> &messages) {
        for (const KIMAP::Message &m : messages) {
            const qint64 id = uidToMessage.value(m.uid);
            if (id == 0 || !m.message)
                continue;
            const QByteArray raw = m.message->encodedContent(true);
            const QString blob = BlobStore::store(m_account.id, QStringLiteral("imap"), raw);
            if (blob.isEmpty())
                continue;
            MailStore::storeBody(id, blob, MessageParser::parse(raw));
            received->insert(id);
            Q_EMIT bodyFetched(id, true);
        }
    });
    connect(job, &KJob::result, this, [this, job, mailbox, uidToMessage, received, done](KJob *) {
        if (job->error())
            qCWarning(lcImap) << "FETCH (Inhalt)" << mailbox << "fehlgeschlagen:" << job->errorString();
        for (auto it = uidToMessage.cbegin(); it != uidToMessage.cend(); ++it) {
            if (!received->contains(it.value()))
                Q_EMIT bodyFetched(it.value(), false);
        }
        done();
    });
    job->start();
}

void ImapAccount::fetchBody(qint64 messageId)
{
    const MessageLocation loc = MailStore::location(messageId);
    if (loc.uid <= 0) {
        Q_EMIT bodyFetched(messageId, false);
        return;
    }
    m_interactive->enqueue(
        [this, loc](KIMAP::Session *session, Done done) {
            selectMailbox(m_interactive, session, loc.folderPath, [this, session, loc, done](bool ok) {
                if (!ok) {
                    Q_EMIT bodyFetched(loc.id, false);
                    done();
                    return;
                }
                fetchBodies(session, loc.folderPath, {{loc.uid, loc.id}}, done);
            });
        },
        true);
}

void ImapAccount::selectMailbox(ImapConnection *connection, KIMAP::Session *session, const QString &mailbox, std::function<void(bool)> done)
{
    if (connection->selectedMailbox() == mailbox) {
        done(true);
        return;
    }
    auto *job = new KIMAP::SelectJob(session);
    job->setMailBox(mailbox);
    connect(job, &KJob::result, this, [connection, job, mailbox, done](KJob *) {
        if (job->error()) {
            qCWarning(lcImap) << "SELECT" << mailbox << "fehlgeschlagen:" << job->errorString();
            connection->setSelectedMailbox(QString());
            done(false);
            return;
        }
        connection->setSelectedMailbox(mailbox);
        done(true);
    });
    job->start();
}

// ------------------------------------------------------ Offline-Operationen

void ImapAccount::flushPendingOps()
{
    if (m_opsQueued || m_shuttingDown || MailStore::pendingOps(m_account.id).isEmpty())
        return;
    m_opsQueued = true;
    m_interactive->enqueue([this](KIMAP::Session *session, Done done) {
        m_opsQueued = false;
        runPendingOps(session, done);
    });
}

void ImapAccount::runPendingOps(KIMAP::Session *session, Done done)
{
    const QList<PendingOp> ops = MailStore::pendingOps(m_account.id);
    if (ops.isEmpty()) {
        done();
        return;
    }
    const PendingOp op = ops.first();
    runOp(session, op, [this, session, op, done](bool keep) {
        if (keep) {
            // Verbindung weg: später erneut versuchen, Reihenfolge beibehalten.
            done();
            return;
        }
        MailStore::removeOp(op.id);
        runPendingOps(session, done);
    });
}

void ImapAccount::runOp(KIMAP::Session *session, const PendingOp &op, std::function<void(bool keep)> done)
{
    // Ein Fehler bei bestehender Verbindung bedeutet: Server lehnt ab (z. B. Nachricht
    // existiert nicht mehr) -> Operation verwerfen. Sonst behalten und später wiederholen.
    auto finished = [this, done](KJob *job) {
        if (job->error())
            qCWarning(lcImap) << "Operation fehlgeschlagen:" << job->errorString();
        done(job->error() && !m_interactive->isConnected());
    };

    if (op.op == QLatin1String("create")) {
        // Existiert der Ordner schon (z. B. von einem anderen Client angelegt), lehnt
        // der Server ab; die Operation ist dann trotzdem erledigt.
        auto *create = new KIMAP::CreateJob(session);
        create->setMailBox(op.folderPath);
        connect(create, &KJob::result, this, [this, session, op, finished](KJob *j) {
            if (j->error() && !m_interactive->isConnected()) {
                finished(j);
                return;
            }
            auto *subscribe = new KIMAP::SubscribeJob(session);
            subscribe->setMailBox(op.folderPath);
            connect(subscribe, &KJob::result, this, finished);
            subscribe->start();
        });
        create->start();
        return;
    }

    selectMailbox(m_interactive, session, op.folderPath, [this, session, op, done, finished](bool ok) {
        if (!ok) {
            done(!m_interactive->isConnected());
            return;
        }

        if (op.op == QLatin1String("flags")) {
            auto *job = new KIMAP::StoreJob(session);
            job->setUidBased(true);
            job->setSequenceSet(uidSet(op.uid));
            job->setFlags({op.argument.mid(1).toUtf8()});
            job->setMode(op.argument.startsWith(QLatin1Char('+')) ? KIMAP::StoreJob::AppendFlags : KIMAP::StoreJob::RemoveFlags);
            connect(job, &KJob::result, this, finished);
            job->start();
            return;
        }

        if (op.op == QLatin1String("move")) {
            const qint64 targetId = MailStore::folderIdForPath(m_account.id, op.argument);
            auto syncTarget = [this, targetId] {
                if (targetId > 0)
                    syncFolder(targetId);
            };
            if (m_interactive->hasCapability(QStringLiteral("MOVE"))) {
                auto *job = new KIMAP::MoveJob(session);
                job->setUidBased(true);
                job->setSequenceSet(uidSet(op.uid));
                job->setMailBox(op.argument);
                connect(job, &KJob::result, this, [finished, syncTarget](KJob *j) {
                    if (!j->error())
                        syncTarget();
                    finished(j);
                });
                job->start();
                return;
            }
            // Ohne MOVE: kopieren, als gelöscht markieren, Ordner bereinigen.
            auto *copy = new KIMAP::CopyJob(session);
            copy->setUidBased(true);
            copy->setSequenceSet(uidSet(op.uid));
            copy->setMailBox(op.argument);
            connect(copy, &KJob::result, this, [this, session, op, finished, syncTarget](KJob *j) {
                if (j->error()) {
                    finished(j);
                    return;
                }
                auto *store = new KIMAP::StoreJob(session);
                store->setUidBased(true);
                store->setSequenceSet(uidSet(op.uid));
                store->setFlags({"\\Deleted"});
                store->setMode(KIMAP::StoreJob::AppendFlags);
                connect(store, &KJob::result, this, [this, session, finished, syncTarget](KJob *) {
                    auto *expunge = new KIMAP::ExpungeJob(session);
                    connect(expunge, &KJob::result, this, [finished, syncTarget](KJob *e) {
                        syncTarget();
                        finished(e);
                    });
                    expunge->start();
                });
                store->start();
            });
            copy->start();
            return;
        }

        if (op.op == QLatin1String("expunge")) {
            auto *store = new KIMAP::StoreJob(session);
            store->setUidBased(true);
            store->setSequenceSet(uidSet(op.uid));
            store->setFlags({"\\Deleted"});
            store->setMode(KIMAP::StoreJob::AppendFlags);
            connect(store, &KJob::result, this, [this, session, finished](KJob *j) {
                if (j->error()) {
                    finished(j);
                    return;
                }
                auto *expunge = new KIMAP::ExpungeJob(session);
                connect(expunge, &KJob::result, this, finished);
                expunge->start();
            });
            store->start();
            return;
        }

        qCWarning(lcImap) << "Unbekannte Operation" << op.op;
        done(false);
    });
}

// ----------------------------------------------------------------- Ablegen

void ImapAccount::append(const QString &role, const QByteArray &raw, const QList<QByteArray> &flags)
{
    const qint64 folderId = MailStore::ensureFolderForRole(m_account.id, role);
    if (folderId == 0)
        return;
    const QString path = MailStore::folder(folderId).path;
    // Ein eben erst angelegter Ordner muss vor dem APPEND auf dem Server existieren;
    // beide Aufgaben laufen nacheinander auf der interaktiven Verbindung.
    flushPendingOps();
    m_interactive->enqueue([this, folderId, path, raw, flags](KIMAP::Session *session, Done done) {
        auto *job = new KIMAP::AppendJob(session);
        job->setMailBox(path);
        job->setFlags(flags);
        job->setContent(raw);
        connect(job, &KJob::result, this, [this, job, folderId, path, done](KJob *) {
            if (job->error())
                Q_EMIT errorOccurred(i18n("Could not store message in %1: %2", path, job->errorString()));
            else
                syncFolder(folderId);
            done();
        });
        job->start();
    });
}

// -------------------------------------------------------------------- IDLE

void ImapAccount::startIdle()
{
    if (m_shuttingDown || m_secret.isEmpty())
        return;
    const qint64 inboxId = MailStore::folderIdForRole(m_account.id, QString::fromLatin1(FolderRole::Inbox));
    if (inboxId == 0) {
        // Ordnerliste noch nicht bekannt; nach dem ersten Abgleich erneut versuchen.
        m_idleRetry.start(30 * 1000);
        return;
    }
    if (m_idleSession) {
        m_idleSession->close();
        m_idleSession->deleteLater();
    }
    m_idleSession = new KIMAP::Session(m_account.incomingHost, quint16(m_account.incomingPort), this);
    m_idleSession->setTimeout(60);

    auto retryLater = [this](int seconds) {
        if (!m_shuttingDown)
            m_idleRetry.start(seconds * 1000);
    };

    auto *login = ImapConnection::createLoginJob(m_idleSession, m_account, m_secret);
    connect(login, &KJob::result, this, [this, login, inboxId, retryLater](KJob *) {
        if (login->error()) {
            retryLater(120);
            return;
        }
        auto *select = new KIMAP::SelectJob(m_idleSession);
        select->setMailBox(MailStore::folder(inboxId).path);
        connect(select, &KJob::result, this, [this, select, inboxId, retryLater](KJob *) {
            if (select->error()) {
                retryLater(120);
                return;
            }
            m_idleJob = new KIMAP::IdleJob(m_idleSession);
            auto changed = [this, inboxId] {
                syncFolder(inboxId);
            };
            connect(m_idleJob, &KIMAP::IdleJob::mailBoxStats, this, changed);
            connect(m_idleJob, &KIMAP::IdleJob::mailBoxMessageFlagsChanged, this, changed);
            connect(m_idleJob, &KJob::result, this, [retryLater](KJob *job) {
                // Server ohne IDLE oder Verbindung beendet: neu aufbauen.
                retryLater(job->error() ? 120 : 1);
            });
            m_idleJob->start();
        });
        select->start();
    });
    login->start();
}

} // namespace Fjordpost

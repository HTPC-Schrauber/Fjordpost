#include "secretmigration.h"

#include "core/accountmanager.h"
#include "core/secretstore.h"
#include "storage/database.h"
#include "storage/davstore.h"

#include <QLoggingCategory>

#include <memory>

Q_LOGGING_CATEGORY(lcSecretMigration, "fjordpost.secrets.migration")

namespace Fjordpost {

namespace {

struct Move {
    QString from;
    QString to;
    std::function<bool(const QString &name)> apply; // neuen Namen in der DB vermerken
};

void runNext(SecretStore *secrets, std::shared_ptr<QList<Move>> moves, bool copyLegacy, std::function<void()> done)
{
    if (moves->isEmpty()) {
        done();
        return;
    }
    const Move move = moves->takeFirst();
    auto next = [secrets, moves, copyLegacy, done] {
        runNext(secrets, moves, copyLegacy, done);
    };
    if (!copyLegacy) {
        move.apply(move.to);
        next();
        return;
    }
    secrets->read(move.from, [secrets, move, next](bool ok, const QString &secret) {
        if (!ok || secret.isEmpty()) {
            // Nichts gespeichert (z. B. Demo-Konto): nur den neuen Namen vergeben.
            move.apply(move.to);
            next();
            return;
        }
        secrets->write(move.to, secret, [secrets, move, next](bool written) {
            // Ohne Kopie bleibt der alte Eintrag; der Umzug wird beim nächsten Start wiederholt.
            if (!written) {
                qCWarning(lcSecretMigration) << "Umzug fehlgeschlagen:" << move.from;
            } else if (move.apply(move.to)) {
                secrets->remove(move.from);
                qCInfo(lcSecretMigration) << move.from << "→" << move.to;
            }
            next();
        });
    });
}

} // namespace

void migrateSecretNames(SecretStore *secrets, AccountManager *accounts, bool copyLegacy, std::function<void()> done)
{
    auto moves = std::make_shared<QList<Move>>();
    const auto mailAccounts = accounts->accounts();
    for (const Account &a : mailAccounts) {
        if (!a.secretName.isEmpty())
            continue;
        moves->append({QStringLiteral("account-%1").arg(a.id), Database::secretEntryName(QStringLiteral("Mail"), a.email, a.id), [accounts, id = a.id](const QString &name) {
                           return accounts->setSecretName(id, name);
                       }});
    }
    const auto davAccounts = DavStore::accounts();
    for (const DavAccount &a : davAccounts) {
        if (!a.secretName.isEmpty())
            continue;
        moves->append({QStringLiteral("dav-%1").arg(a.id), Database::secretEntryName(QStringLiteral("Kalender"), a.name, a.id), [id = a.id](const QString &name) {
                           return DavStore::setSecretName(id, name);
                       }});
    }
    runNext(secrets, moves, copyLegacy, std::move(done));
}

} // namespace Fjordpost

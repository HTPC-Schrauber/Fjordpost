#include "davservice.h"

#include "auth/oauthtokens.h"
#include "core/secretstore.h"
#include "dav/davclient.h"
#include "dav/davdiscovery.h"
#include "dav/davsync.h"
#include "pim/pimdata.h"

#include <KLocalizedString>

#include <QPointer>
#include <QUrl>

namespace Fjordpost {

DavService::DavService(SecretStore *secrets, QObject *parent)
    : QObject(parent)
    , m_secrets(secrets)
{
    m_syncTimer.setInterval(syncIntervalMinutes * 60 * 1000);
    connect(&m_syncTimer, &QTimer::timeout, this, &DavService::syncAll);
    m_alarmTimer.setInterval(30 * 1000);
    connect(&m_alarmTimer, &QTimer::timeout, this, &DavService::checkAlarms);
}

void DavService::start()
{
    m_started = true;
    m_alarmsChecked = QDateTime::currentDateTime();
    const auto accounts = DavStore::accounts();
    for (const DavAccount &a : accounts)
        attachWithSecret(a);
    m_syncTimer.start();
    m_alarmTimer.start();
}

void DavService::attachWithSecret(const DavAccount &account)
{
    if (!account.oauthProvider.isEmpty()) {
        // Das Token holt der Client selbst vor jeder Anfrage.
        attach(account, {});
        m_accounts.value(account.id)->sync();
        return;
    }
    m_secrets->read(account.secretKey(), [this, account](bool ok, const QString &secret) {
        if (!ok) {
            Q_EMIT errorOccurred(i18n("The password for \"%1\" could not be read from the wallet.", account.name));
            return;
        }
        attach(account, secret);
        m_accounts.value(account.id)->sync();
    });
}

void DavService::attach(const DavAccount &account, const QString &password)
{
    auto *client = new DavClient(this);
    if (account.oauthProvider.isEmpty()) {
        client->setBasicAuth(account.username, password);
    } else if (m_tokens) {
        const QString key = account.secretKey();
        client->setTokenSource(m_tokens->fetcher(key, account.oauthProvider), [tokens = QPointer<OAuthTokens>(m_tokens), key] {
            if (tokens)
                tokens->invalidate(key);
        });
    }
    auto *sync = new DavSync(account, client, this);
    client->setParent(sync);
    m_accounts.insert(account.id, sync);

    connect(sync, &DavSync::dataChanged, this, &DavService::dataChanged);
    connect(sync, &DavSync::conflict, this, &DavService::conflict);
    connect(sync, &DavSync::finished, this, [this, name = account.name](bool ok, const QString &error) {
        if (!ok)
            Q_EMIT errorOccurred(i18n("%1: %2", name, error));
    });
}

void DavService::syncAll()
{
    const auto syncs = m_accounts.values();
    for (DavSync *s : syncs)
        s->sync();
}

void DavService::flush(qint64 accountId)
{
    if (DavSync *s = m_accounts.value(accountId))
        s->flush();
}

void DavService::addAccount(const QString &address, const QString &username, const QString &password, qint64 mailAccountId, std::function<void(const QString &error)> done)
{
    auto *client = new DavClient(this);
    client->setBasicAuth(username, password);
    auto *discovery = new DavDiscovery(client, client);
    discovery->start(address, [=, this, done = std::move(done)](const DavDiscovery::Result &result) {
        client->deleteLater();
        if (!result.error.isEmpty()) {
            done(result.error);
            return;
        }
        DavAccount account;
        account.url = result.baseUrl.toString();
        account.name = result.baseUrl.host();
        account.username = username;
        account.mailAccountId = mailAccountId;
        account.calendarHome = result.calendarHome;
        account.addressbookHome = result.addressbookHome;
        account.autoSchedule = result.autoSchedule;
        saveAndAttach(account, password, done);
    });
}

void DavService::saveAndAttach(DavAccount account, const QString &secret, std::function<void(const QString &error)> done)
{
    account.id = DavStore::saveAccount(account);
    if (account.id == 0) {
        done(i18n("The account could not be saved."));
        return;
    }
    m_secrets->write(account.secretKey(), secret, [this, account, secret, done](bool ok) {
        if (!ok) {
            DavStore::removeAccount(account.id);
            done(i18n("The password could not be saved in the wallet."));
            return;
        }
        attach(account, secret);
        m_accounts.value(account.id)->sync();
        done({});
    });
}

void DavService::addOAuthAccount(const OAuthProvider &provider, const QString &email, const OAuthClient::Result &tokens, qint64 mailAccountId,
                                 std::function<void(const QString &error)> done)
{
    if (!provider.hasDav()) {
        done(i18n("%1 offers no calendars or contacts through CalDAV/CardDAV.", provider.name));
        return;
    }
    // Kalender und Adressbücher liegen bei Google auf verschiedenen Hosts: zweimal suchen.
    auto *client = new DavClient(this);
    client->setBearerToken(tokens.accessToken);
    auto *calendars = new DavDiscovery(client, client);
    calendars->start(provider.caldavUrl.toString(), [=, this](const DavDiscovery::Result &calendarResult) {
        auto *books = new DavDiscovery(client, client);
        books->start(provider.carddavUrl.toString(), [=, this](const DavDiscovery::Result &bookResult) {
            client->deleteLater();
            if (calendarResult.calendarHome.isEmpty() && bookResult.addressbookHome.isEmpty()) {
                done(calendarResult.error.isEmpty() ? bookResult.error : calendarResult.error);
                return;
            }
            DavAccount account;
            account.url = (calendarResult.calendarHome.isEmpty() ? bookResult.baseUrl : calendarResult.baseUrl).toString();
            account.name = i18nc("@label account name, %1 provider, %2 address", "%1 (%2)", provider.name, email);
            account.username = email;
            account.mailAccountId = mailAccountId;
            account.calendarHome = calendarResult.calendarHome;
            account.addressbookHome = bookResult.addressbookHome;
            account.autoSchedule = calendarResult.autoSchedule;
            account.oauthProvider = provider.id;
            account.id = DavStore::saveAccount(account);
            if (account.id == 0) {
                done(i18n("The account could not be saved."));
                return;
            }
            if (!m_tokens) {
                done(i18n("Sign-in through the browser is not available."));
                return;
            }
            m_tokens->store(account.secretKey(), provider.id, tokens, [this, account, done](bool ok) {
                if (!ok) {
                    DavStore::removeAccount(account.id);
                    done(i18n("The sign-in could not be saved in the wallet."));
                    return;
                }
                attach(account, {});
                m_accounts.value(account.id)->sync();
                Q_EMIT dataChanged();
                done({});
            });
        });
    });
}

void DavService::removeAccount(qint64 id)
{
    if (DavSync *s = m_accounts.take(id))
        s->deleteLater();
    const DavAccount account = DavStore::account(id);
    if (account.id > 0) {
        m_secrets->remove(account.secretKey());
        if (m_tokens)
            m_tokens->forget(account.secretKey());
    }
    DavStore::removeAccount(id);
    Q_EMIT dataChanged();
}

void DavService::createCollection(qint64 accountId, const QString &kind, const QString &name, const QString &color, Done done)
{
    DavSync *s = m_accounts.value(accountId);
    if (!s) {
        done(i18n("This account is not connected."));
        return;
    }
    s->createCollection(kind, name, color, std::move(done));
}

void DavService::updateCollection(qint64 collectionId, const QString &name, const QString &color, Done done)
{
    const DavCollection collection = DavStore::collection(collectionId);
    DavSync *s = m_accounts.value(collection.accountId);
    if (!s) {
        done(i18n("This account is not connected."));
        return;
    }
    s->updateCollection(collection, name, color, std::move(done));
}

void DavService::removeCollection(qint64 collectionId, Done done)
{
    const DavCollection collection = DavStore::collection(collectionId);
    DavSync *s = m_accounts.value(collection.accountId);
    if (!s) {
        done(i18n("This account is not connected."));
        return;
    }
    s->removeCollection(collection, std::move(done));
}

void DavService::checkAlarms()
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime from = m_alarmsChecked;
    m_alarmsChecked = now;
    const auto items = DavStore::alarmItems(from.toMSecsSinceEpoch(), now.addDays(8).toMSecsSinceEpoch());
    for (const DavItem &item : items) {
        const auto hits = Pim::alarmsBetween(item, from, now);
        for (const Pim::AlarmHit &hit : hits)
            Q_EMIT reminder(hit.title, hit.when);
    }
}

} // namespace Fjordpost

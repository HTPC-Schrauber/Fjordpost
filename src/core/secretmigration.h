#pragma once

#include <functional>

namespace Fjordpost {

class AccountManager;
class SecretStore;

// Zieht SecretStore-Einträge mit alten Namen ("account-N", "dav-N") auf die lesbaren,
// je Datenverzeichnis eindeutigen Namen um (Migration v9). done läuft danach immer.
// copyLegacy nur im Standardprofil: In anderen Datenverzeichnissen gehörten gleich
// benannte alte Einträge womöglich zum Standardprofil; dort gibt es nur neue Namen.
void migrateSecretNames(SecretStore *secrets, AccountManager *accounts, bool copyLegacy, std::function<void()> done);

} // namespace Fjordpost

#pragma once

namespace MopMail {

class AccountManager;

// Füllt eine leere Datenbank mit Beispielkonten und -mails, damit die
// Oberfläche ohne echten Server entwickelt werden kann (Start mit --demo).
void seedDemoData(AccountManager &accounts);

} // namespace MopMail

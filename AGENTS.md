# AGENTS.md – Hinweise für KI-Agenten und Mitwirkende

Fjordpost ist ein E-Mail-Client für KDE Plasma im Stil von Mailbird.
C++20 · Qt 6 · KDE Frameworks 6 · Kirigami/QML · eigene SQLite-Engine (kein Akonadi).

Kommunikation mit dem Projektinhaber, Commit-Nachrichten und Code-Kommentare auf **Deutsch**.
Oberflächentexte im Code auf **Englisch** über `i18n()` (Übersetzung via KI18n folgt).

## Bauen, Testen, Starten

```sh
sudo dev/install-deps-debian.sh            # Abhängigkeiten (Debian 13), braucht Root
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
build/bin/fjordpost --demo --offline --data-dir /tmp/fjordpost-demo
```

- Entwicklung unter WSL2 (Debian 13, WSLg), Tests später auf CachyOS + Plasma.
- Arch/CachyOS: `kimap` enthält `KPim6::kimaptest6` nicht, `imapsynctest` wird dort deshalb nicht gebaut.
- Kein passwortloses `sudo`: Paketinstallation und Docker führt der Nutzer selbst aus
  (im Claude-Code-Prompt mit `! <befehl>`).
- Der Build muss ohne Compiler-Warnungen durchlaufen (ECM-Warnflags sind aktiv).
- Testserver: `sudo docker compose -f dev/docker-compose.yml up -d` (GreenMail,
  Konten `max@fjordpost.test`/`anna@fjordpost.test`, Passwort `geheim`; IMAP 3143, SMTP 3025).
  Integrationstest dagegen: `FJORDPOST_TEST_SERVER=127.0.0.1 build/bin/servertest`
  (ohne die Variable wird er übersprungen). GreenMail akzeptiert jedes Passwort und hat
  anfangs nur `INBOX`. Radicale (CalDAV/CardDAV) läuft im selben Compose auf Port 5232, Konto `max`/`geheim`;
  Test dagegen: `FJORDPOST_TEST_DAV=http://127.0.0.1:5232 build/bin/davtest realServer`. POP3 läuft auf 3110; TLS (3995) lehnt der Client wegen des selbst signierten
  Zertifikats ab, der Servertest prüft genau das.

### Entwickleroptionen von `fjordpost`

| Option | Zweck |
|---|---|
| `--demo` | leere Datenbank mit Beispielkonten/-mails füllen |
| `--offline` | keine Serververbindungen |
| `--data-dir <dir>` | eigenes Datenverzeichnis (echte Daten nie für Tests nutzen) |
| `--screenshot <png>` | Fenster nach 3 s als Bild speichern und beenden (startet auch neben laufender Instanz) |
| `--dev-action <a>` | vor dem Screenshot: `open-first`, `reply`, `compose`, `compose-window`, `setup`, `settings`, `light`, `undo-send`, `open-thread`, `expand-thread`, `thread-history`, `label-menu`, `label-dialog`, `label-filter`, `snooze-menu`, `snooze-date`, `snoozed`, `filter-rules`, `filter-rule`, `compose-completion`, `setup-oauth`, `calendar`, `calendar-week`, `calendar-event`, `collection-new`, `collection-edit`, `collection-delete`, `contacts`, `contact-edit`, `contact-transfer`, `tasks`, `task-edit`, `dav-account`, `invitation` |

Oberflächenänderungen immer per `--screenshot` prüfen und die Konsolenausgabe auf
QML-Warnungen kontrollieren. Achtung: `--dev-action light` speichert den hellen Modus in
`~/.config/fjordblau.de/fjordpost.conf` – danach wieder `dark=true` setzen.

## Architektur

```
src/
  core/         Account, AccountManager (DB), SecretStore (QtKeychain → KWallet)
  auth/         OAuthProvider (Google/Microsoft, Client-ID aus fjordpost.conf), OAuthClient (Browser-Anmeldung
                über localhost + PKCE, Token-Erneuerung), OAuthTokens (Zugriffstokens im Speicher, Refresh-Token im SecretStore)
  storage/      Database (Migrationen, FTS5), BlobStore (.eml-Dateien), MailStore (Sync-DB-Zugriffe),
                LabelStore (Labels, auf dem Server als IMAP-Schlüsselwörter), FilterStore (Filterregeln),
                DavStore (Konten, Sammlungen, Ressourcen für DAV), messageflags.h (Flag-Bitmaske, Ordnerrollen), demodata
  dav/          DavClient (WebDAV auf QNetworkAccessManager), DavDiscovery (Home-Sets, .well-known, SRV),
                DavSync (Abgleich eines Kontos), DavService (alle Konten, Timer, Erinnerungen)
  pim/          pimdata (iCalendar/vCard ↔ Felder, Serien auflösen), itip (Einladungen, RFC 5546)
  mail/
    imap/       ImapConnection (Login + serielle Task-Queue), ImapAccount (Sync, IDLE, Offline-Ops)
    pop3/       Pop3Client (Protokoll), Pop3Account (Abruf; Ordner nur lokal, Nachrichten mit uid 0)
    net/        LineSocket (zeilenweise TCP/TLS-Verbindung für SMTP und POP3)
    smtp/       SmtpSender (eigener SMTP-Client: STARTTLS, AUTH PLAIN/LOGIN/XOAUTH2)
    mime/       MessageParser / MessageBuilder (KMime), Mbox (mboxrd lesen/schreiben)
    autoconfig/ AutoConfig (Anbieter-XML, .well-known, ISPDB, MX, Raten)
    mailservice MailService: koordiniert alle Konten, Postausgang, Snooze-Wecker
    filterengine FilterEngine: wendet Regeln auf neue Nachrichten an (nur Kopfzeilen-Felder)
    mailtransfer MailTransfer: Import/Export (.eml, mbox) nachrichtenweise im Hintergrund
  models/       AccountModel, FolderModel, MessageListModel, ContactModel (für QML)
  integration/  AppController (QML-Fassade "App"), PimController ("App.pim"), AccountSetup, Notifier (KNotification,
                Tray, Taskleisten-Zähler), RemoteContentBlocker (WebEngine)
  qml/          Kirigami-Oberfläche; Theme.qml (Singleton mit Design-Tokens), components/
tests/          QtTest; IMAP gegen KIMAPs FakeServer (KPim6::kimaptest6)
dev/            Abhängigkeits-Skript, docker-compose mit Testserver
```

Grundprinzipien:
- **Offline zuerst:** Benutzeraktionen ändern sofort die lokale DB und legen eine Operation in
  `pending_ops` ab; `ImapAccount::flushPendingOps()` schickt sie zum Server. Beim Abgleich werden
  UIDs mit ausstehenden Operationen nicht vom Serverzustand überschrieben.
- **Drei IMAP-Verbindungen pro Konto:** Sync (Hintergrund), interaktiv (Mail öffnen, Ops,
  APPEND), IDLE. Aufgaben laufen seriell pro Verbindung (`ImapConnection::enqueue`).
- **Roh-Mails** liegen im BlobStore, die DB enthält Metadaten, Vorschau und Volltext (FTS5).
- **DB-Schema** nur über neue Einträge in `migrations[]` (`storage/database.cpp`) ändern,
  bestehende Migrationen nie anfassen. Jeder Thread nutzt `Database::connection()`.
- **QML ↔ C++** nur über den Singleton `App` (`de.fjordblau.fjordpost.core`) und dessen Modelle.
- **Snooze ist rein lokal** (`snooze_until`): die Nachricht bleibt auf dem Server unverändert; beim
  Aufwecken wird sie ungelesen und `woken_at` sortiert sie nach oben. Neue Post in der Konversation weckt
  ebenfalls (nicht eigene Mails, nicht in Papierkorb/Spam gefilterte). Verschieben beendet Snooze.
- **Filterregeln** laufen in `MailService::handleArrived()` auf neuen Posteingang-Nachrichten
  (Änderungen wie Benutzeraktionen über `pending_ops`); Flags vor dem Verschieben, das die UID ungültig macht.
- **POP3:** `uid = 0`, Inhalt immer im BlobStore; `pop3_uids` merkt abgerufene Nachrichten. Erst sichern,
  dann vermerken, dann `DELE`. `uid > 0` ist überall die Bedingung für Serveroperationen.
- **CalDAV/CardDAV:** eigener Client statt KDAV (kein KIO, Bearer-Token für M4 einfach). Eine Zeile in
  `dav_items` ist eine Ressource; `raw` ist die Wahrheit, alle anderen Spalten leitet `pim/pimdata` daraus ab.
  Beim Bearbeiten wird `raw` geändert, nicht neu erzeugt (unbekannte Eigenschaften bleiben). Offline zuerst über
  `dav_items.pending`: `DavSync` sendet erst und vergleicht dann per CTag/ETag; bei `412` gewinnt der Server.
  Serien liegen als eine Ressource vor und werden nur als Ganzes bearbeitet. Einladungen gehen per Mail
  (iTIP), außer der Server meldet `calendar-auto-schedule`. Sammlungen anlegen, bearbeiten und löschen geht direkt
  zum Server und wird nicht vorgemerkt: Er darf ablehnen, und das muss sofort sichtbar sein. Kalender tragen nur `VEVENT`,
  Aufgabenlisten nur `VTODO`. Verschieben ist Neuanlegen plus Löschen (Original zuerst, gleiche UID),
  Kopieren vergibt eine neue UID und entfernt Teilnehmer; Termine mit Gästen werden bei `calendar-auto-schedule` nicht
  verschoben, sonst bekämen alle Absage und Einladung. Farben: `calendar-color`; für Adressbücher gibt es nur die
  Erweiterung `addressbook-color`, lehnt der Server sie ab, gilt die Farbe lokal. Google läuft in M4 über einen eigenen Weg.
- **Daten von außen in QML** (Termintitel, Kontaktnamen, Einladungen) nur mit `textFormat: Text.PlainText`:
  `Label` rendert sonst HTML, ein Titel könnte externe Bilder nachladen.
- **Passwörter/Tokens** nur über `SecretStore`, nie in DB oder Konfiguration.
- **OAuth2 (Google, Microsoft):** eigener Ablauf statt Qt NetworkAuth (API ändert sich zwischen 6.8 und 6.9).
  Client-ID/-Secret stehen in `fjordpost.conf`, Gruppe `[OAuth]` (Vorlage mit Anleitung: `dev/fjordpost.conf.example`),
  nie im Repo. Im SecretStore liegt nur das Refresh-Token; `accounts.oauth_provider` bzw. `dav_accounts.oauth_provider`
  nennt den Anbieter. IMAP, SMTP und DAV holen sich vor jeder Anmeldung bzw. Anfrage über eine `TokenSource` ein
  frisches Zugriffstoken (`OAuthTokens::fetcher`), nach Ablehnung wird es verworfen. Microsoft erneuert das
  Refresh-Token bei jeder Verwendung, es wird dann neu gespeichert. Microsoft hat kein CalDAV/CardDAV, dort nur Mail.

## Code-Konventionen

- Stil wie der umgebende Code: KDE/Qt-Idiome, `QStringLiteral`, `Q_SIGNALS`/`Q_EMIT`
  (`QT_NO_KEYWORDS`), kein `Q_FOREACH`, `std::as_const`, Logging über `Q_LOGGING_CATEGORY`
  (`fjordpost.*`).
- Neue `.cpp` in `src/CMakeLists.txt` (Bibliothek `fjordpostcore`), neue QML-Dateien in
  `ecm_target_qml_sources` eintragen (Komponenten mit `PATH components`).
- Kommentare sparsam und auf Deutsch; sie erklären das *Warum*.

## Bekannte Fallstricke (bereits einmal passiert)

- **Null-`QString` wird als SQL-`NULL` gebunden** und verletzt `NOT NULL`. Auch
  `QString::fromUtf8(QByteArray())` ist null. Beim Binden `nonNull()`/`utf8()` aus
  `mailstore.cpp` bzw. `accountmanager.cpp` verwenden.
- **KIMAP liefert CRLF**, KMime erwartet LF: Kopfzeilen mit
  `MessageParser::parseHeaders(rawHead)` bzw. `KMime::CRLFtoLF` parsen.
- **IMAP-Login** mit `LoginJob::ClearText` (`LOGIN`), nicht `Plain` – `AUTH=PLAIN` ist nicht
  überall vorhanden. Erwartete Befehle im FakeServer-Test exakt so, wie KIMAP sie sendet.
- **QML-Eigenschaften dürfen nicht mit `on` + Großbuchstabe beginnen** (`onAccent` wird als
  Signal-Handler interpretiert).
- **Keine QML-Eigenschaft `data` in eigenen Items** – sie überdeckt die Standard-Eigenschaft
  für Kindelemente, das Item bleibt leer (Composer heißt deshalb `draft`).
- **Dialoge/Popups:** Kirigami setzt dort das Farbschema bewusst zurück; Overrides am Fenster
  wirken nicht. Die Farben kommen über ein generiertes KDE-Farbschema
  (`integration/colorscheme.cpp`, aufgerufen aus `Main.qml`), Dialoge erben von
  `components/ThemedDialog.qml`.
- `Kirigami.Avatar` gibt es in KF6 nicht mehr → `org.kde.kirigamiaddons.components` oder
  `components/InitialsAvatar.qml`.
- **Abschnittsköpfe einer ListView** (`section.delegate`) werden wiederverwendet: keine
  `required property section`, sie behielte den alten Wert – Kontext-Eigenschaft `section` lesen.
- In einem `MenuItem` verdeckt dessen Eigenschaft `menu` eine gleichnamige ID des Menüs
  (Menü daher nie `id: menu` nennen).
- Unicode-Property-Escapes (`\p{L}`) in QML-JavaScript funktionieren nicht.
- `toString()`-Überladungen für eigene Enums kollidieren mit `QCOMPARE` (ADL) → eindeutige Namen.
- **KSMTP wird nicht verwendet:** wertet auf mehrere TCP-Pakete verteilte Antwortzeilen
  falsch aus (GreenMail) und braucht Cyrus-SASL-Plugins. Eigener Client in `mail/smtp`.
- Fehlende Standardordner (Gesendet, Entwürfe, Papierkorb, Archiv, Spam) legt
  `MailStore::ensureFolderForRole()` lokal an und reiht eine `create`-Operation ein –
  nie `folderIdForRole()` für Ziele von Verschieben/Ablegen verwenden.
- Das Breeze-Icon-Theme unter WSL (5.116) ist älter; Icon-Namen vor Verwendung prüfen
  (`find /usr/share/icons/breeze -name '<name>.svg'`).

- `DatePopup` aus kirigamiaddons meldet massenhaft `DatePicker.qml:570: Unable to assign double to
  QDateTime` und ist im Theme schlecht lesbar: nicht verwenden, stattdessen `components/DateTimeField.qml`.
- **Mehrere Datenbanken im selben Test** gehen nicht: `Database::connection()` gilt pro Thread und behält die
  erste Datei. In Tests eine Datenbank verwenden und leeren.
- **`Repeater` direkt im `Kirigami.FormLayout`** sortiert seine Zeilen ans Ende: in ein `ColumnLayout` legen.
- `PROPPATCH` meldet Ablehnung als `207` mit dem Status pro Eigenschaft: `ok()` reicht dort nicht. `403` heißt bei Sammlungen
  „nicht erlaubt“, nicht „falsches Passwort“.
- `QNetworkReply::header(LocationHeader)` ist bei relativem `Location` leer: Umleitungen über `rawHeader("Location")` auflösen.
- Die Datei-URL einer neuen DAV-Ressource nie aus der UID bilden; bei Einladungen kommt sie von außen.
- `QQmlComponent: Cannot create new component instance before completing the previous` beim Start ist nicht neu
  (auch ohne M3) und bisher ohne sichtbare Folgen.
- mbox-Import: ohne Statuskopfzeile gilt eine Nachricht als gelesen; `Mbox::entry` schreibt deshalb immer `Status`.
- `KStatusNotifierItem` hält die Event-Schleife am Leben: Die App endet nicht mit dem letzten Fenster, Beenden geht nur
  ausdrücklich (`Qt.quit()`, `Prefs.general.closeToTray`). `grabWindow()` auf ein verborgenes Fenster stürzt ab.
- **SecretStore-Schlüssel** (`account-N`, `dav-N`) hängen nicht am Datenverzeichnis: Ein Konto in einem frischen
  `--data-dir` bekommt wieder `account-1` und überschreibt in KWallet das Passwort des ersten echten Kontos.
  Mit `--data-dir` nur offline oder mit `FakeSecretStore` testen.
- Die Rückkehr der Browser-Anmeldung geht an `http://localhost:<port>`; der Browser löst `localhost` mal nach IPv4,
  mal nach IPv6 auf, `OAuthClient` lauscht deshalb auf beiden.
- `Settings`-Kategorie nie `General` nennen: `QSettings` behandelt die Gruppe gesondert (`[General]` sind Schlüssel ohne Gruppe).

## Design

Die Oberfläche folgt dem Entwurf `mopmail_ui.html` des Projektinhabers:
- Farben ausschließlich über `Theme.qml` (dunkles „Obsidian“-Theme als Standard, hell,
  Akzentfarben indigo/emerald/rose/amber/cyan; Option „KDE-Farbschema verwenden“).
- Die KWin-Fensterleiste bleibt; Logo, Suche (`Strg+K`) und Sync-Status stehen in der
  Kopfleiste im Fenster. Keine nachgebauten Fensterknöpfe.
- Breeze-Icons und Systemschrift statt Lucide/Inter (KDE-Integration).
- Einstellungen: Darstellung in `Theme.settings`, übrige in `Prefs` (`qml/Prefs.qml`, QtCore
  `Settings`, Datei `~/.config/fjordblau.de/fjordpost.conf`). Der Composer öffnet je nach
  `Prefs.compose.openIn` im Hauptfenster (`ComposerForm` statt Lesebereich) oder als
  `ComposerWindow`.
- Funktionen ohne Backend werden als ehrliche Platzhalter gezeigt, nie mit erfundenen Daten.

## Meilensteine

| | Inhalt | Stand |
|---|---|---|
| M0 | Grundgerüst, DB, Oberfläche mit Demo-Daten | fertig |
| M1 | IMAP + SMTP, Kontoeinrichtung, Lesen/Schreiben, Design-Umbau | fertig, Servertest grün; manueller Oberflächentest offen |
| M2 | POP3, Threading, Snooze, Labels, Filterregeln, Signaturen, Senden rückgängig, Import/Export | fertig, Servertest grün (auch POP3, Filter, Import/Export); manueller Oberflächentest offen |
| M3 | CardDAV/CalDAV, Kalender, Kontakte, Aufgaben, Einladungen | Code fertig, Test gegen Radicale grün; manueller Oberflächentest offen |
| M4 | OAuth2 für Google (Gmail, Kontakte, Kalender) und Microsoft (Mail) | Google mit echtem Konto getestet; Microsoft ungetestet (braucht ein Entra-Verzeichnis, s. `dev/fjordpost.conf.example`) |
| M5 | OpenPGP + S/MIME (QGpgME) | geplant |
| M6 | Übersetzung, Einstellungen, Feinschliff, Flatpak/PKGBUILD | geplant |

## Backlog

Vom Projektinhaber für später vorgemerkt, noch keinem Meilenstein fest zugeordnet:

- **Farbschema und Lesbarkeit überarbeiten:** Die Lesbarkeit ist insgesamt nicht ideal.
- **Download-Zeitraum in den Einstellungen:** wählbar, wie viel vom Server geladen wird
  (X Tage/Wochen/Monate oder alles). Bisher fest: die neuesten `ImapAccount::initialHeaderLimit`
  Kopfzeilen je Ordner und `prefetchCount` Inhalte im Posteingang.

- **Kalender ohne Server:** Bisher braucht jeder Kalender, jede Aufgabe und jeder Kontakt ein DAV-Konto;
  ein rein lokaler Speicher fehlt noch.
- **Einzelne Termine einer Serie bearbeiten:** Bisher wirkt jede Änderung auf die ganze Serie.
- **Mails standardmäßig nur als Text anzeigen:** Einstellung, die statt des HTML-Teils den Textteil zeigt.

## Git

- Branch `main`; Commit-Nachrichten auf Deutsch, erste Zeile kurz, danach Stichpunkte.
- Nur committen bzw. pushen, wenn der Projektinhaber es möchte.
- `build/` und Windows-`Zone.Identifier`-Dateien sind ignoriert.

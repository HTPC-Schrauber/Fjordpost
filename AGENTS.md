# AGENTS.md – Hinweise für KI-Agenten und Mitwirkende

MopMail ist ein E-Mail-Client für KDE Plasma im Stil von Mailbird.
C++20 · Qt 6 · KDE Frameworks 6 · Kirigami/QML · eigene SQLite-Engine (kein Akonadi).

Kommunikation mit dem Projektinhaber, Commit-Nachrichten und Code-Kommentare auf **Deutsch**.
Oberflächentexte im Code auf **Englisch** über `i18n()` (Übersetzung via KI18n folgt).

## Bauen, Testen, Starten

```sh
sudo dev/install-deps-debian.sh            # Abhängigkeiten (Debian 13), braucht Root
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
build/bin/mopmail --demo --offline --data-dir /tmp/mopmail-demo
```

- Entwicklung unter WSL2 (Debian 13, WSLg), Tests später auf CachyOS + Plasma.
- Kein passwortloses `sudo`: Paketinstallation und Docker führt der Nutzer selbst aus
  (im Claude-Code-Prompt mit `! <befehl>`).
- Der Build muss ohne Compiler-Warnungen durchlaufen (ECM-Warnflags sind aktiv).
- Testserver: `sudo docker compose -f dev/docker-compose.yml up -d` (GreenMail,
  Konten `max@mopmail.test`/`anna@mopmail.test`, Passwort `geheim`; IMAP 3143, SMTP 3025).

### Entwickleroptionen von `mopmail`

| Option | Zweck |
|---|---|
| `--demo` | leere Datenbank mit Beispielkonten/-mails füllen |
| `--offline` | keine Serververbindungen |
| `--data-dir <dir>` | eigenes Datenverzeichnis (echte Daten nie für Tests nutzen) |
| `--screenshot <png>` | Fenster nach 3 s als Bild speichern und beenden |
| `--dev-action <a>` | vor dem Screenshot: `open-first`, `reply`, `compose`, `setup`, `light` |

Oberflächenänderungen immer per `--screenshot` prüfen und die Konsolenausgabe auf
QML-Warnungen kontrollieren. Achtung: `--dev-action light` speichert den hellen Modus in
`~/.config/kde.org/mopmail.conf` – danach wieder `dark=true` setzen.

## Architektur

```
src/
  core/         Account, AccountManager (DB), SecretStore (QtKeychain → KWallet)
  storage/      Database (Migrationen, FTS5), BlobStore (.eml-Dateien), MailStore (Sync-DB-Zugriffe),
                messageflags.h (Flag-Bitmaske, Ordnerrollen), demodata
  mail/
    imap/       ImapConnection (Login + serielle Task-Queue), ImapAccount (Sync, IDLE, Offline-Ops)
    smtp/       SmtpSender (KSMTP)
    mime/       MessageParser / MessageBuilder (KMime)
    autoconfig/ AutoConfig (Anbieter-XML, .well-known, ISPDB, MX, Raten)
    mailservice MailService: koordiniert alle Konten, Postausgang
  models/       AccountModel, FolderModel, MessageListModel (für QML)
  integration/  AppController (QML-Fassade "App"), AccountSetup, Notifier (KNotification,
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
- **QML ↔ C++** nur über den Singleton `App` (`org.mopmail.core`) und dessen Modelle.
- **Passwörter/Tokens** nur über `SecretStore`, nie in DB oder Konfiguration.

## Code-Konventionen

- Stil wie der umgebende Code: KDE/Qt-Idiome, `QStringLiteral`, `Q_SIGNALS`/`Q_EMIT`
  (`QT_NO_KEYWORDS`), kein `Q_FOREACH`, `std::as_const`, Logging über `Q_LOGGING_CATEGORY`
  (`mopmail.*`).
- Neue `.cpp` in `src/CMakeLists.txt` (Bibliothek `mopmailcore`), neue QML-Dateien in
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
- `Kirigami.Avatar` gibt es in KF6 nicht mehr → `org.kde.kirigamiaddons.components` oder
  `components/InitialsAvatar.qml`.
- Unicode-Property-Escapes (`\p{L}`) in QML-JavaScript funktionieren nicht.
- `toString()`-Überladungen für eigene Enums kollidieren mit `QCOMPARE` (ADL) → eindeutige Namen.
- Das Breeze-Icon-Theme unter WSL (5.116) ist älter; Icon-Namen vor Verwendung prüfen
  (`find /usr/share/icons/breeze -name '<name>.svg'`).

## Design

Die Oberfläche folgt dem Entwurf `mopmail_ui.html` des Projektinhabers:
- Farben ausschließlich über `Theme.qml` (dunkles „Obsidian“-Theme als Standard, hell,
  Akzentfarben indigo/emerald/rose/amber/cyan; Option „KDE-Farbschema verwenden“).
- Die KWin-Fensterleiste bleibt; Logo, Suche (`Strg+K`) und Sync-Status stehen in der
  Kopfleiste im Fenster. Keine nachgebauten Fensterknöpfe.
- Breeze-Icons und Systemschrift statt Lucide/Inter (KDE-Integration).
- Funktionen ohne Backend werden als ehrliche Platzhalter gezeigt, nie mit erfundenen Daten.

## Meilensteine

| | Inhalt | Stand |
|---|---|---|
| M0 | Grundgerüst, DB, Oberfläche mit Demo-Daten | fertig |
| M1 | IMAP + SMTP, Kontoeinrichtung, Lesen/Schreiben, Design-Umbau | Code fertig, Test gegen echten Server offen |
| M2 | POP3, Threading, Snooze, Labels, Filterregeln, Signaturen, Senden rückgängig, Import/Export | geplant |
| M3 | CardDAV/CalDAV, Kalender, Kontakte, Aufgaben, Einladungen | geplant |
| M4 | Google OAuth2 (Gmail, Kontakte, Kalender) | geplant |
| M5 | OpenPGP + S/MIME (QGpgME) | geplant |
| M6 | Übersetzung, Einstellungen, Feinschliff, Flatpak/PKGBUILD | geplant |

## Git

- Branch `main`; Commit-Nachrichten auf Deutsch, erste Zeile kurz, danach Stichpunkte.
- Nur committen bzw. pushen, wenn der Projektinhaber es möchte.
- `build/` und Windows-`Zone.Identifier`-Dateien sind ignoriert.

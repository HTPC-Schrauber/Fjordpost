# MopMail

Ein moderner E-Mail-Client für KDE Plasma im Stil von Mailbird.
C++20 · Qt 6 · KDE Frameworks 6 · Kirigami

## Status

| Meilenstein | Inhalt | Stand |
|---|---|---|
| M0 | Grundgerüst, Datenbank, Mailbird-Oberfläche mit Demo-Daten | ✅ |
| M1 | IMAP + SMTP, Kontoeinrichtung, Lesen/Schreiben | geplant |
| M2 | POP3, Unified Inbox, Suche, Threading, Filter | geplant |
| M3 | CardDAV / CalDAV | geplant |
| M4 | Google (OAuth2, Gmail, Kontakte, Kalender) | geplant |
| M5 | OpenPGP + S/MIME | geplant |
| M6 | Feinschliff, Packaging (Flatpak, PKGBUILD) | geplant |

## Bauen

Debian 13 / WSL2:

```sh
sudo dev/install-deps-debian.sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Starten mit Beispieldaten (eigenes Datenverzeichnis, damit echte Daten unberührt bleiben):

```sh
build/bin/mopmail --demo --data-dir /tmp/mopmail-demo
```

## Tastenkürzel

| Taste | Aktion |
|---|---|
| `/` oder `Strg+F` | Suche |
| `↑` / `↓` | Vorherige/nächste Nachricht |
| `E` | Archivieren |
| `Entf` | Löschen |
| `S` | Markieren (Fähnchen) |
| `U` | Als ungelesen markieren |

## Architektur

- `src/core` – Konten, Geheimnisse (KWallet über QtKeychain)
- `src/storage` – SQLite mit Migrationen und FTS5-Volltextsuche
- `src/models` – Qt-Modelle für die QML-Oberfläche
- `src/integration` – App-Controller, KDE-Integration
- `src/qml` – Kirigami-Oberfläche

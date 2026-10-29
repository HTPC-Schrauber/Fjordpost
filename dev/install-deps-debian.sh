#!/usr/bin/env bash
# Build- und Laufzeitabhängigkeiten für Fjordpost unter Debian 13 (trixie).
set -euo pipefail
apt-get update
apt-get install -y --no-install-recommends \
  build-essential cmake ninja-build extra-cmake-modules gettext pkg-config git \
  qt6-base-dev qt6-declarative-dev qt6-declarative-private-dev qt6-webengine-dev qt6-networkauth-dev qt6-svg-dev qt6-svg-plugins qt6-tools-dev \
  libqt6sql6-sqlite \
  qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qtwebengine \
  qml6-module-qt-labs-platform \
  libkirigami-dev qml6-module-org-kde-kirigami qml6-module-org-kde-desktop qml6-module-org-kde-coreaddons \
  libkf6i18n-dev libkf6config-dev libkf6colorscheme-dev libkf6coreaddons-dev libkf6notifications-dev \
  libkf6dbusaddons-dev libkf6statusnotifieritem-dev libkf6crash-dev libkf6windowsystem-dev \
  libkf6contacts-dev libkf6calendarcore-dev libkf6kio-dev \
  libkimap-dev libkmime-dev \
  libqgpgmeqt6-dev libgpgmepp-dev qtkeychain-qt6-dev \
  kirigami-addons-dev qml6-module-org-kde-kirigamiaddons-components qml6-module-org-kde-kirigamiaddons-formcard \
  qml6-module-org-kde-kirigamiaddons-delegates qml6-module-org-kde-kirigamiaddons-datetime \
  qt6-wayland breeze-icon-theme gnupg sqlite3

cmake_minimum_required(VERSION 3.22)

project(mopmail VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMQmlModule)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

ecm_setup_version(${PROJECT_VERSION}
    VARIABLE_PREFIX MOPMAIL
    VERSION_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src/mopmail-version.h")

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Network Widgets Test)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons I18n Config DBusAddons Crash Kirigami)
find_package(Qt6Keychain REQUIRED)
find_package(KPim6Mime REQUIRED)
find_package(KPim6IMAP REQUIRED)
find_package(KPim6SMTP REQUIRED)

add_subdirectory(src)

if(BUILD_TESTING)
    # Die Testbibliothek von KIMAP (FakeServer) hängt von KIO ab.
    find_package(KF6KIO ${KF_MIN_VERSION} REQUIRED)
    add_subdirectory(tests)
endif()

install(FILES data/org.mopmail.MopMail.desktop DESTINATION ${KDE_INSTALL_APPDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

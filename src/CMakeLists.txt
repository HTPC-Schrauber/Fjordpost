# Kernbibliothek: alles außer main(), damit Tests sie linken können.
add_library(fjordpostcore STATIC
    core/account.cpp
    core/accountmanager.cpp
    core/secretstore.cpp
    storage/blobstore.cpp
    storage/database.cpp
    storage/demodata.cpp
    storage/mailstore.cpp
    mail/autoconfig/autoconfig.cpp
    mail/imap/imapaccount.cpp
    mail/imap/imapconnection.cpp
    mail/mailservice.cpp
    mail/mime/messagebuilder.cpp
    mail/mime/messageparser.cpp
    mail/net/linesocket.cpp
    mail/smtp/smtpsender.cpp
    models/accountmodel.cpp
    models/foldermodel.cpp
    models/messagelistmodel.cpp
    integration/accountsetup.cpp
    integration/appcontroller.cpp
    integration/colorscheme.cpp
    integration/notifier.cpp
)
target_include_directories(fjordpostcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(fjordpostcore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql Qt6::Network Qt6::DBus
    KF6::CoreAddons KF6::I18n KF6::ConfigCore KF6::ColorScheme KF6::Notifications KF6::StatusNotifierItem
    Qt6Keychain::Qt6Keychain
    KPim6::Mime KPim6::IMAP
)

add_executable(fjordpost main.cpp integration/remotecontentblocker.cpp)

ecm_add_qml_module(fjordpost URI de.fjordblau.fjordpost.app GENERATE_PLUGIN_SOURCE)
set_source_files_properties(qml/Theme.qml qml/Prefs.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
ecm_target_qml_sources(fjordpost SOURCES
    qml/Theme.qml
    qml/Prefs.qml
    qml/HeaderBar.qml
    qml/ContextDrawer.qml
    qml/Main.qml
    qml/AppRail.qml
    qml/FolderPane.qml
    qml/MessageListPane.qml
    qml/MessageDelegate.qml
    qml/ReaderPane.qml
    qml/RailButton.qml
    qml/ComposerForm.qml
    qml/ComposerWindow.qml
    qml/SettingsDialog.qml
    qml/AccountSetupDialog.qml
)
ecm_target_qml_sources(fjordpost PATH components SOURCES
    qml/components/AccentButton.qml
    qml/components/IconButton.qml
    qml/components/InitialsAvatar.qml
    qml/components/Kbd.qml
    qml/components/Pill.qml
    qml/components/SearchBox.qml
    qml/components/SectionLabel.qml
    qml/components/Tag.qml
    qml/components/ThemedDialog.qml
    qml/components/Toast.qml
)

target_link_libraries(fjordpost PRIVATE
    fjordpostcore
    Qt6::Quick Qt6::QuickControls2 Qt6::Widgets Qt6::WebEngineQuick
    KF6::DBusAddons KF6::Crash KF6::I18nQml
)

install(TARGETS fjordpost ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

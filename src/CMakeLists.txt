# Kernbibliothek: alles außer main(), damit Tests sie linken können.
add_library(mopmailcore STATIC
    core/account.cpp
    core/accountmanager.cpp
    core/secretstore.cpp
    storage/database.cpp
    storage/demodata.cpp
    models/accountmodel.cpp
    models/foldermodel.cpp
    models/messagelistmodel.cpp
    integration/appcontroller.cpp
)
target_include_directories(mopmailcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mopmailcore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Sql Qt6::Network
    KF6::CoreAddons KF6::I18n KF6::ConfigCore
    Qt6Keychain::Qt6Keychain
)

add_executable(mopmail main.cpp)

ecm_add_qml_module(mopmail URI org.mopmail.app GENERATE_PLUGIN_SOURCE)
ecm_target_qml_sources(mopmail SOURCES
    qml/Main.qml
    qml/AppRail.qml
    qml/FolderPane.qml
    qml/MessageListPane.qml
    qml/MessageDelegate.qml
    qml/ReaderPane.qml
    qml/RailButton.qml
)

target_link_libraries(mopmail PRIVATE
    mopmailcore
    Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
    KF6::DBusAddons KF6::Crash KF6::I18nQml
)

install(TARGETS mopmail ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

qt_add_executable(danmaku_app WIN32 ../src/app/main.cpp)
set_target_properties(danmaku_app PROPERTIES OUTPUT_NAME DanmaX)
danmax_attach_icon(danmaku_app)
target_include_directories(danmaku_app PRIVATE "${CMAKE_BINARY_DIR}/generated")
if(DANMAKU_STATIC_QT)
    target_compile_definitions(danmaku_app PRIVATE DANMAKU_STATIC_PORTABLE)
    qt_import_plugins(danmaku_app INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin Qt6::QSchannelBackendPlugin)
    qt_import_plugins(danmaku_app EXCLUDE_BY_TYPE generic networkinformation)
    target_link_options(danmaku_app PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/danmaku_app.map")
endif()
set_source_files_properties(theme/Ui.qml i18n/I18n.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(danmaku_app URI DanmaX VERSION 1.0
    QML_FILES Main.qml OverlayWindow.qml theme/Ui.qml i18n/I18n.qml
    components/PageFrame.qml components/Disclosure.qml
    components/AppComboBox.qml
    components/SettingsSection.qml components/SettingsRow.qml components/NavigationPane.qml components/FontSelector.qml
    components/ActionCard.qml components/WindowCaptionButtons.qml
    components/OnlineDanmakuPane.qml components/CachedDanmakuPane.qml
    pages/PlayerPage.qml pages/LogsPage.qml pages/SettingsPage.qml pages/AboutPage.qml)
target_link_libraries(danmaku_app PRIVATE danmaku_runtime Qt6::Quick Qt6::QuickControls2 QWindowKit::Quick)
danmaku_compile_options(danmaku_app)

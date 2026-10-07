qt_add_executable(danmaku_app WIN32 ../src/app/main.cpp)
if(DANMAKU_STATIC_QT)
    target_compile_definitions(danmaku_app PRIVATE DANMAKU_STATIC_PORTABLE)
    qt_import_plugins(danmaku_app INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
    qt_import_plugins(danmaku_app EXCLUDE_BY_TYPE generic networkinformation tls)
    target_link_options(danmaku_app PRIVATE "/MAP:${CMAKE_CURRENT_BINARY_DIR}/danmaku_app.map")
endif()
set_source_files_properties(theme/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(danmaku_app URI LocalDanmaku VERSION 1.0
    QML_FILES Main.qml OverlayWindow.qml theme/Ui.qml
    components/SettingsSection.qml components/SettingsRow.qml components/NavigationPane.qml
    pages/PlayerPage.qml pages/LogsPage.qml pages/SettingsPage.qml)
target_link_libraries(danmaku_app PRIVATE danmaku_runtime Qt6::Quick Qt6::QuickControls2)
danmaku_compile_options(danmaku_app)

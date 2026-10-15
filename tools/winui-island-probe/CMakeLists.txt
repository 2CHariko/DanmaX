if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "WinUI probe requires Windows x64 / MSVC.")
endif()
set(probe_deps "${PROJECT_SOURCE_DIR}/.deps/winui-island")
if(NOT EXISTS "${probe_deps}/prepared.json")
    message(FATAL_ERROR "Run scripts/prepare-winui-island.ps1 explicitly before configuring.")
endif()
file(READ "${probe_deps}/prepared.json" prepared)
string(JSON prepared_hash GET "${prepared}" lockSha256)
string(JSON prepared_root GET "${prepared}" projectRoot)
file(TO_CMAKE_PATH "${prepared_root}" prepared_root)
if(NOT prepared_root STREQUAL PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "Prepared WinUI projection moved; run the prepare script again.")
endif()
file(SHA256 "${PROJECT_SOURCE_DIR}/toolchain/winui-island.lock.json" expected_hash)
string(TOLOWER "${prepared_hash}" prepared_hash)
if(NOT prepared_hash STREQUAL expected_hash)
    message(FATAL_ERROR "WinUI dependency lock changed; run the prepare script again.")
endif()
if(DANMAKU_STATIC_QT)
    set(probe_qt "${PROJECT_SOURCE_DIR}/.tools/qt/6.11.0/msvc2022_64-static")
else()
    set(probe_qt "${PROJECT_SOURCE_DIR}/.tools/qt/6.11.0/msvc2022_64")
endif()
find_package(Qt6 6.11.0 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2
    PATHS "${probe_qt}/lib/cmake/Qt6" NO_DEFAULT_PATH)
qt_standard_project_setup(REQUIRES 6.11)
qt_add_executable(winui_island_probe WIN32 main.cpp IslandBridge.h)
qt_add_qml_module(winui_island_probe URI WinUIProbe QML_FILES Main.qml)
target_compile_definitions(winui_island_probe PRIVATE UNICODE _UNICODE NOMINMAX QT_NO_KEYWORDS)
target_compile_options(winui_island_probe PRIVATE /utf-8 /bigobj /EHsc)
target_include_directories(winui_island_probe PRIVATE
    "${probe_deps}/include"
    "${probe_deps}/packages/microsoft.windowsappsdk.foundation/include"
    "${probe_deps}/packages/microsoft.windowsappsdk.interactiveexperiences/include")
target_link_libraries(winui_island_probe PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    windowsapp ole32 psapi delayimp
    "${probe_deps}/packages/microsoft.windowsappsdk.foundation/lib/native/x64/Microsoft.WindowsAppRuntime.Bootstrap.lib"
    "${probe_deps}/packages/microsoft.windowsappsdk.interactiveexperiences/lib/native/win10-x64/Microsoft.UI.Dispatching.lib")
# Bootstrap must add the framework package before the Windowing DLL is resolved.
target_link_options(winui_island_probe PRIVATE /DELAYLOAD:Microsoft.UI.Windowing.Core.dll)
if(DANMAKU_STATIC_QT)
    qt_import_plugins(winui_island_probe INCLUDE Qt6::QWindowsIntegrationPlugin)
endif()
add_custom_command(TARGET winui_island_probe POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${probe_deps}/packages/microsoft.windowsappsdk.foundation/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
    "$<TARGET_FILE_DIR:winui_island_probe>")

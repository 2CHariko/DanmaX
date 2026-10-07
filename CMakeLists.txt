cmake_minimum_required(VERSION 3.31)
project(LocalDanmaku VERSION 0.2.0 LANGUAGES CXX)
if(MSVC AND DEFINED DANMAKU_MSVC_INCLUDES_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${DANMAKU_MSVC_INCLUDES_PREFIX}")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build under out/build.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include(CTest)
include(cmake/CompilerOptions.cmake)
add_library(danmaku_core STATIC src/core/PlaybackClock.cpp src/core/DanmakuEngine.cpp)
target_include_directories(danmaku_core PUBLIC src)
danmaku_compile_options(danmaku_core)

option(DANMAKU_BUILD_APP "Build the Qt Quick desktop application" ON)
if(DANMAKU_BUILD_APP)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The desktop application currently targets Windows/MSVC.")
    endif()
    find_package(Qt6 6.11.0 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2
        PATHS "${CMAKE_SOURCE_DIR}/.tools/qt/6.11.0/msvc2022_64/lib/cmake/Qt6" NO_DEFAULT_PATH)
    qt_standard_project_setup(REQUIRES 6.11)
    add_library(danmaku_runtime STATIC
        src/application/AppController.cpp src/application/AppController.h
        src/infrastructure/AppPaths.cpp src/infrastructure/AppPaths.h
        src/platform/windows/MediaMonitor.cpp src/platform/windows/MediaMonitor.h
        src/renderer/DanmakuItem.cpp src/renderer/DanmakuItem.h
        src/infrastructure/XmlLoader.cpp src/infrastructure/XmlLoader.h
        src/infrastructure/SettingsStore.cpp src/infrastructure/SettingsStore.h
        src/infrastructure/LogModel.cpp src/infrastructure/LogModel.h)
    target_link_libraries(danmaku_runtime PUBLIC danmaku_core Qt6::Core Qt6::Gui Qt6::Quick windowsapp psapi shell32 ole32)
    danmaku_compile_options(danmaku_runtime)
    qt_add_executable(danmaku_app WIN32 src/app/main.cpp)
    set_source_files_properties(qml/theme/Ui.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(danmaku_app URI LocalDanmaku VERSION 1.0
        QML_FILES qml/Main.qml qml/OverlayWindow.qml qml/theme/Ui.qml
        qml/components/SettingsSection.qml qml/components/SettingsRow.qml qml/components/NavigationPane.qml
        qml/pages/PlayerPage.qml qml/pages/LogsPage.qml qml/pages/SettingsPage.qml)
    target_link_libraries(danmaku_app PRIVATE danmaku_runtime Qt6::Quick Qt6::QuickControls2)
    danmaku_compile_options(danmaku_app)
    if(BUILD_TESTING)
        add_executable(runtime_tests tests/runtime/RuntimeTests.cpp)
        target_link_libraries(runtime_tests PRIVATE danmaku_runtime)
        danmaku_compile_options(runtime_tests)
        add_test(NAME runtime_services COMMAND runtime_tests "${CMAKE_SOURCE_DIR}/tests/fixtures/sample.xml")
        set_tests_properties(runtime_services PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_test(NAME application_smoke COMMAND danmaku_app --smoke-test
            --data-dir "${CMAKE_BINARY_DIR}/test-data" --cache-dir "${CMAKE_BINARY_DIR}/test-cache")
        set_tests_properties(application_smoke PROPERTIES TIMEOUT 20
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    endif()
endif()
if(BUILD_TESTING)
    add_executable(core_tests tests/core/PlaybackClockTests.cpp)
    target_link_libraries(core_tests PRIVATE danmaku_core)
    danmaku_compile_options(core_tests)
    add_test(NAME playback_clock COMMAND core_tests)
    add_executable(engine_tests tests/core/EngineTests.cpp)
    target_link_libraries(engine_tests PRIVATE danmaku_core)
    danmaku_compile_options(engine_tests)
    add_test(NAME danmaku_engine COMMAND engine_tests)
endif()

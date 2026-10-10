cmake_minimum_required(VERSION 3.31)
project(DanmaX VERSION 0.2.3 LANGUAGES CXX)
if(MSVC AND DEFINED DANMAKU_MSVC_INCLUDES_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${DANMAKU_MSVC_INCLUDES_PREFIX}")
endif()
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build under out/build.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(DANMAKU_STATIC_QT "Build the application with project-local static Qt and CRT" OFF)
if(DANMAKU_STATIC_QT)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
include(CTest)
include(cmake/CompilerOptions.cmake)
# ---------------------------------------------------------------------------
# Versioning & Build Metadata Configuration
# ---------------------------------------------------------------------------
find_package(Git QUIET)
if(NOT DEFINED DANMAX_BUILD_NUMBER OR DANMAX_BUILD_NUMBER STREQUAL "")
    set(DANMAX_BUILD_NUMBER 0)
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_rev_count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_git_rev_count MATCHES "^[0-9]+$")
            set(DANMAX_BUILD_NUMBER ${_git_rev_count})
        endif()
    endif()
endif()
if(NOT DEFINED DANMAX_GIT_HASH OR DANMAX_GIT_HASH STREQUAL "")
    set(DANMAX_GIT_HASH "local")
    if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            OUTPUT_VARIABLE _git_hash
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(_git_hash MATCHES "^[0-9a-fA-F]+$")
            set(DANMAX_GIT_HASH ${_git_hash})
        endif()
    endif()
endif()
if(NOT DEFINED DANMAX_DISPLAY_VERSION OR DANMAX_DISPLAY_VERSION STREQUAL "")
    set(DANMAX_DISPLAY_VERSION "${PROJECT_VERSION} (Build ${DANMAX_BUILD_NUMBER} · ${DANMAX_GIT_HASH})")
endif()
configure_file(
    "${CMAKE_SOURCE_DIR}/src/app/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)
add_library(danmaku_core STATIC src/core/PlaybackClock.cpp src/core/DanmakuEngine.cpp)
target_include_directories(danmaku_core PUBLIC src)
danmaku_compile_options(danmaku_core)

# Optional packaging-only launcher, built by package.ps1 in its own Release directory.
set(PORTABLE_CABINET "" CACHE FILEPATH "Embedded cabinet for the portable launcher")
set(PORTABLE_MANIFEST "" CACHE FILEPATH "SHA-256 file manifest for the portable launcher")
option(DANMAKU_BUILD_APP "Build the Qt Quick desktop application" ON)
if(WIN32 AND (DANMAKU_BUILD_APP OR PORTABLE_CABINET))
    enable_language(RC)
    include(cmake/AppIcon.cmake)
endif()
if(PORTABLE_CABINET)
    if(NOT WIN32 OR NOT MSVC OR NOT EXISTS "${PORTABLE_CABINET}" OR NOT EXISTS "${PORTABLE_MANIFEST}")
        message(FATAL_ERROR "Portable packaging requires Windows/MSVC and both payload files.")
    endif()
    enable_language(RC)
    set(PORTABLE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/portable")
    configure_file("${PORTABLE_SOURCE_DIR}/launcher.manifest.in" "${CMAKE_CURRENT_BINARY_DIR}/launcher.manifest" @ONLY)
    configure_file(src/portable/payload.rc.in portable-payload.rc @ONLY)
    add_executable(danmaku_portable WIN32 src/portable/main.cpp "${CMAKE_CURRENT_BINARY_DIR}/portable-payload.rc")
    danmax_attach_icon(danmaku_portable)
    set_target_properties(danmaku_portable PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreaded OUTPUT_NAME DanmaX)
    danmaku_compile_options(danmaku_portable)
    target_compile_definitions(danmaku_portable PRIVATE UNICODE _UNICODE)
    target_link_options(danmaku_portable PRIVATE /MANIFEST:NO)
    target_link_libraries(danmaku_portable PRIVATE cabinet bcrypt shell32)
endif()

if(DANMAKU_BUILD_APP)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "The desktop application currently targets Windows/MSVC.")
    endif()
    if(DANMAKU_STATIC_QT)
        set(danmaku_qt_directory "${CMAKE_SOURCE_DIR}/.tools/qt/6.11.0/msvc2022_64-static")
    else()
        set(danmaku_qt_directory "${CMAKE_SOURCE_DIR}/.tools/qt/6.11.0/msvc2022_64")
    endif()
    find_package(Qt6 6.11.0 EXACT REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network
        PATHS "${danmaku_qt_directory}/lib/cmake/Qt6" NO_DEFAULT_PATH)
    get_target_property(danmaku_qt_core_type Qt6::Core TYPE)
    if(DANMAKU_STATIC_QT AND NOT danmaku_qt_core_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR "Static application must use the project-local static Qt SDK.")
    endif()
    qt_standard_project_setup(REQUIRES 6.11)
    add_library(danmaku_runtime STATIC
        src/application/DanmakuLibrary.cpp src/application/DanmakuLibrary.h
        src/infrastructure/OnlineDanmakuData.cpp src/infrastructure/OnlineDanmakuData.h
        src/application/AppController.cpp src/application/AppController.h
        src/infrastructure/AppPaths.cpp src/infrastructure/AppPaths.h
        src/platform/windows/MediaMonitor.cpp src/platform/windows/MediaMonitor.h
        src/platform/windows/WindowBackdropController.cpp src/platform/windows/WindowBackdropController.h
        src/renderer/DanmakuItem.cpp src/renderer/DanmakuItem.h
        src/infrastructure/XmlLoader.cpp src/infrastructure/XmlLoader.h
        src/infrastructure/SettingsStore.cpp src/infrastructure/SettingsStore.h
        src/infrastructure/LogModel.cpp src/infrastructure/LogModel.h)
    target_link_libraries(danmaku_runtime PUBLIC danmaku_core Qt6::Core Qt6::Gui Qt6::Quick Qt6::Network windowsapp psapi shell32 ole32 dwmapi)
    file(GLOB fluent_icons CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/resources/fluent/*.png")
    qt_add_resources(danmaku_runtime fluent_icons PREFIX "/fluent"
        BASE "${CMAKE_SOURCE_DIR}/resources/fluent" FILES ${fluent_icons})
    add_dependencies(danmaku_runtime danmax_icon)
    qt_add_resources(danmaku_runtime app_branding PREFIX "/branding"
        BASE "${DANMAX_ICON_DIR}" FILES "${DANMAX_ICON_DIR}/DanmaX.png")
    danmaku_compile_options(danmaku_runtime)
    if(DANMAKU_STATIC_QT)
        target_compile_definitions(danmaku_runtime PRIVATE DANMAKU_STATIC_PORTABLE)
    endif()
    # Create the QML target within its own source directory so Qt's import scanner
    if(EXISTS "${CMAKE_SOURCE_DIR}/.deps/qwindowkit/CMakeLists.txt")
        set(QWINDOWKIT_BUILD_WIDGETS OFF CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_QUICK ON CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_STATIC ON CACHE BOOL "" FORCE)
        set(QWINDOWKIT_INSTALL OFF CACHE BOOL "" FORCE)
        set(QWINDOWKIT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_SOURCE_DIR}/.deps/qwindowkit" qwindowkit EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(qml)
    if(BUILD_TESTING)
        find_package(Qt6 6.11.0 EXACT REQUIRED COMPONENTS Test
            PATHS "${danmaku_qt_directory}/lib/cmake/Qt6" NO_DEFAULT_PATH)
        if(NOT DANMAKU_STATIC_QT)
            add_executable(ui_workflow_tests tests/runtime/UiWorkflowTests.cpp)
            target_link_libraries(ui_workflow_tests PRIVATE danmaku_runtime Qt6::QuickControls2 Qt6::Test QWindowKit::Quick)
            target_compile_definitions(ui_workflow_tests PRIVATE DANMAKU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
            file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/ui-source")
            file(GLOB_RECURSE ui_test_sources CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}/qml" "${CMAKE_SOURCE_DIR}/qml/*.qml")
            foreach(ui_source IN LISTS ui_test_sources)
                configure_file("${CMAKE_SOURCE_DIR}/qml/${ui_source}" "${CMAKE_BINARY_DIR}/ui-source/${ui_source}" COPYONLY)
            endforeach()
            file(WRITE "${CMAKE_BINARY_DIR}/ui-source/theme/qmldir" "singleton Ui 1.0 Ui.qml\n")
            file(WRITE "${CMAKE_BINARY_DIR}/ui-source/i18n/qmldir" "singleton I18n 1.0 I18n.qml\n")
            target_compile_definitions(ui_workflow_tests PRIVATE DANMAKU_UI_DIR="${CMAKE_BINARY_DIR}/ui-source")
            add_test(NAME ui_workflow COMMAND ui_workflow_tests -o "${CMAKE_BINARY_DIR}/ui-workflow-tests.txt,txt")
            set_tests_properties(ui_workflow PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QUICK_BACKEND=software")
        endif()
        add_executable(online_danmaku_tests tests/runtime/OnlineDanmakuTests.cpp)
        target_link_libraries(online_danmaku_tests PRIVATE danmaku_runtime Qt6::Test)
        if(DANMAKU_STATIC_QT)
            qt_import_plugins(online_danmaku_tests INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin Qt6::QSchannelBackendPlugin)
        endif()
        danmaku_compile_options(online_danmaku_tests)
        add_test(NAME online_danmaku COMMAND online_danmaku_tests -o "${CMAKE_BINARY_DIR}/online-danmaku-tests.txt,txt")
        set_tests_properties(online_danmaku PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        add_executable(xml_loader_tests tests/runtime/XmlLoaderTests.cpp
            src/infrastructure/XmlLoader.cpp)
        target_include_directories(xml_loader_tests PRIVATE src)
        target_link_libraries(xml_loader_tests PRIVATE Qt6::Core Qt6::Test)
        danmaku_compile_options(xml_loader_tests)
        add_test(NAME xml_loader COMMAND xml_loader_tests)
        set_tests_properties(xml_loader PROPERTIES TIMEOUT 120)
        add_executable(runtime_tests tests/runtime/RuntimeTests.cpp)
        target_link_libraries(runtime_tests PRIVATE danmaku_runtime)
        if(DANMAKU_STATIC_QT)
            qt_import_plugins(runtime_tests INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
        endif()
        danmaku_compile_options(runtime_tests)
        add_test(NAME runtime_services COMMAND runtime_tests "${CMAKE_SOURCE_DIR}/tests/fixtures/sample.xml")
        set_tests_properties(runtime_services PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
        add_test(NAME runtime_image_services COMMAND runtime_tests "${CMAKE_SOURCE_DIR}/tests/fixtures/sample.xml")
        set_tests_properties(runtime_image_services PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DANMAKU_RENDER_BACKEND=image")
        add_test(NAME runtime_text_services COMMAND runtime_tests "${CMAKE_SOURCE_DIR}/tests/fixtures/sample.xml")
        set_tests_properties(runtime_text_services PROPERTIES TIMEOUT 30
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DANMAKU_RENDER_BACKEND=text")
        add_executable(renderer_tests tests/runtime/RendererTests.cpp)
        target_link_libraries(renderer_tests PRIVATE danmaku_runtime Qt6::Test)
        if(DANMAKU_STATIC_QT)
            qt_import_plugins(renderer_tests INCLUDE Qt6::QWindowsIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
        endif()
        danmaku_compile_options(renderer_tests)
        add_test(NAME renderer_cache COMMAND renderer_tests)
        set_tests_properties(renderer_cache PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        add_test(NAME application_smoke COMMAND danmaku_app --smoke-test
            --data-dir "${CMAKE_BINARY_DIR}/test-data" --cache-dir "${CMAKE_BINARY_DIR}/test-cache")
        set_tests_properties(application_smoke PROPERTIES TIMEOUT 20
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
        add_test(NAME application_image_smoke COMMAND danmaku_app --smoke-test
            --data-dir "${CMAKE_BINARY_DIR}/test-image-data" --cache-dir "${CMAKE_BINARY_DIR}/test-image-cache")
        set_tests_properties(application_image_smoke PROPERTIES TIMEOUT 20
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;DANMAKU_RENDER_BACKEND=image")
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
    add_executable(settings_update_tests tests/core/SettingsUpdateTests.cpp)
    target_link_libraries(settings_update_tests PRIVATE danmaku_core)
    danmaku_compile_options(settings_update_tests)
    add_test(NAME settings_updates COMMAND settings_update_tests)
    add_executable(hidden_playback_tests tests/core/HiddenPlaybackTests.cpp)
    target_link_libraries(hidden_playback_tests PRIVATE danmaku_core)
    danmaku_compile_options(hidden_playback_tests)
    add_test(NAME hidden_playback COMMAND hidden_playback_tests)
endif()

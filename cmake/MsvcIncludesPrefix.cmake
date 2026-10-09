# Applied after project() for Qt source builds as well as the application.
# CMake's compiler probe can misdecode a localized MSVC include prefix on Windows.
if(MSVC AND DEFINED DANMAKU_MSVC_INCLUDES_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${DANMAKU_MSVC_INCLUDES_PREFIX}")
endif()

# Qt defaults to C++17, while the application uses C++20. Qt's WinRT-only
# translation units must use the standard coroutine ABI too. /await:strict is
# supported with C++17 and changes coroutine support without lowering the app
# standard or suppressing MSVC's ABI checks. Only rebuild the affected files.
if(MSVC AND CMAKE_PROJECT_NAME STREQUAL "QtBase")
    function(danmaku_standard_winrt_coroutines)
        set(danmaku_winrt_sources
        src/corelib/text/qlocale_win.cpp
        src/corelib/platform/windows/qfactorycacheregistration.cpp
        src/plugins/platforms/windows/qwindowsintegration.cpp
        src/plugins/platforms/windows/qwindowscontext.cpp
        src/plugins/platforms/windows/qwindowstheme.cpp
        src/plugins/networkinformation/networklistmanager/qnetworklistmanagerevents.cpp
        src/plugins/networkinformation/networklistmanager/qnetworklistmanagernetworkinformationbackend.cpp)
        foreach(danmaku_winrt_source IN LISTS danmaku_winrt_sources)
            if(danmaku_winrt_source MATCHES "^src/corelib/")
                set(danmaku_winrt_target Core)
            elseif(danmaku_winrt_source MATCHES "^src/plugins/platforms/windows/")
                set(danmaku_winrt_target QWindowsIntegrationPlugin)
            else()
                set(danmaku_winrt_target QNLMNIPlugin)
            endif()
            if(TARGET ${danmaku_winrt_target})
                set_property(SOURCE "${CMAKE_SOURCE_DIR}/${danmaku_winrt_source}"
                    TARGET_DIRECTORY ${danmaku_winrt_target}
                    APPEND PROPERTY COMPILE_OPTIONS /await:strict)
                set_property(SOURCE "${CMAKE_SOURCE_DIR}/${danmaku_winrt_source}"
                    TARGET_DIRECTORY ${danmaku_winrt_target}
                    PROPERTY SKIP_PRECOMPILE_HEADERS ON)
            endif()
        endforeach()
    endfunction()
    cmake_language(DEFER CALL danmaku_standard_winrt_coroutines)
endif()

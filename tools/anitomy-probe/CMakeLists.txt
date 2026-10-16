if(NOT BUILD_TESTING)
    message(FATAL_ERROR "The Anitomy evaluation requires BUILD_TESTING=ON.")
endif()
if(DANMAKU_STATIC_QT)
    message(FATAL_ERROR "This probe validates the locked dynamic Qt toolchain only.")
endif()
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
file(REAL_PATH "${CMAKE_BINARY_DIR}" BUILD_ROOT)
string(FIND "${BUILD_ROOT}/" "${PROJECT_ROOT}/out/build/" BUILD_PREFIX)
if(NOT BUILD_PREFIX EQUAL 0)
    message(FATAL_ERROR "Build the probe under the project out/build directory.")
endif()
file(READ "${PROJECT_ROOT}/toolchain/anitomy.lock.json" LOCK)
string(JSON SOURCE_DIRECTORY GET "${LOCK}" sourceDirectory)
file(SHA256 "${PROJECT_ROOT}/toolchain/anitomy.lock.json" LOCK_HASH)
set(DEPENDENCY_ROOT "${PROJECT_ROOT}/.deps/anitomy")
if(NOT EXISTS "${DEPENDENCY_ROOT}/.prepared-lock")
    message(FATAL_ERROR "Run scripts/prepare-anitomy.ps1 explicitly before configuring the probe.")
endif()
file(READ "${DEPENDENCY_ROOT}/.prepared-lock" PREPARED_HASH)
string(STRIP "${PREPARED_HASH}" PREPARED_HASH)
string(TOUPPER "${PREPARED_HASH}" PREPARED_HASH)
string(TOUPPER "${LOCK_HASH}" LOCK_HASH)
if(NOT PREPARED_HASH STREQUAL LOCK_HASH)
    message(FATAL_ERROR "Anitomy lock changed; run scripts/prepare-anitomy.ps1 again.")
endif()
set(ANITOMY_ROOT "${DEPENDENCY_ROOT}/${SOURCE_DIRECTORY}")
add_library(anitomy_probe_dependency STATIC
    "${ANITOMY_ROOT}/anitomy/anitomy.cpp"
    "${ANITOMY_ROOT}/anitomy/element.cpp"
    "${ANITOMY_ROOT}/anitomy/keyword.cpp"
    "${ANITOMY_ROOT}/anitomy/parser.cpp"
    "${ANITOMY_ROOT}/anitomy/parser_helper.cpp"
    "${ANITOMY_ROOT}/anitomy/parser_number.cpp"
    "${ANITOMY_ROOT}/anitomy/string.cpp"
    "${ANITOMY_ROOT}/anitomy/token.cpp"
    "${ANITOMY_ROOT}/anitomy/tokenizer.cpp")
target_include_directories(anitomy_probe_dependency PUBLIC "${ANITOMY_ROOT}")
set_target_properties(anitomy_probe_dependency PROPERTIES AUTOMOC OFF)
include("${PROJECT_ROOT}/cmake/CompilerOptions.cmake")
danmaku_compile_options(anitomy_probe_dependency)
file(READ "${PROJECT_ROOT}/toolchain/dependencies.lock.json" TOOL_LOCK)
string(JSON QT_VERSION GET "${TOOL_LOCK}" qtVersion)
find_package(Qt6 ${QT_VERSION} EXACT REQUIRED COMPONENTS Core Test
    PATHS "${PROJECT_ROOT}/.tools/qt/${QT_VERSION}/msvc2022_64/lib/cmake/Qt6" NO_DEFAULT_PATH)
qt_add_executable(anitomy_probe
    ProbeTests.cpp
    "${PROJECT_ROOT}/src/infrastructure/MediaTitle.cpp"
    "${PROJECT_ROOT}/src/infrastructure/MediaTitleParser.cpp"
    "${PROJECT_ROOT}/src/infrastructure/ChineseConverter.cpp")
target_include_directories(anitomy_probe PRIVATE "${PROJECT_ROOT}/src")
target_link_libraries(anitomy_probe PRIVATE anitomy_probe_dependency Qt6::Core Qt6::Test)
set_target_properties(anitomy_probe PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
danmaku_compile_options(anitomy_probe)
configure_file("${ANITOMY_ROOT}/test/data.json" upstream-data.json COPYONLY)
configure_file("${ANITOMY_ROOT}/LICENSE" ANITOMY-LICENSE.txt COPYONLY)
add_test(NAME anitomy_feasibility COMMAND anitomy_probe -o qtest.txt,txt)
set_tests_properties(anitomy_feasibility PROPERTIES TIMEOUT 60 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

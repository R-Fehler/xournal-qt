# Qt-free libraries built from the upstream Xournal++ sources:
#   xoj-util  (src/util)            xoj-core (model, file I/O, views, PDF, settings, ...)
# Neither links GTK: <gtk/gtk.h> and <gdk/gdk.h> resolve to qt/compat/gtkshim, which only provides plain types
# and pure-cairo helpers. Any real GTK call in an allowlisted file is a compile error by design.

include(${CMAKE_CURRENT_LIST_DIR}/XojSources.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/XojDeps.cmake)

# --- generated config headers (same templates as upstream) ---------------------------------------------------------
set(XOJ_CONFIG_DIR "${CMAKE_BINARY_DIR}/xoj-config")
set(PROJECT_CRASHREPORT "https://github.com/xournalpp/xournalpp/issues/new/choose")
set(DEV_FILE_FORMAT_VERSION 5)
set(GETTEXT_PACKAGE "xournalpp")
set(ENABLE_NLS ON)
set(DEV_TOOLBAR_CONFIG "toolbar.ini")
set(DEV_SETTINGS_XML_FILE "settings.xml")
set(DEV_DEFAULT_PALETTE_FILE "xournal.gpl")
set(DEV_PRINT_CONFIG_FILE "print-config.ini")
set(DEV_METADATA_FILE "metadata.ini")
set(DEV_ERRORLOG_DIR "errorlogs")
set(ENABLE_QPDF ON)
# Floating point std::from_chars (fast .xopp parsing) where the C++ library has it: not in the NDK's libc++, where
# upstream falls back to g_ascii_strtod (the same check as upstream's CMakeLists.txt).
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([[
    #include <charconv>
    int main() {
        const char s[] = "7.38";
        double v{};
        return std::from_chars(s, s + 4, v).ec != std::errc{};
    }
]] XQT_HAVE_FLOAT_FROM_CHARS)
if(XQT_HAVE_FLOAT_FROM_CHARS)
    set(ENABLE_FLOAT_FROM_CHARS ON)
else()
    set(ENABLE_FLOAT_FROM_CHARS OFF)
endif()
set(ENABLE_AUDIO OFF)
set(ENABLE_PLUGINS OFF)
set(ENABLE_X11 OFF)
set(ENABLE_GTK_SOURCEVIEW OFF)
set(ENABLE_CPPTRACE OFF)
execute_process(COMMAND git -C "${XOJ_UPSTREAM_DIR}" rev-parse --short HEAD
    OUTPUT_VARIABLE RELEASE_IDENTIFIER OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C "${XOJ_UPSTREAM_DIR}" rev-parse --abbrev-ref HEAD
    OUTPUT_VARIABLE GIT_BRANCH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
set(GIT_ORIGIN_URL "https://github.com/xournalpp/xournalpp")
set(GIT_ORIGIN_OWNER "xournalpp")
set(GIT_ORIGIN_REPO "xournalpp")
foreach(cfg config config-features config-dev config-debug config-git)
    configure_file("${XOJ_UPSTREAM_DIR}/src/${cfg}.h.in" "${XOJ_CONFIG_DIR}/${cfg}.h" ESCAPE_QUOTES @ONLY)
endforeach()

# --- common compile settings for upstream code ------------------------------------------------------------------------
add_library(xoj-defaults INTERFACE)
target_include_directories(xoj-defaults INTERFACE
    "${XOJ_CONFIG_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/../compat/include"   # fork-owned replacement headers (shadow upstream ones)
    "${CMAKE_CURRENT_LIST_DIR}/../compat/gtkshim"   # <gtk/gtk.h>, <gdk/gdk.h> without GTK
    "${XOJ_SRC}/util/include"
    "${XOJ_SRC}/core")
target_compile_definitions(xoj-defaults INTERFACE
    XOJ_NO_GTK=1
    XOJ_CONFIG_FOLDER_NAME="xournal-qt"
    G_LOG_DOMAIN="xopp"
    GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_40)
target_compile_options(xoj-defaults INTERFACE -Wall -Wreturn-type -Wuninitialized -Wunused-value -Wunused-variable)
target_compile_features(xoj-defaults INTERFACE cxx_std_20)
target_link_libraries(xoj-defaults INTERFACE xoj::deps)

add_library(xoj-util STATIC
    ${XOJ_UTIL_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/../compat/VersionInfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../compat/XojMsgBox.cpp")
target_link_libraries(xoj-util PUBLIC xoj-defaults)

add_library(xoj-core STATIC ${XOJ_CORE_SOURCES})
target_link_libraries(xoj-core PUBLIC xoj-util)

# Tools (Qt-free): upstream input handlers + overlay views
add_library(xoj-tools STATIC ${XOJ_TOOLS_SOURCES} "${CMAKE_CURRENT_LIST_DIR}/../compat/DeviceId.cpp")
target_link_libraries(xoj-tools PUBLIC xoj-core)
set_target_properties(xoj-tools PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Render service (Qt-free): page rasters rendered by worker threads, port of upstream RenderJob.
add_library(xoj-render STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../src/render/PageRaster.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../src/render/RenderService.cpp")
target_include_directories(xoj-render PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src")
target_link_libraries(xoj-render PUBLIC xoj-core)
set_target_properties(xoj-render PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# The core is Qt-free: no moc/uic/rcc scanning.
set_target_properties(xoj-util xoj-core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

enable_testing()

# Desktop only (not on Android): the headless CLI, the image diff tool and the golden tests that use both.
if(NOT XQT_BUILD_CLI)
    return()
endif()

# Headless CLI (mirrors upstream's export options; used by the golden tests)
add_executable(xournal-qt-cli "${CMAKE_CURRENT_LIST_DIR}/../cli/main.cpp")
target_link_libraries(xournal-qt-cli PRIVATE xoj-core)
set_target_properties(xournal-qt-cli PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Developer tools and tests
add_executable(xoj-imgdiff "${CMAKE_CURRENT_LIST_DIR}/../tools/imgdiff.cpp")
target_link_libraries(xoj-imgdiff PRIVATE xoj::deps)
set_target_properties(xoj-imgdiff PROPERTIES AUTOMOC OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(XQT_GOLDEN_ENV "QT_CLI=$<TARGET_FILE:xournal-qt-cli>;IMGDIFF=$<TARGET_FILE:xoj-imgdiff>")
# Routine run (part of plain `ctest`): a few representative fixtures at 72 dpi, a few seconds.
add_test(NAME golden-quick
    COMMAND "${CMAKE_CURRENT_LIST_DIR}/../tests/golden/run_golden.sh"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(golden-quick PROPERTIES
    ENVIRONMENT "${XQT_GOLDEN_ENV};GOLDEN_MODE=quick;GOLDEN_OUT=${CMAKE_BINARY_DIR}/golden-out-quick"
    SKIP_RETURN_CODE 77 LABELS golden TIMEOUT 120)
# Full run (opt-in, several minutes): every fixture at 72 and 150 dpi. For upstream merges / milestone sign-off:
#   ctest -C Full -L golden-full --output-on-failure
add_test(NAME golden-full CONFIGURATIONS Full
    COMMAND "${CMAKE_CURRENT_LIST_DIR}/../tests/golden/run_golden.sh"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
set_tests_properties(golden-full PROPERTIES
    ENVIRONMENT "${XQT_GOLDEN_ENV};GOLDEN_MODE=full;GOLDEN_OUT=${CMAKE_BINARY_DIR}/golden-out-full"
    SKIP_RETURN_CODE 77 LABELS golden-full TIMEOUT 3600)

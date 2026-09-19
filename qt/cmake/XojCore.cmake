# Qt-free libraries built from the upstream Xournal++ sources:
#   xoj-util  (src/util)            xoj-core (model, file I/O, views, PDF, settings, ...)
# Neither links GTK: <gtk/gtk.h> and <gdk/gdk.h> resolve to qt/compat/gtkshim, which only provides plain types
# and pure-cairo helpers. Any real GTK call in an allowlisted file is a compile error by design.

include(${CMAKE_CURRENT_LIST_DIR}/XojSources.cmake)

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(XOJ_DEPS REQUIRED IMPORTED_TARGET
    "glib-2.0 >= 2.32.0" gio-2.0 gthread-2.0 cairo cairo-pdf cairo-svg pangocairo
    "poppler-glib >= 0.41.0" gdk-pixbuf-2.0 "libxml-2.0 >= 2.0.0" "libzip >= 1.0.1")

find_package(qpdf QUIET)
if(NOT qpdf_FOUND)
    pkg_search_module(qpdf REQUIRED "libqpdf >= 10.6.0")
    add_library(xoj_qpdf INTERFACE)
    target_link_libraries(xoj_qpdf INTERFACE ${qpdf_LIBRARIES})
    target_include_directories(xoj_qpdf INTERFACE ${qpdf_INCLUDE_DIRS})
    add_library(qpdf::libqpdf ALIAS xoj_qpdf)
endif()

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
set(ENABLE_FLOAT_FROM_CHARS ON)
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
target_link_libraries(xoj-defaults INTERFACE PkgConfig::XOJ_DEPS qpdf::libqpdf ZLIB::ZLIB Threads::Threads)

add_library(xoj-util STATIC
    ${XOJ_UTIL_SOURCES}
    "${CMAKE_CURRENT_LIST_DIR}/../compat/VersionInfo.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/../compat/XojMsgBox.cpp")
target_link_libraries(xoj-util PUBLIC xoj-defaults)

add_library(xoj-core STATIC ${XOJ_CORE_SOURCES})
target_link_libraries(xoj-core PUBLIC xoj-util)

# The core is Qt-free: no moc/uic/rcc scanning.
set_target_properties(xoj-util xoj-core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

# Headless CLI (mirrors upstream's export options; used by the golden tests)
add_executable(xournal-qt-cli "${CMAKE_CURRENT_LIST_DIR}/../cli/main.cpp")
target_link_libraries(xournal-qt-cli PRIVATE xoj-core)
set_target_properties(xournal-qt-cli PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

# Developer tools and tests
add_executable(xoj-imgdiff "${CMAKE_CURRENT_LIST_DIR}/../tools/imgdiff.cpp")
target_link_libraries(xoj-imgdiff PRIVATE PkgConfig::XOJ_DEPS)
set_target_properties(xoj-imgdiff PROPERTIES AUTOMOC OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

enable_testing()
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

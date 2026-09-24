# xournal-qt: Qt 6 frontend for the Xournal++ core.
# Build root of the fork (see ../FORK.md). Configure with:
#   cmake -S qt -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake_minimum_required(VERSION 3.21)

project(xournal-qt VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ccache, when installed. base_dir is the folder that holds the checkout, so worktrees next to it
# (../xournal_qt-<block>, see ../AGENTS.md) share cache hits. nohashdir: debug info may then name
# another worktree's build folder, which only matters to a debugger.
option(XQT_USE_CCACHE "Compile through ccache when it is installed" ON)
find_program(XQT_CCACHE ccache)
if(XQT_USE_CCACHE AND XQT_CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    cmake_path(GET CMAKE_CURRENT_SOURCE_DIR PARENT_PATH _xqt_checkout)
    cmake_path(GET _xqt_checkout PARENT_PATH _xqt_workspace)
    set(_xqt_launcher ${CMAKE_COMMAND} -E env CCACHE_BASEDIR=${_xqt_workspace} CCACHE_NOHASHDIR=1 ${XQT_CCACHE})
    set(CMAKE_C_COMPILER_LAUNCHER ${_xqt_launcher})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${_xqt_launcher})
endif()

# Absolute path of the upstream Xournal++ sources inside this fork.
set(XOJ_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE INTERNAL "")

# Android (see docs/android.md): the app only. Tests, the CLI and the spikes are desktop tools.
if(ANDROID)
    set(_xqt_desktop OFF)
else()
    set(_xqt_desktop ON)
endif()
option(XQT_BUILD_SPIKES "Build experimental spikes (qt/spikes)" ${_xqt_desktop})
option(XQT_BUILD_CORE "Build the Qt-free Xournal++ core libraries and CLI" ON)
option(XQT_BUILD_TESTS "Build the unit tests (downloads GoogleTest if not installed)" ${_xqt_desktop})
option(XQT_BUILD_CLI "Build the headless CLI and the golden tests" ${_xqt_desktop})

set(_xqt_qt_test)
if(XQT_BUILD_TESTS)
    set(_xqt_qt_test Test)
endif()
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network PrintSupport Quick QuickControls2 ${_xqt_qt_test}
    OPTIONAL_COMPONENTS DBus)  # DBus: "Show in file manager" on Linux (SystemApps.cpp)

qt_standard_project_setup(REQUIRES 6.5)

# glib/gio headers use "signals" as an identifier: never use Qt's keyword macros.
add_compile_definitions(QT_NO_KEYWORDS)

# Windows with MinGW (MSYS2, see docs/windows.md): big translation units (AppController, the QML cache) have more
# sections than COFF allows by default.
if(MINGW)
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-Wa,-mbig-obj>")
endif()

if(XQT_BUILD_CORE)
    include(cmake/XojCore.cmake)
    if(XQT_BUILD_TESTS)
        include(cmake/XojTests.cmake)
    endif()
    include(cmake/XqtMarkdown.cmake)
    include(cmake/XqtSession.cmake)
    include(cmake/XqtApp.cmake)
    if(ANDROID)
        include(cmake/XqtAndroid.cmake)
    else()
        include(cmake/XqtPackage.cmake)
    endif()
endif()

if(XQT_BUILD_SPIKES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XQT_CAIRO REQUIRED IMPORTED_TARGET cairo)
    pkg_check_modules(XQT_GLIB REQUIRED IMPORTED_TARGET glib-2.0)
    add_subdirectory(spikes/inkpad)
endif()

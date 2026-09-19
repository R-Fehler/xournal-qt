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

# Absolute path of the upstream Xournal++ sources inside this fork.
set(XOJ_UPSTREAM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE INTERNAL "")

option(XQT_BUILD_SPIKES "Build experimental spikes (qt/spikes)" ON)
option(XQT_BUILD_CORE "Build the Qt-free Xournal++ core libraries and CLI" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Quick QuickControls2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XQT_CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(XQT_GLIB REQUIRED IMPORTED_TARGET glib-2.0)

qt_standard_project_setup(REQUIRES 6.5)

# glib/gio headers use "signals" as an identifier: never use Qt's keyword macros.
add_compile_definitions(QT_NO_KEYWORDS)

option(XQT_BUILD_TESTS "Build the unit tests (downloads GoogleTest if not installed)" ON)

if(XQT_BUILD_CORE)
    include(cmake/XojCore.cmake)
    if(XQT_BUILD_TESTS)
        include(cmake/XojTests.cmake)
    endif()
endif()

if(XQT_BUILD_SPIKES)
    add_subdirectory(spikes/inkpad)
endif()

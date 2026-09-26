# The C libraries of the core, as one interface target: xoj::deps.
#
# Toolchain-agnostic: CMake packages first (vcpkg, and later Windows/macOS), pkg-config for what only ships .pc files
# (the GNOME libraries: glib, cairo, pango, poppler-glib, gdk-pixbuf; vcpkg installs .pc files for them too). On the
# Linux desktop pkg-config alone finds the distribution's libraries, exactly as before.
#
# XQT_STATIC_DEPS: the dependencies are static archives (vcpkg's Android triplet). pkg-config's --static flags are
# then used, so that the private dependencies (pcre2, ffi, freetype, harfbuzz, fontconfig, png, jpeg, ...) are linked
# as well.

option(XQT_STATIC_DEPS "Link the C dependencies statically (pkg-config --static), as vcpkg builds them for Android"
    ${ANDROID})

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# Cross builds through vcpkg: only the .pc files of the target triplet, never the build machine's.
if(CMAKE_CROSSCOMPILING AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
    set(ENV{PKG_CONFIG_LIBDIR} "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/lib/pkgconfig")
    set(ENV{PKG_CONFIG_PATH} "")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
endif()

# CMake packages where they exist. Not on the Linux desktop by default: there pkg-config finds everything, and some
# distributions ship broken CMake files (Ubuntu 22.04's libzip-dev references tools that are not installed, which
# is a hard error even with QUIET).
if(VCPKG_TOOLCHAIN OR WIN32 OR APPLE)
    set(_xoj_cmake_packages ON)
else()
    set(_xoj_cmake_packages OFF)
endif()
option(XQT_DEPS_CMAKE_PACKAGES "Look for libxml2 and libzip as CMake packages before pkg-config" ${_xoj_cmake_packages})
if(XQT_DEPS_CMAKE_PACKAGES)
    find_package(LibXml2 2.0.0 QUIET)
    find_package(libzip CONFIG QUIET)
    if(libzip_FOUND AND libzip_VERSION VERSION_LESS 1.0.1)
        set(libzip_FOUND FALSE)
    endif()
endif()
set(_xoj_pc_modules
    "glib-2.0 >= 2.32.0" gio-2.0 gthread-2.0 cairo cairo-pdf cairo-svg pangocairo
    # (fontconfig and Pango's fontconfig fonts: the app's emoji font, qt/src/markdown/EmojiFont.cpp)
    "fontconfig >= 2.13" pangoft2
    "poppler-glib >= 0.41.0" gdk-pixbuf-2.0)
if(NOT TARGET LibXml2::LibXml2)
    list(APPEND _xoj_pc_modules "libxml-2.0 >= 2.0.0")
endif()
if(NOT libzip_FOUND OR NOT TARGET libzip::zip)
    list(APPEND _xoj_pc_modules "libzip >= 1.0.1")
endif()
pkg_check_modules(XOJ_DEPS REQUIRED IMPORTED_TARGET ${_xoj_pc_modules})

# qpdf 12: built here, or the system's (XQT_SYSTEM_QPDF)
include(${CMAKE_CURRENT_LIST_DIR}/XqtQpdf.cmake)

add_library(xoj-deps INTERFACE)
add_library(xoj::deps ALIAS xoj-deps)
if(XQT_STATIC_DEPS)
    # The static link line in pkg-config's order (-L... -lpoppler-glib ... -lz -lm), and every include directory.
    target_include_directories(xoj-deps INTERFACE ${XOJ_DEPS_STATIC_INCLUDE_DIRS})
    target_compile_options(xoj-deps INTERFACE ${XOJ_DEPS_STATIC_CFLAGS_OTHER})
    target_link_libraries(xoj-deps INTERFACE ${XOJ_DEPS_STATIC_LDFLAGS})
else()
    target_link_libraries(xoj-deps INTERFACE PkgConfig::XOJ_DEPS)
endif()
if(TARGET LibXml2::LibXml2)
    target_link_libraries(xoj-deps INTERFACE LibXml2::LibXml2)
endif()
if(libzip_FOUND AND TARGET libzip::zip)
    target_link_libraries(xoj-deps INTERFACE libzip::zip)
endif()
target_link_libraries(xoj-deps INTERFACE qpdf::libqpdf ZLIB::ZLIB Threads::Threads)

# gettext (upstream's _() through <libintl.h>): part of the C library on Linux and Android, libintl on Windows
# (MSYS2: mingw-w64-*-gettext-runtime, a dependency of glib).
if(WIN32)
    find_package(Intl REQUIRED)
    target_include_directories(xoj-deps INTERFACE ${Intl_INCLUDE_DIRS})
    target_link_libraries(xoj-deps INTERFACE ${Intl_LIBRARIES})
endif()

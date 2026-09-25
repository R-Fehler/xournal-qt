# qpdf, as the target qpdf::libqpdf. qpdf 12 or newer: the incremental save (src/session/IncrementalPdf) and the
# hybrid and archive PDFs are written and tested against it.
#
# The desktop build compiles a pinned qpdf release as a static library (native crypto, the system's zlib and libjpeg),
# so the app behaves the same on every distribution: Ubuntu 22.04 has qpdf 10.6, Debian 13 has 12.2. It is built as
# an external project (qpdf's own CMake run, only its library target), so its tests, install rules and CPack settings
# stay out of this build; the compiler launcher (ccache) is passed on. The source is downloaded once per build folder
# at configure time; XQT_QPDF_SOURCE_DIR points at an unpacked copy instead (offline builds). `cmake --build` runs
# its build with CMAKE_BUILD_PARALLEL_LEVEL jobs when that is set in the environment.
#
# XQT_SYSTEM_QPDF: the qpdf of the system or the package manager (distribution packages; MSYS2 on Windows and vcpkg
# on Android ship qpdf 12, so there it is the default).

if(ANDROID OR WIN32 OR APPLE OR VCPKG_TOOLCHAIN)
    set(_xqt_system_qpdf ON)
else()
    set(_xqt_system_qpdf OFF)
endif()
option(XQT_SYSTEM_QPDF "Use the system's qpdf (12 or newer) instead of building the pinned release" ${_xqt_system_qpdf})

set(XQT_QPDF_VERSION 12.4.1)
set(XQT_QPDF_SHA256 f045aa277be2356ff53a89a8622945958291177d2483afc20ede7c8a8cd3873c)
set(XQT_QPDF_URL "https://github.com/qpdf/qpdf/releases/download/v${XQT_QPDF_VERSION}/qpdf-${XQT_QPDF_VERSION}.tar.gz")

if(XQT_SYSTEM_QPDF)
    find_package(qpdf QUIET)
    if(qpdf_FOUND AND TARGET qpdf::libqpdf)
        if(qpdf_VERSION VERSION_LESS 12)
            message(FATAL_ERROR "qpdf ${qpdf_VERSION} is too old: 12 or newer is needed (or XQT_SYSTEM_QPDF=OFF)")
        endif()
        message(STATUS "qpdf ${qpdf_VERSION} (system, CMake package)")
    else()
        pkg_search_module(qpdf REQUIRED "libqpdf >= 12")
        add_library(xqt_qpdf INTERFACE)
        target_link_libraries(xqt_qpdf INTERFACE ${qpdf_LIBRARIES})
        target_include_directories(xqt_qpdf INTERFACE ${qpdf_INCLUDE_DIRS})
        add_library(qpdf::libqpdf ALIAS xqt_qpdf)
        message(STATUS "qpdf ${qpdf_VERSION} (system, pkg-config)")
    endif()
    return()
endif()

find_package(JPEG REQUIRED)

# The source: unpacked into the build folder at configure time (the include folder must exist before the build)
set(XQT_QPDF_SOURCE_DIR "" CACHE PATH "An unpacked qpdf ${XQT_QPDF_VERSION} source to build instead of downloading it")
if(XQT_QPDF_SOURCE_DIR)
    set(_qpdf_src "${XQT_QPDF_SOURCE_DIR}")
else()
    set(_qpdf_src "${CMAKE_BINARY_DIR}/_qpdf/qpdf-${XQT_QPDF_VERSION}")
    if(NOT EXISTS "${_qpdf_src}/CMakeLists.txt")
        set(_qpdf_tarball "${CMAKE_BINARY_DIR}/_qpdf/qpdf-${XQT_QPDF_VERSION}.tar.gz")
        message(STATUS "qpdf ${XQT_QPDF_VERSION}: downloading ${XQT_QPDF_URL}")
        file(DOWNLOAD "${XQT_QPDF_URL}" "${_qpdf_tarball}" EXPECTED_HASH SHA256=${XQT_QPDF_SHA256} STATUS _qpdf_status)
        list(GET _qpdf_status 0 _qpdf_code)
        if(NOT _qpdf_code EQUAL 0)
            file(REMOVE "${_qpdf_tarball}")
            message(FATAL_ERROR "Could not download qpdf (${_qpdf_status}). Set XQT_QPDF_SOURCE_DIR to an unpacked "
                                "qpdf-${XQT_QPDF_VERSION}, or XQT_SYSTEM_QPDF=ON to use the system's qpdf 12.")
        endif()
        file(ARCHIVE_EXTRACT INPUT "${_qpdf_tarball}" DESTINATION "${CMAKE_BINARY_DIR}/_qpdf")
        file(REMOVE "${_qpdf_tarball}")
    endif()
endif()
if(NOT EXISTS "${_qpdf_src}/include/qpdf/QPDF.hh")
    message(FATAL_ERROR "No qpdf source in ${_qpdf_src}")
endif()

include(ExternalProject)
set(_qpdf_bin "${CMAKE_BINARY_DIR}/_qpdf/build")
set(_qpdf_lib "${_qpdf_bin}/libqpdf/${CMAKE_STATIC_LIBRARY_PREFIX}qpdf${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(_qpdf_cache_args
    -DCMAKE_BUILD_TYPE:STRING=Release
    -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
    -DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON
    -DBUILD_SHARED_LIBS:BOOL=OFF
    -DBUILD_STATIC_LIBS:BOOL=ON
    -DUSE_IMPLICIT_CRYPTO:BOOL=OFF
    -DREQUIRE_CRYPTO_NATIVE:BOOL=ON
    -DBUILD_DOC:BOOL=OFF)
if(CMAKE_CXX_COMPILER_LAUNCHER)
    list(APPEND _qpdf_cache_args
        -DCMAKE_C_COMPILER_LAUNCHER:STRING=${CMAKE_C_COMPILER_LAUNCHER}
        -DCMAKE_CXX_COMPILER_LAUNCHER:STRING=${CMAKE_CXX_COMPILER_LAUNCHER})
endif()
if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND _qpdf_cache_args -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE})
endif()
ExternalProject_Add(xqt_qpdf_build
    SOURCE_DIR "${_qpdf_src}"
    BINARY_DIR "${_qpdf_bin}"
    PREFIX "${CMAKE_BINARY_DIR}/_qpdf"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_CACHE_ARGS ${_qpdf_cache_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target libqpdf
    BUILD_BYPRODUCTS "${_qpdf_lib}"
    INSTALL_COMMAND ""
    TEST_COMMAND "")

add_library(xqt_qpdf STATIC IMPORTED GLOBAL)
set_target_properties(xqt_qpdf PROPERTIES
    IMPORTED_LOCATION "${_qpdf_lib}"
    INTERFACE_INCLUDE_DIRECTORIES "${_qpdf_src}/include"
    INTERFACE_LINK_LIBRARIES "JPEG::JPEG;ZLIB::ZLIB;Threads::Threads")
add_dependencies(xqt_qpdf xqt_qpdf_build)
add_library(qpdf::libqpdf ALIAS xqt_qpdf)
message(STATUS "qpdf ${XQT_QPDF_VERSION} (built here, static)")

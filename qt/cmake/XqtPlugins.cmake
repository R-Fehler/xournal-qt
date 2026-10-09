# JavaScript plugins (qt/src/plugins/README.md, ADR 0008): the host, one QJSEngine per plugin, the API module
# "xournal" over the operations layer. QtQml and QtCore only (no Qt Quick): the CLI can run plugins too.
find_package(Qt6 REQUIRED COMPONENTS Qml)
add_library(xqt-plugins STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginManifest.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginManifest.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/ImportCheck.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/ImportCheck.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/Watchdog.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/Watchdog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginScript.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginScript.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginHost.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugins/PluginHost.cpp)
target_include_directories(xqt-plugins PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/plugins")
target_link_libraries(xqt-plugins PUBLIC Qt6::Qml xqt-ops)
set_target_properties(xqt-plugins PROPERTIES AUTOMOC ON)
qt_add_resources(xqt-plugins xqt_plugins_api PREFIX /xqt-plugins BASE "${CMAKE_CURRENT_LIST_DIR}/../src/plugins"
    FILES "${CMAKE_CURRENT_LIST_DIR}/../src/plugins/api.js")

# The bundled plugins (qt/resources/plugins/<folder>/): copied into the resource dir as plugins/<folder>/, where the
# host finds them (the build tree's share/xournal-qt for development; installed with it by XqtPackage.cmake; on
# Android in the APK's /xqt-share, copied to the app's data folder at start like the handwriting models)
file(GLOB_RECURSE _xqt_plugin_files CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_LIST_DIR}/../resources/plugins"
    "${CMAKE_CURRENT_LIST_DIR}/../resources/plugins/*")
foreach(_xqt_file ${_xqt_plugin_files})
    configure_file("${CMAKE_CURRENT_LIST_DIR}/../resources/plugins/${_xqt_file}"
        "${XQT_BUILD_RESOURCE_DIR}/plugins/${_xqt_file}" COPYONLY)
endforeach()

if(XQT_BUILD_TESTS)
    add_executable(xqt-plugins-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/plugins/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/plugins/PluginTestSupport.h
        ${CMAKE_CURRENT_LIST_DIR}/../tests/plugins/PluginHostTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/plugins/PlotterTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/plugins/ExamplesTest.cpp)
    target_link_libraries(xqt-plugins-tests PRIVATE xqt-plugins xqt-test-support Qt6::Test GTest::gtest)
    target_include_directories(xqt-plugins-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-plugins-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}"
        XQT_PLUGINS_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/../resources/plugins")
    gtest_discover_tests(xqt-plugins-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS plugins
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

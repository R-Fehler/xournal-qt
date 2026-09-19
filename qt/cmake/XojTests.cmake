# Upstream Xournal++ unit tests (test/unit_tests) compiled against the Qt-free core.
include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.16.0.zip)
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
    FetchContent_MakeAvailable(googletest)
endif()

set(PROJECT_SOURCE_DIR_FOR_TESTS "${XOJ_UPSTREAM_DIR}")
set(TEST_CONFIG_DIR "${CMAKE_BINARY_DIR}/xoj-test-config")
block()
    set(PROJECT_SOURCE_DIR "${XOJ_UPSTREAM_DIR}")
    configure_file("${XOJ_UPSTREAM_DIR}/test/config-test.h.in" "${TEST_CONFIG_DIR}/config-test.h" ESCAPE_QUOTES @ONLY)
endblock()

set(XOJ_TEST "${XOJ_UPSTREAM_DIR}/test/unit_tests")
set(XOJ_UNIT_TEST_SOURCES
    ${XOJ_TEST}/control/LoadHandlerTest.cpp
    ${XOJ_TEST}/control/MetadataManagerTest.cpp
    ${XOJ_TEST}/control/SettingsTest.cpp
    ${XOJ_TEST}/control/ToolEnumsTest.cpp
    ${XOJ_TEST}/model/ColorPaletteTest.cpp
    ${XOJ_TEST}/model/DocumentNameTest.cpp
    ${XOJ_TEST}/model/ErasableStrokeTest.cpp
    ${XOJ_TEST}/model/ImageTest.cpp
    ${XOJ_TEST}/model/LineStyleTest.cpp
    ${XOJ_TEST}/model/StrokeStyleTest.cpp
    ${XOJ_TEST}/util/ColorTest.cpp
    ${XOJ_TEST}/util/ElementRangeTest.cpp
    ${XOJ_TEST}/util/I18nTest.cpp
    ${XOJ_TEST}/util/IntervalTest.cpp
    ${XOJ_TEST}/util/MatrixTest.cpp
    ${XOJ_TEST}/util/ObjectIOStreamTest.cpp
    ${XOJ_TEST}/util/PathTest.cpp
    ${XOJ_TEST}/util/RAIIWrappersTest.cpp
    ${XOJ_TEST}/util/RangeTest.cpp
    ${XOJ_TEST}/util/SaveNameUtilsTest.cpp
    ${XOJ_TEST}/util/StringUtilsTest.cpp
    ${XOJ_TEST}/util/TinySmallVectorTest.cpp
    ${XOJ_TEST}/util/XojPreviewExtractorTest.cpp
)
set(XQT_UNIT_TEST_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/../tests/unit/UndoRedoTest.cpp
)
add_executable(xoj-unit-tests "${CMAKE_CURRENT_LIST_DIR}/../tests/unit/main.cpp"
    ${XOJ_UNIT_TEST_SOURCES} ${XQT_UNIT_TEST_SOURCES})
target_link_libraries(xoj-unit-tests PRIVATE xoj-core GTest::gtest)
target_include_directories(xoj-unit-tests PRIVATE "${TEST_CONFIG_DIR}")
set_target_properties(xoj-unit-tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

include(GoogleTest)
gtest_discover_tests(xoj-unit-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS unit)

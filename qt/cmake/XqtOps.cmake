# The permission-checked operations layer (qt/src/ops/README.md, ADR 0008): what plugins (later remote peers and
# agents) change in a document, checked against what they may do, one undo step per transaction. Headless: Qt Core and
# the session.
add_library(xqt-ops STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/Operations.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/Operations.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/DocumentOps.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/DocumentOps.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/Shapes.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/ops/Shapes.cpp)
target_include_directories(xqt-ops PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/ops")
target_link_libraries(xqt-ops PUBLIC xqt-session)

if(XQT_BUILD_TESTS)
    add_executable(xqt-ops-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ops/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ops/OperationsTest.cpp)
    target_link_libraries(xqt-ops-tests PRIVATE xqt-ops xqt-test-support Qt6::Test GTest::gtest)
    target_include_directories(xqt-ops-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-ops-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-ops-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS ops
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

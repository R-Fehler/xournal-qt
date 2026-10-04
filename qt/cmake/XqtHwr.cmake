# Handwriting search (qt/docs/handwriting-search.md): what the recognised words are and how the search matches them
# live in xqt-session (InkText.h); the layout of ink into lines and words, the recognisers and the background service
# are added here as they come.

if(XQT_BUILD_TESTS)
    add_executable(xqt-hwr-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkTextTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkSearchTest.cpp)
    target_link_libraries(xqt-hwr-tests PRIVATE xqt-session Qt6::Test GTest::gtest)
    target_include_directories(xqt-hwr-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-hwr-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-hwr-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS hwr
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

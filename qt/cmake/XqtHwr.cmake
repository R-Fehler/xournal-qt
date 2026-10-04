# Handwriting search (qt/docs/handwriting-search.md): what the recognised words are and how the search matches them
# live in xqt-session (InkText.h); the layout of ink into lines and words, the recognisers and the background service
# are added here as they come.

add_library(xqt-hwr STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/Recognizer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/Recognizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FakeRecognizer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/FakeRecognizer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineImage.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/LineImage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/WordAlignment.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/WordAlignment.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkRecognitionService.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkRecognitionService.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkTextIndexer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/InkTextIndexer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HandwritingSearch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/hwr/HandwritingSearch.cpp)
target_include_directories(xqt-hwr PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/hwr")
target_link_libraries(xqt-hwr PUBLIC xqt-session)
set_target_properties(xqt-hwr PROPERTIES AUTOMOC ON)

if(XQT_BUILD_TESTS)
    add_executable(xqt-hwr-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkTextTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkSearchTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkLayoutTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/RecognizerTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkIndexerTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/hwr/InkLibraryTest.cpp)
    target_link_libraries(xqt-hwr-tests PRIVATE xqt-hwr xqt-shell Qt6::Test GTest::gtest)
    target_include_directories(xqt-hwr-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-hwr-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-hwr-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS hwr
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

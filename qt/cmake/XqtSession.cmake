# Qt-side application libraries: per-tab document sessions + shared application context.

# Runtime resources in the build tree (the install layout is <prefix>/share/xournal-qt).
set(XQT_BUILD_RESOURCE_DIR "${CMAKE_BINARY_DIR}/share/xournal-qt")
configure_file("${XOJ_UPSTREAM_DIR}/resources-templates/pagetemplates.ini.in"
    "${XQT_BUILD_RESOURCE_DIR}/pagetemplates.ini" COPYONLY)
file(COPY "${XOJ_UPSTREAM_DIR}/palettes" DESTINATION "${XQT_BUILD_RESOURCE_DIR}")

add_library(xqt-session STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/AppContext.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/AppContext.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSession.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSession.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/HeadlessViews.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.cpp
)
target_include_directories(xqt-session PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/session")
target_link_libraries(xqt-session PUBLIC Qt6::Core xoj-render xoj-core)
target_compile_definitions(xqt-session PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
set_target_properties(xqt-session PROPERTIES AUTOMOC ON)

if(XQT_BUILD_TESTS)
    add_executable(xqt-session-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentSessionTest.cpp)
    target_link_libraries(xqt-session-tests PRIVATE xqt-session Qt6::Test GTest::gtest)
    target_include_directories(xqt-session-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-session-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-session-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

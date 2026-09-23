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
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSearch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSearch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageOrderUndoAction.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageOrderUndoAction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/HeadlessViews.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.cpp
)
target_include_directories(xqt-session PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/session")
target_link_libraries(xqt-session PUBLIC Qt6::Core xoj-render xoj-core)
target_compile_definitions(xqt-session PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
set_target_properties(xqt-session PROPERTIES AUTOMOC ON)

# Canvas model: pages (port of XojPageView), layout, zoom/scroll, input (port of PenInputHandler & co.)
add_library(xqt-canvas STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/DocumentLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/DocumentLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ViewController.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ViewController.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasPage.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasPage.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasView.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasView.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/Perf.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/Perf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasMemory.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasMemory.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/GeometryToolLayer.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/GeometryToolLayer.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasInput.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasInput.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/PenHover.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/PenHover.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextEditor.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextEditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextFlow.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextFlow.cpp
)
target_include_directories(xqt-canvas PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/canvas")
target_link_libraries(xqt-canvas PUBLIC Qt6::Gui xqt-session xoj-tools)
set_target_properties(xqt-canvas PROPERTIES AUTOMOC ON)

if(XQT_BUILD_TESTS)
    add_executable(xqt-session-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentSessionTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentSearchTest.cpp)
    target_link_libraries(xqt-session-tests PRIVATE xqt-session Qt6::Test GTest::gtest)
    target_include_directories(xqt-session-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-session-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-session-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(xqt-canvas-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/CanvasReplayTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/DocumentLayoutTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/TextFlowTest.cpp)
    target_link_libraries(xqt-canvas-tests PRIVATE xqt-canvas Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-canvas-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    target_include_directories(xqt-canvas-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-canvas-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS canvas
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

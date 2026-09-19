# The xournal-qt application: Qt Quick UI + document canvas.

# Tool bar icons: upstream Xournal++'s Lucide icon theme.
file(GLOB XQT_UPSTREAM_ICONS "${XOJ_UPSTREAM_DIR}/ui/iconsLucide-light/hicolor/scalable/actions/*.svg")
file(COPY ${XQT_UPSTREAM_ICONS} DESTINATION "${XQT_BUILD_RESOURCE_DIR}/icons")

# Qt Quick canvas item (library, so that tests can use it)
add_library(xqt-quick STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/DocumentCanvasItem.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/DocumentCanvasItem.cpp)
target_include_directories(xqt-quick PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/quick)
target_link_libraries(xqt-quick PUBLIC Qt6::Quick Qt6::Qml xqt-canvas)
set_target_properties(xqt-quick PROPERTIES AUTOMOC ON)

# Application shell: tabs, single instance
add_library(xqt-shell STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/TabManager.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/TabManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SingleInstance.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SingleInstance.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.cpp)
target_include_directories(xqt-shell PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src ${CMAKE_CURRENT_LIST_DIR}/../src/app)
target_link_libraries(xqt-shell PUBLIC Qt6::Network xqt-canvas)
set_target_properties(xqt-shell PROPERTIES AUTOMOC ON)

qt_add_executable(xournal-qt
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/main.cpp
)
set_source_files_properties(src/app/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(src/app/qml/IconButton.qml PROPERTIES QT_RESOURCE_ALIAS IconButton.qml)
set_source_files_properties(src/app/qml/TabStrip.qml PROPERTIES QT_RESOURCE_ALIAS TabStrip.qml)
qt_add_qml_module(xournal-qt
    URI XournalQt
    VERSION 1.0
    QML_FILES
        src/app/qml/Main.qml
        src/app/qml/IconButton.qml
        src/app/qml/TabStrip.qml
)
target_include_directories(xournal-qt PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/app)
target_link_libraries(xournal-qt PRIVATE Qt6::Widgets Qt6::Quick Qt6::QuickControls2 xqt-quick xqt-shell)
set_target_properties(xournal-qt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(XQT_BUILD_TESTS)
    add_executable(xqt-quick-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/CanvasItemInputTest.cpp)
    target_link_libraries(xqt-quick-tests PRIVATE xqt-quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-quick-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-quick-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS quick
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(xqt-shell-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/TabsTest.cpp)
    target_link_libraries(xqt-shell-tests PRIVATE xqt-shell Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-shell-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    target_include_directories(xqt-shell-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-shell-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS shell
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

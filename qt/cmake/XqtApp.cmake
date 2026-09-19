# The xournal-qt application: Qt Quick UI + document canvas.

# Tool bar icons: upstream Xournal++'s Lucide icon theme.
file(GLOB XQT_UPSTREAM_ICONS "${XOJ_UPSTREAM_DIR}/ui/iconsLucide-light/hicolor/scalable/actions/*.svg")
file(COPY ${XQT_UPSTREAM_ICONS} DESTINATION "${XQT_BUILD_RESOURCE_DIR}/icons")

qt_add_executable(xournal-qt
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.cpp
)
set_source_files_properties(src/app/qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(src/app/qml/IconButton.qml PROPERTIES QT_RESOURCE_ALIAS IconButton.qml)
qt_add_qml_module(xournal-qt
    URI XournalQt
    VERSION 1.0
    QML_FILES
        src/app/qml/Main.qml
        src/app/qml/IconButton.qml
    SOURCES
        src/quick/DocumentCanvasItem.h
        src/quick/DocumentCanvasItem.cpp
)
target_include_directories(xournal-qt PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/quick ${CMAKE_CURRENT_LIST_DIR}/../src/app)
target_link_libraries(xournal-qt PRIVATE Qt6::Quick Qt6::QuickControls2 xqt-canvas)
set_target_properties(xournal-qt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

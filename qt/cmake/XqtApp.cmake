# The xournal-qt application: Qt Quick UI + document canvas.

# Tool bar icons: upstream Xournal++'s Lucide icon theme.
file(GLOB XQT_UPSTREAM_ICONS "${XOJ_UPSTREAM_DIR}/ui/iconsLucide-light/hicolor/scalable/actions/*.svg")
file(COPY ${XQT_UPSTREAM_ICONS} DESTINATION "${XQT_BUILD_RESOURCE_DIR}/icons")
file(GLOB XQT_OWN_ICONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../resources/icons/*.svg")
file(COPY ${XQT_OWN_ICONS} DESTINATION "${XQT_BUILD_RESOURCE_DIR}/icons")
# The program icon (window icon when not installed)
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../packaging/xournal-qt.svg" DESTINATION "${XQT_BUILD_RESOURCE_DIR}/icons")

# Qt Quick canvas item (library, so that tests can use it)
add_library(xqt-quick STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/DocumentCanvasItem.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/DocumentCanvasItem.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/TextFlowEditor.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/TextFlowEditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/TouchGestures.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/TouchGestures.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/InputLog.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/InputLog.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/EmojiNames.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/quick/EmojiNames.cpp)
target_include_directories(xqt-quick PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src/quick)
target_link_libraries(xqt-quick PUBLIC Qt6::Quick Qt6::Qml xqt-canvas)
set_target_properties(xqt-quick PROPERTIES AUTOMOC ON)

# Application shell: tabs, single instance
add_library(xqt-shell STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/TabManager.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/TabManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ReferenceMode.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ReferenceMode.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SingleInstance.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SingleInstance.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Thumbnails.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Thumbnails.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageSketches.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageSketches.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PagesModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PagesModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageFilterModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageFilterModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageClipboard.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PageClipboard.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SettingsModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SettingsModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SessionRecovery.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SessionRecovery.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentFiles.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentFiles.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ContentFiles.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ContentFiles.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SyncConflicts.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SyncConflicts.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/GridSelection.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryCache.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryCache.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Library.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Library.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryArchive.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryArchive.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryMigration.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LibraryMigration.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ShortcutsModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/ShortcutsModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LayersModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LayersModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentChapters.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentChapters.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentLinks.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentLinks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LinkRewrite.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LinkRewrite.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/OutlineModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/OutlineModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Annotations.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Annotations.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/AnnotationsModel.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/AnnotationsModel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/HitPages.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/HitPages.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/MdSnippets.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/MdSnippets.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Previews.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Previews.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentPlaces.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/DocumentPlaces.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/RecentFiles.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/RecentFiles.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SystemApps.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/SystemApps.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LocalUrl.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/LocalUrl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PdfPrinting.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/PdfPrinting.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Citations.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/Citations.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/NetFetch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/shell/NetFetch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppController.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppTextFiles.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppLinks.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppAnnotations.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppMarkdownFormat.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/AppMarkdownImages.cpp)
target_include_directories(xqt-shell PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../src ${CMAKE_CURRENT_LIST_DIR}/../src/app)
target_link_libraries(xqt-shell PUBLIC Qt6::Network Qt6::PrintSupport Qt6::Widgets Qt6::Quick xqt-canvas)
set_target_properties(xqt-shell PROPERTIES AUTOMOC ON)
# "Show in file manager" on Linux: org.freedesktop.FileManager1 over D-Bus, when Qt has D-Bus (not on Android; Qt
# on Windows and macOS has D-Bus too, but the file manager is reached another way there, see SystemApps.cpp)
if(TARGET Qt6::DBus AND NOT ANDROID AND NOT WIN32 AND NOT APPLE)
    target_link_libraries(xqt-shell PUBLIC Qt6::DBus)
    target_compile_definitions(xqt-shell PRIVATE XQT_HAVE_DBUS)
endif()

# The QML UI as a static QML module (XournalQt), used by the app and by the UI tests.
set(XQT_QML_FILES
    src/app/qml/Main.qml
    src/app/qml/IconButton.qml
    src/app/qml/TabStrip.qml
    src/app/qml/PageSidebar.qml
    src/app/qml/SettingsPage.qml
    src/app/qml/TabOverview.qml
    src/app/qml/SearchBar.qml
    src/app/qml/PageGrid.qml
    src/app/qml/HitBadge.qml
    src/app/qml/SearchFilterChip.qml
    src/app/qml/TouchpadMomentum.qml
    src/app/qml/PageDragOverlay.qml
    src/app/qml/PageArea.qml
    src/app/qml/PageMenu.qml
    src/app/qml/PageKeys.qml
    src/app/qml/Snackbar.qml
    src/app/qml/SelectionMark.qml
    src/app/qml/HomeView.qml
    src/app/qml/FolderChooser.qml
    src/app/qml/DocumentCard.qml
    src/app/qml/NewDocumentDialog.qml
    src/app/qml/DocumentModeCards.qml
    src/app/qml/DocumentModeDialog.qml
    src/app/qml/BackgroundPreview.qml
    src/app/qml/HighlightColors.qml
    src/app/qml/BackgroundChooser.qml
    src/app/qml/InsertPagesDialog.qml
    src/app/qml/NoteSpaceDialog.qml
    src/app/qml/ContentsOverview.qml
    src/app/qml/OutlineList.qml
    src/app/qml/AnnotationList.qml
    src/app/qml/TextFlowPanel.qml
    src/app/qml/MarkdownPanel.qml
    src/app/qml/EmojiSuggestions.qml
    src/app/qml/EmojiPicker.qml
    src/app/qml/CustomWidthPopup.qml
    src/app/qml/AppendPages.qml
    src/app/qml/PageJump.qml
    src/app/qml/BackgroundDialog.qml
    src/app/qml/ChapterDialog.qml
    src/app/qml/ContextPill.qml
    src/app/qml/PdfTextHandles.qml
    src/app/qml/PenPill.qml
    src/app/qml/GeometryPill.qml
    src/app/qml/PrintDialog.qml
    src/app/qml/ShortcutSheet.qml
    src/app/qml/LayerList.qml
    src/app/qml/PagePicture.qml
    src/app/qml/RaceWatch.qml
    src/app/qml/FuzzyToggle.qml
    src/app/qml/FuzzyHelp.qml
    src/app/qml/Fuzzy.js
    src/app/qml/ReferenceSplit.qml
    src/app/qml/CanvasScrollBars.qml
    src/app/qml/PdfTextPill.qml
    src/app/qml/NotePill.qml
    src/app/qml/SelectionPill.qml
    src/app/qml/Popups.js
    src/app/qml/MarkdownFormatBar.qml
    src/app/qml/MarkdownTableEditor.qml
    src/app/qml/LookUpMenu.qml
    src/app/qml/WebConfirm.qml
    src/app/qml/WebImageConfirm.qml
    src/app/qml/UnusedImagesDialog.qml
    src/app/qml/FindPaperSheet.qml
    src/app/qml/ArxivSheet.qml)
foreach(f ${XQT_QML_FILES})
    get_filename_component(alias ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${alias})
endforeach()
qt_add_library(xqt-ui STATIC)
qt_add_qml_module(xqt-ui
    URI XournalQt
    VERSION 1.0
    # Not next to the executables: a qmldir there would be found before the one in the resources.
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/XournalQt"
    QML_FILES ${XQT_QML_FILES}
)
target_link_libraries(xqt-ui PRIVATE Qt6::Quick Qt6::QuickControls2)

qt_add_executable(xournal-qt
    ${CMAKE_CURRENT_LIST_DIR}/../src/app/main.cpp
)
target_include_directories(xournal-qt PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/app)
target_link_libraries(xournal-qt PRIVATE Qt6::Widgets Qt6::Quick Qt6::QuickControls2 xqt-quick xqt-shell xqt-uiplugin)
target_compile_definitions(xournal-qt PRIVATE XQT_VERSION="${PROJECT_VERSION}")
set_target_properties(xournal-qt PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(WIN32)
    # A GUI program (no console window), and what it sets up before the core starts (docs/windows.md).
    set_target_properties(xournal-qt PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(xournal-qt PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../src/app/WindowsSetup.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/app/WindowsSetup.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../src/app/WindowsFonts.h
        ${CMAKE_CURRENT_LIST_DIR}/../src/app/WindowsFonts.cpp)
endif()

if(XQT_BUILD_TESTS)
    add_executable(xqt-quick-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/CanvasItemInputTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/CanvasItemRenderTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/GeometryToolTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/quick/ReferenceCanvasTest.cpp)
    target_link_libraries(xqt-quick-tests PRIVATE xqt-quick Qt6::QuickControls2 Qt6::GuiPrivate Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-quick-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    gtest_discover_tests(xqt-quick-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS quick
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # The real window (Main.qml) with an AppController, off-screen: shortcuts, sheets, tab overview.
    add_executable(xqt-ui-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/MainWindowTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/ReferenceWindowTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/DocumentLinksTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/AnnotationsPanelTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/ui/CitationsTest.cpp)
    target_link_libraries(xqt-ui-tests PRIVATE xqt-quick xqt-shell xqt-uiplugin Qt6::QuickControls2 Qt6::Test
        GTest::gtest)
    target_compile_definitions(xqt-ui-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    target_include_directories(xqt-ui-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-ui-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS ui
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(xqt-shell-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/TabsTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LinkRewriteTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/AnnotationsTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/ReferenceModeTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/PagesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/SettingsModelTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/RecoveryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/ExternalChangesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/SyncConflictsTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryFilesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryArchiveTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryFilterTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryKindsTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryFuzzyTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/RecentLibrariesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryHomeTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/LibraryCacheTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/CliTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/ThumbnailsTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/CanvasMemoryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/PastedPdfPagesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/PdfOnlyModeTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/TextPdfTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/PdfPrintingTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/CitationLibraryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/shell/ArxivTest.cpp)
    target_link_libraries(xqt-shell-tests PRIVATE xqt-shell Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-shell-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    target_include_directories(xqt-shell-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-shell-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS shell
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

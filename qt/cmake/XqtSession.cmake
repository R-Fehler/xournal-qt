# Qt-side application libraries: per-tab document sessions + shared application context.

# Runtime resources in the build tree (the install layout is <prefix>/share/xournal-qt).
set(XQT_BUILD_RESOURCE_DIR "${CMAKE_BINARY_DIR}/share/xournal-qt")
configure_file("${XOJ_UPSTREAM_DIR}/resources-templates/pagetemplates.ini.in"
    "${XQT_BUILD_RESOURCE_DIR}/pagetemplates.ini" COPYONLY)
file(COPY "${XOJ_UPSTREAM_DIR}/palettes" DESTINATION "${XQT_BUILD_RESOURCE_DIR}")
# The colour emoji font and its license (qt/resources/fonts/README.md): a file that fontconfig reads, <resources>/fonts
foreach(_xqt_font XqtEmoji.ttf LICENSE-NotoColorEmoji.txt)
    configure_file("${CMAKE_CURRENT_LIST_DIR}/../resources/fonts/${_xqt_font}" "${XQT_BUILD_RESOURCE_DIR}/fonts/${_xqt_font}"
        COPYONLY)
endforeach()

# The sRGB profile of archive PDFs (qt/resources/icc/README.md), compiled in as bytes
set(XQT_SRGB_ICC "${CMAKE_CURRENT_LIST_DIR}/../resources/icc/sRGB.icc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${XQT_SRGB_ICC}")
file(READ "${XQT_SRGB_ICC}" XQT_SRGB_HEX HEX)
file(SIZE "${XQT_SRGB_ICC}" XQT_SRGB_SIZE)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," XQT_SRGB_BYTES "${XQT_SRGB_HEX}")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/SrgbIcc.cpp" CONTENT
"// Generated from qt/resources/icc/sRGB.icc by XqtSession.cmake
namespace xqt::ArchivePdf {
extern const unsigned char SRGB_ICC[] = {
${XQT_SRGB_BYTES}
};
extern const unsigned long SRGB_ICC_SIZE = ${XQT_SRGB_SIZE};
}  // namespace xqt::ArchivePdf
" @ONLY)

add_library(xqt-session STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/AppContext.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/AppContext.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSession.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSession.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSave.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentMode.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentMode.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSaveTask.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSearch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentSearch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentTextIndex.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentTextIndex.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextMatch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextMatch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/WordMatch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/WordMatch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/Vocabulary.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/Vocabulary.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/FuzzyMatch.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/FuzzyMatch.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/FuzzyQuery.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/FuzzyQuery.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageOrderUndoAction.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/MergedPdf.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/MergedPdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/IncrementalPdf.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/IncrementalPdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/HybridPdf.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/HybridPdf.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/ArchivePdf.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/ArchivePdf.cpp
    ${CMAKE_BINARY_DIR}/generated/SrgbIcc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PdfPageKeeper.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PdfPageKeeper.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageOrderUndoAction.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/HeadlessViews.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/SessionActions.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextFile.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextFile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextDocument.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/TextDocument.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentLink.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/DocumentLink.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/StickyNote.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/StickyNote.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageNoteSpace.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PageNoteSpace.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/Citation.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/Citation.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PdfTitle.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/session/PdfTitle.cpp
)
target_include_directories(xqt-session PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src" "${CMAKE_CURRENT_LIST_DIR}/../src/session")
target_link_libraries(xqt-session PUBLIC Qt6::Core xoj-render xoj-core xqt-markdown)
target_compile_definitions(xqt-session PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
set_target_properties(xqt-session PROPERTIES AUTOMOC ON)

# Canvas model: pages (port of XojPageView), layout, zoom/scroll, input (port of PenInputHandler & co.)
add_library(xqt-canvas STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/DocumentLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/DocumentLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ViewController.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ViewController.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ScreenCalibration.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ScreenCalibration.cpp
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
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/GeometryToolPicture.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/GeometryToolPicture.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasInput.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasInput.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/PenHover.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/PenHover.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextEditor.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextEditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextFlow.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/TextFlow.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownSession.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownSession.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownEditor.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownEditor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/EmojiCompletion.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/EmojiCompletion.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownFile.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/MarkdownFile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ImageFile.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/ImageFile.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/CanvasTextInput.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/StickyNotes.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/canvas/StickyNotes.cpp
)
target_include_directories(xqt-canvas PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/canvas")
target_link_libraries(xqt-canvas PUBLIC Qt6::Gui xqt-session xoj-tools)
set_target_properties(xqt-canvas PROPERTIES AUTOMOC ON)

if(XQT_BUILD_TESTS)
    add_executable(xqt-session-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentSessionTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentSearchTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/FuzzyQueryTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/MergedPdfTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/HybridPdfTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/IncrementalPdfTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/BackgroundSaveTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/TextFileTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/DocumentLinkTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/StickyNoteTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/NoteSpaceTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/session/CitationTest.cpp)
    target_link_libraries(xqt-session-tests PRIVATE xqt-session Qt6::Test GTest::gtest)
    target_include_directories(xqt-session-tests PRIVATE "${TEST_CONFIG_DIR}")
    target_compile_definitions(xqt-session-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}"
        # upstream Xournal++ built beside the fork (as for the golden tests): StickyNoteTest opens a file with it
        XQT_UPSTREAM_BIN="${XOJ_UPSTREAM_DIR}/../xournalpp/build/xournalpp")
    gtest_discover_tests(xqt-session-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS session
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(xqt-canvas-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/CanvasReplayTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/DocumentLayoutTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/GeometryToolPictureTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/TextFlowTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/MarkdownSessionTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/MarkdownEditorTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/TextDocumentTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/PdfTextDocumentTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/EmojiEditingTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/ScreenCalibrationTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/canvas/SecondViewTest.cpp)
    target_link_libraries(xqt-canvas-tests PRIVATE xqt-canvas Qt6::Test GTest::gtest)
    target_compile_definitions(xqt-canvas-tests PRIVATE XQT_BUILD_RESOURCE_DIR="${XQT_BUILD_RESOURCE_DIR}")
    target_include_directories(xqt-canvas-tests PRIVATE "${TEST_CONFIG_DIR}")
    gtest_discover_tests(xqt-canvas-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS canvas
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

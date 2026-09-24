# Markdown boxes: parse (md4c), lay out and draw (Pango / Cairo) Markdown on pages. Qt-free: the renderer runs in
# upstream's render path (worker threads, thumbnails, PDF export).

add_library(xqt-md4c STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c/md4c.c
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c/md4c.h)
target_include_directories(xqt-md4c PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c")
target_compile_definitions(xqt-md4c PUBLIC MD4C_USE_UTF8)
set_target_properties(xqt-md4c PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)

add_library(xqt-markdown STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdDocument.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdDocument.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdBox.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdBox.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdHighlight.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdHighlight.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPaginate.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPaginate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPassages.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPassages.cpp)
target_include_directories(xqt-markdown PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/markdown")
target_link_libraries(xqt-markdown PUBLIC xoj-core PRIVATE xqt-md4c)

# Syntax highlighting of code blocks (optional; Debian / Ubuntu: libkf6syntaxhighlighting-dev)
find_package(KF6SyntaxHighlighting QUIET)
if(KF6SyntaxHighlighting_FOUND)
    message(STATUS "Markdown code blocks: syntax highlighting with KSyntaxHighlighting ${KF6SyntaxHighlighting_VERSION}")
    target_link_libraries(xqt-markdown PRIVATE KF6::SyntaxHighlighting)
    target_compile_definitions(xqt-markdown PRIVATE XQT_HAVE_KSYNTAXHIGHLIGHTING)
else()
    message(STATUS "Markdown code blocks: no syntax highlighting (KF6SyntaxHighlighting not found)")
endif()
set_target_properties(xqt-markdown PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(XQT_BUILD_TESTS)
    add_executable(xqt-markdown-tests
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdDocumentTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdLayoutTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdBoxTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdHighlightTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdPaginateTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdPassagesTest.cpp)
    target_link_libraries(xqt-markdown-tests PRIVATE xqt-markdown GTest::gtest)
    set_target_properties(xqt-markdown-tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    gtest_discover_tests(xqt-markdown-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS markdown)
endif()

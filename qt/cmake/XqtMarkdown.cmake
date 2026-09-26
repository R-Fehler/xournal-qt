# Markdown boxes: parse (md4c), lay out and draw (Pango / Cairo) Markdown on pages. Qt-free: the renderer runs in
# upstream's render path (worker threads, thumbnails, PDF export).

add_library(xqt-md4c STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c/md4c.c
    ${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c/md4c.h)
target_include_directories(xqt-md4c PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/md4c")
target_compile_definitions(xqt-md4c PUBLIC MD4C_USE_UTF8)
set_target_properties(xqt-md4c PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)

# MicroTeX (vendored, qt/3rdparty/microtex): the formulas of the Markdown text ($…$, $$…$$). Its glyphs are drawn
# as paths from its own font data (Latin Modern Math, compiled into the binary), so it needs no font files, no
# LaTeX and no platform backend: qt/src/markdown/MdMath.cpp draws with Cairo.
set(XQT_MICROTEX_DIR "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/microtex")
file(GLOB_RECURSE XQT_MICROTEX_SOURCES CONFIGURE_DEPENDS "${XQT_MICROTEX_DIR}/src/*.cpp")
add_library(xqt-microtex STATIC ${XQT_MICROTEX_SOURCES})
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/microtex/microtexconfig.h" CONTENT
    "#pragma once\n#define MICROTEX_VERSION_MAJOR 1\n#define MICROTEX_VERSION_MINOR 0\n#define MICROTEX_VERSION_PATCH 0\n")
target_include_directories(xqt-microtex PUBLIC "${XQT_MICROTEX_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/microtex")
# Glyphs as paths only (GLYPH_RENDER_TYPE_PATH); no font files looked for on disk (no HAVE_AUTO_FONT_FIND)
target_compile_definitions(xqt-microtex PUBLIC GLYPH_RENDER_TYPE=1)
# (C++17: it uses u8"" literals as std::string)
set_target_properties(xqt-microtex PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(xqt-microtex PRIVATE -w)  # (third-party code: its warnings are not ours)
endif()
# The math font's data (MicroTeX's .clm2 of Latin Modern Math, with the glyphs' outlines) as a C array
set(XQT_MATH_FONT "${XQT_MICROTEX_DIR}/res/lm-math/latinmodern-math.clm2")
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/microtex/math_font.c"
    COMMAND "${CMAKE_COMMAND}" -DINPUT=${XQT_MATH_FONT} -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/microtex/math_font.c
            -DNAME=xqt_math_font -P "${CMAKE_CURRENT_LIST_DIR}/XqtEmbedFile.cmake"
    DEPENDS "${XQT_MATH_FONT}" "${CMAKE_CURRENT_LIST_DIR}/XqtEmbedFile.cmake"
    COMMENT "Math font for the Markdown formulas"
    VERBATIM)
target_sources(xqt-microtex PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/microtex/math_font.c")

add_library(xqt-markdown STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdDocument.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdDocument.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdLayout.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdLayout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdMath.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdMath.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdImages.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdImages.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdBox.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdBox.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdHighlight.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdHighlight.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPaginate.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPaginate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPassages.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdPassages.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/EmojiFont.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/EmojiFont.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/EmojiData.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/EmojiData.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/Grapheme.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/Grapheme.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdTexDelimiters.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdTexDelimiters.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdFormat.h
    ${CMAKE_CURRENT_LIST_DIR}/../src/markdown/MdFormat.cpp)
target_include_directories(xqt-markdown PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../src/markdown")
# (the emoji names: gemoji's table, EmojiData.cpp)
target_include_directories(xqt-markdown PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../3rdparty/gemoji")
target_link_libraries(xqt-markdown PUBLIC xoj-core PRIVATE xqt-md4c xqt-microtex)

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
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdMathTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdBoxTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdHighlightTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdPaginateTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdPassagesTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/EmojiFontTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/EmojiDataTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdTexDelimitersTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdFormatTest.cpp
        ${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/MdImagesTest.cpp)
    target_link_libraries(xqt-markdown-tests PRIVATE xqt-markdown GTest::gtest)
    target_compile_definitions(xqt-markdown-tests PRIVATE
        XQT_MARKDOWN_GOLDEN="${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/golden"
        XQT_EMOJI_FONT="${CMAKE_CURRENT_LIST_DIR}/../resources/fonts/XqtEmoji.ttf"
        XQT_MARKDOWN_TEST_FONTS="${CMAKE_CURRENT_LIST_DIR}/../tests/markdown/fonts")
    set_target_properties(xqt-markdown-tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    gtest_discover_tests(xqt-markdown-tests DISCOVERY_TIMEOUT 30 PROPERTIES LABELS markdown)
endif()

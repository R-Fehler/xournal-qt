/*
 * xournal-qt: syntax highlighting of code blocks.
 *
 * @license GNU GPLv2 or later
 */
#include <string>

#include <gtest/gtest.h>

#include "MdHighlight.h"
#include "MdLayout.h"

using namespace xqt::md;

TEST(MdHighlight, knownLanguagesGetColors) {
    if (!highlightingAvailable()) {
        GTEST_SKIP() << "built without KSyntaxHighlighting";
    }
    const std::string code = "def greet(name):\n    return \"hi \" + name  # comment\n";
    for (const char* language: {"python", "Python", "py"}) {
        const auto spans = highlight(code, language);
        ASSERT_FALSE(spans.empty()) << language;
        bool keyword = false;
        bool string = false;
        for (const CodeSpan& s: spans) {
            ASSERT_GE(s.start, 0);
            ASSERT_LE(s.start + s.length, static_cast<int>(code.size()));
            const std::string part = code.substr(static_cast<size_t>(s.start), static_cast<size_t>(s.length));
            keyword = keyword || part == "def";
            string = string || part.find("hi") != std::string::npos;
        }
        EXPECT_TRUE(keyword) << language;
        EXPECT_TRUE(string) << language;
    }
    EXPECT_FALSE(highlight("int x = 1;", "cpp").empty());
    EXPECT_FALSE(highlight("echo $HOME", "bash").empty());
    EXPECT_TRUE(highlight("anything", "no-such-language").empty());
    EXPECT_TRUE(highlight("int x;", "").empty());
}

TEST(MdHighlight, byteOffsetsAfterNonAsciiText) {
    if (!highlightingAvailable()) {
        GTEST_SKIP() << "built without KSyntaxHighlighting";
    }
    const std::string code = "s = \"\xc3\xa4\xc3\xb6\xc3\xbc\"  # \xe2\x82\xac\nx = 1\n";  // äöü, €
    bool comment = false;
    for (const CodeSpan& s: highlight(code, "python")) {
        const std::string part = code.substr(static_cast<size_t>(s.start), static_cast<size_t>(s.length));
        comment = comment || part.rfind("# \xe2\x82\xac", 0) == 0;
    }
    EXPECT_TRUE(comment) << "the comment's bytes, after the umlauts";
}

/*
 * xournal-qt: a Markdown text split into pages, each page's slice a Markdown text of its own.
 *
 * @license GNU GPLv2 or later
 */
#include <string>

#include <gtest/gtest.h>

#include "MdPaginate.h"

using namespace xqt::md;

namespace {
Style style() {
    Style s;
    s.size = 10;
    return s;
}
Frame small(size_t) { return {300, 160}; }

std::string paragraphs(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        s += "Paragraph " + std::to_string(i) +
             " has a few words, enough to wrap onto a second line in a box as narrow as this one is.\n\n";
    }
    return s;
}

/// Every page but the last fits (no block too high here), and joining the pages gives the text again.
void expectGoodPages(const std::string& text, const Pagination& p, Frame frame = small(0)) {
    ASSERT_GE(p.slices.size(), 2u) << "more than one page";
    EXPECT_EQ(join(p.slices), text) << "joined again: the same text";
    for (size_t i = 0; i < p.slices.size(); ++i) {
        Style s = style();
        s.width = frame.width;
        const double h = layout(parse(p.slices[i]), s).height;
        EXPECT_LE(h, frame.height + 0.5) << "page " << i << ":\n" << p.slices[i];
        EXPECT_EQ(continues(p.slices[i]), i > 0) << "page " << i;
    }
    // Split again: the same pages
    const Pagination again = paginate(join(p.slices), style(), small);
    EXPECT_EQ(again.slices, p.slices);
}
}  // namespace

TEST(MdPaginate, shortTextIsOnePage) {
    const auto p = paginate("# Title\n\nShort.\n", style(), small);
    ASSERT_EQ(p.slices.size(), 1u);
    EXPECT_EQ(p.slices[0], "# Title\n\nShort.\n");
    EXPECT_EQ(p.overflow, 0);
}

TEST(MdPaginate, paragraphsFlowOntoPages) {
    const std::string text = "# Notes\n\n" + paragraphs(12);
    const auto p = paginate(text, style(), small);
    expectGoodPages(text, p);
    EXPECT_EQ(p.overflow, 0);
}

TEST(MdPaginate, longParagraphIsSplitBetweenLines) {
    std::string text;
    for (int i = 0; i < 200; ++i) {
        text += "word" + std::to_string(i) + " ";
    }
    text += "**bold part** and [a link](https://example.org) at the end.\n";
    const auto p = paginate(text, style(), small);
    expectGoodPages(text, p);
    // Each page is one paragraph (the continuation starts with the comment)
    for (size_t i = 1; i < p.slices.size(); ++i) {
        EXPECT_TRUE(p.slices[i].rfind("<!-- xqt:cont p -->\n", 0) == 0) << p.slices[i];
    }
}

TEST(MdPaginate, headingGoesWithTheBlockAfterIt) {
    // Paragraphs fill the page, then a heading and a paragraph that only fit on the next page
    const std::string text = paragraphs(3) + "## Section\n\n" + paragraphs(1);
    const Frame frame{300, 160};
    const auto p = paginate(text, style(), [&](size_t) { return frame; });
    ASSERT_GE(p.slices.size(), 2u);
    EXPECT_EQ(join(p.slices), text);
    for (size_t i = 0; i + 1 < p.slices.size(); ++i) {
        const Document d = parse(p.slices[i]);
        ASSERT_FALSE(d.root.children.empty());
        EXPECT_NE(d.root.children.back().kind, BlockKind::Heading) << "a page ends with a heading:\n" << p.slices[i];
    }
}

TEST(MdPaginate, codeBlocksAreSplitWithTheirFences) {
    std::string text = "Code:\n\n```python\n";
    for (int i = 0; i < 40; ++i) {
        text += "print(" + std::to_string(i) + ")\n";
    }
    text += "```\n\nAfter.\n";
    const auto p = paginate(text, style(), small);
    expectGoodPages(text, p);
    EXPECT_EQ(p.slices[0].substr(p.slices[0].size() - 4), "```\n") << "the page closes the fence";
    EXPECT_TRUE(p.slices[1].rfind("<!-- xqt:cont code -->\n```python\n", 0) == 0) << p.slices[1];
    const Document second = parse(p.slices[1]);
    ASSERT_GE(second.root.children.size(), 2u);
    EXPECT_EQ(second.root.children[1].kind, BlockKind::CodeBlock);
    EXPECT_EQ(second.root.children[1].language, "python") << "highlighted on the next page as well";
}

TEST(MdPaginate, tablesRepeatTheirHeader) {
    std::string text = "| Name | Value |\n|:-----|------:|\n";
    for (int i = 0; i < 30; ++i) {
        text += "| row " + std::to_string(i) + " | " + std::to_string(i * i) + " |\n";
    }
    const auto p = paginate(text, style(), small);
    expectGoodPages(text, p);
    EXPECT_TRUE(p.slices[1].rfind("<!-- xqt:cont table -->\n| Name | Value |\n|:-----|------:|\n", 0) == 0)
            << p.slices[1];
}

TEST(MdPaginate, numberedListsGoOnWithTheirNumbers) {
    std::string text;
    for (int i = 0; i < 30; ++i) {
        text += "1. item number " + std::to_string(i + 1) + "\n";  // (all "1.": numbered by the list)
    }
    const auto p = paginate(text, style(), small);
    expectGoodPages(text, p);
    const std::string& second = p.slices[1];
    const auto start = second.find("start=");
    ASSERT_NE(start, std::string::npos) << second;
    const unsigned n = static_cast<unsigned>(std::stoul(second.substr(start + 6)));
    EXPECT_GT(n, 1u);
    EXPECT_NE(second.find("item number " + std::to_string(n) + "\n"), std::string::npos)
            << "the page starts with item " << n << ":\n" << second;
    // Drawn with its number
    Style s = style();
    s.width = 300;
    const Layout l = layout(parse(second), s);
    bool numbered = false;
    for (const Item& it: l.items) {
        if (it.kind == Item::Kind::Text && std::string(pango_layout_get_text(it.layout.get())) == std::to_string(n) + ".") {
            numbered = true;
        }
    }
    EXPECT_TRUE(numbered);
}

TEST(MdPaginate, aBlockHigherThanAPageStaysOnItsPage) {
    std::string quote;
    for (int i = 0; i < 40; ++i) {
        quote += "> quoted paragraph " + std::to_string(i) + "\n>\n";
    }
    const std::string text = "Before.\n\n" + quote + "\nAfter.\n";
    const auto p = paginate(text, style(), small);
    EXPECT_EQ(join(p.slices), text);
    EXPECT_GT(p.overflow, 0) << "the quote goes below its page";
    EXPECT_GE(p.slices.size(), 2u);
}

TEST(MdPaginate, pagesOfDifferentSizes) {
    const std::string text = paragraphs(20);
    const auto p = paginate(text, style(), [](size_t i) { return i == 0 ? Frame{300, 100} : Frame{400, 300}; });
    EXPECT_EQ(join(p.slices), text);
    Style s = style();
    s.width = 300;
    EXPECT_LE(layout(parse(p.slices[0]), s).height, 100.5);
}

// Mixed texts, split on pages of a few heights: joining the pages always gives the text again, and splitting that
// again gives the same pages.
TEST(MdPaginate, joinedAgainAlwaysTheSameText) {
    const std::vector<std::string> pieces = {
            "# Heading\n\n",
            "## Sub heading\n\n",
            "A paragraph with **bold**, *italic*, `code` and a [link](https://x.org) in it, long enough to wrap.\n\n",
            "Soft\nbreaks\nin a\nparagraph.\n\n",
            "- item one\n- item two\n  - nested\n- item three\n\n",
            "3. three\n4. four\n5. five\n\n",
            "- [ ] open\n- [x] done\n\n",
            "```cpp\nint a = 1;\nint b = 2;\nint c = a + b;\n```\n\n",
            "~~~\nplain code\n~~~\n\n",
            "| a | b |\n|---|---|\n| 1 | 2 |\n| 3 | 4 |\n| 5 | 6 |\n\n",
            "> a quote\n> of two lines\n\n",
            "---\n\n",
            "Setext heading\n==============\n\n",
            "Text &amp; entities &copy; here.\n\n",
            "\n\n",
    };
    unsigned seed = 12345;
    const auto next = [&]() {
        seed = seed * 1103515245u + 12345u;
        return (seed >> 16) & 0x7fff;
    };
    for (int doc = 0; doc < 25; ++doc) {
        std::string text;
        const int n = 8 + static_cast<int>(next() % 25);
        for (int i = 0; i < n; ++i) {
            text += pieces[next() % pieces.size()];
        }
        for (double h: {90.0, 150.0, 400.0}) {
            const auto frame = [h](size_t) { return Frame{260, h}; };
            const auto p = paginate(text, style(), frame);
            ASSERT_EQ(join(p.slices), text) << "document " << doc << ", height " << h;
            EXPECT_EQ(paginate(join(p.slices), style(), frame).slices, p.slices) << "document " << doc;
        }
    }
}

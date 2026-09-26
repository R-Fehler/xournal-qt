/*
 * xournal-qt: a Markdown text split into pages, each page's slice a Markdown text of its own.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cstdlib>
#include <iostream>
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
        if (it.kind == Item::Kind::Text &&
            std::string(pango_layout_get_text(it.layout.get())) == std::to_string(n) + ".") {
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

// Each page knows what of the text it holds: its slice is its prefix, that part of the text, and lines closing it.
TEST(MdPaginate, partsSayWhatOfTheTextAPageHolds) {
    std::string text = "# Parts\n\n" + paragraphs(6) + "```cpp\n";
    for (int i = 0; i < 30; ++i) {
        text += "int x" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
    }
    text += "```\n\n| a | b |\n|---|---|\n";
    for (int i = 0; i < 20; ++i) {
        text += "| " + std::to_string(i) + " | x |\n";
    }
    const auto p = paginate(text, style(), small);
    ASSERT_EQ(p.parts.size(), p.slices.size());
    std::vector<Part> joined;
    ASSERT_EQ(join(p.slices, &joined), text);
    ASSERT_EQ(joined.size(), p.parts.size());
    size_t at = 0;
    for (size_t i = 0; i < p.parts.size(); ++i) {
        const Part& part = p.parts[i];
        EXPECT_EQ(part.begin, at) << "page " << i;
        EXPECT_EQ(p.slices[i].substr(part.prefix, part.end - part.begin),
                  text.substr(part.begin, part.end - part.begin))
                << "page " << i;
        EXPECT_EQ(joined[i].begin, part.begin) << "page " << i;
        EXPECT_EQ(joined[i].end, part.end) << "page " << i;
        EXPECT_EQ(joined[i].prefix, part.prefix) << "page " << i;
        at = part.end;
    }
    EXPECT_EQ(at, text.size());
}

// Opt-in (XQT_BENCH=1): how long splitting a long text takes (once per key while writing on the page).
TEST(MdPaginate, benchLongText) {
    if (!std::getenv("XQT_BENCH")) {
        GTEST_SKIP() << "set XQT_BENCH";
    }
    std::string text;
    for (int i = 0; i < 20; ++i) {
        text += "## Section " + std::to_string(i) + "\n\n" + paragraphs(6);
    }
    const auto frame = [](size_t) { return Frame{480, 730}; };
    const auto start = std::chrono::steady_clock::now();
    Pagination p;
    for (int i = 0; i < 5; ++i) {
        p = paginate(text + std::to_string(i), style(), frame);
    }
    const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << p.slices.size() << " pages, " << text.size() << " bytes: " << ms / 5 << " ms per split" << std::endl;

    {
        const auto a = std::chrono::steady_clock::now();
        const Document d = parse(text);
        const auto b = std::chrono::steady_clock::now();
        Style s = style();
        s.width = 480;
        const Layout l = layout(d, s);
        const auto c = std::chrono::steady_clock::now();
        std::cout << "parse " << std::chrono::duration_cast<std::chrono::microseconds>(b - a).count() << " us, layout "
                  << std::chrono::duration_cast<std::chrono::microseconds>(c - b).count() << " us (" << l.items.size()
                  << " items)" << std::endl;
    }
    // Typing on the second page: only the pages around the change again
    const std::string base = text + "0";
    Pagination before = paginate(base, style(), frame);
    const size_t at = before.parts[1].begin + 200;
    const auto t0 = std::chrono::steady_clock::now();
    std::string typed = base;
    for (int i = 0; i < 20; ++i) {
        std::string next = typed;
        next.insert(at + static_cast<size_t>(i), "x");
        Pagination after = paginate(next, style(), frame, &before, &typed);
        before = std::move(after);
        typed = std::move(next);
    }
    const auto typing =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "typing: " << typing / 20 / 1000.0 << " ms per key" << std::endl;
}

// Splitting again after a change gives the same pages as splitting all (only the pages around it are split).
TEST(MdPaginate, splittingAfterAChangeIsTheSameAsSplittingAll) {
    std::string text = "# Changes\n\n" + paragraphs(30) + "```\ncode\n```\n\n" + paragraphs(10);
    const auto frame = small;
    Pagination before = paginate(text, style(), frame);
    ASSERT_GE(before.slices.size(), 5u);
    const std::vector<std::pair<size_t, std::string>> edits = {
            {before.parts[2].begin + 5, "inserted words "},            // on the third page
            {before.parts[1].begin, "# A new heading\n\n"},          // at the start of a page
            {before.parts[3].end - 1, "\n\n" + paragraphs(4)},        // a lot more
            {0, "x"},                                                  // at the very start
    };
    for (const auto& [at, what]: edits) {
        std::string changed = text;
        changed.insert(std::min(at, changed.size()), what);
        const Pagination incremental = paginate(changed, style(), frame, &before, &text);
        const Pagination all = paginate(changed, style(), frame);
        EXPECT_EQ(incremental.slices, all.slices) << "inserted at " << at;
        ASSERT_EQ(incremental.parts.size(), all.parts.size());
        for (size_t i = 0; i < all.parts.size(); ++i) {
            EXPECT_EQ(incremental.parts[i].begin, all.parts[i].begin);
            EXPECT_EQ(incremental.parts[i].end, all.parts[i].end);
        }
        before = incremental;
        text = changed;
    }
    // And a deletion
    std::string shorter = text;
    shorter.erase(before.parts[1].begin, before.parts[3].begin - before.parts[1].begin);
    EXPECT_EQ(paginate(shorter, style(), frame, &before, &text).slices, paginate(shorter, style(), frame).slices);
}

// --- plain text (a .txt edited): no Markdown, lines as they are -----------------------------------------------------

namespace {
Style plainStyle() {
    Style s = style();
    s.family = "Monospace";
    s.plain = true;
    return s;
}
std::string plainLines(int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
        s += i % 7 == 3 ? "\n" : "# line " + std::to_string(i) + " with **stars**, `ticks` and - a dash\n";
    }
    return s;
}
}  // namespace

TEST(MdPlain, aPlainTextIsLaidOutLineByLineWithoutFormatting) {
    const std::string source = std::string(PLAIN_MARKER) + "\n# not a heading\n**not bold**\n\n- not a list\n";
    const Document doc = parse(source);
    ASSERT_TRUE(doc.plain);
    ASSERT_EQ(doc.root.children.size(), 6u) << "the marker, four lines, the empty last line";
    const Layout lay = layout(doc, plainStyle());
    std::vector<std::string> texts;
    for (const Item& it: lay.items) {
        ASSERT_EQ(it.kind, Item::Kind::Text) << "no rules, fills or check boxes";
        texts.emplace_back(pango_layout_get_text(it.layout.get()));
        for (const SourceMap& m: it.sources) {
            EXPECT_EQ(m.flags, 0) << "no formatting";
        }
    }
    EXPECT_EQ(texts, (std::vector<std::string>{"# not a heading", "**not bold**", "", "- not a list", ""}));
    // Every line as high as the others (an empty one too): no heading sizes, no paragraph spacing. The empty line
    // after the last line break takes room only with the cursor on it.
    EXPECT_EQ(lay.items.back().height, 0);
    EXPECT_NEAR(lay.height, 4 * lay.items[0].height, 0.01);
    for (size_t i = 1; i + 1 < lay.items.size(); ++i) {
        EXPECT_NEAR(lay.items[i].height, lay.items[0].height, 0.01) << i;
        EXPECT_NEAR(lay.items[i].y, lay.items[i - 1].y + lay.items[i - 1].height, 0.01) << i;
    }
    // While written: the line with the cursor is the raw item, its text exactly its source
    const size_t cursor = source.find("not bold") + 2;
    const Layout editing = layout(doc, plainStyle(), source, cursor);
    ASSERT_GE(editing.rawItem, 0);
    EXPECT_EQ(source.substr(editing.rawBegin, editing.rawEnd - editing.rawBegin), "**not bold**");
    const Layout atEnd = layout(doc, plainStyle(), source, source.size());
    EXPECT_EQ(atEnd.rawItem, static_cast<int>(atEnd.items.size()) - 1);
    EXPECT_NEAR(atEnd.height, 5 * lay.items[0].height, 0.01);
}

TEST(MdPlain, aPlainTextFlowsOntoPagesAndJoinsAgain) {
    const std::string text = plainLines(80);
    const Pagination p = paginate(text, plainStyle(), small);
    ASSERT_GE(p.slices.size(), 3u);
    EXPECT_EQ(join(p.slices), text);
    for (size_t i = 0; i < p.slices.size(); ++i) {
        EXPECT_TRUE(isPlain(p.slices[i])) << "every page is plain text: " << i;
        EXPECT_EQ(continues(p.slices[i]), i > 0);
        const Layout l = layout(parse(p.slices[i]), plainStyle());
        EXPECT_LE(l.height, small(0).height + 0.5) << i << "\n" << p.slices[i] << "\nitems " << l.items.size()
                                                   << " first h " << (l.items.empty() ? 0 : l.items[0].height);
    }
    // A line longer than a page is split within it, and joined again
    std::string longLine;
    for (int i = 0; i < 400; ++i) {
        longLine += "word" + std::to_string(i) + " ";
    }
    const std::string text2 = "start\n" + longLine + "\nend";
    const Pagination q = paginate(text2, plainStyle(), small);
    ASSERT_GE(q.slices.size(), 2u);
    EXPECT_EQ(join(q.slices), text2) << "no newline added at the end, none lost";
    // An empty plain text is a page with its marker (and a line for the cursor)
    const Pagination e = paginate("", plainStyle(), small);
    ASSERT_EQ(e.slices.size(), 1u);
    EXPECT_EQ(join(e.slices), "");
}

// A page break (the formatting bar's "Page break") ends the page after it; it is not drawn.
TEST(MdPaginate, aPageBreakEndsThePage) {
    const std::string pageBreak = "<div style=\"page-break-after: always\"></div>";
    const std::string text = "# One\n\nShort.\n\n" + pageBreak + "\n\n# Two\n\nAlso short.\n";
    const auto p = paginate(text, style(), small);
    ASSERT_EQ(p.slices.size(), 2u);
    EXPECT_EQ(p.slices[0], "# One\n\nShort.\n\n" + pageBreak + "\n\n");
    EXPECT_EQ(p.slices[1], "<!-- xqt:cont block -->\n# Two\n\nAlso short.\n");
    EXPECT_EQ(join(p.slices), text);
    Style s = style();
    s.width = 300;
    EXPECT_EQ(layout(parse(pageBreak + "\n"), s).items.size(), 0u) << "not drawn";
    // Text that does not fit before it: split as always, the break ends the page it is on
    const std::string longer = "# Notes\n\n" + paragraphs(6) + pageBreak + "\n\nAfter the break.\n";
    const auto q = paginate(longer, style(), small);
    ASSERT_GE(q.slices.size(), 3u);
    EXPECT_EQ(join(q.slices), longer);
    EXPECT_NE(q.slices[q.slices.size() - 2].find(pageBreak), std::string::npos);
    EXPECT_EQ(q.slices.back(), "<!-- xqt:cont block -->\nAfter the break.\n");
    // At the top of a page (nothing before it there): no empty page
    const auto r = paginate(pageBreak + "\n\nFirst.\n", style(), small);
    EXPECT_EQ(r.slices.size(), 1u);
}

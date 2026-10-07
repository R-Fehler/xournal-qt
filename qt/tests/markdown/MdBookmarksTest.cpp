/*
 * xournal-qt: bookmarks in a Markdown text, "<!-- xqt:bookmark label -->" before the block they mark
 * (qt/docs/features/bookmarks.md, "Markdown").
 *
 * @license GNU GPLv2 or later
 */
#include <string>

#include <gtest/gtest.h>

#include "MdBookmarks.h"
#include "MdPaginate.h"

using namespace xqt::md;
namespace bm = xqt::md::bookmarks;

namespace {
Style style() {
    Style s;
    s.size = 10;
    return s;
}
Frame small(size_t) { return {300, 160}; }
Frame narrow(size_t) { return {200, 160}; }

std::string paragraph(int i) {
    return "Paragraph " + std::to_string(i) +
           " has a few words, enough to wrap onto a second line in a box as narrow as this one is.\n\n";
}

/// The page whose slice has a bookmark (the first), and the page the text `marked` starts on.
struct Pages {
    int mark = -1;
    int block = -1;
};
Pages pagesOf(const Pagination& p, const std::string& marked) {
    Pages out;
    for (size_t i = 0; i < p.slices.size(); ++i) {
        if (out.mark < 0 && bm::ofPage(p.slices[i])) {
            out.mark = static_cast<int>(i);
        }
        if (out.block < 0 && p.slices[i].find(marked) != std::string::npos) {
            out.block = static_cast<int>(i);
        }
    }
    return out;
}
}  // namespace

TEST(MdBookmarks, theCommentIsFoundWithItsLabel) {
    EXPECT_EQ(bm::labelOf("<!-- xqt:bookmark Proof of theorem 3.2 -->"), "Proof of theorem 3.2");
    EXPECT_EQ(bm::labelOf("  <!--   xqt:bookmark   spaced   -->\n"), "spaced");
    EXPECT_EQ(bm::labelOf("<!-- xqt:bookmark -->"), "");
    EXPECT_EQ(bm::labelOf("<!--xqt:bookmark-->"), "");
    EXPECT_FALSE(bm::labelOf("<!-- xqt:bookmarks x -->"));
    EXPECT_FALSE(bm::labelOf("<!-- a comment -->"));
    EXPECT_FALSE(bm::labelOf("<!-- xqt:cont block -->"));
    EXPECT_FALSE(bm::labelOf("<!-- xqt:bookmark x --> and text"));
    EXPECT_FALSE(bm::labelOf("<!-- xqt:bookmark a\nb -->"));

    const std::string text = "# Title\n\nSome text.\n\n<!-- xqt:bookmark Proof -->\n## Proof\n\nMore.\n";
    const auto marks = bm::find(text);
    ASSERT_EQ(marks.size(), 1u);
    EXPECT_EQ(marks[0].label, "Proof");
    EXPECT_EQ(text.substr(marks[0].begin, marks[0].end - marks[0].begin), "<!-- xqt:bookmark Proof -->\n");
    EXPECT_EQ(text.substr(marks[0].begin, marks[0].lineEnd - marks[0].begin), "<!-- xqt:bookmark Proof -->");
    // Right after a paragraph (an HTML comment interrupts it) and as the last line without a line break
    EXPECT_EQ(bm::find("Text\n<!-- xqt:bookmark a -->\nmore").size(), 1u);
    const auto last = bm::find("Text\n\n<!-- xqt:bookmark end -->");
    ASSERT_EQ(last.size(), 1u);
    EXPECT_EQ(last[0].end, std::string("Text\n\n<!-- xqt:bookmark end -->").size());
}

TEST(MdBookmarks, anExampleInCodeIsNotOne) {
    EXPECT_TRUE(bm::find("```markdown\n<!-- xqt:bookmark example -->\n```\n").empty());
    EXPECT_TRUE(bm::find("~~~\n<!-- xqt:bookmark example -->\n~~~\n").empty());
    EXPECT_TRUE(bm::find("Write `<!-- xqt:bookmark example -->` before a block.\n").empty());
    EXPECT_TRUE(bm::find("Text\n\n    <!-- xqt:bookmark indented code -->\n").empty());
    EXPECT_TRUE(bm::find("> <!-- xqt:bookmark in a quote -->\n> text\n").empty());  // (top-level blocks only)
    EXPECT_FALSE(bm::ofPage("```\n<!-- xqt:bookmark example -->\n```\n"));
    // A plain text (a .txt) has none
    EXPECT_TRUE(bm::find(std::string(PLAIN_MARKER) + "\n<!-- xqt:bookmark x -->\n").empty());
}

TEST(MdBookmarks, labelsStayOnOneLineAndKeepTheCommentValid) {
    EXPECT_EQ(bm::cleanLabel("  two\nlines\r\n and\ttabs  "), "two lines and tabs");
    EXPECT_EQ(bm::cleanLabel("a -- b --> c"), "a \xe2\x80\x93 b \xe2\x80\x93> c");
    EXPECT_EQ(bm::cleanLabel("---"), "\xe2\x80\x93-");
    EXPECT_EQ(bm::comment("Proof"), "<!-- xqt:bookmark Proof -->");
    EXPECT_EQ(bm::comment("  "), "<!-- xqt:bookmark -->");
    for (const char* label: {"Proof of theorem 3.2", "a -- b", "x -->", "ends with -", "<b>html</b>", "Ünïcödé 🔖"}) {
        const std::string c = bm::comment(label);
        EXPECT_EQ(c.find("--", 4), c.size() - 3) << c;  // (no "--" before the end)
        EXPECT_EQ(bm::labelOf(c), bm::cleanLabel(label)) << c;
        EXPECT_EQ(bm::find(c + "\n# H\n").size(), 1u) << c;
    }
}

TEST(MdBookmarks, anEmptyLabelIsNamedAfterTheHeadingOrThePage) {
    auto p = bm::ofPage("<!-- xqt:bookmark -->\n## The *proof*\n\nText.\n");
    ASSERT_TRUE(p);
    EXPECT_TRUE(p->isAutomatic);
    EXPECT_EQ(p->label, "The proof");
    p = bm::ofPage("# First\n\nText.\n\n<!-- xqt:bookmark -->\nMore text.\n\n## Second\n");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->label, "First");  // (not a heading: the page's first one)
    p = bm::ofPage("<!-- xqt:bookmark -->\nText without headings.\n");
    ASSERT_TRUE(p);
    EXPECT_EQ(p->label, "");  // ("Page N")
    p = bm::ofPage("<!-- xqt:bookmark Mine -->\n# Heading\n");
    ASSERT_TRUE(p);
    EXPECT_FALSE(p->isAutomatic);
    EXPECT_EQ(p->label, "Mine");
    EXPECT_EQ(p->automatic, "Heading");
    EXPECT_FALSE(bm::ofPage("# No bookmark\n\n<!-- a comment -->\n"));
}

TEST(MdBookmarks, severalOnAPageTheFirstNamesIt) {
    const std::string text = "<!-- xqt:bookmark One -->\n# A\n\nText.\n\n<!-- xqt:bookmark Two -->\n## B\n\nEnd.\n";
    EXPECT_EQ(bm::find(text).size(), 2u);
    EXPECT_EQ(bm::ofPage(text)->label, "One");
    // Removing the page's bookmark removes both lines, in one change
    const auto e = bm::remove(text, 0, text.size());
    ASSERT_TRUE(e);
    std::string after = text;
    after.replace(e->from, e->to - e->from, e->with);
    EXPECT_EQ(after, "# A\n\nText.\n\n## B\n\nEnd.\n");
    // Renaming renames the first
    const auto r = bm::rename(text, 0, text.size(), "Uno");
    ASSERT_TRUE(r);
    after = text;
    after.replace(r->from, r->to - r->from, r->with);
    EXPECT_EQ(after.substr(0, after.find('\n')), "<!-- xqt:bookmark Uno -->");
    EXPECT_EQ(after.substr(after.find('\n')), text.substr(text.find('\n')));
}

TEST(MdBookmarks, theCommentTakesNoRoom) {
    Style s = style();
    const double without = layout(parse("Text.\n\n# Heading\n\nMore.\n"), s).height;
    const double with = layout(parse("Text.\n\n<!-- xqt:bookmark Here -->\n# Heading\n\nMore.\n"), s).height;
    EXPECT_NEAR(with, without, 0.01);
    const double first = layout(parse("<!-- xqt:bookmark -->\nText.\n"), s).height;
    EXPECT_NEAR(first, layout(parse("Text.\n"), s).height, 0.01);
}

TEST(MdBookmarks, itsPageIsThePageOfTheBlockItMarksAlsoAfterAReflow) {
    // Before each paragraph in turn: the page of the bookmark is the page that paragraph starts on (also where the
    // paragraph is pushed onto the next page), on wider and narrower pages
    for (int at = 1; at < 14; ++at) {
        std::string text = "# Notes\n\n";
        for (int i = 0; i < 16; ++i) {
            if (i == at) {
                text += "<!-- xqt:bookmark Mark -->\n";
            }
            text += paragraph(i);
        }
        const std::string marked = "Paragraph " + std::to_string(at) + " has";
        for (auto frame: {small, narrow}) {
            const Pagination p = paginate(text, style(), frame);
            ASSERT_GE(p.slices.size(), 3u);
            EXPECT_EQ(join(p.slices), text);
            const Pages pages = pagesOf(p, marked);
            EXPECT_EQ(pages.mark, pages.block) << "before paragraph " << at;
        }
    }
    // Text added before it: it moves on with its paragraph
    std::string text = "# Notes\n\n";
    for (int i = 0; i < 12; ++i) {
        text += (i == 6 ? "<!-- xqt:bookmark Mark -->\n" : "") + paragraph(i);
    }
    const int before = pagesOf(paginate(text, style(), small), "Paragraph 6 has").mark;
    const std::string longer = "# Notes\n\n" + paragraph(100) + paragraph(101) + text.substr(9);
    const Pages after = pagesOf(paginate(longer, style(), small), "Paragraph 6 has");
    EXPECT_GT(after.mark, before);
    EXPECT_EQ(after.mark, after.block);
}

TEST(MdBookmarks, aBookmarkAtTheEndIsOnTheLastPage) {
    std::string text = "# Notes\n\n";
    for (int i = 0; i < 10; ++i) {
        text += paragraph(i);
    }
    text += "<!-- xqt:bookmark The end -->\n";
    const Pagination p = paginate(text, style(), small);
    ASSERT_GE(p.slices.size(), 2u);
    EXPECT_TRUE(bm::ofPage(p.slices.back()));
    for (size_t i = 0; i + 1 < p.slices.size(); ++i) {
        EXPECT_FALSE(bm::ofPage(p.slices[i])) << i;
    }
}

TEST(MdBookmarks, aBookmarkIsAddedBeforeTheFirstBlockThatStartsOnThePage) {
    std::string text = "# Notes\n\n";
    for (int i = 0; i < 12; ++i) {
        text += paragraph(i);
    }
    const Pagination p = paginate(text, style(), small);
    ASSERT_GE(p.slices.size(), 3u);
    for (size_t page = 0; page < p.parts.size(); ++page) {
        bool earlier = true;
        const auto e = bm::add(text, p.parts[page].begin, p.parts[page].end, "", &earlier);
        ASSERT_TRUE(e) << page;
        EXPECT_FALSE(earlier) << page;
        EXPECT_EQ(e->from, e->to);
        EXPECT_EQ(e->with, "<!-- xqt:bookmark -->\n");
        std::string after = text;
        after.replace(e->from, 0, e->with);
        // On that page after pagination (pages that begin inside a paragraph: its next one)
        const Pagination again = paginate(after, style(), small);
        ASSERT_GT(again.slices.size(), page);
        EXPECT_TRUE(bm::ofPage(again.slices[page])) << page << ":\n" << again.slices[page];
        // Already there: nothing to add
        const Pagination q = paginate(after, style(), small);
        EXPECT_FALSE(bm::add(after, q.parts[page].begin, q.parts[page].end, "")) << page;
        // Removed again: the text as it was
        const auto r = bm::remove(after, q.parts[page].begin, q.parts[page].end);
        ASSERT_TRUE(r);
        after.replace(r->from, r->to - r->from, r->with);
        EXPECT_EQ(after, text);
    }
}

TEST(MdBookmarks, aPageInsideOneBlockGetsItsBlocksPage) {
    // A long code block over several pages: a page in its middle has no block of its own
    std::string text = "Intro.\n\n```\n";
    for (int i = 0; i < 60; ++i) {
        text += "line " + std::to_string(i) + "\n";
    }
    text += "```\n\nAfter.\n";
    const Pagination p = paginate(text, style(), small);
    ASSERT_GE(p.slices.size(), 3u);
    bool earlier = false;
    const auto e = bm::add(text, p.parts[1].begin, p.parts[1].end, "Code", &earlier);
    ASSERT_TRUE(e);
    EXPECT_TRUE(earlier);
    EXPECT_EQ(text.substr(e->from, 3), "```");
    std::string after = text;
    after.replace(e->from, 0, e->with);
    const Pagination again = paginate(after, style(), small);
    EXPECT_TRUE(bm::ofPage(again.slices[0]));
    EXPECT_FALSE(bm::ofPage(again.slices[1]));
    // Asked again for that page: its block has one already
    earlier = false;
    EXPECT_FALSE(bm::add(after, again.parts[1].begin, again.parts[1].end, "", &earlier));
    EXPECT_TRUE(earlier);
}

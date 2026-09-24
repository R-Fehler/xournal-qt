/*
 * xournal-qt: the passages of a Markdown text (what the library search finds in a .md and shows as snippet cards).
 *
 * @license GNU GPLv2 or later
 */
#include <string>

#include <gtest/gtest.h>

#include "MdLayout.h"
#include "MdPassages.h"

using namespace xqt::md;

namespace {
const std::string TEXT = "# Lecture 3\n"
                         "\n"
                         "Intro with **bold** and a [link](https://example.org) and [[Kalman filter]].\n"
                         "\n"
                         "## Kalman filter\n"
                         "\n"
                         "3. first step\n"
                         "4. second &amp; last\n"
                         "   - nested point\n"
                         "\n"
                         "| Symbol | Meaning |\n"
                         "|---|---|\n"
                         "| x | state |\n"
                         "| P | covariance |\n"
                         "\n"
                         "> quoted prediction\n"
                         "\n"
                         "```cpp\n"
                         "int x = predict();\n"
                         "```\n";

std::string sourceOf(const Passage& p, size_t a, size_t b) {
    const auto [begin, end] = sourceRange(p, a, b);
    return begin == NO_SOURCE ? std::string() : TEXT.substr(begin, end - begin);
}
}  // namespace

TEST(MdPassages, eachBlockWithTextIsAPassageWithoutTheSyntax) {
    const Document doc = parse(TEXT);
    const auto ps = passages(doc);
    std::vector<std::string> texts;
    for (const auto& p: ps) {
        texts.push_back(p.text);
    }
    EXPECT_EQ(texts, (std::vector<std::string>{"Lecture 3",
                                               "Intro with bold and a link and Kalman filter.",
                                               "Kalman filter",
                                               "first step",
                                               "second & last",
                                               "nested point",
                                               "Symbol\nMeaning",
                                               "x\nstate",
                                               "P\ncovariance",
                                               "quoted prediction",
                                               "int x = predict();\n"}));
    EXPECT_EQ(ps[0].kind, Passage::Kind::Heading);
    EXPECT_EQ(ps[0].level, 1);
    EXPECT_EQ(ps[2].level, 2);
    EXPECT_EQ(ps[3].kind, Passage::Kind::ListItem);
    EXPECT_EQ(ps[6].kind, Passage::Kind::TableRow);
    EXPECT_TRUE(ps[6].header);
    EXPECT_FALSE(ps[7].header);
    EXPECT_EQ(ps[10].kind, Passage::Kind::Code);
    // Where the text is in the source
    EXPECT_EQ(sourceOf(ps[1], 11, 15), "bold");
    EXPECT_EQ(sourceOf(ps[4], 7, 8), "&amp;") << "an entity shows all of it";
    EXPECT_EQ(sourceOf(ps[7], 2, 7), "state");
    EXPECT_EQ(TEXT.substr(ps[9].begin, ps[9].end - ps[9].begin), "quoted prediction");
}

TEST(MdPassages, aSnippetIsThePassageAloneAsItIsShown) {
    const Document doc = parse(TEXT);
    const auto ps = passages(doc);
    // A list item: in its list, with its number, without the list in it
    Document s = snippet(doc, ps[4]);
    ASSERT_EQ(s.root.children.size(), 1u);
    const Block& list = s.root.children[0];
    EXPECT_EQ(list.kind, BlockKind::OrderedList);
    EXPECT_EQ(list.start, 4u);
    ASSERT_EQ(list.children.size(), 1u);
    EXPECT_EQ(list.children[0].children.size(), 1u) << "the nested list left out";
    // A nested item alone
    s = snippet(doc, ps[5]);
    EXPECT_EQ(s.root.children[0].kind, BlockKind::BulletList);
    // A table row under the header
    s = snippet(doc, ps[8]);
    const Block& table = s.root.children[0];
    ASSERT_EQ(table.kind, BlockKind::Table);
    ASSERT_EQ(table.children.size(), 2u);
    EXPECT_EQ(plainText(table.children[0].children[0].children[0]), "Symbol");
    EXPECT_EQ(plainText(table.children[1].children[0].children[1]), "covariance");
    // In its quote
    s = snippet(doc, ps[9]);
    EXPECT_EQ(s.root.children[0].kind, BlockKind::Quote);

    // Laid out, a match is drawn where it is
    Style style;
    style.width = 300;
    const Layout layout = xqt::md::layout(snippet(doc, ps[8]), style);
    const auto [begin, end] = sourceRange(ps[8], 2, 12);
    const auto rects = sourceRects(layout, begin, end);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_GT(rects[0].y, 5) << "below the header row";
}

TEST(MdPassages, linksAndWikiLinksAreKept) {
    const Document doc = parse(TEXT + "\nSee [[Kalman filter|the filter]] and ![img](a.png) and [again](https://example.org).\n");
    EXPECT_EQ(linksOf(doc), (std::vector<LinkTarget>{{"https://example.org", false}, {"Kalman filter", true}}));
}

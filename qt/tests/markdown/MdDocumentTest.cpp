#include <string>

#include <gtest/gtest.h>

#include "MdDocument.h"

using namespace xqt::md;

namespace {
/// Runs whose text is the source they point to (not entities): shown text == source text.
void expectRunsMatchSource(const Block& b, const std::string& src) {
    for (const xqt::md::Run& r: b.runs) {
        if (r.source != NO_SOURCE && r.text.find('&') == std::string::npos) {
            EXPECT_EQ(src.substr(r.source, r.sourceLength), r.text);
        }
    }
    for (const Block& c: b.children) {
        expectRunsMatchSource(c, src);
    }
}
}  // namespace

TEST(MdDocument, HeadingAndInlineFormatting) {
    const std::string src = "# Title\n\nSome **bold** and *it* with `code`.\n";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 2u);
    const Block& h = d.root.children[0];
    EXPECT_EQ(h.kind, BlockKind::Heading);
    EXPECT_EQ(h.level, 1);
    EXPECT_EQ(plainText(h), "Title");
    EXPECT_EQ(h.textBegin, 2u);

    const Block& p = d.root.children[1];
    EXPECT_EQ(p.kind, BlockKind::Paragraph);
    EXPECT_EQ(plainText(p), "Some bold and it with code.");
    ASSERT_GE(p.runs.size(), 6u);
    EXPECT_EQ(p.runs[1].text, "bold");
    EXPECT_EQ(p.runs[1].flags, Strong);
    EXPECT_EQ(p.runs[1].source, src.find("bold"));
    EXPECT_EQ(p.runs[3].text, "it");
    EXPECT_EQ(p.runs[3].flags, Emphasis);
    EXPECT_EQ(p.runs[5].text, "code");
    EXPECT_EQ(p.runs[5].flags, Code);
    expectRunsMatchSource(d.root, src);
    EXPECT_EQ(p.textBegin, src.find("Some"));
    EXPECT_EQ(p.textEnd, src.find(".\n") + 1);
}

TEST(MdDocument, BreaksAndEntities) {
    const std::string src = "one\ntwo  \nthree &amp; &#x41;";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 1u);
    EXPECT_EQ(plainText(d.root.children[0]), "one two\xe2\x80\xa8three & A");
    int madeUp = 0;
    for (const xqt::md::Run& r: d.root.children[0].runs) {
        if (r.text == "&") {
            EXPECT_EQ(src.substr(r.source, r.sourceLength), "&amp;");
        }
        madeUp += r.source == NO_SOURCE;
    }
    EXPECT_EQ(madeUp, 2);  // the soft and the hard break
}

TEST(MdDocument, Lists) {
    const std::string src = "- a\n- [x] b\n- [ ] c\n\n3. c\n4. d\n";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 2u);
    const Block& ul = d.root.children[0];
    EXPECT_EQ(ul.kind, BlockKind::BulletList);
    EXPECT_TRUE(ul.tight);
    EXPECT_EQ(ul.mark, '-');
    ASSERT_EQ(ul.children.size(), 3u);
    EXPECT_FALSE(ul.children[0].task);
    EXPECT_TRUE(ul.children[1].task);
    EXPECT_TRUE(ul.children[1].checked);
    EXPECT_TRUE(ul.children[2].task);
    EXPECT_FALSE(ul.children[2].checked);
    ASSERT_EQ(ul.children[1].children.size(), 1u);
    EXPECT_EQ(plainText(ul.children[1].children[0]), "b");  // (without the check box)

    const Block& ol = d.root.children[1];
    EXPECT_EQ(ol.kind, BlockKind::OrderedList);
    EXPECT_EQ(ol.start, 3u);
    EXPECT_EQ(ol.mark, '.');
    EXPECT_EQ(ol.children.size(), 2u);
}

TEST(MdDocument, CodeQuoteRuleAndComment) {
    const std::string src = "```python\nprint(1)\n```\n\n> quoted\n\n---\n\n<!-- xqt:cont -->\nafter\n";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 5u);
    const Block& code = d.root.children[0];
    EXPECT_EQ(code.kind, BlockKind::CodeBlock);
    EXPECT_TRUE(code.fenced);
    EXPECT_EQ(code.language, "python");
    EXPECT_EQ(plainText(code), "print(1)\n");
    EXPECT_EQ(d.root.children[1].kind, BlockKind::Quote);
    EXPECT_EQ(d.root.children[2].kind, BlockKind::Rule);
    EXPECT_EQ(d.root.children[2].textBegin, NO_SOURCE);
    EXPECT_EQ(d.root.children[3].kind, BlockKind::Html);
    EXPECT_EQ(d.root.children[4].kind, BlockKind::Paragraph);
    expectRunsMatchSource(d.root, src);
}

TEST(MdDocument, TableAndLinks) {
    const std::string src = "| a | b |\n|:--|--:|\n| 1 | 2 |\n\nSee [site](https://x.org) and [[Page 2]].\n";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 2u);
    const Block& t = d.root.children[0];
    EXPECT_EQ(t.kind, BlockKind::Table);
    ASSERT_EQ(t.children.size(), 2u);  // head, body
    const Block& head = t.children[0].children[0];
    ASSERT_EQ(head.children.size(), 2u);
    EXPECT_EQ(head.children[0].kind, BlockKind::TableHeaderCell);
    EXPECT_EQ(head.children[0].align, Align::Left);
    EXPECT_EQ(head.children[1].align, Align::Right);

    const Block& p = d.root.children[1];
    ASSERT_EQ(d.links.size(), 2u);
    EXPECT_EQ(d.links[0], "https://x.org");
    EXPECT_EQ(d.links[1], "Page 2");
    int linked = 0;
    for (const xqt::md::Run& r: p.runs) {
        if (r.flags & Link) {
            ++linked;
            EXPECT_GE(r.link, 0);
        }
    }
    EXPECT_EQ(linked, 2);
}

// A fenced code block whose code starts with blank lines: the block begins at its fence and ends after its closing
// fence, and the paragraph after it is a block of its own (not swallowed by the code block).
TEST(MdDocument, spansOfCodeThatStartsWithBlankLines) {
    const std::string src = "Intro\n\n```py\n\nsome code\n\n#stuff\n\n```\n\nafter";
    const Document d = parse(src);
    ASSERT_EQ(d.root.children.size(), 3u);
    ASSERT_EQ(d.root.children[1].kind, BlockKind::CodeBlock);
    const auto spans = topLevelSpans(src, d);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[1].begin, src.find("```py")) << "at its opening fence, not at the blank line after it";
    EXPECT_EQ(spans[1].end, src.find("```\n\nafter") + 4) << "after its closing fence";
    EXPECT_EQ(spans[2].begin, src.find("after"));
    EXPECT_EQ(spans[2].end, src.size());
}

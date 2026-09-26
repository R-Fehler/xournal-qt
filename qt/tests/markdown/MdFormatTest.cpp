/*
 * xournal-qt: the formatting tools of the Markdown editors (md::format) and the table editor's tables (md::table).
 *
 * The texts mark the selection with ⟨ ⟩ and a cursor without a selection with ‸.
 *
 * @license GNU GPLv2 or later
 */
#include <string>

#include <gtest/gtest.h>

#include "MdFormat.h"

using namespace xqt::md;
using format::Action;

namespace {
const std::string CARET = "‸";
const std::string OPEN = "⟨";
const std::string CLOSE = "⟩";

struct Marked {
    std::string text;
    size_t anchor = 0;
    size_t caret = 0;
};

Marked unmark(std::string s) {
    Marked m;
    if (const size_t c = s.find(CARET); c != std::string::npos) {
        s.erase(c, CARET.size());
        m.anchor = m.caret = c;
    } else {
        const size_t a = s.find(OPEN);
        s.erase(a, OPEN.size());
        const size_t b = s.find(CLOSE);
        s.erase(b, CLOSE.size());
        m.anchor = a;
        m.caret = b;
    }
    m.text = std::move(s);
    return m;
}

std::string mark(std::string s, size_t anchor, size_t caret) {
    if (anchor == caret) {
        return s.insert(caret, CARET);
    }
    s.insert(std::max(anchor, caret), CLOSE);
    return s.insert(std::min(anchor, caret), OPEN);
}

/// The text after the tool, with the selection marked.
std::string run(const std::string& marked, Action a, const std::string& arg = {}) {
    const Marked m = unmark(marked);
    const format::Edit e = format::apply(m.text, m.anchor, m.caret, a, arg);
    std::string out = m.text;
    out.replace(e.from, e.to - e.from, e.with);
    return mark(out, e.anchor, e.caret);
}

format::State state(const std::string& marked) {
    const Marked m = unmark(marked);
    return format::stateAt(m.text, m.anchor, m.caret);
}
}  // namespace

// --- inline marks ------------------------------------------------------------------------------------------------

TEST(MdFormat, boldTogglesAroundTheSelection) {
    EXPECT_EQ(run("Some ⟨word⟩ here", Action::Bold), "Some **⟨word⟩** here");
    EXPECT_EQ(run("Some **⟨word⟩** here", Action::Bold), "Some ⟨word⟩ here") << "again: the marks go";
    EXPECT_EQ(run("Some ⟨**word**⟩ here", Action::Bold), "Some ⟨word⟩ here") << "selected with its marks";
    EXPECT_EQ(run("a **wo‸rd** b", Action::Bold), "a wo‸rd b") << "the cursor in a bold word";
    EXPECT_EQ(run("Some⟨ word ⟩here", Action::Bold), "Some⟨ **word** ⟩here") << "not around the spaces";
}

TEST(MdFormat, boldWithoutASelectionInsertsEmptyMarks) {
    EXPECT_EQ(run("Some ‸", Action::Bold), "Some **‸**");
    EXPECT_EQ(run("Some **‸**", Action::Bold), "Some ‸") << "pressed again: they go";
    EXPECT_EQ(run("‸", Action::Italic), "*‸*");
    EXPECT_EQ(run("*‸*", Action::Italic), "‸");
    EXPECT_EQ(run("x ‸", Action::Code), "x `‸`");
}

TEST(MdFormat, italicAndBoldTogether) {
    EXPECT_EQ(run("**⟨x⟩**", Action::Italic), "***⟨x⟩***");
    EXPECT_EQ(run("***⟨x⟩***", Action::Italic), "**⟨x⟩**");
    EXPECT_EQ(run("***⟨x⟩***", Action::Bold), "*⟨x⟩*");
    EXPECT_EQ(run("_⟨x⟩_", Action::Italic), "⟨x⟩") << "(underscores too)";
}

TEST(MdFormat, otherInlineMarks) {
    EXPECT_EQ(run("x ⟨a+b⟩", Action::Code), "x `⟨a+b⟩`");
    EXPECT_EQ(run("x `⟨a+b⟩`", Action::Code), "x ⟨a+b⟩");
    EXPECT_EQ(run("⟨gone⟩", Action::Strike), "~~⟨gone⟩~~");
    EXPECT_EQ(run("~~go‸ne~~", Action::Strike), "go‸ne");
    EXPECT_EQ(run("so ⟨x^2⟩", Action::InlineMath), "so $⟨x^2⟩$");
    EXPECT_EQ(run("so $x^‸2$", Action::InlineMath), "so x^‸2");
    EXPECT_EQ(run("`**not‸bold**`", Action::Bold), "`**not**‸**bold**`") << "no marks inside code";
}

TEST(MdFormat, marksOnSeveralLinesGoOnEachLine) {
    EXPECT_EQ(run("- ⟨one\n- two⟩", Action::Bold), "- **⟨one**\n- **two⟩**");
    EXPECT_EQ(run("- **⟨one**\n- **two⟩**", Action::Bold), "- ⟨one\n- two⟩");
    EXPECT_EQ(run("⟨a\n\nb⟩", Action::Italic), "*⟨a*\n\n*b⟩*") << "(not the empty line)";
}

TEST(MdFormat, links) {
    EXPECT_EQ(run("see ⟨docs⟩", Action::Link), "see [docs](⟨https://⟩)") << "the address to type selected";
    EXPECT_EQ(run("go ⟨https://x.org⟩", Action::Link), "go [‸](https://x.org)") << "an address: its text to type";
    EXPECT_EQ(run("‸", Action::Link), "[‸](https://)");
    EXPECT_EQ(run("see [do‸cs](https://x)", Action::Link), "see do‸cs") << "on a link: its text again";
}

TEST(MdFormat, imagePlaceholder) {
    EXPECT_EQ(run("‸", Action::Image), "![image](⟨image.png⟩)");
    EXPECT_EQ(run("⟨Plot⟩", Action::Image, "fig/plot.png"), "![Plot](fig/plot.png)‸");
}

// --- line marks --------------------------------------------------------------------------------------------------

TEST(MdFormat, headingLevelToggles) {
    EXPECT_EQ(run("Title‸", Action::Heading1), "# Title‸");
    EXPECT_EQ(run("# Title‸", Action::Heading1), "Title‸") << "the same level again: a paragraph";
    EXPECT_EQ(run("# Ti‸tle", Action::Heading2), "## Ti‸tle");
    EXPECT_EQ(run("### Title‸", Action::Paragraph), "Title‸");
    EXPECT_EQ(run("- item‸", Action::Heading2), "## item‸") << "instead of the list's mark";
    EXPECT_EQ(run("> quoted‸", Action::Heading3), "> ### quoted‸") << "the quote stays";
    EXPECT_EQ(run("‸", Action::Heading1), "# ‸") << "an empty line";
    EXPECT_EQ(run("Plain‸", Action::Paragraph), "Plain‸");
}

TEST(MdFormat, listsToggleOnSeveralLines) {
    EXPECT_EQ(run("⟨one\ntwo\n\nthree⟩", Action::BulletList), "- ⟨one\n- two\n\n- three⟩");
    EXPECT_EQ(run("- ⟨one\n- two\n\n- three⟩", Action::BulletList), "⟨one\ntwo\n\nthree⟩") << "again: they go";
    EXPECT_EQ(run("⟨a\nb\nc⟩", Action::NumberedList), "1. ⟨a\n2. b\n3. c⟩");
    EXPECT_EQ(run("- ⟨a\n- b⟩", Action::TaskList), "- [ ] ⟨a\n- [ ] b⟩") << "a bullet list becomes a task list";
    EXPECT_EQ(run("- [x] ⟨a\n- [ ] b⟩", Action::BulletList), "- ⟨a\n- b⟩");
    EXPECT_EQ(run("1. a\nb‸", Action::NumberedList), "1. a\n2. b‸") << "a numbered list goes on";
    EXPECT_EQ(run("  - nested‸", Action::NumberedList), "  1. nested‸") << "the indentation stays";
    EXPECT_EQ(run("⟨a\nb\n⟩c", Action::BulletList), "- ⟨a\n- b\n⟩c") << "(not the line the selection ends at)";
}

TEST(MdFormat, quotes) {
    EXPECT_EQ(run("Wise words‸", Action::Quote), "> Wise words‸");
    EXPECT_EQ(run("> Wise words‸", Action::Quote), "Wise words‸");
    EXPECT_EQ(run("⟨a\n\nb⟩", Action::Quote), "> ⟨a\n>\n> b⟩") << "one quote over the empty line";
    EXPECT_EQ(run("> ⟨a\n>\n> b⟩", Action::Quote), "⟨a\n\nb⟩");
    EXPECT_EQ(run("- item‸", Action::Quote), "> - item‸");
    EXPECT_EQ(run("A\n\n‸", Action::Quote), "A\n\n> ‸") << "an empty line: a quote to write";
}

// --- blocks ------------------------------------------------------------------------------------------------------

TEST(MdFormat, codeBlockGoesOnLinesOfItsOwn) {
    EXPECT_EQ(run("Text‸", Action::CodeBlock, "python"), "Text\n\n```python\n‸\n```\n");
    EXPECT_EQ(run("⟨a = 1\nb = 2⟩", Action::CodeBlock), "```\n⟨a = 1\nb = 2⟩\n```\n") << "around the lines";
    EXPECT_EQ(run("A\n\n‸\n\nB", Action::CodeBlock), "A\n\n```\n‸\n```\n\nB") << "on the empty line";
    EXPECT_EQ(run("‸Para", Action::CodeBlock), "```\n‸\n```\n\nPara") << "before the line";
    EXPECT_EQ(run("Te‸xt\nMore", Action::MathBlock), "Text\n\n$$\n‸\n$$\n\nMore") << "after the line";
}

TEST(MdFormat, ruleAndPageBreak) {
    EXPECT_EQ(run("Para‸\nNext", Action::Rule), "Para\n\n---\n\n‸Next") << "a blank line keeps it from making a heading";
    EXPECT_EQ(run("End of chapter‸", Action::PageBreak),
              "End of chapter\n\n<div style=\"page-break-after: always\"></div>\n\n‸");
    EXPECT_EQ(run("A\n\n‸", Action::PageBreak), "A\n\n<div style=\"page-break-after: always\"></div>\n\n‸");
}

// --- the state at the cursor -------------------------------------------------------------------------------------

TEST(MdFormat, stateAtTheCursor) {
    EXPECT_TRUE(state("a **bo‸ld** b").bold);
    EXPECT_FALSE(state("a **bo‸ld** b").italic);
    EXPECT_FALSE(state("a **bold** b‸").bold);
    EXPECT_TRUE(state("*it‸*").italic);
    EXPECT_TRUE(state("`co‸de`").code);
    EXPECT_TRUE(state("$x‸$").math);
    EXPECT_TRUE(state("$$\nx‸").math) << "in a formula block";
    EXPECT_TRUE(state("[li‸nk](x)").link);
    EXPECT_EQ(state("## Head‸").heading, 2);
    EXPECT_EQ(state("- [ ] t‸").list, format::State::List::Task);
    EXPECT_EQ(state("3. t‸").list, format::State::List::Numbered);
    EXPECT_TRUE(state("> q‸").quote);
    EXPECT_TRUE(state("```\n# co‸de\n```").codeBlock);
    EXPECT_EQ(state("```\n# co‸de\n```").heading, 0) << "(code, not a heading)";
    EXPECT_FALSE(state("```\ncode\n```\nafter‸").codeBlock);
    EXPECT_TRUE(state("| a | b |\n|---|---|\n| 1 | 2‸ |").table);
    EXPECT_FALSE(state("a | b‸").table);
}

// --- tables ------------------------------------------------------------------------------------------------------

namespace {
const std::string TABLE_TEXT = "Intro\n"
                               "\n"
                               "| Name | Value |\n"
                               "|:-----|------:|\n"
                               "| a | 1 |\n"
                               "| b \\| c | 2 |\n"
                               "\n"
                               "After";
}

TEST(MdTable, rowsAreSplitAtPipesThatAreNotEscaped) {
    EXPECT_EQ(table::splitRow("| a | b \\| c |"), (std::vector<std::string>{"a", "b | c"}));
    EXPECT_EQ(table::splitRow("a|b"), (std::vector<std::string>{"a", "b"})) << "(the outer pipes are optional)";
    EXPECT_EQ(table::splitRow("| `x` | **y** |"), (std::vector<std::string>{"`x`", "**y**"}))
            << "inline Markdown stays text";
    EXPECT_EQ(table::splitRow("|  |  |"), (std::vector<std::string>{"", ""}));
}

TEST(MdTable, theTableAtTheCursorIsReadIntoCells) {
    const size_t cursor = TABLE_TEXT.find("| 1 |") + 2;
    const auto found = table::at(TABLE_TEXT, cursor);
    ASSERT_TRUE(found);
    EXPECT_EQ(found->begin, TABLE_TEXT.find("| Name"));
    EXPECT_EQ(found->end, TABLE_TEXT.find("\n\nAfter"));
    EXPECT_EQ(found->row, 1u);
    EXPECT_EQ(found->column, 1u);
    const table::Table& t = found->table;
    EXPECT_EQ(t.header, (std::vector<std::string>{"Name", "Value"}));
    EXPECT_EQ(t.align, (std::vector<table::Align>{table::Align::Left, table::Align::Right}));
    ASSERT_EQ(t.rows.size(), 2u);
    EXPECT_EQ(t.rows[1], (std::vector<std::string>{"b | c", "2"})) << "the escaped pipe is a pipe of the text";

    EXPECT_EQ(table::at(TABLE_TEXT, TABLE_TEXT.find("Name"))->row, 0u) << "the header";
    EXPECT_EQ(table::at(TABLE_TEXT, TABLE_TEXT.find(":--") + 1)->row, 0u) << "(the delimiter row: the header's)";
    EXPECT_EQ(table::at(TABLE_TEXT, TABLE_TEXT.find("| b") + 1)->column, 0u);
    EXPECT_FALSE(table::at(TABLE_TEXT, 2)) << "not in the table";
    EXPECT_FALSE(table::at("a | b\nc | d", 1)) << "no delimiter row: no table";
}

TEST(MdTable, writtenPaddedWithItsAlignmentAndEscapedPipes) {
    const auto found = table::at(TABLE_TEXT, TABLE_TEXT.find("Name"));
    ASSERT_TRUE(found);
    EXPECT_EQ(table::write(found->table), "| Name   | Value |\n"
                                          "| :----- | ----: |\n"
                                          "| a      |     1 |\n"
                                          "| b \\| c |     2 |");
    table::Table t;
    t.header = {"Ω", "中文"};
    t.align = {table::Align::Center, table::Align::None};
    t.rows = {{"x", "line\nbreak"}};
    EXPECT_EQ(table::write(t), "|  Ω  | 中文          |\n"
                               "| :-: | ------------- |\n"
                               "|  x  | line<br>break |")
            << "wide characters take two columns; a line break in a cell is <br>";
}

TEST(MdTable, aRoundTripWithAnEditedCell) {
    const size_t cursor = TABLE_TEXT.find("| 1 |") + 2;
    auto found = table::at(TABLE_TEXT, cursor);
    ASSERT_TRUE(found);
    table::Table t = found->table;
    t.rows[0][1] = "a|b";
    t.align[1] = table::Align::Center;
    t.rows.push_back({"new", ""});
    const format::Edit e = table::replaceOrInsert(TABLE_TEXT, cursor, cursor, t);
    std::string out = TABLE_TEXT;
    out.replace(e.from, e.to - e.from, e.with);
    EXPECT_EQ(out, "Intro\n"
                   "\n"
                   "| Name   | Value |\n"
                   "| :----- | :---: |\n"
                   "| a      | a\\|b  |\n"
                   "| b \\| c |   2   |\n"
                   "| new    |       |\n"
                   "\n"
                   "After");
    const auto again = table::at(out, e.caret - 1);
    ASSERT_TRUE(again);
    EXPECT_EQ(again->table.rows[0][1], "a|b") << "read back as written";
    EXPECT_EQ(again->table.align[1], table::Align::Center);
    EXPECT_EQ(again->table.rows.size(), 3u);
}

TEST(MdTable, aNewTableIsABlockOfItsOwn) {
    table::Table t;
    t.header = {"A", "B"};
    t.align = {table::Align::None, table::Align::None};
    t.rows = {{"", ""}};
    const std::string text = "Text";
    const format::Edit e = table::replaceOrInsert(text, 4, 4, t);
    std::string out = text;
    out.replace(e.from, e.to - e.from, e.with);
    EXPECT_EQ(out, "Text\n\n| A   | B   |\n| --- | --- |\n|     |     |\n\n");
    EXPECT_EQ(e.caret, out.size()) << "the cursor after it";
}

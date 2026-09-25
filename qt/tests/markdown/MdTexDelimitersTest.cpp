/*
 * xournal-qt: \( … \) and \[ … \] as formulas (MdTexDelimiters): what is rewritten, what is not, and that every
 * place of the parsed text is still the place in the text as written.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "MdDocument.h"
#include "MdLayout.h"
#include "MdTexDelimiters.h"

using namespace xqt::md;

namespace {
/// What md4c reads (the source itself when nothing was rewritten).
std::string rewritten(const std::string& src) {
    const tex::Rewritten r = tex::rewrite(src);
    return r.changed ? r.text : src;
}
std::vector<const Run*> mathRuns(const Block& b, std::vector<const Run*> out = {}) {
    for (const Run& r: b.runs) {
        if (r.flags & Math) {
            out.push_back(&r);
        }
    }
    for (const Block& c: b.children) {
        out = mathRuns(c, out);
    }
    return out;
}
/// Every run with a source is its source as written (the text and its place; an entity is decoded).
void expectRunsAtTheirSource(const Block& b, const std::string& src) {
    for (const Run& r: b.runs) {
        if (r.source == NO_SOURCE || r.text == "&") {
            continue;
        }
        ASSERT_LE(r.source + r.sourceLength, src.size()) << r.text;
        EXPECT_EQ(src.substr(r.source, r.sourceLength), r.text) << "at " << r.source;
    }
    for (const Block& c: b.children) {
        expectRunsAtTheirSource(c, src);
    }
}
}  // namespace

TEST(MdTexDelimiters, InlineFormula) {
    EXPECT_EQ(rewritten(R"(Energy \(x^2\) here.)"), "Energy $x^2$ here.");
    const std::string src = R"(Energy \(E = mc^2\) and \(\frac{1}{2}\), also (\(a\)).)";
    const Document doc = parse(src);
    const auto runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0]->text, "E = mc^2");
    EXPECT_FALSE(runs[0]->flags & DisplayMath);
    EXPECT_EQ(runs[1]->text, R"(\frac{1}{2})");
    EXPECT_EQ(runs[2]->text, "a");
    // Each run's place is in the text as written: the text before, the formulas and the text after
    EXPECT_EQ(runs[0]->source, src.find("E = mc^2"));
    EXPECT_EQ(runs[1]->source, src.find(R"(\frac)"));
    EXPECT_EQ(runs[2]->source, src.find("a\\)"));
    expectRunsAtTheirSource(doc.root, src);
    const Block& p = doc.root.children.at(0);
    EXPECT_EQ(p.textBegin, 0u);
    EXPECT_EQ(p.textEnd, src.size());
    EXPECT_EQ(plainText(p).substr(0, 7), "Energy ");
    EXPECT_EQ(p.runs.back().text, ").");
}

TEST(MdTexDelimiters, DisplayFormula) {
    // On one line
    std::string src = "Before\n\n\\[ \\int_0^1 x\\,dx \\]\n\nAfter\n";
    EXPECT_EQ(rewritten(src), "Before\n\n$$ \\int_0^1 x\\,dx $$\n\nAfter\n");
    Document doc = parse(src);
    auto runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0]->text, " \\int_0^1 x\\,dx ");
    EXPECT_TRUE(runs[0]->flags & DisplayMath);
    expectRunsAtTheirSource(doc.root, src);

    // Over lines, in a paragraph after text (as chat apps write it); its block goes from "\[" to "\]"
    src = "Before\n\nThe integral:\n\\[\n\\int_0^1 x\\,dx\n\\]\n\nAfter\n";
    doc = parse(src);
    runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 3u);  // (its line breaks are made-up spaces: runs of their own)
    EXPECT_TRUE(runs[1]->flags & DisplayMath);
    EXPECT_EQ(runs[1]->text, "\\int_0^1 x\\,dx");
    EXPECT_EQ(runs[1]->source, src.find("\\int"));
    expectRunsAtTheirSource(doc.root, src);
    auto spans = topLevelSpans(src, doc);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[1].begin, src.find("The"));
    EXPECT_EQ(spans[1].end, src.find("\nAfter"));  // (the "\]" line is in it)

    // A block of its own: "\[" and "\]" on lines of their own belong to it
    src = "Before\n\n\\[\n\\sum_k k\n\\]\n\nAfter\n";
    doc = parse(src);
    spans = topLevelSpans(src, doc);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[1].begin, src.find("\\["));
    EXPECT_EQ(spans[1].end, src.find("\nAfter"));
    // In a list item, indented, and with a full stop after it
    src = "1. The sum:\n   \\[\n   \\sum_k k\n   \\].\n2. Next\n";
    doc = parse(src);
    runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 3u) << rewritten(src);
    EXPECT_EQ(runs[1]->text, "\\sum_k k");
    EXPECT_TRUE(runs[1]->flags & DisplayMath);
    expectRunsAtTheirSource(doc.root, src);
}

TEST(MdTexDelimiters, NotInCode) {
    for (const std::string src: {
                 "Code `\\(x\\)` stays\n",
                 "Code ``a ` \\(x\\)`` stays\n",
                 "Code `a\n\\(x\\)` over lines\n",
                 "```\n\\(x\\) and \\[y\\]\n```\n",
                 "~~~~ tex\n\\[\nx\n\\]\n~~~~\n",
                 "> ```\n> \\(x\\)\n> ```\n",
                 "Text\n\n    \\(x\\) indented code\n",
                 "Formula $a \\(b\\) c$ as it is\n",
         }) {
        EXPECT_EQ(rewritten(src), src);
    }
    // After the code: formulas again ("\`" is no code span's backtick)
    EXPECT_EQ(rewritten("`a` \\(x\\)\n"), "`a` $x$\n");
    EXPECT_EQ(rewritten("\\` \\(x\\) `\n"), "\\` $x$ `\n");
    EXPECT_EQ(rewritten("```\ncode\n```\n\\(x\\)\n"), "```\ncode\n```\n$x$\n");
    // An indented line that continues a paragraph or a list is no code
    EXPECT_EQ(rewritten("Text\n    \\(x\\)\n"), "Text\n    $x$\n");
    EXPECT_EQ(rewritten("- item\n\n    \\(x\\) in the item\n"), "- item\n\n    $x$ in the item\n");
    // A fence that is not closed: code to the end
    EXPECT_EQ(rewritten("```\n\\(x\\)\n"), "```\n\\(x\\)\n");
}

TEST(MdTexDelimiters, OnlyPairs) {
    for (const std::string src: {
                 "Unclosed \\(x here\n",
                 "\\[\nunclosed\n",
                 "\\[\nx\n\nclosed after a blank line\n\\]\n",  // (a paragraph ends at a blank line)
                 "Closing \\) before \\( opening\n",
                 "Escaped \\\\(x\\\\) backslashes\n",           // ("\\(" is a backslash and a "(")
                 "A \\[not a link\\] in the text\n",             // (escaped brackets, as pandoc writes them)
                 "Mixed \\(x\\] kinds\n",
                 "Empty \\(\\) pair\n",
                 "# Heading \\(x\n\nnot closed by \\)\n",
         }) {
        EXPECT_EQ(rewritten(src), src);
    }
    // A later opening one takes the place of an unclosed one
    EXPECT_EQ(rewritten("\\( a \\(b\\)\n"), "\\( a $b$\n");
    // Three backslashes: an escaped backslash and a delimiter
    EXPECT_EQ(rewritten("\\\\\\(x\\)\n"), "\\\\$x$\n");
}

// md4c takes a "$" as an opening mark only after a blank or a punctuation mark (or at a line's start), and as a
// closing mark only before one: next to a letter the pair stays as written (a known limit, "\(n\)th" is no formula).
TEST(MdTexDelimiters, WhereMd4cReadsAFormula) {
    EXPECT_EQ(rewritten("the \\(n\\)th element\n"), "the \\(n\\)th element\n");
    EXPECT_EQ(rewritten("a\\(x\\) b\n"), "a\\(x\\) b\n");
    EXPECT_EQ(rewritten("(\\(x\\)), \"\\(y\\)\" and \\(z\\).\n"), "($x$), \"$y$\" and $z$.\n");
    EXPECT_EQ(rewritten("für \\(x\\) – \\(y\\)\n"), "für $x$ – $y$\n");
    // Not next to a "$" (md4c would read "$$"); money around it stays text, formulas of the source stay
    EXPECT_EQ(rewritten("\\(a\\)\\(b\\)\n"), "\\(a\\)\\(b\\)\n");
    EXPECT_EQ(rewritten("$\\(x\\) and \\(y\\)$\n"), "$\\(x\\) and \\(y\\)$\n");
    EXPECT_EQ(rewritten("costs $5 and \\(x\\) or $10\n"), "costs $5 and $x$ or $10\n");
    EXPECT_EQ(rewritten("$a$ and \\(b\\)\n"), "$a$ and $b$\n");
    // A "$" that would close a formula begun by money before it: the pair stays as written
    EXPECT_EQ(rewritten("costs $5, so \\( x \\)\n"), "costs $5, so \\( x \\)\n");
    // What md4c reads matches: formulas where they were rewritten, and nothing else
    const std::string src = "costs $5 and \\(x\\) or $10, the \\(n\\)th\n";
    const Document doc = parse(src);
    const auto runs = mathRuns(doc.root);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0]->text, "x");
    expectRunsAtTheirSource(doc.root, src);
}

TEST(MdTexDelimiters, MoneyAndDollarsUntouched) {
    const std::string src = "Costs $5 and $10.\n";
    EXPECT_FALSE(tex::rewrite(src).changed);
    const Document doc = parse(src);
    EXPECT_TRUE(mathRuns(doc.root).empty());
    EXPECT_EQ(plainText(doc.root.children.at(0)), "Costs $5 and $10.");
}

// An empty pair is text as written, as an empty $ $ is (MdMathText.EmptyFormulasAreText)
TEST(MdTexDelimiters, EmptyFormulaIsTextAsWritten) {
    const std::string src = "a \\( \\) b\n\n\\[\n\\]\n";
    const Document doc = parse(src);
    EXPECT_TRUE(mathRuns(doc.root).empty());
    ASSERT_EQ(doc.root.children.size(), 2u);
    EXPECT_EQ(plainText(doc.root.children[0]), "a \\( \\) b");
    EXPECT_EQ(plainText(doc.root.children[1]), "\\[ \\]");
    EXPECT_EQ(doc.root.children[1].textBegin, src.find("\\["));
    EXPECT_EQ(doc.root.children[1].textEnd, src.size() - 1);
}

// The places md4c reports are in the text as written: task marks, blocks, runs after formulas, entities
TEST(MdTexDelimiters, PlacesAreInTheTextAsWritten) {
    const std::string src = "Say \\(x\\) and \\(y\\) &amp; *more*\n\n- [ ] task \\(z\\)\n- [x] done\n\n"
                            "| a | b |\n|---|---|\n| \\(c\\) | d |\n";
    const Document doc = parse(src);
    expectRunsAtTheirSource(doc.root, src);
    const auto amp = std::find_if(doc.root.children.at(0).runs.begin(), doc.root.children.at(0).runs.end(),
                                  [](const xqt::md::Run& r) { return r.text == "&"; });
    ASSERT_NE(amp, doc.root.children.at(0).runs.end());
    EXPECT_EQ(amp->source, src.find("&amp;"));
    EXPECT_EQ(amp->sourceLength, 5u);
    ASSERT_EQ(doc.root.children.size(), 3u);
    const Block& list = doc.root.children[1];
    ASSERT_EQ(list.children.size(), 2u);
    EXPECT_EQ(list.children[0].taskMark, src.find("[ ]") + 1);
    EXPECT_EQ(list.children[1].taskMark, src.find("[x]") + 1);
    EXPECT_EQ(doc.root.children[0].textEnd, src.find("*\n"));
    const auto spans = topLevelSpans(src, doc);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[1].begin, src.find("- [ ]"));
    EXPECT_EQ(spans[2].begin, src.find("| a"));
    EXPECT_EQ(mathRuns(doc.root).size(), 4u);
    // The mapping itself
    const tex::Rewritten r = tex::rewrite("\\(x\\) y");
    ASSERT_TRUE(r.changed);
    EXPECT_EQ(r.text, "$x$ y");
    EXPECT_EQ(r.toSource(0), 0u);
    EXPECT_EQ(r.toSource(1), 2u);  // x
    EXPECT_EQ(r.toSource(2), 3u);  // the closing "\)"
    EXPECT_EQ(r.toSource(3), 5u);
    EXPECT_EQ(r.toSource(4), 6u);  // y
}

// Drawn: the formula is laid out (not "(x)"), and the block being written shows "\(" as written
TEST(MdTexDelimiters, DrawnAndWritten) {
    const std::string src = "A line with \\(x^2\\) and more.";
    Style st;
    st.width = 400;
    const Layout drawn = layout(parse(src), st, src, NO_SOURCE);
    size_t formulas = 0;
    for (const Item& it: drawn.items) {
        formulas += it.maths.size();
        for (const MathSpan& m: it.maths) {
            EXPECT_TRUE(m.error.empty()) << m.error;
            EXPECT_EQ(m.tex, "x^2");
        }
    }
    EXPECT_EQ(formulas, 1u);
    // (the cursor in it: its text is exactly the source)
    const Layout written = layout(parse(src), st, src, 3);
    ASSERT_GE(written.rawItem, 0);
    const Item& raw = written.items[static_cast<size_t>(written.rawItem)];
    EXPECT_EQ(std::string(pango_layout_get_text(raw.layout.get())), src);
}

// Pasted text: its pairs become $…$ where they are formulas in the text after the paste
TEST(MdTexDelimiters, ConvertPasted) {
    const std::string pasted = "The energy \\(E = mc^2\\) and\n\\[\n\\sum_k k\n\\]\n";
    EXPECT_EQ(tex::convertPasted("", 0, 0, pasted), "The energy $E = mc^2$ and\n$$\n\\sum_k k\n$$\n");
    // Next to a letter where it goes: that pair stays
    EXPECT_EQ(tex::convertPasted("the th", 4, 4, "\\(n\\)"), "\\(n\\)");
    EXPECT_EQ(tex::convertPasted("the  th", 4, 4, "\\(n\\)"), "$n$");
    // Into code (a fenced block, inline code): as it is
    EXPECT_EQ(tex::convertPasted("```\n\n```\n", 4, 4, "\\(x\\)"), "\\(x\\)");
    EXPECT_EQ(tex::convertPasted("`a  b`", 3, 3, "\\(x\\)"), "\\(x\\)");
    // In place of a selection; a pair only half pasted stays
    EXPECT_EQ(tex::convertPasted("see OLD here", 4, 7, "\\(y\\)"), "$y$");
    EXPECT_EQ(tex::convertPasted("see \\(a here", 7, 7, "b\\) and \\(c\\)"), "b\\) and $c$");
    // A plain text is no Markdown
    EXPECT_EQ(tex::convertPasted("<!-- xqt:plain -->\n", 19, 19, "\\(x\\)"), "\\(x\\)");
    // Nothing to do: the same text
    EXPECT_EQ(tex::convertPasted("abc", 1, 1, "costs $5"), "costs $5");
}

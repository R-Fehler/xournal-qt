#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <poppler.h>

#include "MdLayout.h"

using namespace xqt::md;

namespace {
Layout lay(const std::string& src, double width = 400) {
    Style s;
    s.width = width;
    return layout(parse(src), s);
}

const Item* firstText(const Layout& l, size_t block) {
    for (const Item& it: l.items) {
        if (it.block == block && it.kind == Item::Kind::Text) {
            return &it;
        }
    }
    return nullptr;
}
}  // namespace

TEST(MdLayout, BlocksGoDown) {
    const Layout l = lay("# Title\n\nA paragraph.\n\n- one\n- two\n");
    ASSERT_EQ(l.blocks.size(), 3u);
    EXPECT_DOUBLE_EQ(l.blocks[0].top, 0);  // no space above the first block
    EXPECT_GT(l.blocks[1].top, l.blocks[0].bottom);
    EXPECT_GT(l.blocks[2].top, l.blocks[1].bottom);
    EXPECT_GE(l.height, l.blocks[2].bottom - 0.01);
    // heading text is bigger than the paragraph's
    EXPECT_GT(firstText(l, 0)->height, firstText(l, 1)->height);
}

TEST(MdLayout, WrapsAtTheWidth) {
    const std::string text(400, 'x');
    const std::string words = "lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod tempor ";
    const Layout wide = lay(words + words + words, 2000);
    const Layout narrow = lay(words + words + words, 150);
    EXPECT_GT(narrow.height, 3 * wide.height);
    for (const Item& it: narrow.items) {
        EXPECT_LE(it.x + it.width, 150.5);
    }
}

TEST(MdLayout, CommentsAreInvisible) {
    const Layout l = lay("<!-- xqt:cont -->\nHello\n");
    ASSERT_EQ(l.blocks.size(), 2u);
    const Item* hello = firstText(l, 1);
    ASSERT_NE(hello, nullptr);
    EXPECT_DOUBLE_EQ(hello->y, 0);  // as if the comment was not there
    EXPECT_EQ(firstText(l, 0), nullptr);
}

TEST(MdLayout, ListMarkersOnTheFirstLine) {
    const Layout l = lay("3. first\n4. second\n");
    int markers = 0;
    for (const Item& it: l.items) {
        if (it.kind == Item::Kind::Text) {
            const std::string t = pango_layout_get_text(it.layout.get());
            if (t == "3." || t == "4.") {
                ++markers;
            }
        }
    }
    EXPECT_EQ(markers, 2);
}

TEST(MdLayout, SameLayoutAtEveryCall) {
    const std::string src = "## Words\n\nSome *text* that wraps a few times at this width, with `code` in it.\n";
    const Layout a = lay(src, 180);
    const Layout b = lay(src, 180);
    ASSERT_EQ(a.items.size(), b.items.size());
    for (size_t i = 0; i < a.items.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.items[i].y, b.items[i].y);
        EXPECT_DOUBLE_EQ(a.items[i].height, b.items[i].height);
    }
}

TEST(MdLayout, DrawsInkOnlyInsideTheLayout) {
    const Layout l = lay("# Title\n\nSome text.\n", 300);
    const int w = 300;
    const int h = static_cast<int>(l.height) + 40;
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surface);
    draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    int inkInside = 0;
    int inkBelow = 0;
    for (int yy = 0; yy < h; ++yy) {
        for (int xx = 0; xx < w; ++xx) {
            const unsigned char alpha = data[yy * stride + xx * 4 + 3];
            if (alpha > 0) {
                (yy <= l.height + 1 ? inkInside : inkBelow)++;
            }
        }
    }
    cairo_surface_destroy(surface);
    EXPECT_GT(inkInside, 100);
    EXPECT_EQ(inkBelow, 0);
}

TEST(MdLayout, PdfKeepsTheTextAsText) {
    const Layout l = lay("# Title\n\nHello **bold** world.\n", 400);
    const std::string path = ::testing::TempDir() + "xqt-md-layout-test.pdf";
    cairo_surface_t* surface = cairo_pdf_surface_create(path.c_str(), 500, 300);
    cairo_t* cr = cairo_create(surface);
    cairo_translate(cr, 50, 50);
    draw(cr, l);
    cairo_destroy(cr);
    cairo_surface_finish(surface);
    cairo_surface_destroy(surface);

    gchar* uri = g_filename_to_uri(path.c_str(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    ASSERT_NE(doc, nullptr);
    PopplerPage* page = poppler_document_get_page(doc, 0);
    gchar* text = poppler_page_get_text(page);
    const std::string s = text;
    g_free(text);
    g_object_unref(page);
    g_object_unref(doc);
    EXPECT_NE(s.find("Title"), std::string::npos) << s;
    EXPECT_NE(s.find("Hello bold world."), std::string::npos) << s;
}

TEST(MdLayout, checkBoxesKnowTheirMarks) {
    const std::string src = "Tasks:\n\n- [ ] open\n- [x] done\n- no task\n";
    Style s;
    s.width = 400;
    const Layout l = layout(parse(src), s);
    ASSERT_EQ(l.checkBoxes.size(), 2u);
    EXPECT_EQ(src[l.checkBoxes[0].mark], ' ');
    EXPECT_FALSE(l.checkBoxes[0].checked);
    EXPECT_EQ(src[l.checkBoxes[1].mark], 'x');
    EXPECT_TRUE(l.checkBoxes[1].checked);
    const auto& b = l.checkBoxes[0];
    const auto hit = checkBoxAt(l, b.x + b.size / 2, b.y + b.size / 2);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->mark, b.mark);
    EXPECT_FALSE(checkBoxAt(l, b.x + 100, b.y + b.size / 2).has_value()) << "on the text: no";
    EXPECT_EQ(toggledTask(src, b.mark), "Tasks:\n\n- [x] open\n- [x] done\n- no task\n");
    EXPECT_EQ(toggledTask(src, l.checkBoxes[1].mark), "Tasks:\n\n- [ ] open\n- [ ] done\n- no task\n");
}

// Writing after a code block that ends the text (the cursor at the end): the paragraph being written is laid out
// as its source, below the code block, whose grey background ends at its closing fence.
TEST(MdLayout, writingAfterACodeBlockAtTheEnd) {
    Style s;
    s.width = 400;
    for (const std::string code: {std::string("```py\nsome code\n#stuff\n```"),
                                  std::string("```py\n\nsome code\n\n#stuff\n\n```")}) {
        const std::string src = "Intro\n\n" + code + "\n\nafter";
        const Layout l = layout(parse(src), s, src, src.size());
        ASSERT_GE(l.rawItem, 0) << code;
        const Item& raw = l.items[static_cast<size_t>(l.rawItem)];
        EXPECT_STREQ(pango_layout_get_text(raw.layout.get()), "after") << code;
        EXPECT_EQ(l.rawBegin, src.size() - 5) << code;
        const Item* fill = nullptr;
        for (const Item& it: l.items) {
            fill = it.kind == Item::Kind::Fill ? &it : fill;
        }
        ASSERT_NE(fill, nullptr);
        EXPECT_LE(fill->y + fill->height, raw.y) << code << ": the text is below the code block";
    }
    // Just after the closing fence (Enter pressed, nothing typed yet): the code is done and drawn as code; the
    // cursor's empty line is below its grey background, where the next paragraph goes
    const std::string src = "Intro\n\n```py\nsome code\n```\n\n";
    const Layout l = layout(parse(src), s, src, src.size());
    ASSERT_GE(l.rawItem, 0);
    const Item& raw = l.items[static_cast<size_t>(l.rawItem)];
    EXPECT_STREQ(pango_layout_get_text(raw.layout.get()), "");
    EXPECT_EQ(l.rawBegin, src.size());
    const Item* fill = nullptr;
    for (const Item& it: l.items) {
        fill = it.kind == Item::Kind::Fill ? &it : fill;
    }
    ASSERT_NE(fill, nullptr);
    EXPECT_LE(fill->y + fill->height, raw.y) << "the cursor's line is not in the code's background";
    const std::string typed = src + "a";
    const Layout after = layout(parse(typed), s, typed, typed.size());
    ASSERT_GE(after.rawItem, 0);
    EXPECT_DOUBLE_EQ(after.items[static_cast<size_t>(after.rawItem)].y, raw.y) << "the text goes where the cursor was";
}

// A text found over a line break (the paragraph wraps inside it) is marked on each line where it is drawn: one
// rectangle per line, not one from its start on the first line to its end on the next (which covers other words).
TEST(MdLayout, foundTextOverALineBreakIsMarkedOnEachLine) {
    const std::string src = "Before words alpha beta after words.";
    const Layout wide = lay(src, 2000);
    ASSERT_EQ(findText(wide, "alpha beta").size(), 1u);
    // As narrow as to break between "alpha" and "beta"
    const Rect alpha = findText(wide, "alpha")[0];
    const Layout l = lay(src, alpha.x + alpha.width + 3);
    const auto found = findText(l, "alpha beta");
    const auto a = findText(l, "alpha");
    const auto b = findText(l, "beta");
    ASSERT_EQ(a.size(), 1u);
    ASSERT_EQ(b.size(), 1u);
    ASSERT_GT(b[0].y, a[0].y + 1) << "on the next line";
    ASSERT_EQ(found.size(), 2u) << "one rectangle per line";
    EXPECT_NEAR(found[0].x, a[0].x, 0.01);
    EXPECT_NEAR(found[0].y, a[0].y, 0.01);
    EXPECT_NEAR(found[0].height, a[0].height, 0.01);
    EXPECT_GE(found[0].x + found[0].width, a[0].x + a[0].width - 0.01);  // (and the space after it)
    EXPECT_NEAR(found[1].x, b[0].x, 0.01);
    EXPECT_NEAR(found[1].y, b[0].y, 0.01);
    EXPECT_NEAR(found[1].width, b[0].width, 0.01);
    EXPECT_NEAR(found[1].height, b[0].height, 0.01);
}

// A range of the source is marked where it is drawn: the same places the search finds for its words, in a heading,
// emphasis, a list, inline code and a code block; the marks around them are not drawn and have no place.
TEST(MdLayout, sourceRangesAreWhereTheyAreDrawn) {
    const std::string src = "# A needle heading\n\nSome **strong needle** and `needle` code.\n\n- one\n- a needle item\n\n"
                            "```py\nx = 1  # needle in code\n```\n";
    const Layout l = lay(src, 300);
    const auto found = findText(l, "needle");
    std::vector<Rect> mapped;
    for (size_t p = src.find("needle"); p != std::string::npos; p = src.find("needle", p + 1)) {
        const auto r = sourceRects(l, p, p + 6);
        ASSERT_EQ(r.size(), 1u) << "at " << p;
        mapped.push_back(r[0]);
    }
    ASSERT_EQ(mapped.size(), found.size());
    for (const Rect& m: mapped) {
        bool same = false;
        for (const Rect& f: found) {
            same = same || (std::abs(m.x - f.x) < 0.01 && std::abs(m.y - f.y) < 0.01 &&
                            std::abs(m.width - f.width) < 0.01 && std::abs(m.height - f.height) < 0.01);
        }
        EXPECT_TRUE(same) << "at " << m.x << ", " << m.y;
    }
    EXPECT_TRUE(sourceRects(l, src.find("**"), src.find("**") + 2).empty()) << "a mark is not drawn";
    const size_t strong = src.find("**strong");
    const auto word = sourceRects(l, strong, strong + 8);  // "**strong": the mark and the word
    ASSERT_EQ(word.size(), 1u);
    EXPECT_NEAR(word[0].x, findText(l, "strong")[0].x, 0.01) << "only the word";
}

// --- tables: column widths as a browser's automatic table layout (VS Code's preview, GitHub) ----------------------
namespace {
/// A table's column edges (its vertical rules), left to right.
std::vector<double> columnEdges(const Layout& l, size_t block) {
    std::vector<double> edges;
    for (const Item& it: l.items) {
        if (it.block == block && it.kind == Item::Kind::Line && it.width == 0 && it.height > 0) {
            edges.push_back(it.x);
        }
    }
    std::sort(edges.begin(), edges.end());
    return edges;
}
/// The text items of a table's cells, row by row.
std::vector<const Item*> cells(const Layout& l, size_t block) {
    std::vector<const Item*> out;
    for (const Item& it: l.items) {
        if (it.block == block && it.kind == Item::Kind::Text) {
            out.push_back(&it);
        }
    }
    return out;
}
std::string textOf(const Item& it) { return pango_layout_get_text(it.layout.get()); }
const Item* cell(const Layout& l, size_t block, const std::string& text) {
    for (const Item* it: cells(l, block)) {
        if (textOf(*it) == text) {
            return it;
        }
    }
    return nullptr;
}
int lineCount(const Item& it) { return pango_layout_get_line_count(it.layout.get()); }
/// Whether a text's lines break inside a word: a line after the first that starts right after a letter or digit.
bool breaksInAWord(const Item& it) {
    const std::string t = textOf(it);
    for (GSList* l = pango_layout_get_lines_readonly(it.layout.get()); l; l = l->next) {
        const auto* line = static_cast<PangoLayoutLine*>(l->data);
        const int at = line->start_index;
        if (at > 0 && at <= static_cast<int>(t.size()) && std::isalnum(static_cast<unsigned char>(t[at - 1]))) {
            return true;
        }
    }
    return false;
}
/// Every cell's text lies inside its column (columns: the table's column count).
void expectCellsInsideTheirColumns(const Layout& l, size_t block, size_t columns, const std::string& what) {
    const auto edges = columnEdges(l, block);
    ASSERT_EQ(edges.size(), columns + 1) << what;
    const auto all = cells(l, block);
    ASSERT_EQ(all.size() % columns, 0u) << what;
    for (size_t k = 0; k < all.size(); ++k) {
        const size_t i = k % columns;
        EXPECT_GE(all[k]->x, edges[i] - 0.01) << what << ": " << textOf(*all[k]);
        EXPECT_LE(all[k]->x + all[k]->width, edges[i + 1] + 0.01) << what << ": " << textOf(*all[k]) << " overlaps";
    }
}
const std::string LONG_TEXT = "This column holds a long description that goes on and on, much longer than the page "
                              "is wide, so that it has to wrap over several lines while the short columns beside it "
                              "keep their words whole, as a browser lays out the table.";
}  // namespace

// A short column next to a column of long text keeps its width: its words stay on one line (the browser gives each
// column at least its widest word and spreads the rest by how much more text a column has). Before, every column
// was narrowed by the same factor, so "Status" broke into letters.
TEST(MdLayout, aShortColumnBesideALongOneKeepsItsWords) {
    const std::string src = "| Key | Description |\n|---|---|\n| Status | " + LONG_TEXT + " |\n| Owner | short |\n";
    const Layout l = lay(src, 400);
    for (const std::string word: {"Key", "Status", "Owner"}) {
        const Item* c = cell(l, 0, word);
        ASSERT_NE(c, nullptr) << word;
        EXPECT_EQ(lineCount(*c), 1) << word << " is broken";
    }
    const Item* d = cell(l, 0, LONG_TEXT);
    ASSERT_NE(d, nullptr);
    EXPECT_GT(lineCount(*d), 2) << "the long text wraps";
    EXPECT_FALSE(breaksInAWord(*d));
    const auto edges = columnEdges(l, 0);
    ASSERT_EQ(edges.size(), 3u);
    EXPECT_NEAR(edges.back() - edges.front(), 400, 0.5) << "a table with more text than room is as wide as the box";
    expectCellsInsideTheirColumns(l, 0, 2, "short and long");
}

// Headers and words in cells wrap between words, not inside them, as long as the widest words fit.
TEST(MdLayout, tableCellsWrapBetweenWords) {
    const std::string src = "| Identifier | Description | Temperature | Remarks |\n|---|---|---|---|\n"
                            "| alpha | A short sentence about the first thing in this table | 21.5 | none |\n"
                            "| beta | Another sentence, about the second thing, longer than the first one by far "
                            "and wrapping | 19.0 | " +
                            LONG_TEXT + " |\n";
    const Layout l = lay(src, 400);
    for (const Item* c: cells(l, 0)) {
        EXPECT_FALSE(breaksInAWord(*c)) << textOf(*c);
    }
    for (const std::string word: {"Identifier", "Description", "Temperature", "Remarks", "alpha", "21.5"}) {
        const Item* c = cell(l, 0, word);
        ASSERT_NE(c, nullptr) << word;
        EXPECT_EQ(lineCount(*c), 1) << word;
    }
    expectCellsInsideTheirColumns(l, 0, 4, "four columns");
}

// Inline code and formulas in a cell are words that cannot break: the column is as wide as they are, and a formula
// keeps the size it has in a paragraph (it is not made smaller to fit a narrow column).
TEST(MdLayout, codeAndFormulasInCellsKeepTheirWidth) {
    const std::string src = "| Name | Formula | Notes |\n|---|---|---|\n| `configuration_value_name` | "
                            "$\\alpha + \\beta + \\gamma = \\delta$ | " +
                            LONG_TEXT + " |\n";
    const Layout l = lay(src, 400);
    const Item* code = cell(l, 0, "configuration_value_name");
    ASSERT_NE(code, nullptr);
    EXPECT_EQ(lineCount(*code), 1) << "inline code broken inside";
    const Layout para = lay("$\\alpha + \\beta + \\gamma = \\delta$", 400);
    ASSERT_EQ(para.items.size(), 1u);
    ASSERT_EQ(para.items[0].maths.size(), 1u);
    const double natural = para.items[0].maths[0].inkWidth;
    const Item* formula = nullptr;
    for (const Item* c: cells(l, 0)) {
        formula = c->maths.empty() ? formula : c;
    }
    ASSERT_NE(formula, nullptr);
    EXPECT_NEAR(formula->maths[0].inkWidth, natural, 0.01) << "the formula was made smaller";
    expectCellsInsideTheirColumns(l, 0, 3, "code and formula");
}

// A table that fits is as wide as its text (as a browser's table: not stretched to the box), each cell on one line.
TEST(MdLayout, aTableThatFitsIsAsWideAsItsText) {
    const Layout l = lay("| a | bb |\n|---|---|\n| one | two three |\n", 400);
    const auto edges = columnEdges(l, 0);
    ASSERT_EQ(edges.size(), 3u);
    EXPECT_LT(edges.back() - edges.front(), 200);
    for (const Item* c: cells(l, 0)) {
        EXPECT_EQ(lineCount(*c), 1) << textOf(*c);
    }
}

// Of two columns of long text, the one with more text gets more of the room.
TEST(MdLayout, theRoomGoesToTheColumnWithMoreText) {
    const std::string shorter = "Some words that are more than fit on one line of this column, a few of them.";
    const std::string src = "| A | B |\n|---|---|\n| " + shorter + " | " + LONG_TEXT + " " + LONG_TEXT + " |\n";
    const Layout l = lay(src, 400);
    const auto edges = columnEdges(l, 0);
    ASSERT_EQ(edges.size(), 3u);
    EXPECT_GT(edges[2] - edges[1], edges[1] - edges[0]);
    expectCellsInsideTheirColumns(l, 0, 2, "two long columns");
}

// A table whose words do not fit side by side is drawn smaller to fit the box (its words stay whole while it is not
// much too wide), and never wider than the box nor over its neighbouring cells.
TEST(MdLayout, aTableTooWideForTheBoxIsDrawnSmaller) {
    const std::string head = "| Temperature | Measurement | Description | Calibration | Uncertainty | Reference |\n"
                             "|---|---|---|---|---|---|\n";
    const std::string row = "| 21.5 | thermometer | indoors | yesterday | small | handbook |\n";
    const Layout l = lay(head + row, 400);
    for (const Item* c: cells(l, 0)) {
        EXPECT_FALSE(breaksInAWord(*c)) << textOf(*c);
        EXPECT_EQ(lineCount(*c), 1) << textOf(*c);
    }
    const auto edges = columnEdges(l, 0);
    ASSERT_EQ(edges.size(), 7u);
    EXPECT_LE(edges.back(), 400.01);
    EXPECT_GE(edges.front(), -0.01);
    expectCellsInsideTheirColumns(l, 0, 6, "six wide columns");
    // Far too wide (many columns): still inside the box, no cell over the next one
    std::string many = "|";
    std::string rule = "|";
    std::string cellsRow = "|";
    for (int i = 0; i < 40; ++i) {
        many += " Heading" + std::to_string(i) + " |";
        rule += "---|";
        cellsRow += " word" + std::to_string(i) + " |";
    }
    const Layout m = lay(many + "\n" + rule + "\n" + cellsRow + "\n", 300);
    const auto e = columnEdges(m, 0);
    ASSERT_EQ(e.size(), 41u);
    EXPECT_LE(e.back(), 300.01);
    for (const Item& it: m.items) {
        EXPECT_LE(it.x + it.width, 300.01);
    }
    expectCellsInsideTheirColumns(m, 0, 40, "forty columns");
}

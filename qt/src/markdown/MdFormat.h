/*
 * xournal-qt: the formatting tools of the Markdown editors, as changes of the source text.
 *
 * Every tool is one replacement of a range of the text (`Edit`), so an editor applies it as one undo step. The tools
 * work on the selection (anchor, caret: byte offsets in the UTF-8 source), as in Typora, Obsidian and Zettlr:
 * - inline marks (bold, italic, strikethrough, code, math, link) are toggled around the selection, or inserted empty
 *   with the cursor between them; on a text that has the mark, the mark goes;
 * - line marks (headings, lists, check boxes, quotes) are toggled on every line of the selection;
 * - blocks (code, formula, rule, page break, table) go on lines of their own, with blank lines around them.
 *
 * `table` reads a GFM pipe table into cells and writes it back, the columns padded to line up.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xqt::md::format {

enum class Action {
    Paragraph,
    Heading1,
    Heading2,
    Heading3,
    Bold,
    Italic,
    Strike,
    Code,
    Link,
    InlineMath,
    BulletList,
    NumberedList,
    TaskList,
    Quote,
    CodeBlock,  ///< arg: the language
    MathBlock,
    Rule,
    Image,      ///< arg: the image's path (a placeholder without one)
    PageBreak,
};

/// "bold", "heading2", "codeBlock", ... (the names the tool bar uses); nothing for an unknown name.
std::optional<Action> actionNamed(std::string_view name);

/// Replace text[from, to) by `with`; afterwards the selection is [anchor, caret] (offsets in the new text).
struct Edit {
    size_t from = 0;
    size_t to = 0;
    std::string with;
    size_t anchor = 0;
    size_t caret = 0;
};

/// What a tool does to the text with this selection.
Edit apply(std::string_view text, size_t anchor, size_t caret, Action action, std::string_view arg = {});

/// Lines of a block of their own where the cursor is, with blank lines around them: on an empty line, before the
/// line when the cursor is at its start, else after the line (after the selection's last line). The cursor goes
/// after them.
Edit insertBlock(std::string_view text, size_t anchor, size_t caret, const std::string& lines);

/// What is at the cursor (for the buttons' state). Cheap: the line of the cursor, the fences before it.
struct State {
    bool bold = false;
    bool italic = false;
    bool strike = false;
    bool code = false;
    bool math = false;
    bool link = false;
    int heading = 0;  ///< 1-6, 0: none
    enum class List { None, Bullet, Numbered, Task } list = List::None;
    bool quote = false;
    bool codeBlock = false;  ///< in a fenced code block
    bool table = false;
};
State stateAt(std::string_view text, size_t anchor, size_t caret);

/// The line that makes a page break (`<div style="page-break-after: always"></div>`).
extern const std::string_view PAGE_BREAK;

}  // namespace xqt::md::format

namespace xqt::md::table {

enum class Align { None, Left, Center, Right };

struct Table {
    std::vector<std::string> header;       ///< the cells as text (inline Markdown kept, "\|" as "|")
    std::vector<Align> align;              ///< one per column
    std::vector<std::vector<std::string>> rows;
    size_t columns() const { return header.size(); }
};

/// The cells of a table row ("| a | b \| c |" -> "a", "b | c"). Leading and trailing pipes are optional.
std::vector<std::string> splitRow(std::string_view line);
/// The table in these lines (a header, the delimiter row, the rows); nothing if they are not a table. Rows with
/// fewer cells get empty ones, extra cells are dropped (as GFM does).
std::optional<Table> parse(std::string_view lines);
/// A GFM pipe table, the columns padded to line up, a "|" in a cell as "\|", a line break in a cell as "<br>". No
/// line break after the last row.
std::string write(const Table& table);

/// The table the cursor is in: where it is in the text (its lines, without the last line break), and the cell of
/// the cursor (row 0: the header; the delimiter row counts as the header).
struct Found {
    size_t begin = 0;
    size_t end = 0;
    Table table;
    size_t row = 0;
    size_t column = 0;
};
std::optional<Found> at(std::string_view text, size_t offset);

/// Write a table where the cursor is: over the table there, or as a new block (blank lines around it). The cursor
/// goes after the table.
format::Edit replaceOrInsert(std::string_view text, size_t anchor, size_t caret, const Table& table);

}  // namespace xqt::md::table

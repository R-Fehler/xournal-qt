/*
 * xournal-qt: a parsed Markdown text (CommonMark + GitHub extensions, parsed by md4c) as a tree of blocks with
 * formatted text runs. Every run knows where its text is in the source, so a place in the rendered text can be
 * mapped back to the source (editing), and blocks know the source range they came from (pages are split there).
 *
 * Qt-free: it is used from the page renderer, which runs in upstream's render path (worker threads, PDF export).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xqt::md {

constexpr size_t NO_SOURCE = static_cast<size_t>(-1);

/// Inline formatting of a run (combined).
enum RunFlag : uint16_t {
    Strong = 1 << 0,
    Emphasis = 1 << 1,
    Strike = 1 << 2,
    Code = 1 << 3,   ///< inline code (not a code block)
    Link = 1 << 4,
    Underline = 1 << 5,
    Image = 1 << 6,  ///< alt text of an image
    Math = 1 << 7,
    Html = 1 << 8,   ///< raw inline HTML (shown as it is)
    Marker = 1 << 9, ///< Markdown marks shown while editing (the source of the block being edited)
};

struct Run {
    std::string text;         ///< as shown (UTF-8): entities decoded, a soft break is a space, a hard break U+2028
    uint16_t flags = 0;
    int link = -1;            ///< index into Document::links (Link / Image)
    size_t source = NO_SOURCE;  ///< offset of the text in the source (NO_SOURCE: made up, e.g. a soft break)
    size_t sourceLength = 0;
};

enum class BlockKind {
    Document,
    Paragraph,
    Heading,
    CodeBlock,
    Quote,
    BulletList,
    OrderedList,
    ListItem,
    Rule,
    Html,
    Table,
    TableHead,
    TableBody,
    TableRow,
    TableHeaderCell,
    TableCell,
};

enum class Align { Default, Left, Center, Right };

struct Block {
    BlockKind kind = BlockKind::Document;
    int level = 0;            ///< heading 1–6
    // lists
    bool tight = true;
    char mark = 0;            ///< '-', '*', '+' (bullet) or '.', ')' (ordered)
    unsigned start = 1;       ///< first number of an ordered list
    bool task = false;        ///< a list item with [ ] / [x]
    bool checked = false;
    // code blocks
    std::string info;         ///< the fence's info string
    std::string language;     ///< its first word
    bool fenced = false;
    // table cells
    Align align = Align::Default;

    std::vector<Run> runs;    ///< text (leaf blocks: paragraph, heading, code, html, table cell)
    std::vector<Block> children;
    /// Source range of the text of the block and everything in it (NO_SOURCE: no text, e.g. a rule). The markup
    /// before the first and after the last text (a "# ", a fence) is not included.
    size_t textBegin = NO_SOURCE;
    size_t textEnd = NO_SOURCE;
};

struct Document {
    Block root;
    std::vector<std::string> links;  ///< link targets and image sources
};

/// Parse a Markdown text (UTF-8). Never fails: anything is some Markdown.
Document parse(std::string_view source);

/// Where a top-level block is in the source: whole lines, [begin, end). What matters most is where each block begins
/// (splitting pages, editing): the lines between two blocks (blank lines) are after the end of the one before.
struct BlockSpan {
    size_t begin = 0;  ///< the start of its first line
    size_t end = 0;    ///< the start of the line after its last one
};
std::vector<BlockSpan> topLevelSpans(std::string_view source, const Document& doc);

/// The text of the runs, joined (as shown).
std::string plainText(const Block& block);

}  // namespace xqt::md

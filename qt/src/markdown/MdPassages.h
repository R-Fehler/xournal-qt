/*
 * xournal-qt: the passages of a Markdown text - what the library's search finds in a Markdown file, and shows as a
 * snippet card.
 *
 * A passage is a block that holds text: a heading, a paragraph, a list item (its own text, without the lists in it:
 * their items are passages of their own), a table row, a code block. Its text is the text of its runs as shown (no
 * Markdown syntax), with where each byte came from in the source, so a match in it is marked where it is drawn
 * (sourceRects, MdLayout.h). snippet() makes a document of one passage alone, to be drawn as a card.
 *
 * Qt-free, like the rest of the Markdown engine.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "MdDocument.h"

namespace xqt::md {

struct Passage {
    enum class Kind { Heading, Paragraph, ListItem, TableRow, Code };
    Kind kind = Kind::Paragraph;
    int level = 0;        ///< a heading's level (1-6)
    bool header = false;  ///< a table's header row
    /// As shown (UTF-8). The texts of its parts (the paragraphs of a list item, the cells of a row) are joined with
    /// '\n'.
    std::string text;
    std::vector<size_t> from;  ///< per byte of `text`: where the source it shows starts (NO_SOURCE: made up)
    std::vector<size_t> to;    ///< per byte of `text`: where the source it shows ends
    size_t begin = NO_SOURCE;  ///< where its text is in the source: [begin, end)
    size_t end = NO_SOURCE;
    std::vector<size_t> path;  ///< its block: the index of each block from the root's children down
};

/// The passages of a text, in order. Passages without text (an empty list item) are left out.
std::vector<Passage> passages(const Document& doc);

/// The source that bytes [a, b) of a passage's text show: [begin, end) (NO_SOURCE for both when all is made up).
std::pair<size_t, size_t> sourceRange(const Passage& p, size_t a, size_t b);

/// A document with only this passage, as it is shown alone: a list item in its list (numbered as it is, without the
/// lists in it), a table row under its table's header, in the quotes it is in. Its runs keep their places in the
/// source of `doc`.
Document snippet(const Document& doc, const Passage& p);

/// The links of a text (not images): each target once, in order.
struct LinkTarget {
    std::string target;
    bool wiki = false;  ///< [[a wiki link]]
    bool operator==(const LinkTarget& o) const { return target == o.target && wiki == o.wiki; }
};
std::vector<LinkTarget> linksOf(const Document& doc);

}  // namespace xqt::md

/*
 * xournal-qt: bookmarks in a Markdown text (qt/docs/features/bookmarks.md, "Markdown").
 *
 * A bookmark is an HTML comment on a line of its own, right before the block it marks (a paragraph, a heading, a
 * list, ...):
 *
 *     <!-- xqt:bookmark Proof of theorem 3.2 -->
 *     ## Proof
 *
 * Other Markdown apps (GitHub, Obsidian, Typora, pandoc) do not show it, and it moves with the text. `<!--
 * xqt:bookmark -->` (no label) is the automatic one: named after the heading it marks, else the first heading of its
 * page, else "Page N". It is found by the parser (a top-level HTML block that is exactly such a comment), so an example
 * in a code block or in `code` is not one. MdPaginate keeps it with the block after it, so its page is the page that
 * block starts on (a bookmark at the very end of the text: the last page).
 *
 * Qt-free, like the rest of the engine.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "MdDocument.h"

namespace xqt::md::bookmarks {

/// What a text must contain to have a bookmark (a quick test before parsing).
constexpr std::string_view KEY = "xqt:bookmark";

/// A bookmark comment in a text.
struct Mark {
    size_t begin = 0;    ///< the start of its line
    size_t end = 0;      ///< the start of the line after it (the end of the text if it is the last line)
    size_t lineEnd = 0;  ///< the end of its line (before its line break)
    size_t block = 0;    ///< its top-level block (of the parsed text)
    std::string label;   ///< as written, trimmed ("": the automatic one)
};

/// Whether a text may have a bookmark (it contains KEY): texts without one are not parsed.
inline bool mayContain(std::string_view source) { return source.find(KEY) != std::string_view::npos; }

/// The label of a bookmark comment ("<!-- xqt:bookmark label -->", surrounding white space allowed), or nothing if
/// the text is not one (another comment, more than one line, text after it).
std::optional<std::string> labelOf(std::string_view html);
/// Whether a top-level block of a parsed text is a bookmark comment.
bool isMark(const Block& block);

/// The bookmarks of a text (parsed as `doc`) in text order.
std::vector<Mark> find(std::string_view source, const Document& doc);
std::vector<Mark> find(std::string_view source);

/// A label as it can be written into the comment: on one line (line breaks are spaces), no "--" (the comment would
/// end or be invalid there: "--" becomes "–"), trimmed.
std::string cleanLabel(std::string_view label);
/// The comment for a label (cleaned), without a line break.
std::string comment(std::string_view label);

/// A page's bookmark from its slice (MdPaginate): its first bookmark comment. `label` is what it is called: its label,
/// or the automatic one (the heading it marks, else the page's first heading, else "": "Page N"); `automatic` is what
/// the automatic one would be.
struct PageMark {
    std::string label;
    std::string automatic;
    bool isAutomatic = false;  ///< written without a label
};
std::optional<PageMark> ofPage(std::string_view slice);

/// A change of a text: [from, to) is replaced by `with`.
struct Edit {
    size_t from = 0;
    size_t to = 0;
    std::string with;
};
/// Bookmark the page that holds [begin, end) of the text (Pagination::parts): a comment before the first block that
/// starts on it (a page that begins inside a block: the first block after that; comments are skipped). If no block
/// starts on the page (it is all inside one block), before the block that goes on over it: that block's page gets
/// the bookmark (`onEarlierPage`). Nothing if that block has a bookmark already.
std::optional<Edit> add(std::string_view source, size_t begin, size_t end, std::string_view label,
                        bool* onEarlierPage = nullptr);
/// Remove the bookmarks of that page (all of its bookmark comments, their lines).
std::optional<Edit> remove(std::string_view source, size_t begin, size_t end);
/// Give the first bookmark of that page another label ("": the automatic one).
std::optional<Edit> rename(std::string_view source, size_t begin, size_t end, std::string_view label);

}  // namespace xqt::md::bookmarks

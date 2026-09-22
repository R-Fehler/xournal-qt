/*
 * xournal-qt: a Markdown text split into pages.
 *
 * Every page gets a slice of the text that is a Markdown text of its own: a page is drawn from its own box alone
 * (on the canvas, in thumbnails, in the PDF export, and Xournal++ shows each page's source). So a page is only
 * split where the rest reads the same on its own:
 *  - between blocks (a heading goes with the block after it),
 *  - between the lines of a paragraph (not inside **bold**, a link, ...; at least two lines on each page where
 *    possible),
 *  - between the lines of a fenced code block (the next page opens the fence again, this one closes it),
 *  - between the items of a list (a numbered list goes on with its number),
 *  - between the rows of a table (the next page repeats the header).
 * A block that cannot be split and is higher than a page stays on its page and goes below its bottom margin.
 *
 * A slice that continues the page before starts with a Markdown comment, "<!-- xqt:cont KIND ... -->" (not shown).
 * It tells join() which lines were added for the page (a fence, a table header), so join(paginate(text)) is the
 * text again, byte for byte.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "MdLayout.h"

namespace xqt::md {

/// The first line of a slice that continues the page before (a Markdown comment: not shown).
constexpr std::string_view CONTINUATION = "<!-- xqt:cont";

/// The box of a page: its width, and how high the text may go.
struct Frame {
    double width = 480;
    double height = 700;
};

/// What of the text a page holds: its slice is `prefix` bytes (the continuation lines), then text[begin, end),
/// then lines closing it (a fence).
struct Part {
    size_t begin = 0;
    size_t end = 0;
    size_t prefix = 0;
    double overflow = 0;  ///< how far a block that could not be split goes below this page
};

struct Pagination {
    std::vector<std::string> slices;  ///< one per page, each a Markdown text of its own
    std::vector<Part> parts;          ///< what of the text each slice holds
    double overflow = 0;              ///< how far blocks that could not be split go below their page (the most)
};

/// Split a Markdown text into pages. `frame(i)`: the box of page i (style.width is not used).
/// `before` / `beforeSource`: the pages of the text before a change (the same style and pages): only the pages from
/// the one before the change on are split again, until a page starts where it did before (moved by the change).
/// While typing, that is one or two pages instead of all.
Pagination paginate(const std::string& source, Style style, const std::function<Frame(size_t)>& frame,
                    const Pagination* before = nullptr, const std::string* beforeSource = nullptr);

/// The text again from the slices of consecutive pages (with what of it each holds, if `parts` is given).
std::string join(const std::vector<std::string>& slices, std::vector<Part>* parts = nullptr);

/// Whether a slice continues the slice of the page before.
bool continues(std::string_view slice);

}  // namespace xqt::md

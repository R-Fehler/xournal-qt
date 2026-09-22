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

struct Pagination {
    std::vector<std::string> slices;  ///< one per page, each a Markdown text of its own
    double overflow = 0;              ///< how far blocks that could not be split go below their page (the most)
};

/// Split a Markdown text into pages. `frame(i)`: the box of page i (style.width is not used).
Pagination paginate(const std::string& source, Style style, const std::function<Frame(size_t)>& frame);

/// The text again from the slices of consecutive pages.
std::string join(const std::vector<std::string>& slices);

/// Whether a slice continues the slice of the page before.
bool continues(std::string_view slice);

}  // namespace xqt::md

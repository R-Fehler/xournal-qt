/*
 * xournal-qt: steps over a text by grapheme cluster: what a reader takes as one character (👩‍💻, 🇩🇪, 👍🏽, an "é" of
 * two code points), where the cursor stops and what Backspace / Delete remove. Pango's rules, the ones the text is
 * drawn with (Qt 6.7's QTextBoundaryFinder splits flags).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string_view>

namespace xqt::text {

/// The cluster boundary after (`forward`) or before the byte offset `pos` of a UTF-8 text. Clusters never span a
/// line break, so only the line is looked at (a long text stays fast).
size_t graphemeStep(std::string_view text, size_t pos, bool forward);

}  // namespace xqt::text

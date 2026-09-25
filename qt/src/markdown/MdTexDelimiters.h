/*
 * xournal-qt: TeX's math delimiters \( … \) and \[ … \], as ChatGPT and other chat apps write formulas, read as the
 * $ … $ and $$ … $$ that md4c knows (MD_FLAG_LATEXMATHSPANS; without this "\(" is an escaped "(").
 *
 * md::parse gives md4c the text rewritten ("\(" and "\)" as "$", "\[" and "\]" as "$$") and maps every place md4c
 * reports back to the source, which stays as it was written: the editor shows "\(" as typed, and the cursor, the
 * search and the page splits keep their places. Text pasted into a Markdown text is converted for good
 * (convertPasted): the file then has $…$, which Obsidian and GitHub show.
 *
 * A pair is rewritten only where md4c then reads a formula: both in one paragraph, not in code (fenced, indented,
 * `inline`), not escaped ("\\(" is a backslash and a "("), not inside a $…$ formula, and where md4c takes the "$"
 * as a formula's marks. md4c does not take a "$" after a letter or digit as an opening mark, nor one followed by
 * one as a closing mark: "\(n\)th" stays text. One pass over the lines, linear in the length of the text.
 *
 * Qt-free (the page renderer, worker threads).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace xqt::md::tex {

/// A text with its \(…\) and \[…\] pairs as $…$ and $$…$$.
struct Rewritten {
    std::string text;
    /// Where the text is shorter than the source ("\(" and "\)" are one "$"): from the offset `at` of `text` on,
    /// the source is `shift` bytes further. Sorted by `at`; empty if nothing changed in length.
    struct Change {
        size_t at = 0;
        size_t shift = 0;
    };
    std::vector<Change> changes;
    bool changed = false;  ///< any delimiter was rewritten (also "\[", which keeps the length)

    /// The source offset of an offset in `text` (a binary search over `changes`).
    size_t toSource(size_t offset) const;
};

/// The text md4c gets for `src` (see above).
Rewritten rewrite(std::string_view src);

/// Text pasted into a Markdown text: `pasted` in place of [from, to) of `text`. Its \(…\) and \[…\] pairs become
/// $…$ and $$…$$ where they are formulas in the text after the paste (not in code, not next to a letter); the rest
/// stays as it is. A plain text (md::isPlain) is left alone.
std::string convertPasted(std::string_view text, size_t from, size_t to, std::string_view pasted);

}  // namespace xqt::md::tex

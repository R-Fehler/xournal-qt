/*
 * xournal-qt: find and replace in the source of a text (qt/docs/md-editor.md, "Find and replace").
 *
 * The search bar's replace row changes the Markdown (or plain text) as it is written, not the text as it is drawn.
 * The source is matched the way the search matches what the page shows (TextMatch.h), as far as a source allows:
 *  - case-insensitive unless Options::caseSensitive;
 *  - the query is trimmed, and a run of whitespace in it matches a run of spaces and tabs with at most one line break
 *    in the source (the lines of a paragraph are one line when drawn), never a blank line;
 *  - a whole word (Options::wholeWord) is not next to a letter or digit;
 *  - a regular expression (Options::regex) is matched as it is (QRegularExpression, Perl syntax, Unicode), also
 *    across lines; ^ and $ are the ends of lines.
 * Never inside the app's own comments (`<!-- xqt:… -->`: the continued pages, plain text, bookmarks), and never an
 * empty match.
 *
 * The replacement is taken as it is typed. For a regular expression, $0..$99, ${n} and \0..\9 are its groups, \n is a
 * line break, \t a tab, and \\ and $$ a backslash and a dollar sign.
 *
 * Offsets are bytes of the UTF-8 source. Any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <QString>

#include "TextMatch.h"

namespace xqt::replace {

using Options = textmatch::Options;

/// A match: source[begin, end), and what replaces it.
struct Match {
    size_t begin = 0;
    size_t end = 0;
    std::string with;
};

/// The matches of `query` in `source`, in order, not overlapping, each with its replacement `with` (see above). None
/// when the query is empty or not a valid regular expression.
std::vector<Match> find(std::string_view source, const QString& query, const QString& with, const Options& options);

/// `source` with the matches replaced (in order, not overlapping).
std::string apply(std::string_view source, const std::vector<Match>& matches);

/// The ranges of the app's own comments in a source (`<!-- xqt:… -->`), where nothing is replaced.
std::vector<std::pair<size_t, size_t>> hiddenRanges(std::string_view source);

}  // namespace xqt::replace

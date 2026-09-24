/*
 * xournal-qt: fzf's matching of a pattern against a name, with its score and the characters it matched.
 *
 * A port of fzf's algorithm (src/algo/algo.go of https://github.com/junegunn/fzf): FuzzyMatchV2, a modified
 * Smith-Waterman that finds the alignment with the best score, and the exact, word-boundary, prefix, suffix and
 * equal matches of its extended search. The score rewards characters at the start of a word (after a space most, then
 * after a delimiter "/,:;|", then after any other non-word character), camelCase humps and digits after letters, and
 * runs of consecutive characters; gaps cost a little per character. The first character of the pattern counts twice
 * at such a place. The constants are fzf's ("default" scheme), so names are ranked as fzf ranks them.
 *
 * The pattern is prepare()d (TextMatch.h: case folded, whitespace runs as one space); the text is compared folded the
 * same way, one UTF-16 unit at a time. Positions are UTF-16 indices into the text.
 *
 * fzf is Copyright (c) 2013-2026 Junegunn Choi, under the MIT License:
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 *   documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 *   to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included in all copies or substantial portions of
 *   the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 *   THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 *   CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *   IN THE SOFTWARE.
 *
 * The port (this file and FuzzyMatch.cpp) is part of xournal-qt, GNU GPLv2 or later.
 */
#pragma once

#include <vector>

#include <QStringView>

namespace xqt::fuzzy {

struct Result {
    int start = -1;  ///< the first character matched (-1: no match)
    int end = -1;    ///< after the last one
    int score = 0;
    std::vector<int> positions;  ///< the characters matched, in order
    bool matched() const { return start >= 0; }
};

/// fzf's fuzzy match (FuzzyMatchV2): the characters of `pattern` in this order, anywhere in `text`, the best-scoring
/// alignment.
Result match(QStringView text, QStringView pattern);
/// An exact substring, the occurrence at the best place (fzf's 'term); with `boundary` only one that begins and ends at
/// a word boundary ('term').
Result exact(QStringView text, QStringView pattern, bool boundary = false);
/// The text starts with / ends with / is the pattern (^term, term$, ^term$).
Result prefix(QStringView text, QStringView pattern);
Result suffix(QStringView text, QStringView pattern);
Result equal(QStringView text, QStringView pattern);

/// fzf's score of the characters [start, end) of `text` as a match of `pattern` (for tests: the score of an alignment).
int scoreOf(QStringView text, QStringView pattern, int start, int end);

}  // namespace xqt::fuzzy

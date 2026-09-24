/*
 * xournal-qt: when a word of a document's text matches a term of the fuzzy search.
 *
 * With the fuzzy search on, a plain term of three or more letters and digits (`tbine`, not `'tbine`, `^tb`, `c++`) is
 * matched against the text word by word (TextMatch::words(): case folded, ligatures written out, a word broken at a
 * line end joined). A word matches when
 *  1. it contains the term (what the plain search finds in it: "turbine", "turbines" for `turbine`): exact;
 *  2. or it starts with the term's first letter and has the term's letters in this order (as fzf matches them), with
 *     at most half as many other letters between them as the term has (rounded down; letters after the last one do
 *     not count): `tbine` finds "turbine" and "turbines" (u, r between), not "tambourine" (5 between); `klmn`
 *     "kalman", `thrm` "thermal", `mtrx` "matrix", `abc` "abacus" but not "abduct". fzf's score, made to rank
 *     names, does not tell these apart (on a dictionary of 73,000 words: turbine 77 % of a perfect score,
 *     tambourine 71 %; kalman 72 %, klansman 75 %), the letters in between do;
 *  3. or it is the term with a typo, if the term has 5 or more letters (the typo tolerance, a setting): one letter
 *     swapped with the next, left out, added or wrong (Damerau-Levenshtein distance, optimal string alignment);
 *     `turbnie`, `trbine`, `turbinne`, `turbime` find "turbine". Tolerance 2 allows two such typos in terms of 8 or
 *     more letters (one below that), 0 none.
 * 2 and 3 are fuzzy: a document found only by them ranks below one with an exact word.
 *
 * Terms shorter than three letters (and terms with other characters) are not matched per word: they stay
 * substrings, as in the plain search.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QString>
#include <QStringView>

namespace xqt::wordmatch {

enum Quality : unsigned char {
    None = 0,
    Fuzzy = 1,  ///< the term's letters in order, or the term with a typo
    Exact = 2,  ///< the word contains the term
};

/// The shortest term matched per word.
constexpr int MIN_LETTERS = 3;
/// The shortest term that may have a typo; with tolerance 2, the shortest that may have two.
constexpr int TYPO_LETTERS = 5;
constexpr int TWO_TYPOS_LETTERS = 8;
/// The typo tolerances (a setting): none, one typo (the default), two in long terms.
constexpr int MAX_TYPOS = 2;
constexpr int DEFAULT_TYPOS = 1;

/// A term (prepare()d, TextMatch.h) is matched per word: MIN_LETTERS or more, letters and digits only.
bool perWord(QStringView term);

/// How many letters a word may have between the letters of a term with this many letters (rule 2).
inline int gapsAllowed(int letters) { return letters / 2; }
/// The letters of `word` between those of `term` when they are found from its first letter on, each as early as
/// possible (the fewest); -1 if the word does not start with the term's first letter or lacks one of its letters.
int gapsOf(QStringView term, QStringView word);
/// How many typos a term of this many letters may have, with the typo tolerance `typos` (0..MAX_TYPOS).
int typosAllowed(int letters, int typos);
/// The Damerau-Levenshtein distance (optimal string alignment) of two words, if it is at most `max` (else max + 1).
int editDistance(QStringView a, QStringView b, int max);

/// One term, prepared for matching many words.
class Rule {
public:
    /// `term`: prepare()d and perWord(); `typos`: the typo tolerance.
    Rule(QStringView term, int typos);
    /// How `word` (case folded, as TextMatch::words() gives it) matches.
    Quality match(QStringView word) const;
    const QString& term() const { return text; }

private:
    QString text;
    int gaps = 0;
    int edits = 0;
};

}  // namespace xqt::wordmatch

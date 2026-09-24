/*
 * xournal-qt: how the search compares text - one matcher for counting and for marking.
 *
 * The search of an open document counts hits in the text of its pages (DocumentTextIndex) and marks them on the
 * pages it shows. Both run this matcher over the same text, so the count and the marks agree:
 *  - the text is kept simplified, as the library index keeps it: whitespace runs are one space (the PDF text has
 *    line breaks where the page has them, so a phrase across a line break is found);
 *  - case-insensitive (Unicode case folding);
 *  - a ligature (U+FB00..U+FB06, "ﬁ") matches its letters;
 *  - a word broken at a line end, "hyphen- ated" (a letter, '-', the space of the line break, a lower-case
 *    letter), is found as "hyphenated", and as "hyphen-ated" too; typed with the hyphen and the space it is found as
 *    well. A soft hyphen (U+00AD) is skipped;
 *  - matches do not overlap; a '\n' is never matched (the pieces of a page's text are joined with it: a hit does not
 *    run from one text box into the next).
 * The fuzzy search (FuzzyQuery.h) matches text through it too: a term can ask for a match at the start or the end of
 * a word (Bounds), and several terms are found at once (the hits of all of them, in order, without overlaps). A fuzzy
 * term (Bounds::Fuzzy) matches whole words, word by word (WordMatch.h): the words of the text are read by words(),
 * and a hit is a word that matches, from its first to its last character.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringView>

namespace xqt::textmatch {

/// The query as it is compared: whitespace runs to one space, trimmed, case folded, ligatures written out.
QString prepare(const QString& query);
/// One character case folded, as prepare() and the matcher fold them.
inline char16_t fold(char16_t c) {
    if (c < 0x80) {
        return c >= u'A' && c <= u'Z' ? static_cast<char16_t>(c + 32) : c;
    }
    return static_cast<char16_t>(QChar::toCaseFolded(static_cast<char32_t>(c)));
}

/// A match of a query in a text: [start, end) of the text.
struct Span {
    qsizetype start = 0;
    qsizetype end = 0;
};
/// Where a match must lie in the words of the text: a word is a run of letters and digits.
enum Bounds : unsigned {
    Anywhere = 0,
    WordStart = 1,          ///< at the start of a word (the character before it is none of a word)
    WordEnd = 2,            ///< at the end of a word
    Word = WordStart | WordEnd,  ///< a whole word (or words)
    /// The words that match the term fuzzily (WordMatch.h: its letters in this order, or with a typo); the hit is the
    /// whole word. The term is one word (letters and digits only). The typos it tolerates are in the bits of
    /// FuzzyTypos (typosOf()).
    Fuzzy = 4,
    FuzzyTypos = 8 | 16,
};
/// The typo tolerance of a fuzzy term (WordMatch.h: 0 none, 1, 2), and its bits.
inline int typosOf(unsigned bounds) { return static_cast<int>((bounds & FuzzyTypos) >> 3); }
inline unsigned typoBits(int typos) { return (static_cast<unsigned>(typos) << 3) & FuzzyTypos; }
/// The matches of `query` (prepare()d) in `text`, in order.
std::vector<Span> find(QStringView text, QStringView query, unsigned bounds = Anywhere);
/// Their number (without collecting them).
int count(QStringView text, QStringView query, unsigned bounds = Anywhere);
/// There is a match (stops at the first one).
bool contains(QStringView text, QStringView query, unsigned bounds = Anywhere);

/// A text to look for: prepare()d, and where it must lie.
struct Term {
    QString text;
    unsigned bounds = Anywhere;
    bool operator==(const Term&) const = default;
};
/// The matches of all terms, in order; where matches of different terms overlap, the one that starts first (of the
/// same start: the longer one) is kept - so the count and the marks of several terms agree as they do for one.
std::vector<Span> find(QStringView text, const std::vector<Term>& terms);
int count(QStringView text, const std::vector<Term>& terms);
/// Matches of several terms in order, without overlaps (as find() keeps them).
std::vector<Span> merged(std::vector<Span> spans);
/// Terms as one string and back (for image URLs).
QString encode(const std::vector<Term>& terms);
std::vector<Term> decode(QStringView encoded);

/// Calls f(start, end, word) for each word of `text`, in order: a run of letters and digits, case folded,
/// ligatures written out, a word broken at a line end ("hyphen- ated") and a soft hyphen in it joined; [start, end) is
/// where it is in the text. `word` is valid during the call. The words the fuzzy search matches (a hit is one of them).
template <typename F>
void words(QStringView text, F&& f);

/// A text simplified (whitespace runs to one space, trimmed: QString::simplified), with where each of its characters
/// came from: `origin[i]` is the index in the original text of character i (and origin[size] the end of the last one).
struct Simplified {
    QString text;
    std::vector<qsizetype> origin;
};
Simplified simplify(QStringView original);

namespace detail {
/// words(): the next word from `j` on into `word` (cleared first); returns its start (-1: none), `j` is after it.
qsizetype nextWord(QStringView text, qsizetype& j, QString& word);
}  // namespace detail

template <typename F>
void words(QStringView text, F&& f) {
    QString word;
    qsizetype j = 0;
    for (;;) {
        const qsizetype start = detail::nextWord(text, j, word);
        if (start < 0) {
            return;
        }
        f(start, j, QStringView(word));
    }
}

}  // namespace xqt::textmatch

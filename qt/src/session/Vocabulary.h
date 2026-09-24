/*
 * xournal-qt: the words of texts, so that fuzzy terms (WordMatch.h) are matched against distinct words, not text.
 *
 * A fuzzy term of the fuzzy search matches words (TextMatch: textmatch::Fuzzy). Reading megabytes of text word by word
 * for every key typed would be slow, so the search works on vocabularies instead:
 *  - every word seen gets a number in one dictionary for the whole program (words(); never forgotten: a library's
 *    words are a few MB at most);
 *  - a Vocabulary is the distinct words of a text (a page, a passage) with how often each occurs, made once from the
 *    text (the library index and the index of an open document make them when a fuzzy term first needs them, and
 *    forget them when the text changes);
 *  - Matches is the answer for one term: which words of the dictionary match it, and how (exact or fuzzy). It is
 *    worked out once over the dictionary (much smaller than the text) and kept for the last few terms; words added
 *    later are matched when they are asked for;
 *  - Terms counts the hits of a search's terms in a text from its vocabulary: a hit of a fuzzy term is an occurrence
 *    of a word that matches, so the count is the sum of the counts of the matching words - the same number
 *    TextMatch's find() marks in the text (the agreement the tests check). Terms that are not fuzzy are counted in the
 *    text by TextMatch as before.
 *
 * Thread-safe (the dictionary has a lock; Vocabulary and Matches do not change once made).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

#include <QStringView>

#include "TextMatch.h"
#include "WordMatch.h"

namespace xqt::words {

using Id = std::uint32_t;

/// Words in the dictionary, and the memory it takes (tests, measurements).
size_t dictionarySize();
size_t dictionaryBytes();

/// The distinct words of one or more texts (TextMatch::words()), with how often each occurs.
class Vocabulary {
public:
    struct Word {
        Id id = 0;
        std::uint32_t count = 0;
    };
    Vocabulary() = default;
    explicit Vocabulary(std::initializer_list<QStringView> texts);
    /// By their number.
    const std::vector<Word>& words() const { return list; }
    bool empty() const { return list.empty(); }
    size_t bytes() const { return sizeof(Vocabulary) + list.capacity() * sizeof(Word); }

private:
    std::vector<Word> list;
};

/// How the words of the dictionary match a fuzzy term.
class Matches {
public:
    /// For a term with textmatch::Fuzzy (kept for the last few terms, and extended when the dictionary grew).
    static std::shared_ptr<const Matches> of(const textmatch::Term& term);
    wordmatch::Quality quality(Id id) const;
    /// How a word (as TextMatch::words() gives it) matches.
    wordmatch::Quality match(QStringView word) const { return rule.match(word); }

    explicit Matches(const textmatch::Term& term);  // (use of())

private:
    friend struct MatchCache;
    wordmatch::Rule rule;
    std::vector<wordmatch::Quality> known;  ///< by number, the words the dictionary had then
};

/// The terms of a search, counted in texts with their vocabularies.
class Terms {
public:
    Terms() = default;
    /// `counted`: per term, whether its hits count (default: all; the fuzzy search counts the terms that are not
    /// negated, and only needs to know whether the others are there).
    explicit Terms(std::vector<textmatch::Term> terms, std::vector<char> counted = {});
    bool empty() const { return list.empty(); }
    size_t size() const { return list.size(); }
    /// Some term is fuzzy: a vocabulary of the texts is needed.
    bool fuzzy() const { return anyFuzzy; }

    struct Found {
        int count = 0;         ///< hits of the counted terms (where hits of different terms overlap: once)
        bool exact = false;    ///< a hit of them is exact (not only a fuzzy match of a word, WordMatch.h)
        std::vector<char> on;  ///< per term: it is in the texts
    };
    /// The terms in `texts`; `vocab` is the vocabulary of all of them (may be null if no term is fuzzy).
    Found examine(std::initializer_list<QStringView> texts, const Vocabulary* vocab) const;
    /// examine().count
    int count(std::initializer_list<QStringView> texts, const Vocabulary* vocab) const;
    /// Term `i` is in the texts.
    bool contains(size_t i, std::initializer_list<QStringView> texts, const Vocabulary* vocab) const;

private:
    /// A match (of a counted term that is not fuzzy) overlaps a word that a counted fuzzy term matches.
    bool touchesMatchingWord(QStringView text, const textmatch::Span& m) const;

    std::vector<textmatch::Term> list;
    std::vector<char> counts;
    std::vector<std::shared_ptr<const Matches>> matches;  ///< per term: of a fuzzy one
    std::vector<textmatch::Term> countedTerms, countedPlain;
    size_t onlyPlain = 0;  ///< the counted term that is not fuzzy, if there is one
    bool anyFuzzy = false, countedFuzzy = false;
};

}  // namespace xqt::words

/*
 * xournal-qt: the fuzzy search's query - fzf's extended search syntax, with parentheses.
 *
 * Used when the fuzzy search is on (a toggle in the library's search field and in the tab overview's; an app-wide
 * setting). The syntax is fzf's (https://github.com/junegunn/fzf#search-syntax):
 *
 *   term      fuzzy on names (the characters in this order); in text word by word (WordMatch.h: the letters in
 *             this order within a word, or a typo; a term of fewer than 3 letters: a substring)
 *   'term     exact: a substring, also in names
 *   'term'    a whole word (fzf: at word boundaries on both sides)
 *   ^term     names: the name starts with it; text: a word starts with it
 *   term$     names: the name ends with it; text: a word ends with it
 *   ^term$    names: the name is it; text: the whole word
 *   !term     not: neither the name nor the text has it (an exact substring; !'term: fuzzy on names, as in fzf)
 *   a b       a and b (a space)
 *   a | b     a or b; `|` binds closer than the space: `a b | c` is a and (b or c)
 *   ( … )     a group (an extension of fzf's syntax): `(a b) | c`; `!( … )` negates it
 *   \  \( \)  a space, a parenthesis in a term
 *
 * Case never matters (as in the plain search). Terms that are empty after their marks (`^`, `!`) are left out, as
 * fzf leaves them out. An expression that is not valid (a parenthesis not closed or not opened, a `|` without a term on
 * both sides, empty parentheses) is never an error: hint() says why, and the text is searched as plain text.
 *
 * Where a term is looked for (the caller decides what it looks at, this class only evaluates):
 *  - a document's **name** and the **folder path** it is in, with fzf's matching and score (FuzzyMatch.h): the name
 *    first; a term not in the name is looked for in "folder/name" (so `uni lect` finds Uni/Lecture 3) and scores less;
 *  - its **text** (PDF text, text elements, Markdown passages, text files) through TextMatch (case, whitespace,
 *    ligatures, hyphenation): a plain term word by word (a word matches when it has the term's letters in this order
 *    close together, or is the term with a typo: WordMatch.h; the hit is the whole word) - a fuzzy subsequence over
 *    megabytes of text would match nearly everything, over a single word it does not; the other terms as substrings,
 *    at word bounds for ^, $ and 'term'.
 * A term holds for a document when its name/path or its text has it; the document matches when the expression
 * holds with these values.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <vector>

#include <QString>
#include <QStringView>

#include "TextMatch.h"

namespace xqt {

class FuzzyQuery {
public:
    enum class Type { Fuzzy, Exact, Boundary, Prefix, Suffix, Equal };
    struct Term {
        QString text;  ///< prepare()d (TextMatch.h)
        Type type = Type::Fuzzy;
        bool negated = false;  ///< !term
        int typos = 0;         ///< a fuzzy term: the typo tolerance when it was parsed (typoTolerance())
        /// How it is looked for in text: a fuzzy term of letters and digits word by word (textmatch::Fuzzy), the
        /// others as substrings, at word bounds for ^, $, 'term'.
        textmatch::Term textTerm() const;
    };

    FuzzyQuery() = default;
    explicit FuzzyQuery(const QString& query);

    const QString& source() const { return text; }
    /// There is something to search, and the expression is valid.
    bool isValid() const { return error.isEmpty() && root >= 0; }
    /// Why the text is searched as plain text instead ("" if it is valid, or empty).
    const QString& hint() const { return error; }
    const std::vector<Term>& terms() const { return list; }

    /// The expression, with whether each term (by its index; `!` not applied) was found.
    bool evaluate(const std::function<bool(size_t)>& found) const;
    /// The terms whose hits count and are marked: not negated (an even number of negations above them), each once.
    std::vector<textmatch::Term> markTerms() const;
    /// The term is not negated (see markTerms).
    bool positive(size_t term) const { return term < polarity.size() && polarity[term]; }

    /// A name (and the folder path it is in, relative to the library; "" at the top).
    struct NameMatch {
        std::vector<char> found;     ///< per term: found in the name or in "folder/name"
        int score = 0;               ///< the scores of the positive terms found (in the folder path: a quarter)
        std::vector<int> positions;  ///< the characters of the name matched by positive terms (sorted, once each)
    };
    NameMatch matchName(QStringView name, QStringView folder = {}) const;
    /// The expression holds for the name alone (names-only search, folders, files without text).
    bool matchesName(QStringView name, QStringView folder = {}) const;

    /// What a search marks in text: of a valid query its markTerms(); else (the plain search, an expression that is
    /// not valid) the whole text as one term.
    static std::vector<textmatch::Term> textTerms(const QString& query, bool fuzzy);

    /// The typo tolerance of fuzzy terms in text (WordMatch.h: 0 none, 1 one typo in terms of 5+ letters, 2 two in
    /// terms of 8+): an app-wide setting, taken by the queries parsed from then on. Any thread.
    static void setTypoTolerance(int typos);
    static int typoTolerance();

private:
    struct Node {
        enum Op { TermOp, And, Or, Not } op = TermOp;
        int term = -1;
        std::vector<int> kids;
    };
    bool eval(int node, const std::function<bool(size_t)>& found) const;
    void markPolarity(int node, bool positive);

    QString text;
    QString error;
    std::vector<Term> list;
    std::vector<Node> nodes;
    std::vector<char> polarity;  ///< per term: positive
    int root = -1;
    friend class FuzzyQueryParser;
};

}  // namespace xqt

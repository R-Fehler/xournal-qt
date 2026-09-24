/*
 * xournal-qt: the fuzzy search's parts: fzf's matching and score (FuzzyMatch), the query syntax and its evaluation
 * (FuzzyQuery), and what TextMatch does for it (word bounds, several terms at once).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "session/FuzzyMatch.h"
#include "session/FuzzyQuery.h"
#include "session/TextMatch.h"

using namespace xqt;

namespace {
// fzf's constants (algo.go, "default" scheme)
constexpr int M = 16, GS = -3, GE = -1, B = 8, NW = 8, CAMEL = 7, CONS = 4, MULT = 2, WHITE = 10, DELIM = 9;

QString q(const char* s) { return QString::fromUtf8(s); }
}  // namespace

// --- fzf's algorithm ------------------------------------------------------------------------------------------

TEST(FuzzyMatch, scoresAsFzfDoes) {
    // The case-insensitive cases of fzf's TestFuzzyMatch (algo_test.go), forward
    struct Case {
        const char* text;
        const char* pattern;
        int start, end, score;
    };
    const std::vector<Case> cases = {
            {"fooBarbaz1", "obz", 2, 9, M * 3 + CAMEL + GS + GE * 3},
            {"foo bar baz", "fbb", 0, 9, M * 3 + WHITE * MULT + WHITE * 2 + 2 * GS + 4 * GE},
            {"/AutomatorDocument.icns", "rdoc", 9, 13, M * 4 + CAMEL + CONS * 2},
            {"/man1/zshcompctl.1", "zshc", 6, 10, M * 4 + DELIM * MULT + DELIM * 3},
            {"/.oh-my-zsh/cache", "zshc", 8, 13, M * 4 + B * MULT + B * 2 + GS + DELIM},
            {".vimrc", ".vimrc", 0, 6, M * 6 + WHITE * (MULT + 5)},
            {"/.vimrc", ".vimrc", 1, 7, M * 6 + DELIM * (MULT + 5)},
            {"a.vimrc", ".vimrc", 1, 7, M * 6 + B * (MULT + 5)},
            {"ab0123 456", "12356", 3, 10, M * 5 + CONS * 3 + GS + GE},
            {"abc123 456", "12356", 3, 10, M * 5 + CAMEL * MULT + CAMEL * 2 + CONS + GS + GE},
            {"foo/bar/baz", "fbb", 0, 9, M * 3 + WHITE * MULT + DELIM * 2 + 2 * GS + 4 * GE},
            {"fooBarBaz", "fbb", 0, 7, M * 3 + WHITE * MULT + CAMEL * 2 + 2 * GS + 2 * GE},
            {"foo barbaz", "fbb", 0, 8, M * 3 + WHITE * MULT + WHITE + GS * 2 + GE * 3},
            {"fooBar Baz", "foob", 0, 4, M * 4 + WHITE * MULT + WHITE * 3},
            {"xFoo-Bar Baz", "foo-b", 1, 6, M * 5 + CAMEL * MULT + CAMEL * 2 + NW + B},
            {"foo-bar", "o-ba", 2, 6, M * 4 + B * 3},
            {"fooBarbaz", "fooBarbazz", -1, -1, 0},
            {"abc", "abd", -1, -1, 0},
    };
    for (const Case& c: cases) {
        const QString text = q(c.text);
        const auto r = fuzzy::match(text, textmatch::prepare(q(c.pattern)));
        SCOPED_TRACE(std::string(c.text) + " / " + c.pattern);
        EXPECT_EQ(r.start, c.start);
        EXPECT_EQ(r.end, c.end);
        EXPECT_EQ(r.score, c.score);
        if (r.matched()) {
            EXPECT_EQ(static_cast<int>(r.positions.size()), static_cast<int>(textmatch::prepare(q(c.pattern)).size()));
            EXPECT_EQ(r.positions.front(), c.start);
            EXPECT_EQ(r.positions.back() + 1, c.end);
            EXPECT_TRUE(std::is_sorted(r.positions.begin(), r.positions.end()));
            // The score of the alignment it chose is fzf's score of those characters
            EXPECT_EQ(fuzzy::scoreOf(text, textmatch::prepare(q(c.pattern)), r.start, r.end), c.score);
        }
    }
    // The positions: the word starts, not the first letters that fit
    const auto r = fuzzy::match(u"foo bar baz", u"fbb");
    EXPECT_EQ(r.positions, (std::vector<int>{0, 4, 8}));
}

TEST(FuzzyMatch, exactPrefixSuffixEqualAsFzf) {
    // fzf's TestExactMatchNaive (case-insensitive)
    auto e = fuzzy::exact(u"fooBarbaz", u"oba");
    EXPECT_EQ(e.start, 2);
    EXPECT_EQ(e.end, 5);
    EXPECT_EQ(e.score, M * 3 + CAMEL + CONS);
    EXPECT_EQ(e.positions, (std::vector<int>{2, 3, 4}));
    e = fuzzy::exact(u"/man1/zshcompctl.1", u"zshc");
    EXPECT_EQ(e.start, 6);
    EXPECT_EQ(e.score, M * 4 + DELIM * (MULT + 3));
    e = fuzzy::exact(u"/.oh-my-zsh/cache", u"zsh/c");
    EXPECT_EQ(e.start, 8);
    EXPECT_EQ(e.score, M * 5 + B * (MULT + 3) + DELIM);
    EXPECT_FALSE(fuzzy::exact(u"fooBarbaz", u"fooBarbazz").matched());
    // The best place wins over the first one
    EXPECT_EQ(fuzzy::exact(u"xkalman kalman", u"kalman").start, 8);

    // Word boundaries ('term')
    EXPECT_TRUE(fuzzy::exact(u"the kalman filter", u"kalman", true).matched());
    EXPECT_FALSE(fuzzy::exact(u"the kalmanfilter", u"kalman", true).matched());
    EXPECT_FALSE(fuzzy::exact(u"xkalman", u"kalman", true).matched());
    EXPECT_TRUE(fuzzy::exact(u"x/kalman.pdf", u"kalman", true).matched());

    EXPECT_TRUE(fuzzy::prefix(u"Lecture 3", u"lec").matched());
    EXPECT_FALSE(fuzzy::prefix(u"My Lecture", u"lec").matched());
    EXPECT_TRUE(fuzzy::suffix(u"Lecture 3", u"3").matched());
    EXPECT_FALSE(fuzzy::suffix(u"Lecture 3", u"lec").matched());
    EXPECT_TRUE(fuzzy::equal(u"Lecture 3", u"lecture 3").matched());
    EXPECT_FALSE(fuzzy::equal(u"Lecture 3b", u"lecture 3").matched());
    EXPECT_EQ(fuzzy::prefix(u"Lecture 3", u"lec").positions, (std::vector<int>{0, 1, 2}));
}

TEST(FuzzyMatch, ranksNamesAsFzf) {
    // Better matches score higher: at word starts, consecutive, fewer gaps
    auto score = [](const char16_t* name, const char16_t* pattern) { return fuzzy::match(name, pattern).score; };
    EXPECT_GT(score(u"Kalman filter", u"kf"), score(u"kalmanfilter", u"kf"));
    EXPECT_GT(score(u"lecture notes", u"lect"), score(u"selected", u"lect"));
    EXPECT_GT(score(u"lecture", u"lec"), score(u"l-e-c", u"lec"));
    EXPECT_GT(score(u"fuzzy-finder", u"ff"), score(u"fuzzyfinder", u"ff"));
    EXPECT_EQ(score(u"abc", u"x"), 0);
}

// --- TextMatch for the fuzzy search -----------------------------------------------------------------------------

TEST(TextMatch, wordBounds) {
    using namespace textmatch;
    struct Case {
        const char16_t* text;
        const char* query;
        unsigned bounds;
        int count;
    };
    const std::vector<Case> cases = {
            {u"filter filters prefilter", "filter", Anywhere, 3},
            {u"filter filters prefilter", "filter", WordStart, 2},
            {u"filter filters prefilter", "filter", WordEnd, 2},
            {u"filter filters prefilter", "filter", Word, 1},
            {u"Kalman-Filter (filter).", "filter", Word, 2},
            {u"x2filter", "filter", WordStart, 0},
            {u"über überall", "über", Word, 1},
            {u"a hyphen- ated word", "hyphenated", Word, 1},
            {u"aaa", "aa", WordEnd, 1},  // (the match at 1, after the one at 0 failed)
            {u"", "x", Word, 0},
    };
    for (const Case& c: cases) {
        SCOPED_TRACE(c.query);
        EXPECT_EQ(count(c.text, prepare(q(c.query)), c.bounds), c.count);
        EXPECT_EQ(contains(c.text, prepare(q(c.query)), c.bounds), c.count > 0);
        EXPECT_EQ(static_cast<int>(find(c.text, prepare(q(c.query)), c.bounds).size()), c.count);
    }
}

TEST(TextMatch, severalTermsAtOnce) {
    using namespace textmatch;
    const std::vector<Term> terms{{prepare("kalman"), Anywhere}, {prepare("filter"), Anywhere}};
    const QString text = QStringLiteral("The Kalman filter; a filter by Kalman.");
    const auto spans = find(text, terms);
    ASSERT_EQ(spans.size(), 4u);
    EXPECT_EQ(spans[0].start, 4);
    EXPECT_EQ(spans[1].start, 11);
    EXPECT_TRUE(std::is_sorted(spans.begin(), spans.end(), [](const Span& a, const Span& b) { return a.start < b.start; }));
    EXPECT_EQ(count(text, terms), 4);
    // Overlapping terms: the one starting first (of the same start, the longer) - count and marks agree
    const std::vector<Term> overlapping{{prepare("kal"), Anywhere}, {prepare("kalman"), Anywhere}, {prepare("man f"), Anywhere}};
    const auto o = find(text, overlapping);
    EXPECT_EQ(count(text, overlapping), static_cast<int>(o.size()));
    ASSERT_EQ(o.size(), 2u);
    EXPECT_EQ(o[0].end - o[0].start, 6) << "kalman over kal";
    EXPECT_EQ(o[1].start, 31);

    // Encoded for image URLs, and back
    const std::vector<Term> t{{prepare("kalman"), WordStart}, {prepare("a b"), Word}};
    EXPECT_EQ(decode(encode(t)), t);
    EXPECT_TRUE(decode(QString()).empty());
}

// --- the query ------------------------------------------------------------------------------------------------

namespace {
/// Evaluate a query where exactly the words in `present` are found (a term is found when its text is one of them).
bool holds(const FuzzyQuery& query, std::set<std::string> present) {
    return query.evaluate([&](size_t t) { return present.count(query.terms()[t].text.toStdString()) > 0; });
}
}  // namespace

TEST(FuzzyQuery, parsesFzfSyntaxWithGroups) {
    struct Case {
        const char* query;
        std::vector<std::set<std::string>> matching;  ///< sets of words present for which it holds
        std::vector<std::set<std::string>> failing;
    };
    const std::vector<Case> cases = {
            {"a", {{"a"}, {"a", "b"}}, {{}, {"b"}}},
            {"a b", {{"a", "b"}}, {{"a"}, {"b"}, {}}},
            {"a | b", {{"a"}, {"b"}, {"a", "b"}}, {{}, {"c"}}},
            // fzf: `|` binds closer than the space
            {"a b | c", {{"a", "b"}, {"a", "c"}}, {{"b", "c"}, {"a"}, {"b"}}},
            {"a | b c", {{"a", "c"}, {"b", "c"}}, {{"a", "b"}, {"c"}}},
            {"(a b) | c", {{"a", "b"}, {"c"}}, {{"a"}, {"b"}}},
            {"(a | b) (c | d)", {{"a", "c"}, {"b", "d"}}, {{"a", "b"}, {"c", "d"}}},
            {"a !b", {{"a"}, {"a", "c"}}, {{"a", "b"}, {"b"}}},
            {"!b", {{}, {"a"}}, {{"b"}}},
            {"!(a b)", {{}, {"a"}, {"b"}}, {{"a", "b"}}},
            {"!(a | b) c", {{"c"}}, {{"a", "c"}, {"b", "c"}}},
            {"((a))", {{"a"}}, {{}}},
            {"a | !b", {{"a", "b"}, {}}, {{"b"}}},
            {"a ^ b", {{"a", "b"}}, {{"a"}}},  // an empty term is left out
            {"a | ^", {{"a"}}, {{}}},
    };
    for (const Case& c: cases) {
        SCOPED_TRACE(c.query);
        const FuzzyQuery query(q(c.query));
        ASSERT_TRUE(query.isValid()) << query.hint().toStdString();
        EXPECT_TRUE(query.hint().isEmpty());
        for (const auto& m: c.matching) {
            EXPECT_TRUE(holds(query, m)) << "with " << m.size() << " words";
        }
        for (const auto& f: c.failing) {
            EXPECT_FALSE(holds(query, f)) << "with " << f.size() << " words";
        }
    }
}

TEST(FuzzyQuery, termMarks) {
    using T = FuzzyQuery::Type;
    struct Case {
        const char* query;
        const char* text;
        T type;
        bool negated;
        unsigned bounds;  ///< in text
    };
    const std::vector<Case> cases = {
            {"kalman", "kalman", T::Fuzzy, false, textmatch::Anywhere},
            {"'kalman", "kalman", T::Exact, false, textmatch::Anywhere},
            {"'kalman'", "kalman", T::Boundary, false, textmatch::Word},
            {"^kal", "kal", T::Prefix, false, textmatch::WordStart},
            {"man$", "man", T::Suffix, false, textmatch::WordEnd},
            {"^kalman$", "kalman", T::Equal, false, textmatch::Word},
            {"!draft", "draft", T::Exact, true, textmatch::Anywhere},
            {"!'draft", "draft", T::Fuzzy, true, textmatch::Anywhere},
            {"!^draft", "draft", T::Prefix, true, textmatch::WordStart},
            {"!old$", "old", T::Suffix, true, textmatch::WordEnd},
            {"KALMAN", "kalman", T::Fuzzy, false, textmatch::Anywhere},
            {"$", "$", T::Fuzzy, false, textmatch::Anywhere},
            {"a\\ b", "a b", T::Fuzzy, false, textmatch::Anywhere},
            {"f\\(x\\)", "f(x)", T::Fuzzy, false, textmatch::Anywhere},
            {"'|", "|", T::Exact, false, textmatch::Anywhere},
    };
    for (const Case& c: cases) {
        SCOPED_TRACE(c.query);
        const FuzzyQuery query(q(c.query));
        ASSERT_TRUE(query.isValid()) << query.hint().toStdString();
        ASSERT_EQ(query.terms().size(), 1u);
        const auto& t = query.terms().front();
        EXPECT_EQ(t.text, q(c.text));
        EXPECT_EQ(t.type, c.type);
        EXPECT_EQ(t.negated, c.negated);
        EXPECT_EQ(t.textTerm().bounds, c.bounds);
    }
}

TEST(FuzzyQuery, invalidExpressionsAreHintsNotErrors) {
    for (const char* bad: {"(a b", "a b)", "a | ", "| a", "a | | b", "a ()", "(a |) b", "!(a", ")", "a ) (b"}) {
        SCOPED_TRACE(bad);
        const FuzzyQuery query(q(bad));
        EXPECT_FALSE(query.isValid());
        EXPECT_FALSE(query.hint().isEmpty());
        EXPECT_FALSE(query.evaluate([](size_t) { return true; }));
        // Searched as plain text
        const auto terms = FuzzyQuery::textTerms(q(bad), true);
        ASSERT_EQ(terms.size(), 1u);
        EXPECT_EQ(terms.front().text, textmatch::prepare(q(bad)));
    }
    // Nothing to search: no hint, not valid
    for (const char* empty: {"", "   ", "^", "!", "'"}) {
        const FuzzyQuery query(q(empty));
        EXPECT_FALSE(query.isValid()) << empty;
        EXPECT_TRUE(query.hint().isEmpty()) << empty;
    }
}

TEST(FuzzyQuery, marksTheTermsThatAreNotNegated) {
    auto marked = [](const char* query) {
        std::vector<std::string> out;
        for (const auto& t: FuzzyQuery(q(query)).markTerms()) {
            out.push_back(t.text.toStdString() + "/" + std::to_string(t.bounds));
        }
        return out;
    };
    EXPECT_EQ(marked("kalman !draft"), (std::vector<std::string>{"kalman/0"}));
    EXPECT_EQ(marked("a | b a"), (std::vector<std::string>{"a/0", "b/0"})) << "each once";
    EXPECT_EQ(marked("!(a !b) ^c"), (std::vector<std::string>{"b/0", "c/1"})) << "two negations: marked";
    EXPECT_EQ(marked("'x' y$"), (std::vector<std::string>{"x/3", "y/2"}));
    // The plain search: the whole text, one term
    const auto plain = FuzzyQuery::textTerms(QStringLiteral("kalman | filter"), false);
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_EQ(plain.front().text, QStringLiteral("kalman | filter"));
    EXPECT_TRUE(FuzzyQuery::textTerms(QStringLiteral("  "), true).empty());
}

TEST(FuzzyQuery, matchesNamesAndFolders) {
    struct Case {
        const char* query;
        const char* name;
        const char* folder;
        bool matches;
    };
    const std::vector<Case> cases = {
            {"lect", "Lecture 3", "", true},
            {"lct3", "Lecture 3", "", true},  // fuzzy: in this order
            {"3lect", "Lecture 3", "", false},
            {"'lct", "Lecture 3", "", false},  // exact
            {"'ture", "Lecture 3", "", true},
            {"^lec", "Lecture 3", "", true},
            {"^ture", "Lecture 3", "", false},
            {"3$", "Lecture 3", "", true},
            {"^lecture 3$", "Lecture 3", "", true},  // two terms: "^lecture" and "3$"
            {"^lecture\\ 3$", "Lecture 3", "", true},
            {"^lecture\\ 3$", "Lecture 31", "", false},
            {"uni lect", "Lecture 3", "Uni/Semester 3", true},  // "uni" in the folder path
            {"uni lect", "Lecture 3", "Work", false},
            {"^uni", "Lecture 3", "Uni", true},
            {"!archive", "Lecture 3", "Archive/2019", false},
            {"!archive", "Lecture 3", "Uni", true},
            {"lecture !draft", "Lecture 3 draft", "", false},
            {"lecture | notes", "My notes", "", true},
            {"(kalman | lqr) lect", "LQR lecture", "", true},
            {"(kalman | lqr) lect", "Kalman seminar", "", false},
            {"'lecture'", "Lectures", "", false},
            {"'lecture'", "The lecture", "", true},
    };
    for (const Case& c: cases) {
        SCOPED_TRACE(std::string(c.query) + " / " + c.name);
        const FuzzyQuery query(q(c.query));
        ASSERT_TRUE(query.isValid());
        EXPECT_EQ(query.matchesName(q(c.name), q(c.folder)), c.matches);
    }
    // Ranking: better names first, a match in the folder path counts less than in the name
    const FuzzyQuery lect(QStringLiteral("lect"));
    auto score = [&](const char* name, const char* folder = "") { return lect.matchName(q(name), q(folder)).score; };
    EXPECT_GT(score("Lecture 3"), score("Selected works"));
    EXPECT_GT(score("Lecture 3"), score("Notes", "Lectures"));
    EXPECT_GT(score("Notes", "Lectures"), 0);
    EXPECT_EQ(score("Notes", "Work"), 0);
    // The characters to highlight: of the name only, of positive terms only
    const FuzzyQuery two(QStringLiteral("lec 3 !x"));
    EXPECT_EQ(two.matchName(u"Lecture 3", u"").positions, (std::vector<int>{0, 1, 2, 8}));
    const FuzzyQuery inFolder(QStringLiteral("uni"));
    EXPECT_TRUE(inFolder.matchName(u"Notes", u"Uni").positions.empty());
    // A fuzzy term across folder and name highlights the part in the name
    const FuzzyQuery across(QStringLiteral("unino"));
    const auto m = across.matchName(u"Notes", u"Uni");
    EXPECT_EQ(m.found, (std::vector<char>{1}));
    EXPECT_EQ(m.positions, (std::vector<int>{0, 1}));
}

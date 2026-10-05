/*
 * xournal-qt: find and replace in the source of a text (session/TextReplace.h).
 *
 * @license GNU GPLv2 or later
 */
#include <gtest/gtest.h>

#include "session/TextReplace.h"

using namespace xqt;

namespace {
std::string replaced(const std::string& source, const QString& query, const QString& with,
                     replace::Options options = {}) {
    return replace::apply(source, replace::find(source, query, with, options));
}
}  // namespace

TEST(TextReplace, replacesInTheSourceAsTheSearchMatches) {
    EXPECT_EQ(replaced("Cat, cat and CAT.", "cat", "dog"), "dog, dog and dog.") << "case-insensitive";
    EXPECT_EQ(replaced("Cat, cat and CAT.", "cat", "dog", {true, false, false}), "Cat, dog and CAT.");
    EXPECT_EQ(replaced("cat cats concat cat.", "cat", "dog", {false, true, false}), "dog cats concat dog.");
    EXPECT_EQ(replaced("a **cat** here", "cat", "dog"), "a **dog** here") << "the marks stay";
    EXPECT_EQ(replaced("one\ntwo", "one two", "1 2"), "1 2") << "the lines of a paragraph";
    EXPECT_EQ(replaced("one\n\ntwo", "one two", "x"), "one\n\ntwo") << "not across a blank line";
    EXPECT_EQ(replaced("one   two", " one  two ", "x"), "x") << "trimmed, whitespace runs";
    EXPECT_EQ(replaced("a.b axb", "a.b", "c"), "c axb") << "a query is not an expression";
    EXPECT_EQ(replaced("Größe größe", "größe", "Maß", {true, false, false}), "Größe Maß");
    EXPECT_EQ(replaced("Größe größe", "GRÖSSE", "x"), "Größe größe") << "simple case folding only";
    EXPECT_EQ(replaced("abc", "", "x"), "abc");
    EXPECT_EQ(replaced("abc", "   ", "x"), "abc");
}

TEST(TextReplace, offsetsAreBytesOfTheSource) {
    const std::string source = "ä 🙂 cat";
    const auto m = replace::find(source, "cat", "dog", {});
    ASSERT_EQ(m.size(), 1u);
    EXPECT_EQ(source.substr(m[0].begin, m[0].end - m[0].begin), "cat");
    EXPECT_EQ(m[0].begin, source.size() - 3);
}

TEST(TextReplace, regularExpressionsWithGroups) {
    const replace::Options re{false, false, true};
    EXPECT_EQ(replaced("2026-10-05", "(\\d+)-(\\d+)-(\\d+)", "$3.$2.$1", re), "05.10.2026");
    EXPECT_EQ(replaced("2026-10-05", "(\\d+)-(\\d+)-(\\d+)", "\\3/\\2/\\1", re), "05/10/2026");
    EXPECT_EQ(replaced("ab", "(?<x>a)(b)", "${x}${2}$0", re), "abab");
    EXPECT_EQ(replaced("a b", "a b", "a\\nb\\t$$\\\\", re), "a\nb\t$\\");
    EXPECT_EQ(replaced("ab", "(a)b", "$5|\\7|$1", re), "||a") << "groups it does not have: nothing";
    EXPECT_EQ(replaced("Item item", "^item", "x", re), "x item") << "^: a line's start; case-insensitive";
    EXPECT_EQ(replaced("one\ntwo", "^", "- ", re), "one\ntwo") << "no empty matches";
    EXPECT_EQ(replaced("one\ntwo", "^(\\w)", "- $1", re), "- one\n- two");
    EXPECT_EQ(replaced("x(", "(", "y", re), "x(") << "not valid: nothing";
    EXPECT_TRUE(replace::find("a", "(", "", re).empty());
    EXPECT_EQ(replaced("cat concat", "cat", "dog", {false, true, true}), "dog concat") << "whole words";
}

TEST(TextReplace, neverInTheAppsOwnComments) {
    const std::string source = "<!-- xqt:bookmark Notes -->\n# Notes\n\nnotes <!-- other notes -->";
    EXPECT_EQ(replaced(source, "notes", "x"), "<!-- xqt:bookmark Notes -->\n# x\n\nx <!-- other x -->");
    const auto hidden = replace::hiddenRanges(source);
    ASSERT_EQ(hidden.size(), 1u);
    EXPECT_EQ(hidden[0].first, 0u);
    EXPECT_EQ(hidden[0].second, 27u);
    EXPECT_EQ(replaced("a <!-- xqt:plain", "plain", "x"), "a <!-- xqt:plain") << "an open one to the end";
}

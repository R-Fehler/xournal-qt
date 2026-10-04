/*
 * xournal-qt: the words recognised in handwriting and how the search matches them (InkText.h).
 *
 * @license GNU GPLv2 or later
 */
#include <gtest/gtest.h>

#include "session/FuzzyQuery.h"
#include "session/InkText.h"
#include "session/Vocabulary.h"

using namespace xqt;

namespace {
/// A word with readings {text, p}, best first.
ink::Word word(QRectF box, std::initializer_list<std::pair<const char*, float>> readings, float conf = 0.9f) {
    ink::Word w;
    w.box = box;
    w.conf = conf;
    for (const auto& [text, p]: readings) {
        if (w.text.isEmpty()) {
            w.text = QString::fromUtf8(text);
        }
        w.candidates.push_back(ink::candidate(QString::fromUtf8(text), p));
    }
    return w;
}

/// One line of words, 40 pt apart, at `origin`.
std::shared_ptr<const ink::LineResult> line(std::vector<ink::Word> words) {
    auto l = std::make_shared<ink::LineResult>();
    l->words = std::move(words);
    return l;
}

std::shared_ptr<const ink::PageText> page(std::vector<ink::PlacedLine> lines) {
    return ink::PageText::assemble(lines);
}

std::vector<ink::Hit> find(const ink::PageText& p, const QString& query, bool fuzzy = false, int typos = 1) {
    return ink::find(p, FuzzyQuery::textTerms(query, fuzzy), typos);
}
}  // namespace

TEST(InkTextTest, readingsAreFoldedAndNumbered) {
    EXPECT_EQ(ink::folded(u"Don't"), QStringLiteral("dont"));
    EXPECT_EQ(ink::folded(u"ﬁlter."), QStringLiteral("filter"));
    const ink::Candidate a = ink::candidate(u"Kalman,", 0.5f);
    const ink::Candidate b = ink::candidate(u"kalman", 0.2f);
    EXPECT_EQ(a.word, b.word);
    EXPECT_EQ(words::textOf(a.word), QStringLiteral("kalman"));
    EXPECT_EQ(ink::candidate(u"--", 1).word, words::NO_WORD);
}

TEST(InkTextTest, linesArePlacedOnThePage) {
    auto p = page({{{100, 200}, line({word({0, 0, 30, 10}, {{"Kalman", 1}}), word({40, 0, 30, 10}, {{"filter", 1}})})},
                   {{100, 220}, nullptr},  // (a line not recognised yet)
                   {{100, 240}, line({word({0, 0, 30, 10}, {{"next", 1}})})}});
    ASSERT_EQ(p->words.size(), 3u);
    EXPECT_EQ(p->words[1].box, QRectF(140, 200, 30, 10));
    EXPECT_EQ(p->words[2].box, QRectF(100, 240, 30, 10));
    EXPECT_EQ(p->lineStarts, (std::vector<uint32_t>{0, 2}));
    EXPECT_EQ(p->text(), QStringLiteral("Kalman filter\nnext"));
    EXPECT_GT(p->bytes(), sizeof(ink::PageText));
}

TEST(InkTextTest, aWrongBestReadingIsFoundThroughTheOthers) {
    auto p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"Kalmar", 0.6f}, {"Kalman", 0.3f}, {"Kolmar", 0.1f}})})}});
    auto hits = find(*p, "kalman");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_FALSE(hits[0].exact);  // (the best reading does not have it: ranked after typed text)
    EXPECT_FLOAT_EQ(hits[0].p, 0.3f + 0.6f);  // "kalmar" is "kalman" with a typo: both readings count
    hits = find(*p, "kalmar");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_TRUE(hits[0].exact);
    // Contained in a reading, as text finds "kalman" in "kalmanfilter"
    EXPECT_EQ(find(*p, "alma").size(), 1u);
}

TEST(InkTextTest, unlikelyReadingsDoNotTakePart) {
    auto p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"forest", 0.97f}, {"kalman", 0.03f}})})}});
    EXPECT_TRUE(find(*p, "kalman").empty());
    // ... unless it is the best one, however unsure
    p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"kalman", 0.03f}})})}});
    EXPECT_EQ(find(*p, "kalman").size(), 1u);
    EXPECT_FALSE(find(*p, "kalman")[0].exact);
}

TEST(InkTextTest, typosAreToleratedWithoutTheFuzzySearch) {
    auto p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"turbine", 1}})})}});
    EXPECT_EQ(find(*p, "turbnie", false, 1).size(), 1u);
    EXPECT_EQ(find(*p, "turbime", false, 1).size(), 1u);
    EXPECT_TRUE(find(*p, "turbnie", false, 0).empty());  // (the setting: none)
    // The letters in order (WordMatch rule 2) only with the fuzzy search
    EXPECT_TRUE(find(*p, "tbine", false, 1).empty());
    EXPECT_EQ(find(*p, "tbine", true, 1).size(), 1u);
}

TEST(InkTextTest, shortTermsOnlyMatchSureWholeWords) {
    auto p = page({{{0, 0},
                    line({word({0, 0, 10, 10}, {{"a", 0.9f}}, 0.9f), word({20, 0, 30, 10}, {{"banana", 1}}),
                          word({60, 0, 10, 10}, {{"a", 0.9f}}, 0.4f), word({80, 0, 10, 10}, {{"o", 0.6f}, {"a", 0.4f}})})}});
    const auto hits = find(*p, "a");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].first, 0u);
}

TEST(InkTextTest, phrasesMatchConsecutiveWords) {
    auto p = page({{{0, 0},
                    line({word({0, 0, 30, 10}, {{"This", 1}}), word({40, 0, 20, 10}, {{"is", 1}}),
                          word({70, 0, 10, 10}, {{"a", 1}}), word({90, 0, 40, 10}, {{"dumb", 0.7f}, {"damb", 0.3f}})})},
                   {{0, 20}, line({word({0, 0, 30, 10}, {{"test", 1}}), word({40, 0, 30, 10}, {{"written", 1}})})}});
    auto hits = find(*p, "is a dumb");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].first, 1u);
    EXPECT_EQ(hits[0].last, 3u);
    EXPECT_TRUE(hits[0].exact);
    // Across the line end: one hit, marked on both lines
    hits = find(*p, "dumb test");
    ASSERT_EQ(hits.size(), 1u);
    const auto rects = ink::rectsOf(*p, hits[0]);
    ASSERT_EQ(rects.size(), 2u);
    EXPECT_EQ(rects[0], QRectF(90, 0, 40, 10).adjusted(-2, -2, 2, 2));
    EXPECT_EQ(rects[1], QRectF(0, 20, 30, 10).adjusted(-2, -2, 2, 2));
    EXPECT_TRUE(find(*p, "dumb written").empty());
}

TEST(InkTextTest, oneHitPerInkWord) {
    auto p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"kalman", 1}}), word({40, 0, 30, 10}, {{"filter", 1}})})}});
    // Two terms of the fuzzy search that both match the first word: one hit
    auto hits = find(*p, "kalman kalma", true);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].first, 0u);
    hits = find(*p, "kalman | filter", true);
    EXPECT_EQ(hits.size(), 2u);
}

TEST(InkTextTest, boundsOfTheFuzzySearch) {
    auto p = page({{{0, 0}, line({word({0, 0, 30, 10}, {{"kalman", 1}})})}});
    EXPECT_EQ(find(*p, "^kal", true).size(), 1u);
    EXPECT_TRUE(find(*p, "^alm", true).empty());
    EXPECT_EQ(find(*p, "man$", true).size(), 1u);
    EXPECT_TRUE(find(*p, "kal$", true).empty());
    EXPECT_EQ(find(*p, "'kalman'", true).size(), 1u);
    EXPECT_TRUE(find(*p, "'kalma'", true).empty());
    EXPECT_TRUE(ink::contains(*p, {QStringLiteral("kalman"), textmatch::Anywhere}, 1));
    EXPECT_FALSE(ink::contains(*p, {QStringLiteral("filter"), textmatch::Anywhere}, 1));
}

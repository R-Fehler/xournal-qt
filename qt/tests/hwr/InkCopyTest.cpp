/*
 * xournal-qt: handwriting copied as text (InkCopy.h): the words a sweep takes, put into lines in reading order; and
 * the worker's urgent jobs for it (InkRecognitionService: the area only, before everything else, without waiting).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <memory>
#include <random>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/InkCopy.h"
#include "hwr/InkRecognitionService.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
CopiedWord word(const char* text, double x, double y, double w = 40, double h = 12, float conf = 0.9f) {
    return {QString::fromUtf8(text), conf, QRectF(x, y, w, h)};
}

ink::Word inkWord(const char* text, double x, double y, double w = 40, double h = 12) {
    ink::Word out;
    out.box = QRectF(x, y, w, h);
    out.conf = 0.9f;
    out.text = QString::fromUtf8(text);
    out.candidates.push_back(ink::candidate(out.text, 1.0f));
    return out;
}

/// Two lines of three words: "one two three" at y 100, "four five six" at y 130
ink::PageText twoLines() {
    ink::PageText t;
    const char* names[] = {"one", "two", "three", "four", "five", "six"};
    for (int i = 0; i < 6; ++i) {
        if (i % 3 == 0) {
            t.lineStarts.push_back(static_cast<uint32_t>(t.words.size()));
        }
        t.words.push_back(inkWord(names[i], 50 + 60 * (i % 3), 100 + 30 * (i / 3)));
    }
    return t;
}

QStringList texts(const std::vector<CopiedWord>& words) {
    QStringList out;
    for (const CopiedWord& w: words) {
        out << w.text;
    }
    return out;
}

/// A written word for the worker: a zigzag of 4 letters, 10 pt high, at x, y
InkStroke written(double x, double y, int shape = 0) {
    std::vector<QPointF> pts;
    for (int i = 0; i <= 8; ++i) {
        pts.emplace_back(x + i * 3.0, y + (i % 2 ? 10 : 0) + (i == 1 ? 0.3 * shape : 0));
    }
    return InkStroke::of(std::move(pts), 1.0f);
}
/// `lines` lines of three words, 40 pt apart from y 100
std::vector<InkStroke> page(int lines, int shape = 0) {
    std::vector<InkStroke> out;
    for (int l = 0; l < lines; ++l) {
        for (int w = 0; w < 3; ++w) {
            out.push_back(written(50 + 60 * w, 100 + 40 * l, shape + l + 1));
        }
    }
    return out;
}

bool waitFor(const std::function<bool()>& done, int ms = 10000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}
}  // namespace

// Words in any order become lines top to bottom, words left to right; a line written on a slope or with tall letters
// stays one line; lines far apart get an empty line between them
TEST(InkCopyTest, wordsAreOrderedIntoLinesInReadingOrder) {
    std::vector<CopiedWord> words{
            // a line written downhill (each word 3 pt lower), one word with an ascender (taller, reaching up)
            word("Kalman", 50, 100), word("filter", 100, 103), word("tracks", 150, 99, 40, 18), word("well", 200, 106),
            // the next line, close below
            word("noisy", 50, 125), word("data", 100, 126),
            // a paragraph further down
            word("Done.", 50, 190)};
    std::mt19937 shuffle(7);
    std::shuffle(words.begin(), words.end(), shuffle);
    const CopiedText t = inReadingOrder(words);
    EXPECT_EQ(t.text, QStringLiteral("Kalman filter tracks well\nnoisy data\n\nDone."));
    ASSERT_EQ(t.lines.size(), 3u);
    EXPECT_EQ(t.lines[0].size(), 4u);
    EXPECT_EQ(t.words, 7);
    EXPECT_EQ(t.unsure, 0);
    EXPECT_EQ(t.box, QRectF(QPointF(50, 99), QPointF(240, 202)));
}

TEST(InkCopyTest, wordsBesideEachOtherAreOneLineAndUnsureWordsAreCounted) {
    // Two words side by side at slightly different heights, a third unsure one; an empty reading is left out
    const CopiedText t = inReadingOrder({word("b", 120, 52, 30, 12, 0.3f), word("a", 50, 50), word(" ", 200, 50),
                                         word("c", 180, 49)});
    EXPECT_EQ(t.text, QStringLiteral("a b c"));
    EXPECT_EQ(t.words, 3);
    EXPECT_EQ(t.unsure, 1);
    ASSERT_EQ(t.lines.size(), 1u);
    EXPECT_TRUE(t.lines[0][1].unsure());
    // Lines written far apart, all alike (every other line of the ruling): no paragraphs
    EXPECT_EQ(inReadingOrder({word("x", 50, 100), word("y", 50, 140), word("z", 50, 180)}).text,
              QStringLiteral("x\ny\nz"));
    EXPECT_TRUE(inReadingOrder({}).empty());
    EXPECT_TRUE(inReadingOrder({}).text.isEmpty());
}

// A sweep takes the words it goes over (also a stroke between two lines that touches both); a loop the words it
// encloses; a tap the word under it
TEST(InkCopyTest, aSweepTakesTheWordsItTouchesOrEncloses) {
    const ink::PageText ink = twoLines();
    // Along the first line, from the middle of "two" to "three"
    EXPECT_EQ(texts(sweptWords(ink, {{130, 106}, {170, 107}, {230, 105}}, 2)),
              (QStringList{"two", "three"}));
    // A loop around the second line
    EXPECT_EQ(texts(sweptWords(ink, {{40, 125}, {280, 125}, {280, 150}, {40, 150}, {42, 126}}, 2)),
              (QStringList{"four", "five", "six"}));
    // A tap on "five"
    EXPECT_EQ(texts(sweptWords(ink, {{125, 135}}, 2)), QStringList{"five"});
    // Beside the words: nothing (within reach: taken)
    EXPECT_TRUE(sweptWords(ink, {{20, 60}, {300, 60}}, 2).empty());
    EXPECT_EQ(texts(sweptWords(ink, {{20, 99}, {60, 99}}, 2)), QStringList{"one"});
    // All of them in reading order
    EXPECT_EQ(inReadingOrder(allWords(ink)).text, QStringLiteral("one two three\nfour five six"));
}

// The user waits for a copy: the worker reads only the lines of its area, before the pages queued for the search, and
// does not wait while the user writes
TEST(InkCopyTest, anUrgentJobReadsOnlyItsAreaAndGoesFirst) {
    InkRecognitionService service;
    auto fake = std::make_shared<FakeRecognizer>();
    fake->setDelay(20);
    service.setRecognizer(fake);
    service.setActivityPause(60000);
    service.noteActivity();  // (the user just wrote: the search's pages wait a minute)
    std::vector<int> order;
    QObject search, copier;
    InkRecognitionService::Job background;
    background.strokes = page(5, 10);
    service.submit(&search, std::move(background), [&](PageResult) { order.push_back(1); });
    InkRecognitionService::Job copy;
    copy.strokes = page(3);
    copy.area = QRectF(40, 135, 200, 20);  // (the second line only)
    copy.urgent = true;
    PageResult got;
    service.submit(&copier, std::move(copy), [&](PageResult r) {
        order.push_back(2);
        got = std::move(r);
    });
    ASSERT_TRUE(waitFor([&] { return !order.empty(); }, 5000)) << "not paused by the writing";
    EXPECT_EQ(order, std::vector<int>{2}) << "before the page queued earlier, which still waits";
    EXPECT_EQ(fake->calls(), 1) << "only the line of the area was read";
    ASSERT_TRUE(got.text);
    ASSERT_EQ(got.lines.size(), 1u);
    EXPECT_TRUE(got.complete);
    EXPECT_EQ(inReadingOrder(allWords(*got.text)).text, QStringLiteral("w0 w1 w2"));
    EXPECT_NEAR(got.text->words[0].box.top(), 140, 1e-9);
    service.cancel(&search);
}

// A page being read gives way to it after its line, and goes on afterwards without reading a line twice
TEST(InkCopyTest, aPageBeingReadGivesWayToAnUrgentJob) {
    InkRecognitionService service;
    auto fake = std::make_shared<FakeRecognizer>();
    fake->setDelay(40);
    service.setRecognizer(fake);
    service.setActivityPause(0);
    std::vector<int> order;
    QObject search, copier;
    InkRecognitionService::Job slow;
    slow.strokes = page(6, 20);
    PageResult whole;
    service.submit(&search, std::move(slow), [&](PageResult r) {
        order.push_back(1);
        whole = std::move(r);
    });
    ASSERT_TRUE(waitFor([&] { return fake->calls() >= 2; }));
    InkRecognitionService::Job copy;
    copy.strokes = page(1, 30);
    copy.urgent = true;
    service.submit(&copier, std::move(copy), [&](PageResult) { order.push_back(2); });
    ASSERT_TRUE(waitFor([&] { return order.size() == 2; }));
    EXPECT_EQ(order, (std::vector<int>{2, 1}));
    EXPECT_TRUE(whole.complete);
    EXPECT_EQ(whole.lines.size(), 6u);
    EXPECT_EQ(fake->calls(), 6 + 1) << "no line of the page read twice";
}

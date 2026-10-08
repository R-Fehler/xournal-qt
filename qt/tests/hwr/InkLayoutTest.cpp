/*
 * xournal-qt: lines and words of handwriting from the strokes alone (InkLayout.h).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <iostream>
#include <shared_mutex>

#include <gtest/gtest.h>

#include "hwr/InkLayout.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "config-test.h"

using namespace xqt;
using hwr::InkStroke;
using hwr::layout;
using hwr::strokesOf;

namespace {
/// The pen strokes of the first page of upstream's handwriting benchmark (13,064 strokes: two sentences written
/// many times, partly over each other).
const std::vector<InkStroke>& benchmark() {
    static const std::vector<InkStroke> strokes = [] {
        auto r = DocumentSession::loadFile(GET_TESTFILE(u8"benchmark/handwritten-text.xopp"));
        EXPECT_TRUE(r.document) << r.error;
        std::shared_lock lock(*r.document);
        return strokesOf(*r.document->getPage(0));
    }();
    return strokes;
}

/// Those whose points lie in the band y0..y1 on average (as qt/research/hwr/segment.py selects them).
std::vector<InkStroke> band(double y0, double y1) {
    std::vector<InkStroke> out;
    for (const InkStroke& s: benchmark()) {
        double y = 0;
        for (const QPointF& p: s.points) {
            y += p.y();
        }
        y /= static_cast<double>(s.points.size());
        if (y >= y0 && y <= y1) {
            out.push_back(s);
        }
    }
    return out;
}

/// A stroke: a zigzag (a written word, `letters` letters) at x, y with height h.
InkStroke word(double x, double y, double h, int letters = 4) {
    std::vector<QPointF> pts;
    for (int i = 0; i <= 2 * letters; ++i) {
        pts.emplace_back(x + i * h * 0.3, y + (i % 2 ? h : 0));
    }
    return InkStroke::of(std::move(pts), 1.0f);
}
InkStroke dot(double x, double y) { return InkStroke::of({{x, y}, {x + 0.5, y + 0.5}}, 1.5f); }
InkStroke straight(double x0, double y0, double x1, double y1) {
    return InkStroke::of({{x0, y0}, {(x0 + x1) / 2, (y0 + y1) / 2}, {x1, y1}}, 1.0f);
}
}  // namespace

TEST(InkLayoutTest, theBenchmarkLineHasItsSixteenWords) {
    // The words qt/research/hwr/segment.py finds there (the boxes of all their strokes, page points); the comma of
    // "test," (word 4) is in its word here, segment.py left it out as a line of its own
    const double expected[16][4] = {{60.0, 548.6, 91.4, 566.9},    {99.8, 553.1, 104.9, 563.0},
                                    {113.6, 560.0, 119.9, 565.1},  {124.8, 551.7, 146.4, 565.9},
                                    {151.4, 554.2, 169.7, 568.7},  {175.0, 550.2, 197.5, 564.3},
                                    {204.3, 558.5, 222.7, 575.4},  {227.6, 554.1, 247.9, 566.0},
                                    {251.4, 547.9, 291.0, 565.6},  {300.1, 551.6, 305.8, 559.6},
                                    {315.3, 557.2, 322.1, 561.3},  {327.5, 550.5, 351.1, 561.9},
                                    {356.6, 552.4, 376.7, 564.2},  {382.5, 549.5, 407.1, 560.6},
                                    {414.6, 555.9, 434.7, 569.5},  {440.1, 552.4, 470.3, 561.9}};
    const auto strokes = band(550, 575);
    ASSERT_EQ(strokes.size(), 292u);
    const hwr::Layout l = layout(strokes);
    ASSERT_EQ(l.lines.size(), 1u);  // (the same line pasted four times over itself: one line)
    const auto& words = l.lines[0].words;
    ASSERT_EQ(words.size(), 16u);
    for (size_t i = 0; i < 16; ++i) {
        const QRectF& b = words[i].box;
        EXPECT_NEAR(b.left(), expected[i][0], 0.06) << i;
        EXPECT_NEAR(b.top(), expected[i][1], 0.06) << i;
        EXPECT_NEAR(b.right(), expected[i][2], 0.06) << i;
        EXPECT_NEAR(b.bottom(), expected[i][3], 0.06) << i;
    }
    size_t inWords = 0;
    for (const auto& w: words) {
        inWords += w.strokes.size();
    }
    EXPECT_EQ(inWords, l.lines[0].strokes.size());
}

TEST(InkLayoutTest, aWholeBenchmarkPageIsQuick) {
    const auto& strokes = benchmark();
    ASSERT_EQ(strokes.size(), 13064u);
    const auto start = std::chrono::steady_clock::now();
    const hwr::Layout l = layout(strokes);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    EXPECT_LT(ms.count(), 500);
    // Lines written over each other are one line each; between 10 and 30 of them, each with words
    EXPECT_GE(l.lines.size(), 10u);
    EXPECT_LE(l.lines.size(), 30u);
    for (size_t i = 1; i < l.lines.size(); ++i) {
        EXPECT_LE(l.lines[i - 1].box.top(), l.lines[i].box.top());
    }
    std::cout << "[ layout   ] " << strokes.size() << " strokes, " << l.lines.size() << " lines in " << ms.count()
              << " ms\n";
}

TEST(InkLayoutTest, linesFollowTheOrderOfWriting) {
    std::vector<InkStroke> s;
    s.push_back(word(10, 100, 10));   // line 1: two words
    s.push_back(word(60, 100, 10));
    s.push_back(word(10, 130, 10));   // line 2: the pen went back left and down
    s.push_back(word(60, 131, 10));
    s.push_back(word(110, 101, 10));  // a word added to line 1 afterwards
    s.push_back(dot(13, 96));         // an i-dot of the first word, written last
    const hwr::Layout l = layout(s);
    ASSERT_EQ(l.lines.size(), 2u);
    EXPECT_EQ(l.lines[0].strokes, (std::vector<uint32_t>{0, 1, 4, 5}));
    EXPECT_EQ(l.lines[1].strokes, (std::vector<uint32_t>{2, 3}));
    ASSERT_EQ(l.lines[0].words.size(), 3u);
    EXPECT_EQ(l.lines[0].words[0].strokes, (std::vector<uint32_t>{0, 5}));  // (the dot joins its word)
    EXPECT_EQ(l.lines[1].words.size(), 2u);
}

TEST(InkLayoutTest, drawingsAreLeftOut) {
    std::vector<InkStroke> s;
    for (int i = 0; i < 5; ++i) {
        s.push_back(word(10 + 50 * i, 100, 10));
    }
    s.push_back(straight(10, 115, 234, 115));  // an underline
    s.push_back(word(200, 50, 80, 3));         // a tall shape
    InkStroke filled = word(10, 300, 10);
    filled.filled = true;
    s.push_back(filled);
    const hwr::Layout l = layout(s);
    EXPECT_EQ(l.drawings, (std::vector<uint32_t>{5, 6, 7}));
    ASSERT_EQ(l.lines.size(), 1u);
    EXPECT_EQ(l.lines[0].words.size(), 5u);
}

TEST(InkLayoutTest, aMovedLineKeepsItsHash) {
    const auto strokes = band(550, 575);
    const hwr::Layout before = layout(strokes);
    auto moved = strokes;
    for (InkStroke& s: moved) {
        for (QPointF& p: s.points) {
            p += QPointF(50.3, -120.7);
        }
        s = InkStroke::of(s.points, s.width, s.widths);
    }
    const hwr::Layout after = layout(moved);
    ASSERT_EQ(after.lines.size(), 1u);
    EXPECT_EQ(after.lines[0].hash, before.lines[0].hash);
    EXPECT_NEAR(after.lines[0].origin().x(), before.lines[0].origin().x() + 50.3, 1e-9);
    // A changed stroke: another hash
    auto changed = strokes;
    changed[10].points[1] += QPointF(0.5, 0);
    EXPECT_NE(layout(changed).lines[0].hash, before.lines[0].hash);
}

namespace {
/// Everything the layout found (lines, their strokes and hashes, words, their strokes and boxes in 0.01 pt, drawings)
/// as one number.
quint64 fingerprint(const hwr::Layout& l) {
    quint64 f = 1469598103934665603ULL;
    auto mix = [&](qint64 v) {
        for (int i = 0; i < 8; ++i) {
            f ^= (static_cast<quint64>(v) >> (8 * i)) & 0xff;
            f *= 1099511628211ULL;
        }
    };
    auto box = [&](const QRectF& b) {
        for (const double v: {b.left(), b.top(), b.right(), b.bottom()}) {
            mix(std::llround(v * 100));
        }
    };
    for (const auto& line: l.lines) {
        mix(static_cast<qint64>(line.hash));
        box(line.box);
        for (const uint32_t s: line.strokes) {
            mix(s);
        }
        for (const auto& w: line.words) {
            box(w.box);
            for (const uint32_t s: w.strokes) {
                mix(s);
            }
        }
    }
    for (const uint32_t d: l.drawings) {
        mix(d);
    }
    return f;
}
}  // namespace

TEST(InkLayoutTest, theBenchmarkPageIsLaidOutAsBefore) {
    // Horizontal handwriting comes out exactly as before lines at an angle were found (the same lines, words and
    // hashes): the value of the layout of 2026-10-08
    const hwr::Layout l = layout(benchmark());
    EXPECT_EQ(fingerprint(l), 14858811291843322082ULL);
}

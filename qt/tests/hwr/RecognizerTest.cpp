/*
 * xournal-qt: what recognisers get and give (Recognizer.h), the scripted one, the picture of a line (LineImage.h) and
 * readings put on word boxes (WordAlignment.h).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <atomic>
#include <cmath>
#include <shared_mutex>
#include <thread>

#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "hwr/InkLayout.h"
#include "hwr/LineImage.h"
#include "hwr/WordAlignment.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "config-test.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
InkStroke zigzag(double x, double y, double h, int letters = 4) {
    std::vector<QPointF> pts;
    for (int i = 0; i <= 2 * letters; ++i) {
        pts.emplace_back(x + i * h * 0.3, y + (i % 2 ? h : 0));
    }
    return InkStroke::of(std::move(pts), 1.0f);
}

/// A line of `n` words, 50 pt apart, at (100, 200)
std::pair<std::vector<InkStroke>, hwr::Layout> lineOf(int n) {
    std::vector<InkStroke> s;
    for (int i = 0; i < n; ++i) {
        s.push_back(zigzag(100 + 50 * i, 200, 10));
    }
    hwr::Layout l = layout(s);
    return {std::move(s), std::move(l)};
}

/// The benchmark's line (y 550-575)
LineInput benchmarkLine() {
    auto r = DocumentSession::loadFile(GET_TESTFILE(u8"benchmark/handwritten-text.xopp"));
    std::vector<InkStroke> all;
    {
        std::shared_lock lock(*r.document);
        all = strokesOf(*r.document->getPage(0));
    }
    std::vector<InkStroke> band;
    for (const InkStroke& s: all) {
        double y = 0;
        for (const QPointF& p: s.points) {
            y += p.y();
        }
        if (y / static_cast<double>(s.points.size()) >= 550 && y / static_cast<double>(s.points.size()) <= 575) {
            band.push_back(s);
        }
    }
    const hwr::Layout l = layout(band);
    return LineInput::of(band, l, l.lines.at(0));
}
}  // namespace

TEST(RecognizerTest, aLineIsGivenRelativeToItsOrigin) {
    auto [strokes, l] = lineOf(3);
    ASSERT_EQ(l.lines.size(), 1u);
    const LineInput in = LineInput::of(strokes, l, l.lines[0]);
    EXPECT_EQ(in.hash, l.lines[0].hash);
    ASSERT_EQ(in.strokes.size(), 3u);
    EXPECT_EQ(in.strokes[0].points[0], QPointF(0, 0));
    EXPECT_EQ(in.strokes[2].box.left(), 100.0);
    ASSERT_EQ(in.words.size(), 3u);
    EXPECT_EQ(in.words[1].box.left(), 50.0);
    EXPECT_EQ(in.words[1].strokes, (std::vector<uint32_t>{1}));
    EXPECT_DOUBLE_EQ(in.size.height(), 10.0);
}

TEST(RecognizerTest, theFakeReadsWhatItIsTold) {
    auto [strokes, l] = lineOf(2);
    const LineInput in = LineInput::of(strokes, l, l.lines[0]);
    FakeRecognizer fake;
    auto r = fake.recognizeLine(in, {});
    ASSERT_TRUE(r);
    ASSERT_EQ(r->words.size(), 2u);
    EXPECT_EQ(r->words[1].text, QStringLiteral("w1"));
    EXPECT_EQ(r->words[1].box, in.words[1].box);
    fake.setLine(in.hash, {{{QStringLiteral("Kalman"), 0.7f}, {QStringLiteral("Kalmar"), 0.3f}}, {}});
    r = fake.recognizeLine(in, {});
    ASSERT_EQ(r->words.size(), 1u);  // (nothing for the second box)
    EXPECT_EQ(r->words[0].candidates.size(), 2u);
    EXPECT_EQ(fake.calls(), 2);
    // Not ready: says why
    fake.setReady(false, QStringLiteral("no model"));
    QString why;
    EXPECT_FALSE(fake.ready(&why));
    EXPECT_EQ(why, QStringLiteral("no model"));
    // Slow, then cancelled
    fake.setDelay(2000);
    std::atomic<bool> stop{false};
    Context context;
    context.cancelled = [&] { return stop.load(); };
    std::thread later([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        stop = true;
    });
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(fake.recognizeLine(in, context));
    later.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(1000));
    EXPECT_EQ(fake.calls(), 2);
}

TEST(RecognizerTest, longLinesAreCutIntoPieces) {
    auto [strokes, l] = lineOf(20);
    ASSERT_EQ(l.lines[0].words.size(), 20u);
    const LineInput in = LineInput::of(strokes, l, l.lines[0]);
    const auto pieces = piecesOf(in);
    ASSERT_EQ(pieces.size(), 3u);
    EXPECT_EQ(pieces[0].first, 0u);
    EXPECT_EQ(pieces[0].last, 5u);  // 6, 7, 7 words
    EXPECT_EQ(pieces[1].first, 6u);
    EXPECT_EQ(pieces[2].last, 19u);
    EXPECT_DOUBLE_EQ(pieces[1].box.left(), 300.0);
    EXPECT_EQ(piecesOf(LineInput::of(lineOf(8).first, lineOf(8).second, lineOf(8).second.lines[0])).size(), 1u);
}

TEST(RecognizerTest, theLineIsDrawnBlackOnWhite) {
    const LineInput in = benchmarkLine();
    ASSERT_EQ(in.words.size(), 16u);
    const auto pieces = piecesOf(in);
    ASSERT_EQ(pieces.size(), 2u);
    int w = 0, h = 0;
    const auto grey = greyOf(in, pieces[0], w, h);
    EXPECT_EQ(h, LINE_PX);
    // The piece is about 4 times as wide as high (8 words of the line)
    EXPECT_GT(w, 3 * h);
    EXPECT_LT(w, 15 * h);
    const auto dark = std::count_if(grey.begin(), grey.end(), [](unsigned char v) { return v < 128; });
    EXPECT_GT(dark, static_cast<long>(grey.size() / 50));  // ink
    EXPECT_LT(dark, static_cast<long>(grey.size() / 3));   // mostly paper
    // The margin is paper
    for (int y = 0; y < h; ++y) {
        EXPECT_EQ(grey[static_cast<size_t>(y * w)], 255) << y;
    }
    // The model's input: 3 equal channels in [-1, 1], mostly white
    const auto pixels = pixelsOf(in, pieces[0]);
    ASSERT_EQ(pixels.size(), 3u * MODEL_PX * MODEL_PX);
    const size_t plane = MODEL_PX * MODEL_PX;
    EXPECT_FLOAT_EQ(pixels[1000], pixels[plane + 1000]);
    EXPECT_FLOAT_EQ(pixels[1000], pixels[2 * plane + 1000]);
    EXPECT_FLOAT_EQ(*std::max_element(pixels.begin(), pixels.end()), 1.0f);
    EXPECT_LT(*std::min_element(pixels.begin(), pixels.end()), -0.5f);
    const auto white = std::count_if(pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(plane),
                                     [](float v) { return v > 0.9f; });
    EXPECT_GT(white, static_cast<long>(plane / 2));
}

TEST(RecognizerTest, readingsArePutOnTheWordBoxes) {
    const std::vector<QRectF> boxes{{0, 0, 40, 10}, {50, 0, 20, 10}, {80, 0, 60, 10}};
    EXPECT_EQ(align(QStringLiteral("This is dumb"), boxes),
              (std::vector<QString>{QStringLiteral("This"), QStringLiteral("is"), QStringLiteral("dumb")}));
    // Fewer words than boxes: by their share of the letters
    EXPECT_EQ(align(QStringLiteral("This dumbest"), boxes),
              (std::vector<QString>{QStringLiteral("This"), QString(), QStringLiteral("dumbest")}));
    // More: joined
    EXPECT_EQ(align(QStringLiteral("Th is is dumb"), boxes),
              (std::vector<QString>{QStringLiteral("Th is"), QStringLiteral("is"), QStringLiteral("dumb")}));
}

TEST(RecognizerTest, beamsBecomeReadingsWithShares) {
    const std::vector<QRectF> boxes{{0, 0, 60, 10}, {70, 0, 60, 10}};
    const std::vector<Beam> beams{{QStringLiteral("Kalman filter."), -1.0, -0.1},
                                  {QStringLiteral("Kalmar filter"), -2.0, -0.3},
                                  {QStringLiteral("Kalman fitter"), -3.0, -0.4}};
    const auto words = wordsOf(beams, boxes);
    ASSERT_EQ(words.size(), 2u);
    const double z = 1 + std::exp(-1.0) + std::exp(-2.0);
    EXPECT_EQ(words[0].text, QStringLiteral("Kalman"));
    ASSERT_EQ(words[0].candidates.size(), 2u);
    EXPECT_NEAR(words[0].candidates[0].p, (1 + std::exp(-2.0)) / z, 1e-6);
    EXPECT_EQ(words::textOf(words[0].candidates[1].word), QStringLiteral("kalmar"));
    // "filter." and "filter" are one reading (the same letters), as the likeliest beam wrote it
    EXPECT_EQ(words[1].text, QStringLiteral("filter."));
    EXPECT_NEAR(words[1].candidates[0].p, (1 + std::exp(-1.0)) / z, 1e-6);
    EXPECT_NEAR(words[0].conf, std::exp(-0.1), 1e-6);
    // At most topK readings
    EXPECT_EQ(wordsOf(beams, boxes, 1)[0].candidates.size(), 1u);
}

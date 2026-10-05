/*
 * xournal-qt: the CTC recogniser (qt/research/hwr/train/FORMATS.md, "kind": "ctc"): the prefix beam search on scripted
 * frames, the alphabet, the manifest, the picture it gets; the whole path through ONNX Runtime with a tiny generated
 * model (tests/hwr/data/tinyctc.py; only with XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>).
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/CtcDecode.h"
#include "hwr/CtcRecognizer.h"
#include "hwr/LineImage.h"
#include "hwr/ModelInfo.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
/// Frames of probabilities (each row sums to 1) as log-probabilities, row by row.
std::vector<float> logsOf(const std::vector<std::vector<double>>& frames) {
    std::vector<float> out;
    for (const auto& f: frames) {
        for (const double p: f) {
            out.push_back(static_cast<float>(std::log(std::max(p, 1e-12))));
        }
    }
    return out;
}

/// A written word: a zigzag, `letters` wide, 10 pt high, at x.
InkStroke zigzag(double x, int letters = 4) {
    std::vector<QPointF> points;
    for (int i = 0; i <= 2 * letters; ++i) {
        points.emplace_back(x + i * 3.0, i % 2 ? 10.0 : 0.0);
    }
    return InkStroke::of(points, 1.0f);
}

/// A line of `n` written words, 36 pt apart.
LineInput lineOf(int n) {
    LineInput line;
    for (int i = 0; i < n; ++i) {
        line.strokes.push_back(zigzag(60.0 * i));
        line.words.push_back({QRectF(60.0 * i, 0, 24, 10), {static_cast<uint32_t>(i)}});
    }
    line.size = QSizeF(60.0 * n - 36, 10);
    return line;
}

QString copyModel(const QTemporaryDir& dir, const QByteArray& manifest = {}) {
    const QString from = QStringLiteral(XQT_HWR_TEST_DATA "/tiny-ctc/");
    for (const char* f: {"model.onnx", "alphabet.txt", "model.json"}) {
        QFile::copy(from + QLatin1String(f), dir.filePath(QLatin1String(f)));
    }
    if (!manifest.isEmpty()) {
        QFile::remove(dir.filePath(QStringLiteral("model.json")));
        QFile m(dir.filePath(QStringLiteral("model.json")));
        m.open(QIODevice::WriteOnly);
        m.write(manifest);
    }
    return dir.path();
}
}  // namespace

// Paths that read the same text count together: "a" is read by "aa", "a-" and "-a" (0.84), more likely than the empty
// reading (0.16), though the likeliest single path is "aa"
TEST(CtcTest, theBeamSearchAddsThePathsOfOneText) {
    const auto r = ctcBeamSearch(logsOf({{0.4, 0.6}, {0.4, 0.6}}), 2, 2, 0, 8, 5);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0].labels, std::vector<int>{1});
    EXPECT_NEAR(std::exp(r[0].logProb), 0.84, 1e-6);
    EXPECT_TRUE(r[1].labels.empty());
    EXPECT_NEAR(std::exp(r[1].logProb), 0.16, 1e-6);
}

// A letter twice needs a blank between ("a-a" is "aa", "aaa" is "a")
TEST(CtcTest, aRepeatedLetterNeedsABlankBetween) {
    const auto twice = ctcBeamSearch(logsOf({{0.05, 0.95}, {0.95, 0.05}, {0.05, 0.95}}), 3, 2);
    ASSERT_FALSE(twice.empty());
    EXPECT_EQ(twice[0].labels, (std::vector<int>{1, 1}));
    const auto once = ctcBeamSearch(logsOf({{0.05, 0.95}, {0.05, 0.95}, {0.05, 0.95}}), 3, 2);
    ASSERT_FALSE(once.empty());
    EXPECT_EQ(once[0].labels, std::vector<int>{1});
}

// The top k readings, best first, with their probabilities; a blank that is not class 0
TEST(CtcTest, theBeamSearchGivesTheTopReadings) {
    // classes: a, b, blank (blank last)
    const auto r = ctcBeamSearch(logsOf({{0.5, 0.3, 0.2}, {0.1, 0.1, 0.8}, {0.1, 0.6, 0.3}}), 3, 3, 2, 8, 3);
    ASSERT_EQ(r.size(), 3u);
    EXPECT_EQ(r[0].labels, (std::vector<int>{0, 1}));  // "ab"
    EXPECT_GT(r[0].logProb, r[1].logProb);
    EXPECT_GT(r[1].logProb, r[2].logProb);
    double sum = 0;
    for (const auto& c: ctcBeamSearch(logsOf({{0.5, 0.3, 0.2}, {0.1, 0.1, 0.8}, {0.1, 0.6, 0.3}}), 3, 3, 2, 64, 64)) {
        sum += std::exp(c.logProb);
    }
    EXPECT_NEAR(sum, 1.0, 1e-6) << "every path is counted once";
    // Scores that are not log-probabilities are normalised per frame
    const auto scores = ctcBeamSearch({3.0f, 1.0f, 1.0f, 3.0f}, 2, 2, 0, 8, 2);
    ASSERT_FALSE(scores.empty());
    EXPECT_TRUE(scores[0].labels.empty() || scores[0].labels == std::vector<int>{1});
    EXPECT_LE(scores[0].logProb, 0.0);
    EXPECT_TRUE(ctcBeamSearch({}, 0, 2).empty());
}

// How sure the model is: per character on the likeliest path (its best frame), not per frame
TEST(CtcTest, theConfidenceIsPerCharacter) {
    // "a" (best frame 0.9), a blank, "a" (0.8) again; blanks do not count
    const double c = ctcConfidence(logsOf({{0.1, 0.9}, {0.3, 0.7}, {0.9, 0.1}, {0.2, 0.8}, {0.95, 0.05}}), 5, 2);
    EXPECT_NEAR(c, (std::log(0.9) + std::log(0.8)) / 2, 1e-6);
    // Many sure frames of one letter: as sure as one
    EXPECT_NEAR(ctcConfidence(logsOf(std::vector<std::vector<double>>(40, {0.01, 0.99})), 40, 2), std::log(0.99), 1e-6);
    // Nothing read: not sure at all
    EXPECT_LT(ctcConfidence(logsOf({{0.9, 0.1}}), 1, 2), -10);
}

TEST(CtcTest, theAlphabetNamesTheClasses) {
    CtcAlphabet a;
    ASSERT_TRUE(a.parse(QByteArray(" \na\nb\nu\xcc\x88\n")));  // (a combining diaeresis: NFC makes it one letter)
    EXPECT_EQ(a.size(), 4u);
    EXPECT_EQ(a.text({2, 1, 3, 4}), QStringLiteral("a bü"));
    EXPECT_EQ(a.text({0, 2, 0}), QStringLiteral("a"));
    // The blank last: the classes are the lines in order
    EXPECT_EQ(a.text({1, 0, 4}, 4), QStringLiteral("a "));
    EXPECT_FALSE(a.parse(QByteArray()));
}

TEST(CtcTest, theManifestNamesTheModel) {
    QTemporaryDir dir;
    const CtcManifest m = CtcManifest::read(copyModel(dir));
    ASSERT_TRUE(m.valid()) << m.error.toStdString();
    EXPECT_EQ(m.name, QStringLiteral("tiny-ctc"));
    EXPECT_EQ(m.languages, QStringList{QStringLiteral("de")});
    EXPECT_EQ(m.inputHeight, 16);
    EXPECT_EQ(m.model, QStringLiteral("model.onnx"));
    EXPECT_EQ(m.id, ModelInfo::read(dir.path()).id() + QStringLiteral("/ctc1"));
    CtcRecognizer rec(dir.path());
    EXPECT_EQ(rec.capabilities().id, m.id);
    EXPECT_EQ(rec.capabilities().languages, QStringList{QStringLiteral("de")});
    // Not a CTC model, or an incomplete one
    QTemporaryDir other;
    EXPECT_FALSE(CtcManifest::read(copyModel(other, R"({"kind": "trocr", "name": "x", "languages": ["en"]})")).valid());
    QTemporaryDir incomplete;
    EXPECT_FALSE(CtcManifest::read(copyModel(incomplete, R"({"kind": "ctc", "name": "x", "languages": ["de"]})")).valid());
    // A file that is not there: not ready, and it says which
    QTemporaryDir missing;
    copyModel(missing);
    QFile::remove(missing.filePath(QStringLiteral("alphabet.txt")));
    QString why;
    EXPECT_FALSE(CtcRecognizer(missing.path()).ready(&why));
    EXPECT_TRUE(why.contains(QStringLiteral("alphabet.txt"))) << why.toStdString();
}

// The picture a CTC model gets: the one TrOCR's comes from, scaled to the model's height, ink 1 on paper 0
TEST(CtcTest, theLineIsScaledToTheModelsHeight) {
    const LineInput line = lineOf(2);
    const auto pieces = piecesOf(line, 8);
    ASSERT_EQ(pieces.size(), 1u);
    int gw = 0, gh = 0;
    greyOf(line, pieces[0], gw, gh);
    int width = 0;
    const std::vector<float> ink = inkOf(line, pieces[0], 16, 2048, width);
    EXPECT_EQ(width, static_cast<int>(std::lround(gw * 16.0 / gh)));
    EXPECT_NEAR(widthAt(pieces[0], 16), width, 1.0);
    ASSERT_EQ(ink.size(), static_cast<size_t>(16 * width));
    EXPECT_EQ(*std::min_element(ink.begin(), ink.end()), 0.0f);
    EXPECT_GT(*std::max_element(ink.begin(), ink.end()), 0.5f);
    // The gap between the words is paper; the first word ink
    auto column = [&](int x) {
        double sum = 0;
        for (int y = 0; y < 16; ++y) {
            sum += ink[static_cast<size_t>(y * width + x)];
        }
        return sum;
    };
    EXPECT_EQ(column(width / 2), 0.0);
    EXPECT_GT(column(width / 5), 0.5);
    // Too wide: squeezed to the most the model takes
    inkOf(line, pieces[0], 16, 20, width);
    EXPECT_EQ(width, 20);
}

// The whole path through ONNX Runtime: the picture, the model's frames, the beam search, words at spaces, the word
// boxes; a line too wide for the model is read in pieces
TEST(CtcTest, aTinyModelIsReadThroughOnnxRuntime) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    QTemporaryDir dir;
    CtcRecognizer rec(copyModel(dir));
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    const LineInput line = lineOf(3);
    const auto result = rec.recognizeLine(line, {});
    ASSERT_TRUE(result);
    EXPECT_TRUE(rec.loaded());
    ASSERT_EQ(result->words.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(result->words[i].text, QStringLiteral("a"));
        EXPECT_EQ(result->words[i].box, line.words[i].box);
        ASSERT_FALSE(result->words[i].candidates.empty());
        EXPECT_EQ(words::textOf(result->words[i].candidates[0].word), QStringLiteral("a"));
        EXPECT_GT(result->words[i].conf, 0.5f);
    }
    rec.unload();
    EXPECT_FALSE(rec.loaded());
    // At most 40 px wide at 16 px high: one word per piece
    QTemporaryDir narrow;
    QFile in(QStringLiteral(XQT_HWR_TEST_DATA "/tiny-ctc/model.json"));
    ASSERT_TRUE(in.open(QIODevice::ReadOnly));
    const QByteArray manifest = in.readAll().replace("\"max_width\": 2048", "\"max_width\": 40");
    CtcRecognizer pieces(copyModel(narrow, manifest));
    ASSERT_TRUE(pieces.ready(&why)) << why.toStdString();
    const auto read = pieces.recognizeLine(line, {});
    ASSERT_TRUE(read);
    ASSERT_EQ(read->words.size(), 3u);
    EXPECT_EQ(read->words[2].text, QStringLiteral("a"));
}

// A long line (more than 16 times as wide as high: a whole line of a page, as a CTC model and the dataset export take
// it) is drawn whole, not cut off at the right
TEST(CtcTest, aLongLineIsDrawnWhole) {
    const LineInput line = lineOf(12);  // (696 x 10 pt: about 5900 px at 128 px high)
    const auto pieces = piecesOf(line, 100);
    ASSERT_EQ(pieces.size(), 1u);
    int w = 0, h = 0;
    const std::vector<unsigned char> grey = greyOf(line, pieces[0], w, h);
    EXPECT_NEAR(w, widthAt(pieces[0], LINE_PX), 1.0);
    // Ink in the last word (its middle column)
    const double scale = LINE_PX / (10.0 + 2 * 2.5);
    const int x = static_cast<int>((2.5 + 60.0 * 11 + 12) * scale);
    ASSERT_LT(x, w);
    int ink = 0;
    for (int y = 0; y < h; ++y) {
        ink += grey[static_cast<size_t>(y * w + x)] < 128 ? 1 : 0;
    }
    EXPECT_GT(ink, 0);
}

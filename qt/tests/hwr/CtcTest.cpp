/*
 * xournal-qt: the CTC recogniser (qt/research/hwr/train/FORMATS.md, "kind": "ctc"): the prefix beam search on scripted
 * frames, the alphabet, the manifest, the picture it gets; the whole path through ONNX Runtime with a tiny generated
 * model (tests/hwr/data/tinyctc.py; only with XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>). With a real model
 * (XQT_HWR_CTC_MODEL) and XQT_HWR_BOXES_OUT=<folder>: the word boxes of the handwriting in test/files by both methods
 * (letter shares, frames), where they differ drawn into that folder for a look (wordBoxesOfRealInk).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <shared_mutex>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextStream>
#include <gtest/gtest.h>

#include "hwr/CtcDecode.h"
#include "hwr/CtcRecognizer.h"
#include "hwr/LineImage.h"
#include "hwr/ModelInfo.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "config-test.h"

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

// Where the characters were read: per character its first and last frame on the likeliest path ("-aa--b-a" reads
// "aba": a on 1-2, b on 5, a on 7)
TEST(CtcTest, theBeamSearchSaysWhereEachCharacterWasRead) {
    // classes: blank, a, b
    const std::vector<std::vector<double>> frames{{0.9, 0.05, 0.05}, {0.05, 0.9, 0.05}, {0.05, 0.9, 0.05},
                                                  {0.9, 0.05, 0.05}, {0.9, 0.05, 0.05}, {0.05, 0.05, 0.9},
                                                  {0.9, 0.05, 0.05}, {0.05, 0.9, 0.05}};
    const auto r = ctcBeamSearch(logsOf(frames), frames.size(), 3);
    ASSERT_FALSE(r.empty());
    ASSERT_EQ(r[0].labels, (std::vector<int>{1, 2, 1}));
    EXPECT_EQ(r[0].spans, (std::vector<CtcSpan>{{1, 2}, {5, 5}, {7, 7}}));
    // Every reading has a span per character, in order and within the frames
    for (const auto& reading: ctcBeamSearch(logsOf(frames), frames.size(), 3, 0, 8, 5)) {
        ASSERT_EQ(reading.spans.size(), reading.labels.size());
        int at = 0;
        for (const CtcSpan& s: reading.spans) {
            EXPECT_LE(at, s.first);
            EXPECT_LE(s.first, s.last);
            EXPECT_LT(s.last, static_cast<int>(frames.size()));
            at = s.last + 1;
        }
    }
    // A letter twice: two characters, each with its own frames ("a-a"); held over frames: one ("aaa")
    const auto twice = ctcBeamSearch(logsOf({{0.05, 0.95}, {0.95, 0.05}, {0.05, 0.95}}), 3, 2);
    ASSERT_EQ(twice[0].labels, (std::vector<int>{1, 1}));
    EXPECT_EQ(twice[0].spans, (std::vector<CtcSpan>{{0, 0}, {2, 2}}));
    const auto once = ctcBeamSearch(logsOf({{0.05, 0.95}, {0.05, 0.95}, {0.05, 0.95}}), 3, 2);
    ASSERT_EQ(once[0].labels, std::vector<int>{1});
    EXPECT_EQ(once[0].spans, (std::vector<CtcSpan>{{0, 2}}));
}

// A reading's words and where they are in the picture: from the first frame of a word's first character to the last
// of its last, as fractions of the width; spaces split words (several, or at the ends, as simplified() does)
TEST(CtcTest, theWordsOfAReadingHaveTheirPlaceInThePicture) {
    CtcAlphabet a;
    ASSERT_TRUE(a.parse(QByteArray(" \na\nb\n")));  // classes: blank, " ", a, b
    CtcReading r;
    r.labels = {1, 2, 3, 1, 1, 3, 1};  // " ab  b "
    r.spans = {{0, 0}, {1, 2}, {4, 5}, {7, 7}, {9, 9}, {12, 15}, {18, 19}};
    const auto spans = wordSpansOf(r, 20, a);
    EXPECT_EQ(a.text(r.labels).simplified(), QStringLiteral("ab b"));
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_DOUBLE_EQ(spans[0].left, 1 / 20.0);
    EXPECT_DOUBLE_EQ(spans[0].right, 6 / 20.0);
    EXPECT_DOUBLE_EQ(spans[1].left, 12 / 20.0);
    EXPECT_DOUBLE_EQ(spans[1].right, 16 / 20.0);
    // Without spans: none (the letters' shares then)
    r.spans.clear();
    EXPECT_TRUE(wordSpansOf(r, 20, a).empty());
}

// A place in the picture is a place on the line: through the picture's margin and scale
TEST(CtcTest, aPlaceInThePictureIsAPlaceOnTheLine) {
    const LineInput line = lineOf(3);  // words at 0, 60, 120, 24 wide; 10 high, a margin of 2.5
    const auto pieces = piecesOf(line, 8);
    ASSERT_EQ(pieces.size(), 1u);
    int w = 0, h = 0;
    greyOf(line, pieces[0], w, h);
    const double scale = LINE_PX / 15.0;
    EXPECT_DOUBLE_EQ(xAt(pieces[0], 0), -2.5);
    EXPECT_NEAR(xAt(pieces[0], 1), w / scale - 2.5, 1e-9);
    EXPECT_NEAR(xAt(pieces[0], 1), 144 + 2.5, 1.0 / scale);
    // The ink of the second word, found in the model's input at any width, is on its box
    int width = 0;
    const std::vector<float> ink = inkOf(line, pieces[0], 16, 2048, width);
    int first = -1, last = -1;
    for (int x = 0; x < width; ++x) {
        double sum = 0;
        for (int y = 0; y < 16; ++y) {
            sum += ink[static_cast<size_t>(y * width + x)];
        }
        const double at = xAt(pieces[0], (x + 0.5) / width);
        if (sum > 0.5 && at > 40 && at < 100) {
            first = first < 0 ? x : first;
            last = x;
        }
    }
    ASSERT_GE(first, 0);
    EXPECT_NEAR(xAt(pieces[0], static_cast<double>(first) / width), 60, 2.0);
    EXPECT_NEAR(xAt(pieces[0], static_cast<double>(last + 1) / width), 84, 2.0);
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
    EXPECT_EQ(m.id, ModelInfo::read(dir.path()).id() + QStringLiteral("/ctc2"));
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

// The tiny model reads where it sees ink (a frame per column): its words go on the boxes they were read in, also
// when the layout has a box the model reads nothing in (here an empty one before the words; by the letters' shares the
// first two words would land one box too early)
TEST(CtcTest, theWordsOfATinyModelGoWhereTheyWereRead) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    QTemporaryDir dir;
    CtcRecognizer rec(copyModel(dir));
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    LineInput line = lineOf(3);
    line.words.insert(line.words.begin(), InkWordBox{QRectF(-48, 0, 36, 10), {}});
    std::vector<QRectF> boxes;
    for (const auto& w: line.words) {
        boxes.push_back(w.box);
    }
    EXPECT_EQ(align(QStringLiteral("a a a"), boxes),
              (std::vector<QString>{QStringLiteral("a"), QStringLiteral("a"), QString(), QStringLiteral("a")}));
    const auto result = rec.recognizeLine(line, {});
    ASSERT_TRUE(result);
    ASSERT_EQ(result->words.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(result->words[i].text, QStringLiteral("a"));
        EXPECT_EQ(result->words[i].box, line.words[i + 1].box);
    }
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

// A model folder exported by the training (qt/research/hwr/train: export.py ... --kind ctc) is read by the app: its
// manifest and files are accepted, it runs, and its words land on the ink's word boxes
TEST(CtcTest, anExportedModelIsRead) {
    const QString dir = qEnvironmentVariable("XQT_HWR_CTC_MODEL");
    if (dir.isEmpty() || qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_HWR_CTC_MODEL to an exported CTC model's folder and XQT_ONNXRUNTIME";
    }
    const ModelInfo info = ModelInfo::read(dir);
    ASSERT_TRUE(info.valid());
    CtcRecognizer rec(dir);
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    const LineInput line = lineOf(3);
    const auto result = rec.recognizeLine(line, {});
    ASSERT_TRUE(result);
    for (const auto& w: result->words) {
        EXPECT_TRUE(std::any_of(line.words.begin(), line.words.end(), [&](const auto& in) { return in.box == w.box; }));
    }
}

// Not a test: a measurement (XQT_HWR_BOXES_OUT=<folder>, XQT_HWR_CTC_MODEL, XQT_ONNXRUNTIME; more documents or folders
// in XQT_HWR_BOXES_FILES, separated by ':'). Reads every line of the handwriting in test/files with the model and puts
// each reading's words on the word boxes by the letters' shares and by the frames. Where the two differ (only where
// the reading has another number of words than the line has boxes), <folder>/NNN.png shows the line: the boxes, the
// letter shares' words above (red), the frames' below (blue) with where each word was read; <folder>/lines.tsv lists
// them (beam 0: the best reading).
TEST(CtcTest, wordBoxesOfRealInk) {
    const QString model = qEnvironmentVariable("XQT_HWR_CTC_MODEL");
    const QString outDir = qEnvironmentVariable("XQT_HWR_BOXES_OUT");
    if (model.isEmpty() || outDir.isEmpty() || qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_HWR_BOXES_OUT, XQT_HWR_CTC_MODEL and XQT_ONNXRUNTIME";
    }
    QDir().mkpath(outDir);
    const CtcManifest manifest = CtcManifest::read(model);
    CtcRecognizer rec(model);
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    ASSERT_TRUE(rec.recognizeLine(lineOf(1), {}));  // (loads the model)
    QFile tsv(QDir(outDir).filePath(QStringLiteral("lines.tsv")));
    ASSERT_TRUE(tsv.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QTextStream out(&tsv);
    out << "n\tfile\tpage\tline\tpiece\tbeam\tboxes\twords\treading\tletters\tframes\n";
    int lines = 0, pieces = 0, otherCount = 0, otherBeams = 0, differ = 0, differBest = 0;
    const std::filesystem::path root(GET_TESTFILE(u8""));
    std::vector<std::filesystem::path> files;
    QStringList places{QString::fromStdString(root.string())};
    places += qEnvironmentVariable("XQT_HWR_BOXES_FILES").split(u':', Qt::SkipEmptyParts);
    for (const QString& place: places) {
        const std::filesystem::path at(place.toStdString());
        if (std::filesystem::is_regular_file(at)) {
            files.push_back(at);
            continue;
        }
        for (const auto& e: std::filesystem::recursive_directory_iterator(at)) {
            const auto ext = e.path().extension();
            if (e.is_regular_file() && (ext == ".xopp" || ext == ".xoj")) {
                files.push_back(e.path());
            }
        }
    }
    std::sort(files.begin(), files.end());
    for (const auto& file: files) {
        auto loaded = DocumentSession::loadFile(file);
        if (!loaded.document) {
            continue;
        }
        std::vector<std::vector<InkStroke>> pages;
        {
            std::shared_lock lock(*loaded.document);
            for (size_t p = 0; p < loaded.document->getPageCount(); ++p) {
                pages.push_back(strokesOf(*loaded.document->getPage(p)));
            }
        }
        const QString name = QString::fromStdString(file.lexically_relative(root).string());
        int fileLines = 0;
        for (size_t p = 0; p < pages.size(); ++p) {
            const hwr::Layout l = hwr::layout(pages[p]);
            for (size_t li = 0; li < l.lines.size(); ++li) {
                const LineInput line = LineInput::of(pages[p], l, l.lines[li]);
                ++lines;
                ++fileLines;
                // The pieces as CtcRecognizer cuts them
                std::vector<LinePiece> parts;
                const size_t n = line.words.size();
                for (size_t count = 1; count <= std::max<size_t>(1, n); ++count) {
                    parts = piecesOf(line, static_cast<int>((n + count - 1) / count));
                    if (std::all_of(parts.begin(), parts.end(), [&](const LinePiece& x) {
                            return widthAt(x, manifest.inputHeight) <= manifest.maxWidth;
                        })) {
                        break;
                    }
                }
                for (size_t k = 0; k < parts.size(); ++k) {
                    const LinePiece& piece = parts[k];
                    ++pieces;
                    int width = 0;
                    const auto ink = inkOf(line, piece, manifest.inputHeight, manifest.maxWidth, width);
                    const auto beams = rec.readPicture(ink, width, {});
                    ASSERT_TRUE(beams);
                    if (beams->empty()) {
                        continue;
                    }
                    std::vector<QRectF> boxes;
                    for (size_t i = piece.first; i <= piece.last; ++i) {
                        boxes.push_back(line.words[i].box);
                    }
                    for (size_t bi = 0; bi < beams->size(); ++bi) {
                        const Beam& best = (*beams)[bi];
                        const auto count = best.text.split(u' ', Qt::SkipEmptyParts).size();
                        if (static_cast<size_t>(count) == boxes.size()) {
                            continue;
                        }
                        if (bi == 0) {
                            ++otherCount;
                        } else {
                            ++otherBeams;
                        }
                        std::vector<WordSpan> spans = best.spans;
                        for (WordSpan& sp: spans) {
                            sp = {xAt(piece, sp.left), xAt(piece, sp.right)};
                        }
                        const auto byLetters = align(best.text, boxes);
                        const auto byFrames = align(best.text, boxes, spans);
                        if (byLetters == byFrames) {
                            continue;
                        }
                        ++differ;
                        if (bi == 0) {
                            ++differBest;
                        }
                        // The picture: the line 64 px high, bands for the words above and below
                        int gw = 0, gh = 0;
                        const auto grey = greyOf(line, piece, gw, gh);
                        QImage pic(gw, gh, QImage::Format_Grayscale8);
                        for (int y = 0; y < gh; ++y) {
                            std::copy_n(grey.data() + static_cast<size_t>(y) * static_cast<size_t>(gw), gw,
                                        pic.scanLine(y));
                        }
                        const double f = 0.5;
                        const int band = 40, iw = std::max(1, static_cast<int>(gw * f)), ih = static_cast<int>(gh * f);
                        QImage img(iw, ih + 2 * band, QImage::Format_RGB32);
                        img.fill(Qt::white);
                        QPainter pt(&img);
                        pt.setRenderHint(QPainter::SmoothPixmapTransform);
                        pt.drawImage(QRect(0, band, iw, ih), pic);
                        const double x0 = xAt(piece, 0), x1 = xAt(piece, 1);
                        auto px = [&](double x) { return (x - x0) / (x1 - x0) * iw; };
                        const QColor colors[] = {QColor(0, 160, 0), QColor(200, 120, 0), QColor(140, 0, 200),
                                                 QColor(0, 150, 160)};
                        QFont font = pt.font();
                        font.setPixelSize(13);
                        pt.setFont(font);
                        const double top = piece.box.top() - std::max(2.0, 0.25 * piece.box.height());  // (the margin)
                        auto py = [&](double y) { return band + (y - top) / (x1 - x0) * iw; };
                        for (size_t i = 0; i < boxes.size(); ++i) {
                            pt.setPen(QPen(colors[i % 4], 1.5));
                            pt.drawRect(QRectF(QPointF(px(boxes[i].left()), py(boxes[i].top())),
                                               QPointF(px(boxes[i].right()), py(boxes[i].bottom()))));
                            pt.setPen(Qt::red);
                            pt.drawText(QPointF(px(boxes[i].left()), band - 6 - 14 * static_cast<int>(i % 2)),
                                        byLetters[i]);
                            pt.setPen(Qt::blue);
                            pt.drawText(QPointF(px(boxes[i].left()), ih + 2 * band - 20 + 14 * static_cast<int>(i % 2)),
                                        byFrames[i]);
                        }
                        pt.setPen(QPen(Qt::blue, 3));
                        for (const WordSpan& sp: spans) {
                            pt.drawLine(QPointF(px(sp.left), band + ih + 4), QPointF(px(sp.right), band + ih + 4));
                        }
                        pt.end();
                        img.save(QDir(outDir).filePath(QStringLiteral("%1.png").arg(differ, 3, 10, QLatin1Char('0'))));
                        auto joined = [](const std::vector<QString>& v) {
                            QStringList l;
                            for (const QString& x: v) {
                                l << (x.isEmpty() ? QStringLiteral("_") : x);
                            }
                            return l.join(QStringLiteral(" | "));
                        };
                        out << differ << '\t' << name << '\t' << p << '\t' << li << '\t' << k << '\t' << bi << '\t'
                            << boxes.size() << '\t' << count << '\t' << best.text << '\t' << joined(byLetters) << '\t'
                            << joined(byFrames) << '\n';
                    }
                }
            }
        }
        if (fileLines > 0) {
            std::cout << name.toStdString() << ": " << fileLines << " lines" << std::endl;
        }
    }
    std::cout << lines << " lines, " << pieces << " pieces; the best reading has another number of words than boxes in "
              << otherCount << " (the methods differ in " << differBest << "); the other readings in " << otherBeams
              << " (all readings: the methods differ in " << differ << ")" << std::endl;
}

/*
 * xournal-qt: the TrOCR recogniser: beam search with a scripted decoder, tokens to text, the model's manifest; the
 * ONNX Runtime calls with a tiny model (only with XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>); the real model
 * (only with XQT_HWR_MODEL=<folder>, see qt/scripts/hwr-model.sh); its speed (XQT_BENCH_HWR=1 with the model).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <shared_mutex>
#include <thread>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/BeamSearch.h"
#include "hwr/InkLayout.h"
#include "hwr/LineImage.h"
#include "hwr/OrtRuntime.h"
#include "hwr/TrocrRecognizer.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

#include "config-test.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
constexpr int64_t START = 0, A = 1, B = 2, C = 3, END = 4;

/// A decoder over the tokens <s> a b c </s> that likes "a b </s>" best and "c </s>" next; it keeps its own copy of the
/// sequences, following `parents` (so it notices when they are wrong).
struct Script {
    std::vector<std::vector<int64_t>> seqs;
    int steps = 0;
    bool operator()(const std::vector<int64_t>& tokens, const std::vector<size_t>& parents,
                    std::vector<std::vector<float>>& logits) {
        if (parents.empty()) {
            seqs.assign(tokens.size(), {});
        } else {
            std::vector<std::vector<int64_t>> next;
            for (size_t r = 0; r < tokens.size(); ++r) {
                next.push_back(seqs.at(parents[r]));
                next.back().push_back(tokens[r]);
            }
            seqs = std::move(next);
        }
        ++steps;
        logits.assign(tokens.size(), std::vector<float>(5, -10.0f));
        for (size_t r = 0; r < tokens.size(); ++r) {
            const auto& s = seqs[r];
            auto& l = logits[r];
            if (s.empty()) {
                l[A] = 2.0f;
                l[C] = 1.5f;
                l[B] = 0.0f;
            } else if (s == std::vector<int64_t>{A}) {
                l[B] = 3.0f;
                l[A] = 0.5f;
            } else if (s.size() >= 2 || s == std::vector<int64_t>{C}) {
                l[END] = 3.0f;
            } else {
                l[END] = 1.0f;
            }
        }
        return true;
    }
};

QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& data) {
    const QString path = dir.filePath(name);
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(data);
    return path;
}

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
        y /= static_cast<double>(s.points.size());
        if (y >= 550 && y <= 575) {
            band.push_back(s);
        }
    }
    const hwr::Layout l = layout(band);
    return LineInput::of(band, l, l.lines.at(0));
}
}  // namespace

TEST(TrocrTest, beamSearchKeepsTheBestSequences) {
    Script script;
    const auto results = beamSearch(std::ref(script), START, END, 3, 10);
    ASSERT_GE(results.size(), 2u);
    EXPECT_EQ(results[0].tokens, (std::vector<int64_t>{A, B, END}));
    EXPECT_EQ(results[1].tokens, (std::vector<int64_t>{C, END}));
    EXPECT_LE(results[0].logProb, 0.0);
    EXPECT_NEAR(results[0].meanLogProb, results[0].logProb / 3, 1e-9);
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_NE(results[i].tokens, results[0].tokens);  // (no duplicates)
    }
    EXPECT_LE(script.steps, 10);
    // A decoder that fails: no result
    EXPECT_TRUE(beamSearch([](auto&, auto&, auto&) { return false; }, START, END, 3, 10).empty());
    // One that never ends: cut at maxLength
    const auto endless = beamSearch(
            [](const std::vector<int64_t>& t, const std::vector<size_t>&, std::vector<std::vector<float>>& l) {
                l.assign(t.size(), {0.0f, 5.0f, 0.0f, 0.0f, -5.0f});
                return true;
            },
            START, END, 2, 6);
    ASSERT_FALSE(endless.empty());
    EXPECT_EQ(endless[0].tokens.size(), 6u);
}

TEST(TrocrTest, tokensBecomeText) {
    Tokenizer sp;  // SentencePiece (TrOCR-small): "▁" for a space
    ASSERT_TRUE(sp.parse(R"({"model": {"type": "Unigram", "vocab": [["<s>", 0], ["<pad>", 0], ["</s>", 0],
        ["▁This", -1], ["▁is", -1], ["▁dum", -2], ["b", -2], [",", -3]]},
        "decoder": {"type": "Metaspace"},
        "added_tokens": [{"id": 0, "content": "<s>", "special": true}, {"id": 2, "content": "</s>", "special": true}]})"));
    EXPECT_EQ(sp.decode({0, 3, 4, 5, 6, 7, 2}), QStringLiteral("This is dumb,"));
    Tokenizer bpe;  // byte-level BPE (TrOCR-base): "Ġ" for a space
    ASSERT_TRUE(bpe.parse(R"({"model": {"type": "BPE", "vocab": {"<s>": 0, "</s>": 2, "Th": 3, "is": 4, "Ġis": 5,
        "ĠK": 6, "Ã¤": 7}}, "decoder": {"type": "ByteLevel"},
        "added_tokens": [{"id": 0, "content": "<s>", "special": true}, {"id": 2, "content": "</s>", "special": true}]})"));
    EXPECT_EQ(bpe.decode({0, 3, 4, 5, 6, 7, 2}), QStringLiteral("This is Kä"));
    EXPECT_FALSE(Tokenizer().parse("{}"));
}

TEST(TrocrTest, theManifestNamesTheModel) {
    QTemporaryDir dir;
    EXPECT_FALSE(Manifest::read(dir.path()).valid());
    const QByteArray encoder("encoder bytes"), decoder("decoder bytes"), tokenizer("{}");
    auto sha = [](const QByteArray& b) {
        return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
    };
    writeFile(dir, QStringLiteral("onnx/encoder.onnx"), encoder);
    writeFile(dir, QStringLiteral("onnx/decoder.onnx"), decoder);
    writeFile(dir, QStringLiteral("tokenizer.json"), tokenizer);
    const QByteArray json = QStringLiteral(R"({"name": "trocr-small-hw-int8", "encoder": "onnx/encoder.onnx",
        "decoder": "onnx/decoder.onnx", "tokenizer": "tokenizer.json", "decoder_start_token_id": 2, "eos_token_id": 2,
        "image_size": 384, "files": {"onnx/encoder.onnx": {"sha256": "%1", "size": %2},
        "onnx/decoder.onnx": {"sha256": "%3", "size": 13}, "tokenizer.json": {"sha256": "%4", "size": 2}}})")
                                   .arg(sha(encoder))
                                   .arg(encoder.size())
                                   .arg(QString(64, u'0'))  // (not its sha256: a damaged file)
                                   .arg(sha(tokenizer))
                                   .toUtf8();
    writeFile(dir, QStringLiteral("model.json"), json);
    const Manifest m = Manifest::read(dir.path());
    ASSERT_TRUE(m.valid()) << m.error.toStdString();
    EXPECT_EQ(m.id, QStringLiteral("trocr-small-hw-int8/") + sha(json).left(12) + QStringLiteral("/seg1"));
    EXPECT_EQ(m.files.size(), 3u);
    TrocrRecognizer rec(dir.path());
    EXPECT_EQ(rec.capabilities().id, m.id);
    // A file of another size: not ready (cheap check)
    writeFile(dir, QStringLiteral("tokenizer.json"), "{ }");
    QString why;
    EXPECT_FALSE(rec.ready(&why));
    EXPECT_TRUE(why.contains(QStringLiteral("incomplete"))) << why.toStdString();
    writeFile(dir, QStringLiteral("tokenizer.json"), tokenizer);
    // A damaged file is found when the model is loaded (sha256), and it says so from then on
    LineInput line;
    line.words.push_back({QRectF(0, 0, 10, 10), {}});
    EXPECT_FALSE(rec.recognizeLine(line, {}));
    EXPECT_FALSE(rec.ready(&why));
    EXPECT_TRUE(why.contains(QStringLiteral("onnx/decoder.onnx"))) << why.toStdString();
    EXPECT_FALSE(rec.loaded());
}

TEST(TrocrTest, onnxRuntimeRunsATinyModel) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    QString why;
    ASSERT_TRUE(ort::api(&why)) << why.toStdString();
    auto s = ort::Session::open(QStringLiteral(XQT_HWR_TEST_DATA "/tiny.onnx"), 2, &why);
    ASSERT_TRUE(s) << why.toStdString();
    EXPECT_EQ(s->inputs, (std::vector<std::string>{"x", "ids", "flag"}));
    EXPECT_EQ(s->outputs, (std::vector<std::string>{"y", "z"}));
    ASSERT_EQ(s->inputShapes[0].size(), 2u);
    EXPECT_EQ(s->inputShapes[0][1], 3);
    EXPECT_EQ(s->inputShapes[0][0], -1);  // (symbolic)
    const auto x = ort::Tensor::floats({2, 3}, {1, 2, 3, 4, 5, 6});
    const auto ids = ort::Tensor::ints({2, 1}, {10, 20});
    const auto flag = ort::Tensor::bools({1}, {1});
    std::vector<ort::Tensor> out;
    ASSERT_TRUE(s->run({{"x", &x}, {"ids", &ids}, {"flag", &flag}}, {"y", "z"}, out, nullptr, &why)) << why.toStdString();
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].shape, (std::vector<int64_t>{2, 3}));
    EXPECT_EQ(out[0].f, (std::vector<float>{12, 14, 16, 28, 30, 32}));
    EXPECT_EQ(out[1].f, x.f);
    // An empty batch works (the first step of the decoder has empty caches)
    const auto none = ort::Tensor::floats({0, 3});
    const auto noIds = ort::Tensor::ints({0, 1}, {});
    ASSERT_TRUE(s->run({{"x", &none}, {"ids", &noIds}, {"flag", &flag}}, {"y"}, out, nullptr, &why)) << why.toStdString();
    EXPECT_EQ(out[0].shape, (std::vector<int64_t>{0, 3}));
    // Interrupted: fails
    ort::RunOptions options;
    options.terminate();
    EXPECT_FALSE(s->run({{"x", &x}, {"ids", &ids}, {"flag", &flag}}, {"y"}, out, options.get(), &why));
    options.reset();
    EXPECT_TRUE(s->run({{"x", &x}, {"ids", &ids}, {"flag", &flag}}, {"y"}, out, options.get(), &why));
}

TEST(TrocrTest, theRealModelReadsTheBenchmarkLine) {
    const QString dir = qEnvironmentVariable("XQT_HWR_MODEL");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "set XQT_HWR_MODEL to the model's folder (qt/scripts/hwr-model.sh)";
    }
    TrocrRecognizer rec(dir);
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    const LineInput line = benchmarkLine();
    const auto start = std::chrono::steady_clock::now();
    const auto result = rec.recognizeLine(line, {});
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    ASSERT_TRUE(result);
    EXPECT_GE(result->words.size(), 12u);
    // "This is a dumb test, written many times" (research: "This", "is", "a", "dumb", "test" at top-1)
    int found = 0;
    for (const char* expected: {"this", "dumb", "test"}) {
        const words::Id id = words::idOf(QString::fromLatin1(expected));
        for (const ink::Word& w: result->words) {
            if (std::any_of(w.candidates.begin(), w.candidates.end(), [&](const ink::Candidate& c) { return c.word == id; })) {
                ++found;
                break;
            }
        }
    }
    EXPECT_GE(found, 2);
    QStringList read;
    for (const ink::Word& w: result->words) {
        read << w.text;
    }
    std::cout << "[ trocr    ] " << read.join(u' ').toStdString() << " (" << ms.count() << " ms, first line, with loading)\n";
    if (!qEnvironmentVariableIsEmpty("XQT_BENCH_HWR")) {
        const int runs = 5;
        const auto t = std::chrono::steady_clock::now();
        for (int i = 0; i < runs; ++i) {
            ASSERT_TRUE(rec.recognizeLine(line, {}));
        }
        const auto each =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t).count() / runs;
        std::cout << "[ bench    ] " << each << " ms per line of " << line.words.size() << " words ("
                  << piecesOf(line).size() << " pieces, " << TrocrRecognizer::THREADS << " threads)\n";
    }
    // Interrupted from another thread: no result, soon
    std::thread stop([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rec.interrupt();
    });
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_FALSE(rec.recognizeLine(line, {}));
    stop.join();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(2000));
    rec.unload();
    EXPECT_FALSE(rec.loaded());
}

// The whole path through ONNX Runtime (encoder, merged decoder with its cache, beams, tokens, word boxes), with a
// stand-in model (tests/hwr/data/tinytrocr.py: <s> -> a b </s>, or c </s>)
TEST(TrocrTest, aStandInModelIsReadThroughOnnxRuntime) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    QTemporaryDir dir;
    const QString from = QStringLiteral(XQT_HWR_TEST_DATA "/tiny-trocr/");
    for (const char* f: {"onnx/encoder.onnx", "onnx/decoder.onnx", "tokenizer.json"}) {
        QFile in(from + QLatin1String(f));
        ASSERT_TRUE(in.open(QIODevice::ReadOnly)) << f;
        writeFile(dir, QLatin1String(f), in.readAll());
    }
    writeFile(dir, QStringLiteral("model.json"), R"({"name": "tiny", "encoder": "onnx/encoder.onnx",
        "decoder": "onnx/decoder.onnx", "tokenizer": "tokenizer.json", "decoder_start_token_id": 0, "eos_token_id": 4,
        "image_size": 384, "files": {}})");
    TrocrRecognizer rec(dir.path());
    QString why;
    ASSERT_TRUE(rec.ready(&why)) << why.toStdString();
    LineInput line;
    line.strokes.push_back(InkStroke::of({{0, 0}, {10, 10}}, 1.0f));
    line.strokes.push_back(InkStroke::of({{30, 0}, {40, 10}}, 1.0f));
    line.words = {{QRectF(0, 0, 10, 10), {0}}, {QRectF(30, 0, 10, 10), {1}}};
    line.size = QSizeF(40, 10);
    const auto result = rec.recognizeLine(line, {});
    ASSERT_TRUE(result);
    EXPECT_TRUE(rec.loaded());
    ASSERT_EQ(result->words.size(), 2u);
    EXPECT_EQ(result->words[0].text, QStringLiteral("a"));
    EXPECT_EQ(result->words[1].text, QStringLiteral("b"));
    EXPECT_EQ(result->words[1].box, QRectF(30, 0, 10, 10));
    // The other beam ("c") is a reading of the first box, less likely
    ASSERT_GE(result->words[0].candidates.size(), 2u);
    EXPECT_EQ(words::textOf(result->words[0].candidates[1].word), QStringLiteral("c"));
    EXPECT_GT(result->words[0].candidates[0].p, result->words[0].candidates[1].p);
    EXPECT_GT(result->words[0].conf, 0.5f);
    rec.unload();
    EXPECT_FALSE(rec.loaded());
}

/*
 * xournal-qt: the handwriting model that comes with the app (qt/docs/features/handwriting-search.md, "The built-in
 * model"): where ONNX Runtime is looked for on each platform, the models the build puts into the resource dir,
 * `xournal-qt --hwr-info`'s report, and (with XQT_ONNXRUNTIME=<path of libonnxruntime.so.1>) the built-in model
 * reading real handwriting.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <filesystem>
#include <shared_mutex>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/CtcRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/HwrInfo.h"
#include "hwr/InkLayout.h"
#include "hwr/ModelInfo.h"
#include "hwr/OrtRuntime.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/Vocabulary.h"

#include "config-test.h"

using namespace xqt;
using namespace xqt::hwr;

namespace {
QString bundledDir() { return HandwritingSearch::bundledModelsDir(fs::path(XQT_BUILD_RESOURCE_DIR)); }

/// The benchmark's line (y 550-575): "This is a dumb test, written many times"
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

// ONNX Runtime where each platform's package puts it: XQT_ONNXRUNTIME alone when set; Linux in lib/xournal-qt beside
// bin/, Windows next to the program, macOS in the bundle's Frameworks, Android by its name (the APK's native
// libraries); the system's last
TEST(OrtRuntimeTest, theRuntimeIsLookedForWhereEachPackagePutsIt) {
    using ort::Platform;
    const QString app = QStringLiteral("/app/bin");
    EXPECT_EQ(ort::candidates(Platform::Linux, app, QString()),
              (QStringList{QStringLiteral("/app/bin/../lib/xournal-qt/libonnxruntime.so.1"),
                           QStringLiteral("/app/bin/libonnxruntime.so.1"), QStringLiteral("libonnxruntime.so.1")}));
    EXPECT_EQ(ort::candidates(Platform::Windows, QStringLiteral("C:/xournal-qt/bin"), QString()),
              (QStringList{QStringLiteral("C:/xournal-qt/bin/../lib/xournal-qt/onnxruntime.dll"),
                           QStringLiteral("C:/xournal-qt/bin/onnxruntime.dll"), QStringLiteral("onnxruntime.dll")}));
    const QString mac = QStringLiteral("/Applications/xournal-qt.app/Contents/MacOS");
    EXPECT_EQ(ort::candidates(Platform::MacOS, mac, QString()),
              (QStringList{mac + QStringLiteral("/../Frameworks/libonnxruntime.1.dylib"),
                           mac + QStringLiteral("/../lib/xournal-qt/libonnxruntime.1.dylib"),
                           mac + QStringLiteral("/libonnxruntime.1.dylib"), QStringLiteral("libonnxruntime.1.dylib")}));
    EXPECT_EQ(ort::candidates(Platform::Android, QStringLiteral("/data/app/x/lib/arm64"), QString()),
              QStringList{QStringLiteral("libonnxruntime.so")});
    // The variable wins, on every platform
    for (const Platform p: {Platform::Linux, Platform::Windows, Platform::MacOS, Platform::Android}) {
        EXPECT_EQ(ort::candidates(p, app, QStringLiteral("/opt/ort/libonnxruntime.so.1.30.0")),
                  QStringList{QStringLiteral("/opt/ort/libonnxruntime.so.1.30.0")});
    }
    // Without the program's folder: the system's only
    EXPECT_EQ(ort::candidates(Platform::Linux, QString(), QString()), QStringList{QStringLiteral("libonnxruntime.so.1")});
#if defined(__linux__) && !defined(__ANDROID__)
    EXPECT_EQ(ort::thisPlatform(), Platform::Linux);
#endif
}

// The build puts the models of qt/resources/hwr into the resource dir (hwr-models/<name>/, with their licence notes):
// found by their manifests, they read German and English, and each file is the one its manifest names
TEST(BundledModelTest, theBuildPutsTheModelsIntoTheResourceDir) {
    const std::vector<ModelInfo> models = HandwritingSearch::bundledModels(bundledDir());
    ASSERT_FALSE(models.empty()) << bundledDir().toStdString();
    for (const char* language: {"de", "en"}) {
        EXPECT_TRUE(std::any_of(models.begin(), models.end(), [&](const ModelInfo& m) { return m.reads(QLatin1String(language)); }))
                << language;
    }
    for (const ModelInfo& m: models) {
        EXPECT_TRUE(m.valid()) << m.error.toStdString();
        EXPECT_TRUE(HandwritingSearch::isBundled(m.folder, bundledDir()));
        EXPECT_TRUE(QFile::exists(QDir(m.folder).filePath(QStringLiteral("LICENCE.md")))) << m.name.toStdString();
        const CtcManifest manifest = CtcManifest::read(m.folder);
        ASSERT_TRUE(manifest.valid()) << manifest.error.toStdString();
        EXPECT_FALSE(manifest.files.empty());
        for (const auto& [name, file]: manifest.files) {
            QFile f(QDir(m.folder).filePath(name));
            ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << name.toStdString();
            EXPECT_EQ(f.size(), file.size) << name.toStdString();
            QCryptographicHash hash(QCryptographicHash::Sha256);
            hash.addData(&f);
            EXPECT_EQ(QString::fromLatin1(hash.result().toHex()), file.sha256) << name.toStdString();
        }
    }
    // The noncommercial note of the manifest is read (the About line and Settings show it)
    EXPECT_TRUE(std::any_of(models.begin(), models.end(), [](const ModelInfo& m) { return m.noncommercial; }));
    // A folder without manifests holds none; a folder with a model.json is one, whatever its name
    QTemporaryDir tmp;
    EXPECT_TRUE(HandwritingSearch::bundledModels(tmp.path()).empty());
    EXPECT_TRUE(HandwritingSearch::bundledModels(QString()).empty());
    QDir().mkpath(tmp.filePath(QStringLiteral("any-name")));
    QFile manifest(tmp.filePath(QStringLiteral("any-name/model.json")));
    ASSERT_TRUE(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"({"kind": "ctc", "name": "other", "languages": ["fr"]})");
    manifest.close();
    const auto other = HandwritingSearch::bundledModels(tmp.path());
    ASSERT_EQ(other.size(), 1u);
    EXPECT_EQ(other[0].name, QStringLiteral("other"));
    EXPECT_FALSE(HandwritingSearch::isBundled(tmp.path(), bundledDir()));
}

// `xournal-qt --hwr-info`: the runtime, the models and what they read in the sample line, one fact per line
TEST(BundledModelTest, theInfoSaysWhatTheSearchRunsOn) {
    QTemporaryDir empty;
    const HwrReport none = describe(empty.filePath(QStringLiteral("hwr-models")));
    EXPECT_FALSE(none.ok);
    EXPECT_TRUE(none.text.rfind("onnxruntime: ", 0) == 0) << none.text;
    EXPECT_NE(none.text.find("\nmodels: none in "), std::string::npos) << none.text;
    EXPECT_NE(none.text.find("\nhwr: not ready\n"), std::string::npos) << none.text;

    const HwrReport report = describe(bundledDir());
    for (const ModelInfo& m: HandwritingSearch::bundledModels(bundledDir())) {
        const std::string line = QStringLiteral("\nmodel: %1 (%2; %3; version %4").arg(m.name, m.kind, m.languages.join(QStringLiteral(", ")), m.version).toStdString();
        EXPECT_NE(report.text.find(line), std::string::npos) << report.text;
    }
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1 to read the sample line";
    }
    EXPECT_TRUE(report.ok) << report.text;
    EXPECT_NE(report.text.find("onnxruntime: found (" + qEnvironmentVariable("XQT_ONNXRUNTIME").toStdString() + ", version 1."),
              std::string::npos)
            << report.text;
    EXPECT_NE(report.text.find("  reads the sample line: \""), std::string::npos) << report.text;
    EXPECT_EQ(report.text.find("cannot read"), std::string::npos) << report.text;
    EXPECT_TRUE(report.text.size() > 12 && report.text.substr(report.text.size() - 11) == "hwr: ready\n") << report.text;
}

// The sample line is one word written with a few strokes
TEST(BundledModelTest, theSampleLineIsOneWord) {
    const LineInput line = sampleLine();
    ASSERT_EQ(line.words.size(), 1u);
    EXPECT_EQ(line.words[0].strokes.size(), line.strokes.size());
    EXPECT_GT(line.size.width(), line.size.height());
}

// A damaged model file is said, not read (the same check as a downloaded model's: sha256 when it is loaded)
TEST(BundledModelTest, aDamagedModelIsNotRead) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    const std::vector<ModelInfo> models = HandwritingSearch::bundledModels(bundledDir());
    ASSERT_FALSE(models.empty());
    QTemporaryDir tmp;
    const QString copy = tmp.filePath(QStringLiteral("hwr-models/") + QDir(models[0].folder).dirName());
    QDir().mkpath(copy);
    const CtcManifest manifest = CtcManifest::read(models[0].folder);
    for (const QString& f: QDir(models[0].folder).entryList(QDir::Files)) {
        ASSERT_TRUE(QFile::copy(QDir(models[0].folder).filePath(f), QDir(copy).filePath(f)));
    }
    {
        // (the same size, another byte)
        QFile f(QDir(copy).filePath(manifest.model));
        ASSERT_TRUE(f.open(QIODevice::ReadWrite));
        f.seek(f.size() / 2);
        char c = 0;
        f.getChar(&c);
        f.seek(f.size() / 2);
        f.putChar(static_cast<char>(c ^ 0x5a));
    }
    const HwrReport report = describe(tmp.filePath(QStringLiteral("hwr-models")));
    EXPECT_FALSE(report.ok);
    EXPECT_NE(report.text.find("cannot read the sample line (The model file " + manifest.model.toStdString() + " is not the one"),
              std::string::npos)
            << report.text;
}

// The built-in model reads real handwriting: the benchmark's line ("This is a dumb test, written many times")
TEST(BundledModelTest, theBuiltInModelReadsRealHandwriting) {
    if (qEnvironmentVariableIsEmpty("XQT_ONNXRUNTIME")) {
        GTEST_SKIP() << "set XQT_ONNXRUNTIME to the path of libonnxruntime.so.1";
    }
    const std::vector<ModelInfo> models = HandwritingSearch::bundledModels(bundledDir());
    auto english = std::find_if(models.begin(), models.end(), [](const ModelInfo& m) { return m.reads(QStringLiteral("en")); });
    ASSERT_NE(english, models.end());
    const auto rec = onnxRecognizerFor(english->folder);
    QString why;
    ASSERT_TRUE(rec->ready(&why)) << why.toStdString();
    const LineInput line = benchmarkLine();
    const auto result = rec->recognizeLine(line, {});
    ASSERT_TRUE(result);
    int found = 0;
    QStringList read;
    for (const ink::Word& w: result->words) {
        read << w.text;
    }
    for (const char* expected: {"this", "dumb", "test"}) {
        const words::Id id = words::idOf(QString::fromLatin1(expected));
        for (const ink::Word& w: result->words) {
            if (std::any_of(w.candidates.begin(), w.candidates.end(), [&](const ink::Candidate& c) { return c.word == id; })) {
                ++found;
                break;
            }
        }
    }
    EXPECT_GE(found, 2) << read.join(u' ').toStdString();
}

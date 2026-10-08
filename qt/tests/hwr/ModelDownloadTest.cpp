/*
 * xournal-qt: the model's download with consent (ModelDownload, HandwritingSettings), against a fake network.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "support/FakeNet.h"
#include "control/settings/Settings.h"
#include "hwr/FakeRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/ModelInfo.h"
#include "session/AppContext.h"
#include "shell/HandwritingSettings.h"
#include "shell/LibraryInkJob.h"
#include "shell/ModelDownload.h"
#include "support/TestSupport.h"

using xqt::test::waitFor;

using namespace xqt;

namespace {
QString sha(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

class ModelDownloadTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        model.name = QStringLiteral("trocr-small-hw-int8");
        model.source = QStringLiteral("https://models.example/Xenova/trocr-small-handwritten");
        model.revision = QStringLiteral("0123abc");
        model.encoder = QStringLiteral("onnx/encoder.onnx");
        model.decoder = QStringLiteral("onnx/decoder.onnx");
        model.tokenizer = QStringLiteral("tokenizer.json");
        bodies = {{model.encoder, QByteArray("the encoder")}, {model.decoder, QByteArray("the decoder!")},
                  {model.tokenizer, QByteArray("{}")}};
        for (const QString& path: {model.encoder, model.decoder, model.tokenizer}) {  // (in this order)
            model.files.push_back({path, sha(bodies[path]), bodies[path].size()});
        }
        model.language = QStringLiteral("en");
        german.name = QStringLiteral("crnn-de");
        german.language = QStringLiteral("de");
        german.kind = QStringLiteral("ctc");
        german.source = QStringLiteral("https://models.example/xournal-qt/crnn-de");
        german.files = {{QStringLiteral("model.json"), QString(), 100}, {QStringLiteral("model.onnx"), QString(), 1000}};
        german.unpinned = QStringLiteral("The German model is not published yet.");
        catalogue = {model, german};
        ModelDownload::setCatalogue(&catalogue);
        net.answer = [this](const QUrl& url) {
            const QString prefix = model.source + QStringLiteral("/resolve/") + model.revision + u'/';
            const QString path = url.toString().mid(prefix.size());
            if (!url.toString().startsWith(prefix) || !bodies.count(path)) {
                NetFetch::Reply r;
                r.status = 404;
                r.error = QStringLiteral("not found");
                return r;
            }
            return test::FakeNet::ok(served.count(path) ? served[path] : bodies[path]);
        };
        folder = tmp.filePath(QStringLiteral("models/trocr-small-hw-int8"));
    }
    void TearDown() override { ModelDownload::setCatalogue(nullptr); }
    QStringList requested() const {
        QStringList out;
        for (const auto& c: net.calls) {
            out << c.url.toString().mid((model.source + QStringLiteral("/resolve/") + model.revision).size() + 1);
        }
        return out;
    }
    QTemporaryDir tmp;
    ModelDownload::Model model, german;
    std::vector<ModelDownload::Model> catalogue;
    std::map<QString, QByteArray> bodies, served;
    test::FakeNet net;
    QString folder;
};
}  // namespace

TEST_F(ModelDownloadTest, theModelIsDownloadedCheckedAndPutInPlace) {
    EXPECT_TRUE(model.pinned());
    EXPECT_EQ(model.urlOf(model.files[0]).toString(),
              QStringLiteral("https://models.example/Xenova/trocr-small-handwritten/resolve/0123abc/onnx/encoder.onnx"));
    ModelDownload d(model, folder);
    bool ok = false;
    QObject::connect(&d, &ModelDownload::finished, [&](bool r) { ok = r; });
    d.start();
    ASSERT_TRUE(waitFor([&] { return !d.running(); }));
    EXPECT_TRUE(ok) << d.error().toStdString();
    EXPECT_EQ(d.state(), ModelDownload::State::Done);
    EXPECT_EQ(net.calls.size(), 3u);
    EXPECT_TRUE(ModelDownload::installed(folder));
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral(".part")));
    QFile f(folder + QStringLiteral("/onnx/decoder.onnx"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), bodies[model.decoder]);
    // The manifest, as qt/scripts/hwr-model.sh writes it
    QFile m(folder + QStringLiteral("/model.json"));
    ASSERT_TRUE(m.open(QIODevice::ReadOnly));
    const QJsonObject o = QJsonDocument::fromJson(m.readAll()).object();
    EXPECT_EQ(o.value(QStringLiteral("name")).toString(), model.name);
    EXPECT_EQ(o.value(QStringLiteral("kind")).toString(), QStringLiteral("trocr"));
    EXPECT_EQ(o.value(QStringLiteral("languages")).toArray(), QJsonArray{QStringLiteral("en")});
    EXPECT_EQ(o.value(QStringLiteral("revision")).toString(), model.revision);
    EXPECT_EQ(o.value(QStringLiteral("decoder")).toString(), model.decoder);
    EXPECT_EQ(o.value(QStringLiteral("files")).toObject().value(model.encoder).toObject().value(QStringLiteral("sha256")).toString(),
              sha(bodies[model.encoder]));
    EXPECT_EQ(ModelDownload::sizeOnDisk(folder), 11 + 12 + 2 + m.size());
    // Removed again
    EXPECT_TRUE(ModelDownload::remove(folder));
    EXPECT_FALSE(QFileInfo::exists(folder));
}

TEST_F(ModelDownloadTest, aWrongFileIsNotKeptAndTheDownloadGoesOnLater) {
    served[model.decoder] = QByteArray("something else");
    ModelDownload d(model, folder);
    d.start();
    ASSERT_TRUE(waitFor([&] { return !d.running(); }));
    EXPECT_EQ(d.state(), ModelDownload::State::Failed);
    EXPECT_TRUE(d.error().contains(model.decoder)) << d.error().toStdString();
    EXPECT_FALSE(ModelDownload::installed(folder));
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral(".part/onnx/decoder.onnx")));
    EXPECT_TRUE(QFileInfo::exists(folder + QStringLiteral(".part/onnx/encoder.onnx")));
    // Again: the encoder is not downloaded twice
    served.clear();
    net.calls.clear();
    d.start();
    ASSERT_TRUE(waitFor([&] { return !d.running(); }));
    EXPECT_EQ(d.state(), ModelDownload::State::Done) << d.error().toStdString();
    EXPECT_EQ(requested(), (QStringList{QStringLiteral("onnx/decoder.onnx"), QStringLiteral("tokenizer.json")}));
}

TEST_F(ModelDownloadTest, aCancelledDownloadWritesNothingMore) {
    ModelDownload d(model, folder);
    d.start();
    EXPECT_TRUE(d.running());
    d.cancel();
    EXPECT_EQ(d.state(), ModelDownload::State::Cancelled);
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    EXPECT_EQ(net.calls.size(), 1u);
    EXPECT_FALSE(QFileInfo::exists(folder + QStringLiteral(".part/onnx/encoder.onnx")));
    EXPECT_FALSE(ModelDownload::installed(folder));
}

TEST_F(ModelDownloadTest, nothingIsDownloadedWithoutPinnedFiles) {
    model.files[1].sha256.clear();
    model.unpinned = QStringLiteral("Install it with qt/scripts/hwr-model.sh");
    ModelDownload d(model, folder);
    d.start();
    EXPECT_EQ(d.state(), ModelDownload::State::Failed);
    EXPECT_TRUE(d.error().contains(QStringLiteral("hwr-model.sh")));
    EXPECT_TRUE(net.calls.empty());
    // The German model until it is published
    ModelDownload de(german, tmp.filePath(QStringLiteral("models/crnn-de")));
    de.start();
    EXPECT_EQ(de.state(), ModelDownload::State::Failed);
    EXPECT_TRUE(de.error().contains(QStringLiteral("not published yet")));
    EXPECT_TRUE(net.calls.empty());
}

// This build's catalogue: English (TrOCR, pinned to a revision) and German (the project's CTC model, not published
// yet: it cannot be downloaded, a folder can be chosen)
TEST_F(ModelDownloadTest, theBuildKnowsAnEnglishAndAGermanModel) {
    ModelDownload::setCatalogue(nullptr);
    const ModelDownload::Model* en = ModelDownload::modelFor(QStringLiteral("en"));
    const ModelDownload::Model* de = ModelDownload::modelFor(QStringLiteral("de"));
    ASSERT_NE(en, nullptr);
    ASSERT_NE(de, nullptr);
    EXPECT_EQ(en->name, QStringLiteral("trocr-small-hw-int8"));
    EXPECT_EQ(en->kind, QStringLiteral("trocr"));
    EXPECT_EQ(de->kind, QStringLiteral("ctc"));
    EXPECT_TRUE(en->pinned());
    EXPECT_EQ(en->revision.size(), 40);
    EXPECT_FALSE(de->pinned());
    EXPECT_TRUE(de->unpinned.contains(QStringLiteral("not published yet")));
    EXPECT_EQ(hwr::HandwritingSearch::defaultModelDir(QStringLiteral("de")),
              hwr::HandwritingSearch::modelsDir() + QStringLiteral("/") + de->name);
    // A model that brings its own manifest keeps it
    std::vector<ModelDownload::Model> own{*de};
    ModelDownload::Model& m = own.front();
    m.revision = QStringLiteral("1");
    const QByteArray manifest = R"({"kind": "ctc", "name": "crnn-de", "languages": ["de"], "version": "1"})";
    bodies = {{QStringLiteral("model.json"), manifest}, {QStringLiteral("model.onnx"), QByteArray("onnx")}};
    m.files = {{QStringLiteral("model.json"), sha(manifest), manifest.size()},
               {QStringLiteral("model.onnx"), sha(bodies[QStringLiteral("model.onnx")]), 4}};
    model.source = m.source;
    model.revision = m.revision;
    const QString target = tmp.filePath(QStringLiteral("models/crnn-de"));
    ModelDownload d(m, target);
    d.start();
    ASSERT_TRUE(waitFor([&] { return !d.running(); }));
    ASSERT_EQ(d.state(), ModelDownload::State::Done) << d.error().toStdString();
    QFile f(target + QStringLiteral("/model.json"));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), manifest);
    const hwr::ModelInfo info = hwr::ModelInfo::read(target);
    EXPECT_TRUE(info.valid()) << info.error.toStdString();
    EXPECT_EQ(info.languages, QStringList{QStringLiteral("de")});
}

TEST_F(ModelDownloadTest, settingsOfferTheDownloadsAndUseTheModels) {
    qputenv("XDG_DATA_HOME", tmp.filePath(QStringLiteral("data")).toUtf8());
    // (an installation without a built-in model: theBuiltInModelReadsWhatHasNoModelOfItsOwn has one)
    AppContext app(fs::path(tmp.filePath(QStringLiteral("resources")).toStdString()), fs::path(tmp.filePath(QStringLiteral("settings.xml")).toStdString()),
                   1);
    int made = 0;
    hwr::HandwritingSearch::setFactory([&](const QString& dir) -> std::shared_ptr<hwr::Recognizer> {
        ++made;
        const hwr::ModelInfo info = hwr::ModelInfo::read(dir);
        auto fake = std::make_shared<hwr::FakeRecognizer>(info.valid() ? info.id() : QStringLiteral("none"),
                                                          info.languages);
        fake->setReady(info.valid(), QStringLiteral("The model is not installed"));
        return fake;
    });
    LibraryInkJob::setPowerSource([] { return true; });
    hwr::HandwritingSearch search(app);
    HandwritingSettings settings(app, search, nullptr);
    EXPECT_FALSE(settings.enabled());
    EXPECT_EQ(settings.status(), QStringLiteral("Off"));
    EXPECT_EQ(settings.languages(), QStringLiteral("en+de"));  // (English and German by default)
    settings.setEnabled(true);
    EXPECT_TRUE(hwr::HandwritingSearch::enabledIn(*app.getSettings()));
    EXPECT_FALSE(settings.ready());
    EXPECT_EQ(settings.status(), QStringLiteral("The model is not installed"));
    ASSERT_EQ(settings.models().size(), 2);
    QVariantMap en = settings.modelOf(QStringLiteral("en"));
    EXPECT_TRUE(en.value(QStringLiteral("own")).toBool());
    EXPECT_TRUE(en.value(QStringLiteral("needed")).toBool());
    EXPECT_FALSE(en.value(QStringLiteral("installed")).toBool());
    EXPECT_TRUE(en.value(QStringLiteral("downloadAvailable")).toBool());
    EXPECT_EQ(en.value(QStringLiteral("source")).toString(), model.source + QStringLiteral("/tree/0123abc"));
    EXPECT_EQ(en.value(QStringLiteral("size")).toString(), QStringLiteral("1 MB"));
    EXPECT_EQ(en.value(QStringLiteral("state")).toString(), QStringLiteral("Not installed"));
    QVariantMap de = settings.modelOf(QStringLiteral("de"));
    EXPECT_FALSE(de.value(QStringLiteral("downloadAvailable")).toBool());
    EXPECT_TRUE(de.value(QStringLiteral("unpinned")).toString().contains(QStringLiteral("not published yet")));
    EXPECT_TRUE(net.calls.empty());  // (nothing before the user asks)
    settings.download(QStringLiteral("de"));  // (refused: not published)
    EXPECT_FALSE(settings.modelOf(QStringLiteral("de")).value(QStringLiteral("downloading")).toBool());
    EXPECT_TRUE(net.calls.empty());
    settings.download(QStringLiteral("en"));
    EXPECT_TRUE(settings.modelOf(QStringLiteral("en")).value(QStringLiteral("downloading")).toBool());
    ASSERT_TRUE(waitFor([&] { return !settings.modelOf(QStringLiteral("en")).value(QStringLiteral("downloading")).toBool(); }));
    en = settings.modelOf(QStringLiteral("en"));
    EXPECT_TRUE(en.value(QStringLiteral("installed")).toBool());
    EXPECT_TRUE(en.value(QStringLiteral("inUse")).toBool());
    EXPECT_TRUE(en.value(QStringLiteral("state")).toString().startsWith(QStringLiteral("In use"))) << en.value(QStringLiteral("state")).toString().toStdString();
    EXPECT_TRUE(settings.ready()) << settings.status().toStdString();
    EXPECT_TRUE(settings.status().startsWith(QStringLiteral("Ready No model for German yet"))) << settings.status().toStdString();
    EXPECT_GE(made, 2);
    const QString englishOnly = search.service().recognizerId();
    EXPECT_TRUE(englishOnly.startsWith(QStringLiteral("trocr-small-hw-int8/")));
    // German from a folder of the user's: both models read, the results are named by both
    QTemporaryDir mine;
    QFile manifest(mine.filePath(QStringLiteral("model.json")));
    ASSERT_TRUE(manifest.open(QIODevice::WriteOnly));
    manifest.write(R"({"kind": "ctc", "name": "crnn-de", "languages": ["de"], "version": "test"})");
    manifest.close();
    settings.chooseFolder(QStringLiteral("de"), QUrl::fromLocalFile(mine.path()));
    de = settings.modelOf(QStringLiteral("de"));
    EXPECT_FALSE(de.value(QStringLiteral("own")).toBool());
    EXPECT_TRUE(de.value(QStringLiteral("installed")).toBool());
    EXPECT_TRUE(de.value(QStringLiteral("inUse")).toBool());
    EXPECT_EQ(settings.status(), QStringLiteral("Ready"));
    EXPECT_EQ(search.modelFoldersInUse().size(), 2);
    const QString both = search.service().recognizerId();
    EXPECT_EQ(both, englishOnly + QStringLiteral("+crnn-de/") + hwr::ModelInfo::read(mine.path()).hash);
    EXPECT_FALSE(settings.removeModel(QStringLiteral("de")));  // (the user's: never removed here)
    EXPECT_TRUE(QFileInfo::exists(mine.filePath(QStringLiteral("model.json"))));
    // German only: the German model alone
    settings.setLanguages(QStringLiteral("de"));
    EXPECT_EQ(hwr::HandwritingSearch::languagesIn(*app.getSettings()), QStringList{QStringLiteral("de")});
    EXPECT_EQ(search.service().recognizerId(), QStringLiteral("crnn-de/") + hwr::ModelInfo::read(mine.path()).hash);
    EXPECT_FALSE(settings.modelOf(QStringLiteral("en")).value(QStringLiteral("inUse")).toBool());
    // The app's own again for German, both languages: the German model is missing again
    settings.chooseFolder(QStringLiteral("de"), QUrl());
    settings.setLanguages(QStringLiteral("en+de"));
    EXPECT_EQ(search.service().recognizerId(), englishOnly);
    // Removed: not ready any more, the files are gone
    EXPECT_TRUE(settings.removeModel(QStringLiteral("en")));
    EXPECT_FALSE(settings.modelOf(QStringLiteral("en")).value(QStringLiteral("installed")).toBool());
    EXPECT_FALSE(settings.ready());
    hwr::HandwritingSearch::setFactory({});
    LibraryInkJob::setPowerSource({});
}

// Which models the languages need: the folder chosen for a language, else the app's own, else any model in the models
// folder that reads it; one model that reads both serves both
TEST_F(ModelDownloadTest, theLanguagesChooseTheirModels) {
    qputenv("XDG_DATA_HOME", tmp.filePath(QStringLiteral("data2")).toUtf8());
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath(QStringLiteral("settings2.xml")).toStdString()),
                   1);
    Settings& s = *app.getSettings();
    auto put = [](const QString& folder, const QByteArray& json) {
        QDir().mkpath(folder);
        QFile f(folder + QStringLiteral("/model.json"));
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(json);
    };
    auto names = [](const hwr::HandwritingSearch::Choice& c) {
        QStringList out;
        for (const auto& m: c.models) {
            out << m.name;
        }
        return out;
    };
    EXPECT_EQ(hwr::HandwritingSearch::choose(s).missing, (QStringList{QStringLiteral("en"), QStringLiteral("de")}));
    // A manifest of the first version (no kind, no languages): TrOCR for English
    put(hwr::HandwritingSearch::defaultModelDir(QStringLiteral("en")), R"({"name": "trocr-small-hw-int8"})");
    auto c = hwr::HandwritingSearch::choose(s);
    EXPECT_EQ(names(c), QStringList{QStringLiteral("trocr-small-hw-int8")});
    EXPECT_EQ(c.missing, QStringList{QStringLiteral("de")});
    // A model for both in the models folder: it serves German (English keeps its own)
    put(hwr::HandwritingSearch::modelsDir() + QStringLiteral("/crnn-de-en"),
        R"({"kind": "ctc", "name": "crnn-de-en", "languages": ["de", "en"]})");
    c = hwr::HandwritingSearch::choose(s);
    EXPECT_EQ(names(c), (QStringList{QStringLiteral("trocr-small-hw-int8"), QStringLiteral("crnn-de-en")}));
    EXPECT_TRUE(c.missing.isEmpty());
    // German alone: the combined one
    hwr::HandwritingSearch::setLanguagesIn(s, {QStringLiteral("de")});
    EXPECT_EQ(names(hwr::HandwritingSearch::choose(s)), QStringList{QStringLiteral("crnn-de-en")});
    // A folder the user chose for German that holds no model: German is missing (not replaced by another)
    hwr::HandwritingSearch::setModelDirIn(s, QStringLiteral("de"), tmp.filePath(QStringLiteral("nothing")));
    EXPECT_EQ(hwr::HandwritingSearch::choose(s).missing, QStringList{QStringLiteral("de")});
    hwr::HandwritingSearch::setModelDirIn(s, QStringLiteral("de"), QString());
    // The order of the languages does not matter (English first)
    hwr::HandwritingSearch::setLanguagesIn(s, {QStringLiteral("de"), QStringLiteral("en")});
    EXPECT_EQ(hwr::HandwritingSearch::languagesIn(s), (QStringList{QStringLiteral("en"), QStringLiteral("de")}));
    // A kind this app does not read is no model
    put(tmp.filePath(QStringLiteral("odd")), R"({"kind": "seq2seq", "name": "x", "languages": ["en"]})");
    EXPECT_FALSE(hwr::ModelInfo::read(tmp.filePath(QStringLiteral("odd"))).valid());
}

namespace {
void putManifest(const QString& folder, const QByteArray& json) {
    QDir().mkpath(folder);
    QFile f(folder + QStringLiteral("/model.json"));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(json);
}
}  // namespace

// The order per language: the folder chosen in Settings, else the environment's, else the app's own downloaded one,
// else any downloaded one that reads it, else one that comes with the app and reads it, else none. One built-in model
// for both languages serves both, once.
TEST_F(ModelDownloadTest, aBuiltInModelComesLastInTheLookup) {
    qputenv("XDG_DATA_HOME", tmp.filePath(QStringLiteral("data3")).toUtf8());
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath(QStringLiteral("settings3.xml")).toStdString()),
                   1);
    Settings& s = *app.getSettings();
    const QString bundled = tmp.filePath(QStringLiteral("res/hwr-models"));
    auto names = [](const hwr::HandwritingSearch::Choice& c) {
        QStringList out;
        for (const auto& m: c.models) {
            out << m.name;
        }
        return out;
    };
    // Nothing built in: both missing
    EXPECT_EQ(hwr::HandwritingSearch::choose(s, bundled).missing, (QStringList{QStringLiteral("en"), QStringLiteral("de")}));
    // A built-in model for both: it serves both, read once
    putManifest(bundled + QStringLiteral("/shared"), R"({"kind": "ctc", "name": "shared", "languages": ["de", "en"]})");
    putManifest(bundled + QStringLiteral("/broken"), R"({"kind": "ctc", "languages": ["en"]})");  // (no name: none)
    auto c = hwr::HandwritingSearch::choose(s, bundled);
    EXPECT_EQ(names(c), QStringList{QStringLiteral("shared")});
    EXPECT_TRUE(c.missing.isEmpty());
    EXPECT_TRUE(hwr::HandwritingSearch::isBundled(c.models.at(0).folder, bundled));
    // Without the bundled folder: as before
    EXPECT_EQ(hwr::HandwritingSearch::choose(s).missing.size(), 2);
    // English downloaded (the app's own): it reads English, the built-in one German
    putManifest(hwr::HandwritingSearch::defaultModelDir(QStringLiteral("en")), R"({"name": "trocr-small-hw-int8"})");
    c = hwr::HandwritingSearch::choose(s, bundled);
    EXPECT_EQ(names(c), (QStringList{QStringLiteral("trocr-small-hw-int8"), QStringLiteral("shared")}));
    // Another downloaded model that reads German comes before the built-in one
    putManifest(hwr::HandwritingSearch::modelsDir() + QStringLiteral("/mine-de"),
                R"({"kind": "ctc", "name": "mine-de", "languages": ["de"]})");
    c = hwr::HandwritingSearch::choose(s, bundled);
    EXPECT_EQ(names(c), (QStringList{QStringLiteral("trocr-small-hw-int8"), QStringLiteral("mine-de")}));
    QDir(hwr::HandwritingSearch::modelsDir() + QStringLiteral("/mine-de")).removeRecursively();
    // The environment's model for English comes first
    QTemporaryDir env;
    putManifest(env.path(), R"({"kind": "ctc", "name": "env-en", "languages": ["en"]})");
    qputenv("XQT_HWR_MODEL", env.path().toUtf8());
    EXPECT_EQ(names(hwr::HandwritingSearch::choose(s, bundled)), (QStringList{QStringLiteral("env-en"), QStringLiteral("shared")}));
    qunsetenv("XQT_HWR_MODEL");
    // A folder chosen in Settings comes first; one that holds no model is not replaced by the built-in one
    hwr::HandwritingSearch::setModelDirIn(s, QStringLiteral("en"), env.path());
    EXPECT_EQ(names(hwr::HandwritingSearch::choose(s, bundled)), (QStringList{QStringLiteral("env-en"), QStringLiteral("shared")}));
    hwr::HandwritingSearch::setModelDirIn(s, QStringLiteral("en"), tmp.filePath(QStringLiteral("nothing")));
    c = hwr::HandwritingSearch::choose(s, bundled);
    EXPECT_EQ(c.missing, QStringList{QStringLiteral("en")});
    EXPECT_EQ(names(c), QStringList{QStringLiteral("shared")});
    hwr::HandwritingSearch::setModelDirIn(s, QStringLiteral("en"), QString());
    // The app's own removed: the built-in one again for both
    QDir(hwr::HandwritingSearch::defaultModelDir(QStringLiteral("en"))).removeRecursively();
    EXPECT_EQ(names(hwr::HandwritingSearch::choose(s, bundled)), QStringList{QStringLiteral("shared")});
    // The app's own bundled folder: the build's
    hwr::HandwritingSearch search(app);
    EXPECT_EQ(search.bundledModelsDir(), QStringLiteral(XQT_BUILD_RESOURCE_DIR "/hwr-models"));
}

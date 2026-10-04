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
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "../FakeNet.h"
#include "control/settings/Settings.h"
#include "hwr/FakeRecognizer.h"
#include "hwr/HandwritingSearch.h"
#include "session/AppContext.h"
#include "shell/HandwritingSettings.h"
#include "shell/LibraryInkJob.h"
#include "shell/ModelDownload.h"

using namespace xqt;

namespace {
QString sha(const QByteArray& b) {
    return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex());
}

bool waitFor(const std::function<bool()>& done, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
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
        ModelDownload::setModel(&model);
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
    void TearDown() override { ModelDownload::setModel(nullptr); }
    QStringList requested() const {
        QStringList out;
        for (const auto& c: net.calls) {
            out << c.url.toString().mid((model.source + QStringLiteral("/resolve/") + model.revision).size() + 1);
        }
        return out;
    }
    QTemporaryDir tmp;
    ModelDownload::Model model;
    std::map<QString, QByteArray> bodies, served;
    test::FakeNet net;
    QString folder;
};
}  // namespace

TEST_F(ModelDownloadTest, theModelIsDownloadedCheckedAndPutInPlace) {
    EXPECT_TRUE(model.pinned());
    EXPECT_EQ(model.urlOf(model.files[0]).toString(),
              QStringLiteral("https://models.example/Xenova/trocr-small-handwritten/resolve/0123abc/onnx/encoder.onnx"));
    ModelDownload d(folder);
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
    ModelDownload d(folder);
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
    ModelDownload d(folder);
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
    ModelDownload d(folder);
    d.start();
    EXPECT_EQ(d.state(), ModelDownload::State::Failed);
    EXPECT_TRUE(d.error().contains(QStringLiteral("hwr-model.sh")));
    EXPECT_TRUE(net.calls.empty());
}

TEST_F(ModelDownloadTest, settingsOfferTheDownloadAndUseTheModel) {
    qputenv("XDG_DATA_HOME", tmp.filePath(QStringLiteral("data")).toUtf8());
    AppContext app(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(tmp.filePath(QStringLiteral("settings.xml")).toStdString()),
                   1);
    int made = 0;
    hwr::HandwritingSearch::setFactory([&](const QString& dir) -> std::shared_ptr<hwr::Recognizer> {
        ++made;
        auto fake = std::make_shared<hwr::FakeRecognizer>();
        fake->setReady(ModelDownload::installed(dir), QStringLiteral("The model is not installed"));
        return fake;
    });
    LibraryInkJob::setPowerSource([] { return true; });
    hwr::HandwritingSearch search(app);
    HandwritingSettings settings(app, search, nullptr);
    EXPECT_FALSE(settings.enabled());
    EXPECT_EQ(settings.status(), QStringLiteral("Off"));
    settings.setEnabled(true);
    EXPECT_TRUE(hwr::HandwritingSearch::enabledIn(*app.getSettings()));
    EXPECT_FALSE(settings.ready());
    EXPECT_EQ(settings.status(), QStringLiteral("The model is not installed"));
    EXPECT_TRUE(settings.ownModel());
    EXPECT_TRUE(settings.downloadAvailable());
    EXPECT_EQ(settings.downloadSource(), model.source + QStringLiteral("/tree/0123abc"));
    EXPECT_EQ(settings.downloadSize(), QStringLiteral("0 MB"));
    EXPECT_TRUE(net.calls.empty());  // (nothing before the user asks)
    settings.download();
    EXPECT_TRUE(settings.downloading());
    ASSERT_TRUE(waitFor([&] { return !settings.downloading(); }));
    EXPECT_TRUE(settings.modelInstalled());
    EXPECT_TRUE(settings.ready()) << settings.status().toStdString();
    EXPECT_EQ(settings.status(), QStringLiteral("Ready"));
    EXPECT_GE(made, 2);
    // Removed: not ready any more, the files are gone
    EXPECT_TRUE(settings.removeModel());
    EXPECT_FALSE(settings.modelInstalled());
    EXPECT_FALSE(settings.ready());
    hwr::HandwritingSearch::setFactory({});
    LibraryInkJob::setPowerSource({});
}

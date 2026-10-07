#include "ModelDownload.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include "NetFetch.h"
#include "session/FileIo.h"

namespace xqt {

namespace {
/// The models this build downloads.
/// English: Xenova's ONNX export of TrOCR-small handwritten (MIT), int8, pinned to a revision with the size and sha256
/// of each file (qt/scripts/hwr-model.sh prints them; the ONNX files' sha256 are also Hugging Face's LFS ids).
/// German: the project's own CTC model (qt/research/hwr/train, FORMATS.md "kind": "ctc"), pinned when it is published;
/// until then a folder holding one can be chosen in Settings.
std::vector<ModelDownload::Model> builtIn() {
    ModelDownload::Model en;
    en.name = QStringLiteral("trocr-small-hw-int8");
    en.language = QStringLiteral("en");
    en.kind = QStringLiteral("trocr");
    en.source = QStringLiteral("https://huggingface.co/Xenova/trocr-small-handwritten");
    en.revision = QStringLiteral("2432e24d184b1d964d07ed04f5d9e21d31a59141");
    en.encoder = QStringLiteral("onnx/encoder_model_quantized.onnx");
    en.decoder = QStringLiteral("onnx/decoder_model_merged_quantized.onnx");
    en.tokenizer = QStringLiteral("tokenizer.json");
    en.files = {
            {en.encoder, QStringLiteral("2f29edbd925f8a49c9c7d1349895f960cf09d2efdc76fe23f957048d476e0d03"), 23082942},
            {en.decoder, QStringLiteral("51076aa396ab5939c4668db9de901ad51765094b4c05c4c8c1f8ae2012ba1e08"), 40527613},
            {en.tokenizer, QStringLiteral("68bcb5468c854362a615f3d2ff6a5e4091a85f4c8198993ed9a30afe0b143737"), 4494727},
            {QStringLiteral("generation_config.json"),
             QStringLiteral("cce308da91e0d656e07404c70d4b9c9f5839d426f679eff6fda6dcc0e727e3b6"), 185},
            {QStringLiteral("preprocessor_config.json"),
             QStringLiteral("70da3434c33eedb3b56caf4067851741dfeed02f67576dcc3c6407c2533bfaf0"), 465}};
    en.unpinned = QObject::tr("This version of the app does not name the model's files yet: install it with "
                              "qt/scripts/hwr-model.sh (see the handwriting search's documentation).");
    ModelDownload::Model de;
    de.name = QStringLiteral("crnn-de");
    de.language = QStringLiteral("de");
    de.kind = QStringLiteral("ctc");
    de.source = QStringLiteral("https://huggingface.co/xournal-qt/crnn-de");  // TODO(author): where it is published
    de.revision = QString();
    // (about 10 MB: a small CRNN in int8; the files, sizes and sha256 are pinned when it is published)
    de.files = {{QStringLiteral("model.json"), QString(), 4 * 1024},
                {QStringLiteral("model_int8.onnx"), QString(), 10 * 1024 * 1024},
                {QStringLiteral("alphabet.txt"), QString(), 1024}};
    de.unpinned = QObject::tr("The German model is not published yet. Choose a folder that holds one (with its "
                              "model.json) instead.");
    return {en, de};
}

const std::vector<ModelDownload::Model>* testModels = nullptr;

QString sha256Of(const QByteArray& data) {
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

bool fileMatches(const QString& path, const ModelDownload::File& f) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || (f.size > 0 && file.size() != f.size)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return QString::fromLatin1(hash.result().toHex()) == f.sha256.toLower();
}
}  // namespace

QUrl ModelDownload::Model::urlOf(const File& f) const {
    return QUrl(source + QStringLiteral("/resolve/") + revision + u'/' + f.path);
}

qint64 ModelDownload::Model::bytes() const {
    qint64 b = 0;
    for (const File& f: files) {
        b += f.size;
    }
    return b;
}

bool ModelDownload::Model::pinned() const {
    return !revision.isEmpty() && !files.empty() &&
           std::all_of(files.begin(), files.end(), [](const File& f) { return f.sha256.size() == 64; });
}

const std::vector<ModelDownload::Model>& ModelDownload::catalogue() {
    static const std::vector<Model> all = builtIn();
    return testModels ? *testModels : all;
}

void ModelDownload::setCatalogue(const std::vector<Model>* models) { testModels = models; }

const ModelDownload::Model* ModelDownload::modelFor(const QString& language) {
    for (const Model& m: catalogue()) {
        if (m.language == language) {
            return &m;
        }
    }
    return nullptr;
}

QByteArray ModelDownload::manifestOf(const Model& m) {
    QString s = QStringLiteral("{\n  \"kind\": \"trocr\",\n  \"languages\": [\"%9\"],\n  \"name\": \"%1\",\n  \"source\": \"%2\",\n  \"revision\": \"%3\",\n"
                               "  \"license\": \"MIT\",\n  \"encoder\": \"%4\",\n  \"decoder\": \"%5\",\n"
                               "  \"tokenizer\": \"%6\",\n  \"decoder_start_token_id\": %7,\n  \"eos_token_id\": %8,\n"
                               "  \"image_size\": 384,\n  \"files\": {\n")
                        .arg(m.name, m.source, m.revision, m.encoder, m.decoder, m.tokenizer)
                        .arg(m.start)
                        .arg(m.end)
                        .arg(m.language.isEmpty() ? QStringLiteral("en") : m.language);
    for (size_t i = 0; i < m.files.size(); ++i) {
        s += QStringLiteral("    \"%1\": {\"sha256\": \"%2\", \"size\": %3}%4\n")
                     .arg(m.files[i].path, m.files[i].sha256.toLower())
                     .arg(m.files[i].size)
                     .arg(i + 1 < m.files.size() ? QStringLiteral(",") : QString());
    }
    s += QStringLiteral("  }\n}\n");
    return s.toUtf8();
}

ModelDownload::ModelDownload(Model model, QString folder, QObject* parent):
        QObject(parent), fetching(std::move(model)), target(std::move(folder)), staging(target + QStringLiteral(".part")) {}

ModelDownload::~ModelDownload() { ++generation; }

bool ModelDownload::installed(const QString& folder) {
    return QFileInfo::exists(QDir(folder).filePath(QStringLiteral("model.json")));
}

bool ModelDownload::remove(const QString& folder, QString* error) {
    bool ok = true;
    for (const QString& dir: {folder, folder + QStringLiteral(".part")}) {
        if (QFileInfo::exists(dir) && !QDir(dir).removeRecursively()) {
            ok = false;
        }
    }
    if (!ok && error) {
        *error = QStringLiteral("Could not remove %1").arg(folder);
    }
    return ok;
}

qint64 ModelDownload::sizeOnDisk(const QString& folder) {
    qint64 bytes = 0;
    QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        bytes += it.nextFileInfo().size();
    }
    return bytes;
}

void ModelDownload::start() {
    if (running()) {
        return;
    }
    ++generation;
    why.clear();
    if (!fetching.pinned()) {
        fail(fetching.unpinned.isEmpty() ? tr("This version of the app does not name the model's files yet.")
                                         : fetching.unpinned);
        return;
    }
    if (!QDir().mkpath(staging)) {
        fail(tr("Could not create %1").arg(staging));
        return;
    }
    now = State::Downloading;
    index = 0;
    done = 0;
    current = 0;
    Q_EMIT changed();
    next();
}

void ModelDownload::cancel() {
    if (!running()) {
        return;
    }
    ++generation;
    now = State::Cancelled;
    current = 0;
    Q_EMIT changed();
    Q_EMIT finished(false);
}

void ModelDownload::fail(const QString& reason) {
    ++generation;
    now = State::Failed;
    why = reason;
    current = 0;
    Q_EMIT changed();
    Q_EMIT finished(false);
}

void ModelDownload::next() {
    const Model& m = fetching;
    while (index < m.files.size()) {
        const File& f = m.files[index];
        const QString local = QDir(staging).filePath(f.path);
        if (!fileMatches(local, f)) {
            break;
        }
        done += f.size;  // (downloaded before: a download that went on)
        ++index;
    }
    if (index >= m.files.size()) {
        finish();
        return;
    }
    const File f = m.files[index];
    currentFile = f.path;
    current = 0;
    Q_EMIT changed();
    const quint64 gen = generation;
    const qint64 limit = (f.size > 0 ? f.size : 100 * 1024 * 1024) + 1024 * 1024;
    NetFetch::instance().download(
            m.urlOf(f), 60000, limit,
            [this, gen](qint64 received, qint64) {
                if (gen != generation) {
                    return false;  // (cancelled)
                }
                current = received;
                Q_EMIT changed();
                return true;
            },
            [this, gen, f](const NetFetch::Reply& r) {
                if (gen != generation) {
                    return;
                }
                if (!r.error.isEmpty() || r.status != 200) {
                    fail(r.error.isEmpty() ? tr("%1 answered with %2.").arg(r.url.host()).arg(r.status) : r.error);
                    return;
                }
                if ((f.size > 0 && r.body.size() != f.size) || sha256Of(r.body) != f.sha256.toLower()) {
                    fail(tr("%1 is not the file this version of the app expects (another size or checksum): "
                            "nothing was kept.")
                                 .arg(f.path));
                    return;
                }
                const QString local = QDir(staging).filePath(f.path);
                QDir().mkpath(QFileInfo(local).path());
                if (!fileio::writeFileAtomically(local, r.body)) {
                    fail(tr("Could not write %1").arg(local));
                    return;
                }
                done += f.size;
                current = 0;
                ++index;
                next();
            });
}

void ModelDownload::finish() {
    const Model& m = fetching;
    const bool ownManifest = std::any_of(m.files.begin(), m.files.end(),
                                         [](const File& f) { return f.path == QLatin1String("model.json"); });
    if (!ownManifest) {
        if (!fileio::writeFileAtomically(QDir(staging).filePath(QStringLiteral("model.json")), manifestOf(m))) {
            fail(tr("Could not write the model's manifest"));
            return;
        }
    }
    // In the model's place (a model there before goes)
    if (QFileInfo::exists(target) && !QDir(target).removeRecursively()) {
        fail(tr("Could not replace %1").arg(target));
        return;
    }
    if (!QDir().rename(staging, target)) {
        fail(tr("Could not move the model to %1").arg(target));
        return;
    }
    now = State::Done;
    currentFile.clear();
    Q_EMIT changed();
    Q_EMIT finished(true);
}

}  // namespace xqt

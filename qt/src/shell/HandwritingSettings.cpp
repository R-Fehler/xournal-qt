#include "HandwritingSettings.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "control/settings/Settings.h"
#include "hwr/HandwritingSearch.h"
#include "hwr/ModelInfo.h"
#include "session/AppContext.h"

#include "LibraryInkJob.h"
#include "ModelDownload.h"

namespace xqt {

HandwritingSettings::HandwritingSettings(AppContext& app, hwr::HandwritingSearch& search, LibraryInkJob* library,
                                         QObject* parent):
        QObject(parent), app(app), search(search), library(library) {
    connect(&search, &hwr::HandwritingSearch::enabledChanged, this, &HandwritingSettings::changed);
    connect(&search.service(), &hwr::InkRecognitionService::recognizerChanged, this, &HandwritingSettings::changed);
    connect(&search, &hwr::HandwritingSearch::progress, this, &HandwritingSettings::progressChanged);
    if (library) {
        connect(library, &LibraryInkJob::progress, this, &HandwritingSettings::progressChanged);
    }
}

HandwritingSettings::~HandwritingSettings() = default;

bool HandwritingSettings::enabled() const { return search.enabled(); }

void HandwritingSettings::setEnabled(bool on) {
    if (on == enabled()) {
        return;
    }
    hwr::HandwritingSearch::setEnabledIn(*app.getSettings(), on);
    search.applySettings();
    Q_EMIT changed();
}

QString HandwritingSettings::languages() const {
    return hwr::HandwritingSearch::languagesIn(*app.getSettings()).join(u'+');
}

void HandwritingSettings::setLanguages(const QString& value) {
    if (value == languages()) {
        return;
    }
    hwr::HandwritingSearch::setLanguagesIn(*app.getSettings(), value.split(u'+', Qt::SkipEmptyParts));
    search.applySettings();
    Q_EMIT changed();
}

bool HandwritingSettings::ready() const { return search.enabled() && search.service().ready(); }

namespace {
QString labelOf(const QString& language) {
    if (language == QLatin1String("en")) {
        return HandwritingSettings::tr("English");
    }
    if (language == QLatin1String("de")) {
        return HandwritingSettings::tr("German");
    }
    return language;
}

QString megabytes(qint64 bytes) {
    return HandwritingSettings::tr("%1 MB").arg(std::max<qint64>(bytes > 0 ? 1 : 0, (bytes + 512 * 1024) / (1024 * 1024)));
}
}  // namespace

QString HandwritingSettings::status() const {
    if (!search.enabled()) {
        return tr("Off");
    }
    if (anyDownloading()) {
        return tr("Downloading a model…");
    }
    const auto r = search.service().recognizer();
    QString why;
    if (!r) {
        return tr("This version of the app has no recogniser for handwriting.");
    }
    if (!r->ready(&why)) {
        return why.isEmpty() ? tr("The recogniser is not ready.") : why;
    }
    QString now;
    if (pagesWaiting() > 0) {
        now = tr("Reading handwriting: %n page(s) of open documents left", nullptr, pagesWaiting());
    } else if (libraryLeft() > 0) {
        now = tr("Reading the library's handwriting: %n document(s) left", nullptr, libraryLeft());
    } else if (onBattery()) {
        now = tr("Ready. The library's other documents are read on mains power.");
    } else {
        now = tr("Ready");
    }
    // A language without a model
    QStringList missing;
    for (const QString& l: search.choice().missing) {
        missing << labelOf(l);
    }
    if (!missing.isEmpty()) {
        now += u' ' + tr("No model for %1 yet: its handwriting is read with the other model only.")
                              .arg(missing.join(QStringLiteral(", ")));
    }
    return now;
}

ModelDownload* HandwritingSettings::downloadOf(const QString& language) const {
    auto it = fetches.find(language);
    return it != fetches.end() ? it->second.get() : nullptr;
}

bool HandwritingSettings::anyDownloading() const {
    return std::any_of(fetches.begin(), fetches.end(), [](const auto& f) { return f.second && f.second->running(); });
}

QVariantMap HandwritingSettings::modelOf(const QString& language) const {
    Settings& s = *app.getSettings();
    const QString folder = hwr::HandwritingSearch::modelDir(s, language);
    const bool own = QDir::cleanPath(folder) == QDir::cleanPath(hwr::HandwritingSearch::defaultModelDir(language));
    const hwr::ModelInfo info = hwr::ModelInfo::read(folder);
    const bool installed = info.valid() && info.reads(language);
    const bool needed = hwr::HandwritingSearch::languagesIn(s).contains(language);
    const QStringList inUse = search.enabled() ? search.modelFoldersInUse() : QStringList();
    const bool used = installed && std::any_of(inUse.begin(), inUse.end(), [&](const QString& f) {
                          return QDir::cleanPath(f) == QDir::cleanPath(folder);
                      });
    const ModelDownload::Model* model = ModelDownload::modelFor(language);
    const ModelDownload* fetch = downloadOf(language);
    const bool downloading = fetch && fetch->running();
    // No model of its own there (nor downloaded for another language): one that comes with the app reads it
    hwr::ModelInfo builtIn;
    if (own && !installed) {
        const QString bundledDir = search.bundledModelsDir();
        const auto chosen = search.choice().models;
        auto taken = std::find_if(chosen.begin(), chosen.end(), [&](const hwr::ModelInfo& m) { return m.reads(language); });
        if (taken != chosen.end()) {
            if (hwr::HandwritingSearch::isBundled(taken->folder, bundledDir)) {
                builtIn = *taken;
            }
        } else {
            // (a language not read now: the model that would read it)
            for (const hwr::ModelInfo& b: hwr::HandwritingSearch::bundledModels(bundledDir)) {
                if (b.reads(language)) {
                    builtIn = b;
                    break;
                }
            }
        }
    }
    const bool isBuiltIn = !builtIn.folder.isEmpty();
    const bool builtInUsed = isBuiltIn && std::any_of(inUse.begin(), inUse.end(), [&](const QString& f) {
                                 return QDir::cleanPath(f) == QDir::cleanPath(builtIn.folder);
                             });
    QString state;
    if (downloading) {
        state = tr("Downloading… %1 %")
                        .arg(fetch->bytesTotal() > 0 ? 100 * fetch->bytesDone() / fetch->bytesTotal() : 0);
    } else if (installed) {
        const QString size = megabytes(ModelDownload::sizeOnDisk(folder));
        state = used ? tr("In use (%1)").arg(size)
                     : needed ? tr("Installed (%1)").arg(size) : tr("Installed (%1), its language is not read").arg(size);
    } else if (isBuiltIn) {
        const QString size = megabytes(ModelDownload::sizeOnDisk(builtIn.folder));
        state = builtInUsed ? tr("Built in, in use (%1, %2)").arg(builtIn.name, size)
                            : tr("Built in (%1, %2)").arg(builtIn.name, size);
    } else if (!own && QFileInfo::exists(folder)) {
        state = info.valid() ? tr("The model in this folder does not read %1").arg(labelOf(language)) : info.error;
    } else {
        // Another model that reads it (one for both languages, or one found in the models folder)
        for (const QString& f: inUse) {
            const hwr::ModelInfo other = hwr::ModelInfo::read(f);
            if (other.valid() && other.reads(language)) {
                state = tr("Read by %1").arg(other.name);
                break;
            }
        }
        if (state.isEmpty()) {
            state = tr("Not installed");
        }
    }
    QVariantMap m;
    m[QStringLiteral("language")] = language;
    m[QStringLiteral("label")] = labelOf(language);
    m[QStringLiteral("needed")] = needed;
    m[QStringLiteral("name")] = model ? model->name : QString();
    m[QStringLiteral("folder")] = folder;
    m[QStringLiteral("own")] = own;
    m[QStringLiteral("installed")] = installed;
    m[QStringLiteral("inUse")] = used || builtInUsed;
    m[QStringLiteral("state")] = state;
    m[QStringLiteral("source")] = !model ? QString()
                                         : model->revision.isEmpty() ? model->source
                                                                     : model->source + QStringLiteral("/tree/") + model->revision;
    m[QStringLiteral("size")] = model ? megabytes(model->bytes()) : QString();
    m[QStringLiteral("downloadAvailable")] = model && model->pinned();
    m[QStringLiteral("unpinned")] = model && !model->pinned() ? model->unpinned : QString();
    m[QStringLiteral("downloading")] = downloading;
    m[QStringLiteral("progress")] = fetch && fetch->bytesTotal() > 0
                                            ? std::min(1.0, static_cast<double>(fetch->bytesDone()) /
                                                                    static_cast<double>(fetch->bytesTotal()))
                                            : 0.0;
    m[QStringLiteral("error")] = fetch ? fetch->error() : QString();
    m[QStringLiteral("builtIn")] = isBuiltIn;
    m[QStringLiteral("builtInName")] = builtIn.name;
    m[QStringLiteral("builtInFolder")] = builtIn.folder;
    m[QStringLiteral("builtInNoncommercial")] = builtIn.noncommercial;
    return m;
}

QVariantList HandwritingSettings::builtInModels() const {
    QVariantList out;
    for (const hwr::ModelInfo& info: hwr::HandwritingSearch::bundledModels(search.bundledModelsDir())) {
        QStringList languages;
        for (const QString& l: info.languages) {
            languages << labelOf(l);
        }
        const QString licence = QDir(info.folder).filePath(QStringLiteral("LICENCE.md"));
        QVariantMap m;
        m[QStringLiteral("name")] = info.name;
        m[QStringLiteral("languages")] = languages.join(QStringLiteral(", "));
        m[QStringLiteral("size")] = megabytes(ModelDownload::sizeOnDisk(info.folder));
        m[QStringLiteral("folder")] = info.folder;
        m[QStringLiteral("noncommercial")] = info.noncommercial;
        m[QStringLiteral("licenceFile")] = QFileInfo::exists(licence) ? licence : QString();
        out << m;
    }
    return out;
}

QString HandwritingSettings::licenceText(const QString& folder) const {
    // (only the notes of the models that come with the app)
    if (!hwr::HandwritingSearch::isBundled(folder, search.bundledModelsDir())) {
        return {};
    }
    QFile f(QDir(folder).filePath(QStringLiteral("LICENCE.md")));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QVariantList HandwritingSettings::models() const {
    QVariantList out;
    for (const hwr::HandwritingSearch::Slot& slot: hwr::HandwritingSearch::slots()) {
        out << modelOf(slot.language);
    }
    return out;
}

int HandwritingSettings::pagesWaiting() const { return search.pagesWaiting(); }

int HandwritingSettings::libraryLeft() const { return library ? library->documentsLeft() : 0; }

bool HandwritingSettings::onBattery() const { return !LibraryInkJob::onMains(); }

void HandwritingSettings::download(const QString& language) {
    const ModelDownload::Model* model = ModelDownload::modelFor(language);
    const QVariantMap m = modelOf(language);
    if (!model || !m.value(QStringLiteral("own")).toBool() || m.value(QStringLiteral("downloading")).toBool()) {
        return;  // (a model the user chose is never replaced)
    }
    auto& fetch = fetches[language];
    if (!fetch) {
        fetch = std::make_unique<ModelDownload>(*model, hwr::HandwritingSearch::defaultModelDir(language));
        connect(fetch.get(), &ModelDownload::changed, this, &HandwritingSettings::changed);
        connect(fetch.get(), &ModelDownload::finished, this, [this](bool ok) {
            if (ok) {
                search.reloadModel();  // (ready now: the documents are read)
            }
            Q_EMIT changed();
        });
    }
    fetch->start();
}

void HandwritingSettings::cancelDownload(const QString& language) {
    if (ModelDownload* fetch = downloadOf(language)) {
        fetch->cancel();
    }
}

bool HandwritingSettings::removeModel(const QString& language) {
    const QVariantMap m = modelOf(language);
    if (!m.value(QStringLiteral("own")).toBool() || m.value(QStringLiteral("downloading")).toBool()) {
        return false;
    }
    search.service().setRecognizer(nullptr);  // (its files go)
    const bool ok = ModelDownload::remove(m.value(QStringLiteral("folder")).toString());
    search.reloadModel();
    Q_EMIT changed();
    return ok;
}

void HandwritingSettings::chooseFolder(const QString& language, const QUrl& folder) {
    if (!hwr::HandwritingSearch::slotOf(language)) {
        return;
    }
    const QString path = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    hwr::HandwritingSearch::setModelDirIn(*app.getSettings(), language, path);
    search.reloadModel();
    Q_EMIT changed();
}

}  // namespace xqt

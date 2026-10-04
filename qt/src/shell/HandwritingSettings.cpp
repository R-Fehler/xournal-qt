#include "HandwritingSettings.h"

#include <QDir>

#include "hwr/HandwritingSearch.h"
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

bool HandwritingSettings::ready() const { return search.enabled() && search.service().ready(); }

QString HandwritingSettings::status() const {
    if (!search.enabled()) {
        return tr("Off");
    }
    if (downloading()) {
        return tr("Downloading the model…");
    }
    const auto r = search.service().recognizer();
    QString why;
    if (!r) {
        return tr("This version of the app has no recogniser for handwriting.");
    }
    if (!r->ready(&why)) {
        return why.isEmpty() ? tr("The recogniser is not ready.") : why;
    }
    if (pagesWaiting() > 0) {
        return tr("Reading handwriting: %n page(s) of open documents left", nullptr, pagesWaiting());
    }
    if (libraryLeft() > 0) {
        return tr("Reading the library's handwriting: %n document(s) left", nullptr, libraryLeft());
    }
    if (onBattery()) {
        return tr("Ready. The library's other documents are read on mains power.");
    }
    return tr("Ready");
}

QString HandwritingSettings::modelFolder() const { return search.modelFolderInUse(); }

bool HandwritingSettings::ownModel() const {
    return QDir::cleanPath(modelFolder()) == QDir::cleanPath(hwr::HandwritingSearch::defaultModelDir());
}

bool HandwritingSettings::modelInstalled() const { return ModelDownload::installed(modelFolder()); }

QString HandwritingSettings::downloadSource() const {
    const auto& m = ModelDownload::model();
    return m.revision.isEmpty() ? m.source : m.source + QStringLiteral("/tree/") + m.revision;
}

QString HandwritingSettings::downloadSize() const {
    return tr("%1 MB").arg(static_cast<int>((ModelDownload::model().bytes() + 512 * 1024) / (1024 * 1024)));
}

bool HandwritingSettings::downloadAvailable() const { return ModelDownload::model().pinned(); }

bool HandwritingSettings::downloading() const { return fetch && fetch->running(); }

double HandwritingSettings::downloadProgress() const {
    if (!fetch || fetch->bytesTotal() <= 0) {
        return 0;
    }
    return std::min(1.0, static_cast<double>(fetch->bytesDone()) / static_cast<double>(fetch->bytesTotal()));
}

QString HandwritingSettings::downloadError() const { return fetch ? fetch->error() : QString(); }

int HandwritingSettings::pagesWaiting() const { return search.pagesWaiting(); }

int HandwritingSettings::libraryLeft() const { return library ? library->documentsLeft() : 0; }

bool HandwritingSettings::onBattery() const { return !LibraryInkJob::onMains(); }

void HandwritingSettings::download() {
    if (!ownModel() || downloading()) {
        return;  // (a model the user chose is never replaced)
    }
    if (!fetch) {
        fetch = std::make_unique<ModelDownload>(hwr::HandwritingSearch::defaultModelDir());
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

void HandwritingSettings::cancelDownload() {
    if (fetch) {
        fetch->cancel();
    }
}

bool HandwritingSettings::removeModel() {
    if (!ownModel() || downloading()) {
        return false;
    }
    search.service().setRecognizer(nullptr);  // (its files go)
    const bool ok = ModelDownload::remove(modelFolder());
    search.reloadModel();
    Q_EMIT changed();
    return ok;
}

}  // namespace xqt

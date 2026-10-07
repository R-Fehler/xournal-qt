/*
 * xournal-qt: sharing a folder or the library as a zip, and "Open in library…" for a zip (qt/docs/library.md,
 * "Sharing a folder or the library"): the window's part of LibraryShare and LibraryUnzip.
 *
 * @license GNU GPLv2 or later
 */
#include <system_error>

#include <QFileInfo>

#include "AppController.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryShare.h"
#include "shell/LibraryUnzip.h"
#include "shell/SystemApps.h"

using namespace xqt;

namespace {
fs::path pathOf(const QString& s) { return fs::path(s.toStdU16String()); }
QString qstr(const fs::path& p) { return QString::fromStdU16String(p.u16string()); }
}  // namespace

QObject* AppController::libraryShareObject() const {
    if (!libraryShareTask) {
        auto* self = const_cast<AppController*>(this);
        self->libraryShareTask = std::make_unique<LibraryShare>();
    }
    return libraryShareTask.get();
}

void AppController::surveyShare(const QString& folder) {
    if (!library || !library->library()) {
        return;
    }
    libraryShareObject();
    const fs::path root = library->library()->root();
    libraryShareTask->startSurvey(folder.isEmpty() ? root : root / pathOf(folder));
}

bool AppController::shareAsZip(const QString& folder, const QVariantMap& options) {
    if (!library || !library->library() || !library->available()) {
        return false;
    }
    libraryShareObject();
    const fs::path root = library->library()->root();
    const fs::path source = folder.isEmpty() ? root : root / pathOf(folder);
    LibraryShare::Options o;
    o.format = LibraryShare::formatNamed(options.value("format").toString());
    o.readings = options.value("readings", true).toBool();
    o.pdfText = options.value("pdfText", false).toBool();
    o.history = options.value("history", false).toBool();
    o.recordings = options.value("recordings", true).toBool();
    o.password = options.value("password").toString().toStdString();
    const std::string name = folder.isEmpty() ? library->name().toStdString() : source.filename().string();
    std::string error;
    const bool ok = libraryShareTask->start(source, root, name, o, library->searchIndex(), error);
    std::fill(o.password.begin(), o.password.end(), '\0');
    if (!ok) {
        Q_EMIT message(tr("Share"), QString::fromStdString(error), true);
    }
    return ok;
}

void AppController::cancelShareZip() {
    if (libraryShareTask) {
        libraryShareTask->cancel();
    }
}

bool AppController::handOverZip(const QString& zip) {
    if (!QFileInfo::exists(zip)) {
        return false;
    }
    return handOver({zip}, false);
}

bool AppController::saveZipCopy(const QString& zip, const QUrl& folder) {
    const fs::path from = pathOf(zip);
    const fs::path dir = pathOf(folder.toLocalFile());
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec) || !fs::exists(from, ec)) {
        return false;
    }
    const std::string stem = from.stem().string();
    fs::path to = dir / from.filename();
    for (int n = 2; fs::exists(to, ec) && n < 10000; ++n) {
        to = dir / (stem + " (" + std::to_string(n) + ").zip");
    }
    fs::copy_file(from, to, ec);
    if (ec) {
        Q_EMIT message(tr("Share"), tr("The zip cannot be saved there: %1").arg(QString::fromStdString(ec.message())),
                       true);
        return false;
    }
    Q_EMIT pageActionDone(tr("%1 saved in %2").arg(qstr(to.filename()), qstr(dir.filename())), false);
    return true;
}

QObject* AppController::libraryUnzipObject() const {
    if (!libraryUnzipTask) {
        auto* self = const_cast<AppController*>(this);
        self->libraryUnzipTask = std::make_unique<LibraryUnzip>();
        connect(self->libraryUnzipTask.get(), &LibraryUnzip::finished, self, [self](const QVariantMap& r) {
            if (!r.value("ok").toBool() || !self->library || !self->library->library()) {
                return;
            }
            const fs::path folder = pathOf(r.value("folder").toString());
            self->library->refresh();
            const QString rel = QString::fromStdString(self->library->library()->relative(folder));
            self->library->setFolder(rel);
            self->setHomeVisible(true);
            const QStringList skipped = r.value("skipped").toStringList();
            Q_EMIT self->pageActionDone(
                    skipped.isEmpty() ? tr("Unpacked into %1").arg(rel)
                                      : tr("Unpacked into %1; left out: %2").arg(rel, skipped.join(QStringLiteral(", "))),
                    false);
        });
    }
    return libraryUnzipTask.get();
}

QVariantMap AppController::inspectZip(const QString& zip) const {
    const auto in = LibraryUnzip::inspect(pathOf(zip));
    return {{"ok", in.ok},
            {"error", QString::fromStdString(in.error)},
            {"files", in.files},
            {"bytes", static_cast<double>(in.bytes)},
            {"encrypted", in.encrypted},
            {"supported", in.supported},
            {"share", in.share},
            {"name", QString::fromStdString(in.name)}};
}

bool AppController::unzipIntoLibrary(const QString& zip, const QString& folder, const QString& password) {
    if (!library || !library->library() || !library->available()) {
        return false;
    }
    libraryUnzipObject();
    const Library* lib = library->library();
    const fs::path into = folder.isEmpty() ? lib->root() : lib->root() / pathOf(folder);
    std::string error;
    if (!libraryUnzipTask->start(pathOf(zip), into, lib->cacheLocation(), password.toStdString(), error)) {
        Q_EMIT message(tr("Open in library"), QString::fromStdString(error), true);
        return false;
    }
    return true;
}

void AppController::cancelUnzip() {
    if (libraryUnzipTask) {
        libraryUnzipTask->cancel();
    }
}

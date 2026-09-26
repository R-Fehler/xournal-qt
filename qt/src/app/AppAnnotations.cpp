/*
 * xournal-qt: the Annotations panel's "Export as Markdown" (qt/docs/annotations-md.md).
 *
 * @license GNU GPLv2 or later
 */
#include <shared_mutex>

#include <QPointer>
#include <QSaveFile>
#include <QUrl>

#include "AppController.h"
#include "model/Document.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "shell/Annotations.h"
#include "shell/AnnotationsModel.h"
#include "shell/DocumentLinks.h"
#include "shell/LocalUrl.h"

using namespace xqt;

namespace {
/// Handwriting as pictures in "<name>.assets/" instead of "(handwriting)": off while the Markdown editor does not
/// show images (qt/docs/annotations-md.md); XQT_ANNOTATION_PICTURES=1 turns it on to try.
bool inkPictures() {
    static const bool on = qEnvironmentVariableIntValue("XQT_ANNOTATION_PICTURES") == 1;
    return on;
}
}  // namespace

QUrl AppController::annotationsFile() const {
    DocumentSession* s = session();
    if (!s || s->documentFile().empty() || pdfOnly()) {
        return {};  // (PDF files: nothing is written next to files, the window asks)
    }
    return QUrl::fromLocalFile(QString::fromStdString(annotations::defaultFile(s->documentFile()).string()));
}

QUrl AppController::suggestedAnnotationsFile() const {
    DocumentSession* s = session();
    if (!s || s->documentFile().empty()) {
        return {};  // (never saved: the export says so)
    }
    return QUrl::fromLocalFile(QString::fromStdString(annotations::defaultFile(s->documentFile()).string()));
}

bool AppController::fileExists(const QUrl& file) const {
    std::error_code ec;
    return !file.isEmpty() && fs::exists(fs::path(localPathOf(file).toStdString()), ec);
}

void AppController::exportAnnotations(const QUrl& url) {
    QPointer<DocumentSession> s = session();
    if (!s) {
        return;
    }
    if (s->documentFile().empty()) {
        Q_EMIT annotationsExported(QString(), tr("Save the document first: the links lead to its file."));
        return;
    }
    const fs::path target(localPathOf(url).toStdString());
    if (target.empty()) {
        return;
    }
    annotations->whenCurrent([this, s, target] {
        if (!s || s != session()) {
            return;  // (another document meanwhile)
        }
        Document& doc = *s->getDocument();
        annotations::ExportInput input;
        input.document = s->documentFile();
        input.title = QString::fromStdString(input.document.stem().string());
        input.chapters = annotations::chaptersOf(doc);
        input.pages = DocumentLinks::pagesOf(doc);
        input.inkImages = inkPictures();
        const std::vector<annotations::Item>& items = annotations->all();
        std::vector<annotations::Picture> pictures;
        const std::string text = annotations::markdown(items, input, target, &pictures);

        const QString file = QString::fromStdString(target.string());
        QSaveFile out(file);
        if (!out.open(QIODevice::WriteOnly) || out.write(text.data(), static_cast<qint64>(text.size())) < 0 ||
            !out.commit()) {
            Q_EMIT annotationsExported(file, out.errorString());
            return;
        }
        if (!pictures.empty()) {
            std::error_code ec;
            fs::create_directories(annotations::assetsFolder(target), ec);
            for (const annotations::Picture& p: pictures) {
                const annotations::Item& item = items[p.item];
                PageRef page;
                {
                    std::shared_lock lock(doc);
                    if (item.page < doc.getPageCount()) {
                        page = doc.getPage(item.page);
                    }
                }
                if (page) {
                    annotations::drawArea(doc, page, annotations::pictureRect(item.rect), 3)
                            .save(QString::fromStdString((target.parent_path() / p.file.toStdString()).string()));
                }
            }
        }
        Q_EMIT annotationsExported(file, QString());
    });
}

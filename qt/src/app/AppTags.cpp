/*
 * xournal-qt: the library's tags (qt/docs/tags.md) as the window offers them: the Tags view of the library home, and
 * "Tags…" of a document (a card's menu, the document's ⋮ → Document): a PDF gets them as its keywords (an incremental
 * update of the file); other documents have the #tags typed in them.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <shared_mutex>

#include <QFileInfo>
#include <QPointer>

#include "AppController.h"
#include "AppServices.h"
#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/PdfKeywords.h"
#include "session/Tags.h"
#include "session/TextFile.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/LibraryTags.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
fs::path pathOf(const QString& s) { return fs::path(s.toStdString()).lexically_normal(); }
bool isPdfFile(const fs::path& p) {
    return QString::fromStdString(p.extension().string()).compare(QLatin1String(".pdf"), Qt::CaseInsensitive) == 0;
}
}  // namespace

QObject* AppController::libraryTagsModel() const { return libraryTags; }

QString AppController::currentDocumentPath() const {
    DocumentSession* s = tabs->currentSession();
    if (!s) {
        return {};
    }
    if (s->hasFilePath()) {
        return QString::fromStdString(s->getFilePath().string());
    }
    if (s->textFile()) {
        return QString::fromStdString(s->textFile()->path().string());
    }
    std::shared_lock lock(*s->getDocument());
    return QString::fromStdString(s->getDocument()->getPdfFilepath().string());  // (a plain PDF)
}

QVariantMap AppController::documentTags(const QString& path) const {
    const fs::path file = pathOf(path);
    const LibraryIndex* index = library ? library->searchIndex() : nullptr;
    QVariantMap m;
    const bool pdf = isPdfFile(file);
    m["pdf"] = pdf;
    m["name"] = QString::fromStdString(file.filename().string());
    m["typed"] = index ? index->textTagsOf(file) : QStringList();
    m["file"] = pdf ? pdfkeywords::tagsOf(file) : QStringList();
    m["suggestions"] = libraryTags ? libraryTags->allTags() : QStringList();
    QString why;
    if (!pdf) {
        why = DocumentFiles::isMarkdownFile(file)
                      ? tr("A Markdown file has the #tags typed in it, and those of its front matter (tags: [a, b]).")
                      : tr("Xournal++ files keep no tags of their own that Xournal++ would keep: type #tag in a text "
                           "box, a Markdown box or a sticky note of the document.");
    } else if (!QFileInfo(QString::fromStdString(file.string())).isWritable()) {
        why = tr("The PDF cannot be changed (it is read-only).");
    }
    m["why"] = why;
    m["editable"] = pdf && why.isEmpty();
    return m;
}

bool AppController::setDocumentTags(const QString& path, const QStringList& wanted) {
    const fs::path file = pathOf(path);
    const QString title = tr("Tags not changed");
    if (!isPdfFile(file)) {
        return false;
    }
    QStringList list;
    for (const QString& t: wanted) {
        if (const QString tag = tags::fromKeyword(t); !tag.isEmpty() && !tags::contains(list, tag)) {
            list << tag;
        }
    }
    // Open in a tab with changes: those are saved first (else the tab's next save would write over the keywords)
    for (const auto& [w, s]: appServices->openDocuments().find(file, {.textFiles = true, .plainPdf = true})) {
        if (s->isModified() || s->isSaving()) {
            Q_EMIT message(title, tr("%1 has unsaved changes: save it first, then give it tags.")
                                          .arg(QString::fromStdString(file.filename().string())),
                           false);
            return false;
        }
    }
    QPointer<AppController> self(this);
    appServices->jobs().start([self, file, list, title] {
        std::string error;
        const bool ok = pdfkeywords::write(file, list, error);
        QMetaObject::invokeMethod(qApp, [self, ok, error, file, title] {
            if (!self) {
                return;
            }
            if (!ok) {
                Q_EMIT self->message(title, QString::fromStdString(error), true);
                return;
            }
            // Tabs showing it read it again (as after a change by another app), the library reads its keywords
            for (const auto& [w, s]: self->appServices->openDocuments().find(file, {.textFiles = true, .plainPdf = true})) {
                if (!s->isModified()) {
                    w->reloadDocument(s);
                }
            }
            self->library->refresh();
        });
    }, BackgroundJobs::Priority::Idle);
    return true;
}

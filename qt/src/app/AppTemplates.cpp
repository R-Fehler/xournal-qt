/*
 * xournal-qt: page templates in the window (qt/templates, qt/docs/templates.md): the picker's list, saving a page as
 * a template (into the library's Templates folder or the app-wide set) and adding a template's page to a document or
 * starting a new document with it. A template's page is added as a copied page is pasted (PageClipboard): one undo
 * step, its PDF page into the document's merged PDF. The files are written and read off the UI thread; the work
 * holds only copies, never the open document.
 *
 * @license GNU GPLv2 or later
 */
#include <mutex>
#include <shared_mutex>

#include <QCoreApplication>
#include <QDate>
#include <QPointer>
#include <QThreadPool>

#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PdfPageKeeper.h"
#include "session/StickerFile.h"
#include "session/TemplateFile.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/PageClipboard.h"
#include "shell/PagesModel.h"
#include "shell/Stickers.h"
#include "shell/TabManager.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"

using namespace xqt;

QObject* AppController::templatesModel() const {
    if (!templateList) {
        templateList = std::make_unique<StickersModel>(stickers::Kind::Templates);
        syncTemplates();
        connect(library, &LibraryModel::libraryChanged, templateList.get(), [this] { syncTemplates(); });
    }
    return templateList.get();
}

void AppController::syncTemplates() const {
    const Library* lib = library ? library->library() : nullptr;
    templateList->setLibrary(lib ? lib->root() : fs::path(), lib ? lib->configDir() : fs::path());
}

bool AppController::canInsertTemplate() const {
    return session() && !session()->isReadOnly() && !textPagesFixed() && textDocument().isEmpty();
}

QVariantMap AppController::templateDraft(int page, bool appWide) const {
    QVariantMap draft;
    DocumentSession* s = session();
    bool offered = false;
    QString background = QStringLiteral("paper");
    if (s && page >= 0 && !textPagesFixed() && textDocument().isEmpty()) {
        Document* doc = s->getDocument();
        std::shared_lock lock(*doc);
        if (static_cast<size_t>(page) < doc->getPageCount()) {
            offered = true;
            const PageType type = doc->getPage(static_cast<size_t>(page))->getBackgroundType();
            background = type.isPdfPage()     ? QStringLiteral("pdf")
                         : type.isImagePage() ? QStringLiteral("image")
                                              : QStringLiteral("paper");
        }
    }
    draft.insert(QStringLiteral("offered"), offered);
    QString name;
    if (s) {
        fs::path shown(s->getDisplayName());
        name = tr("%1, page %2").arg(QString::fromStdString(shown.stem().string())).arg(page + 1);
    }
    if (name.trimmed().isEmpty()) {
        name = tr("Template %1").arg(QDate::currentDate().toString(Qt::ISODate));
    }
    draft.insert(QStringLiteral("name"), name);
    draft.insert(QStringLiteral("background"), background);
    auto* model = static_cast<StickersModel*>(templatesModel());
    const fs::path set = model->rootOf(appWide || !model->hasLibrary() ? QStringLiteral("app") : QStringLiteral("library"));
    draft.insert(QStringLiteral("folders"), stickers::folders(set));
    draft.insert(QStringLiteral("hasLibrary"), model->hasLibrary());
    return draft;
}

bool AppController::saveTemplate(int page, const QString& name, const QString& folder, bool withBackground,
                                 bool withContent, bool appWide) {
    DocumentSession* s = session();
    if (!s || page < 0 || (!withBackground && !withContent) || !templateDraft(page).value("offered").toBool()) {
        return false;
    }
    auto* model = static_cast<StickersModel*>(templatesModel());
    fs::path into = model->rootOf(appWide || !model->hasLibrary() ? QStringLiteral("app") : QStringLiteral("library"));
    if (into.empty() || folder.contains(QLatin1String(".."))) {
        return false;
    }
    for (const QString& part: folder.split('/', Qt::SkipEmptyParts)) {
        const std::string n = stickers::fileNameOf(part.toStdString());
        if (!n.empty()) {
            into /= n;
        }
    }
    std::error_code ec;
    fs::create_directories(into, ec);
    if (ec) {
        Q_EMIT pageActionDone(tr("The template could not be saved: %1").arg(QString::fromStdString(ec.message())),
                              false);
        return false;
    }
    std::string file = stickers::fileNameOf(name.trimmed().toStdString());
    if (file.empty()) {
        file = tr("Template %1").arg(QDate::currentDate().toString(Qt::ISODate)).toStdString();
    }
    const fs::path target = stickers::uniqueTarget(into, file);

    // The page as page copy takes it: its PDF page as a PDF of one page (qt/docs/templates.md)
    PageClipboard copy;
    copy.copy(*s, {static_cast<size_t>(page)}, /*withPdf=*/withBackground);
    PageClipboard::Copied copied = copy.copied(0);
    if (!copied.page) {
        return false;
    }
    PageRef tpl = templates::makePage(copied.page, {withBackground, withContent});
    if (tpl->getBackgroundType().isPdfPage() && copied.pdf.empty()) {
        // (its PDF page could not be copied: a picture of it, as a pasted page gets then)
        if (!copied.pdfPage || !PdfPageKeeper::toImageBackground(*tpl, *copied.pdfPage, PageClipboard::IMAGE_DPI)) {
            tpl->setBackgroundType(PageType(PageTypeFormat::Plain));
        }
    }
    QPointer<AppController> self(this);
    const QString path = QString::fromStdString(target.string());
    const QString shown = QString::fromStdString(target.stem().string());
    QThreadPool::globalInstance()->start([self, tpl, pdf = std::move(copied.pdf), target, path, shown]() mutable {
        std::string error;
        const bool ok = templates::write(std::move(tpl), pdf, target, &error);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, ok, error, path, shown] {
            if (!self) {
                return;
            }
            if (!ok) {
                const QString why = QString::fromStdString(error);
                Q_EMIT self->pageActionDone(tr("The template could not be saved: %1").arg(why), false);
                Q_EMIT self->templateSaved(QString(), why.isEmpty() ? QStringLiteral("not written") : why);
                return;
            }
            if (self->templateList) {
                self->templateList->refresh();
            }
            Q_EMIT self->pageActionDone(tr("Saved template “%1”").arg(shown), false);
            Q_EMIT self->templateSaved(path, QString());
        });
    });
    return true;
}

void AppController::readTemplate(const QString& path,
                                 std::function<void(std::shared_ptr<PageClipboard>, bool, const QString&)> then) {
    QPointer<AppController> self(this);
    const fs::path file(path.toStdString());
    QThreadPool::globalInstance()->start([self, file, then = std::move(then)] {
        std::string error;
        bool without = false;
        auto copy = std::make_shared<PageClipboard>();
        auto loaded = DocumentSession::loadFile(file);
        if (!loaded.document) {
            error = loaded.error.empty() ? std::string("not readable") : loaded.error;
        } else {
            copy->copy(*loaded.document, {0});  // (its PDF page as a PDF in memory: the file may go)
            if (copy->isEmpty()) {
                error = "no page";
            } else {
                std::shared_lock lock(*loaded.document);
                without = templates::withoutBackground(*loaded.document->getPage(0));
            }
        }
        loaded.document.reset();
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, copy, without, error, then] {
            if (self) {
                then(error.empty() ? copy : nullptr, without, QString::fromStdString(error));
            }
        });
    });
}

std::vector<PageRef> AppController::templatePagesFor(PageClipboard& copy, bool withoutBackground, DocumentSession& s,
                                                     int count, bool* addedToMergedPdf) {
    // The background new pages get here: the current page's paper, or (a PDF page, a picture) the one for new pages
    PageType type;
    Color color;
    {
        Document* doc = s.getDocument();
        std::shared_lock lock(*doc);
        const PageRef current = doc->getPage(std::min(s.getCurrentPageNo(), doc->getPageCount() - 1));
        type = current->getBackgroundType();
        color = current->getBackgroundColor();
    }
    if (type.isSpecial()) {
        const auto& settings = app->getSettings()->getPageTemplateSettings();
        type = settings.getBackgroundType();
        color = settings.getBackgroundColor();
        if (type.isSpecial()) {
            type = PageType(PageTypeFormat::Plain);
        }
    }
    std::vector<PageRef> pages;
    for (int i = 0; i < count; ++i) {
        for (PageRef p: copy.pagesFor(s, addedToMergedPdf)) {
            if (withoutBackground) {
                p->setBackgroundType(type);
                p->setBackgroundColor(color);
            }
            pages.push_back(std::move(p));
        }
    }
    return pages;
}

bool AppController::insertTemplate(const QString& path, int position, int count) {
    if (!canInsertTemplate() || path.isEmpty() || count < 1) {
        return false;
    }
    QPointer<DocumentSession> target(session());
    readTemplate(path, [this, target, path, position, count](std::shared_ptr<PageClipboard> copy, bool without,
                                                             const QString& error) {
        if (!copy || !target) {
            const QString why = error.isEmpty() ? QStringLiteral("the document was closed") : error;
            Q_EMIT pageActionDone(tr("The template could not be read: %1").arg(why), false);
            Q_EMIT templateInserted(path, 0, why);
            return;
        }
        DocumentSession* s = target.data();
        bool keptIn = false;
        const auto pages = templatePagesFor(*copy, without, *s, count, &keptIn);
        const size_t pageCount = s->getDocument()->getPageCount();
        const size_t at = position < 0 ? std::min(s->getCurrentPageNo() + 1, pageCount)
                                       : std::min(static_cast<size_t>(position), pageCount);
        s->clearSelectionEndText();
        s->insertPages(pages, at);
        s->setCurrentPageNo(at);
        s->getScrollHandler()->scrollToPage(at);
        if (templateList) {
            templateList->markUsed(path);
        }
        const QString name = QString::fromStdString(fs::path(path.toStdString()).stem().string());
        const int n = static_cast<int>(pages.size());
        Q_EMIT pageActionDone(n == 1 ? tr("Page added from the template “%1”").arg(name)
                                     : tr("%1 pages added from the template “%2”").arg(n).arg(name),
                              true);
        Q_EMIT templateInserted(path, n, QString());
    });
    return true;
}

bool AppController::createDocumentFromTemplate(const QString& name, bool inLibrary, const QString& path) {
    if (path.isEmpty()) {
        return createDocument(name, inLibrary);
    }
    readTemplate(path, [this, name, inLibrary, path](std::shared_ptr<PageClipboard> copy, bool without,
                                                     const QString& error) {
        if (!copy) {
            Q_EMIT message(tr("New document"), tr("The template could not be read: %1").arg(error), true);
            Q_EMIT templateInserted(path, 0, error);
            return;
        }
        auto s = std::make_unique<DocumentSession>(*app);
        DocumentSession* created = s.get();
        tabs->addTab(std::move(s));
        setHomeVisible(false);
        // Its first page is the template's (a new document has nothing to undo)
        const auto pages = templatePagesFor(*copy, without, *created, 1, nullptr);
        created->insertPages(pages, 0);
        created->deletePages({pages.size()});
        created->getPageUndoRedoHandler()->clearContents();
        created->setCurrentPageNo(0);
        if (templateList) {
            templateList->markUsed(path);
        }
        const bool saved = saveNewDocument(*created, name, inLibrary);
        Q_EMIT templateInserted(path, saved ? static_cast<int>(pages.size()) : 0,
                                saved ? QString() : QStringLiteral("not saved"));
    });
    return true;
}

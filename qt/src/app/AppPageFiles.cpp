/*
 * xournal-qt: pages as files in the window (qt/page-files, qt/docs/page-files.md): inserting pages from a PDF or
 * another document (A6), extracting the selected pages into a new document or splitting the document into several
 * (A7), and exporting pages as pictures (A8).
 *
 * - Inserting reads the file on a worker (asking for the password of a protected PDF) and keeps it while the dialog is
 *   open; the pages chosen are then copied as copied pages are (PageClipboard): their PDF pages join the document's
 *   merged PDF, so their text stays searchable; one undo step.
 * - Extracting and splitting copy the pages (PageFiles.h) on the UI thread and write the files on a worker: a PDF
 *   with notes, or a .xopp with its PDF; a protected document gives PDFs protected with its password, never a .xopp
 *   (qt/docs/hybrid-pdf.md, "Encrypted PDFs").
 * - Pictures are drawn as RegionRender draws a snip, in the normal colours (no dark pages), one page at a time on a
 *   worker. A protected document is not exported as pictures: they could not keep its password.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "render/RegionImage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/PageFiles.h"
#include "session/PdfEncryption.h"
#include "session/VersionCache.h"
#include "shell/DocumentFiles.h"
#include "shell/HitPages.h"
#include "shell/LibraryModel.h"
#include "shell/LocalUrl.h"
#include "shell/OutlineModel.h"
#include "shell/PageClipboard.h"
#include "shell/PagesModel.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "AppServices.h"

using namespace xqt;

/// The file pages are inserted from, as read (its document and, for a protected PDF, the hold of its password)
struct PageFileSource {
    fs::path file;
    DocumentSession::LoadResult loaded;
};

namespace {
constexpr const char* SETTINGS = "xournalQt";  // (AppController.cpp's CUSTOM)
/// The most pixels of one exported picture (a poster at 600 dpi gets less)
constexpr double MAX_IMAGE_PIXELS = 64.0 * 1024 * 1024;
/// The most pixels of a picture of a page on the clipboard (A4 at 600 dpi is about 35 megapixels: a little less)
constexpr double MAX_CLIPBOARD_PIXELS = 32.0 * 1024 * 1024;
/// The resolution of pages as pictures until one is chosen
constexpr int DEFAULT_DPI = 300;

QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

std::string stemOf(const DocumentSession& s) {
    std::string stem = fs::path(s.getDisplayName()).stem().string();
    return stem.empty() ? std::string("Untitled") : stem;
}

/// A chapter's title as part of a file name: no path separators or characters some systems forbid
std::string fileNamePart(const std::string& title) {
    std::string out;
    for (char c: title) {
        out += std::string("/\\:*?\"<>|").find(c) != std::string::npos || static_cast<unsigned char>(c) < 32 ? ' ' : c;
    }
    const size_t a = out.find_first_not_of(" .");
    if (a == std::string::npos) {
        return {};
    }
    out = out.substr(a);
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    if (out.size() > 60) {  // (whole UTF-8 characters)
        size_t cut = 60;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        out.resize(cut);
    }
    return out;
}
}  // namespace

// --- inserting pages from a file (A6) ---------------------------------------------------------------------------

bool AppController::readPageFile(const QUrl& url, const QString& password) {
    if (!canInsertTemplate()) {
        return false;
    }
    const QString path = localPathOf(url);
    if (path.isEmpty()) {
        return false;
    }
    const quint64 request = ++pageFileReads;
    QPointer<AppController> self(this);
    const fs::path file(path.toStdString());
    appServices->jobs().start([self, file, request, pw = password.toStdString()]() mutable {
        auto source = std::make_shared<PageFileSource>();
        source->file = file;
        source->loaded = DocumentSession::loadFile(file, false, pw);
        std::fill(pw.begin(), pw.end(), '\0');
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, source, request] {
            if (!self || request != self->pageFileReads) {
                return;  // (another file was chosen meanwhile)
            }
            const DocumentSession::LoadResult& r = source->loaded;
            QVariantMap info;
            info.insert(QStringLiteral("name"), QString::fromStdString(source->file.filename().string()));
            info.insert(QStringLiteral("needsPassword"), r.needsPassword);
            info.insert(QStringLiteral("wrongPassword"), r.wrongPassword);
            if (r.document) {
                size_t pages = 0;
                {
                    std::shared_lock lock(*r.document);
                    pages = r.document->getPageCount();
                }
                info.insert(QStringLiteral("ok"), pages > 0);
                info.insert(QStringLiteral("pages"), static_cast<int>(pages));
                info.insert(QStringLiteral("protectedFile"), r.encrypted);
                // The pictures of its pages as the library's search draws them; none of a protected file (they are
                // drawn without its password)
                info.insert(QStringLiteral("thumbnails"),
                            r.encrypted ? QString()
                                        : HitPageProvider::baseUrl(DocumentFiles::itemOf(source->file), QString()));
                if (pages == 0) {
                    info.insert(QStringLiteral("error"), tr("It has no pages."));
                }
                self->pageFile = pages > 0 ? source : nullptr;
            } else {
                info.insert(QStringLiteral("ok"), false);
                if (!r.needsPassword) {
                    info.insert(QStringLiteral("error"), r.error.empty() ? tr("It could not be read.")
                                                                         : QString::fromStdString(r.error));
                }
                self->pageFile.reset();
            }
            Q_EMIT self->pageFileRead(info);
        });
    }, BackgroundJobs::Priority::Normal);
    return true;
}

QString AppController::checkPageRange(const QString& range, int count) const {
    std::string error;
    if (pagefiles::parseRange(range.toStdString(), static_cast<size_t>(std::max(0, count)), &error).empty()) {
        return QString::fromStdString(error);
    }
    return {};
}

void AppController::closePageFile() {
    ++pageFileReads;  // (a read still running is not kept)
    pageFile.reset();
}

bool AppController::insertPagesFromFile(const QString& range, const QList<int>& picked, int position) {
    DocumentSession* s = session();
    if (!pageFile || !s || !canInsertTemplate()) {
        return false;
    }
    std::shared_ptr<PageFileSource> source = pageFile;
    size_t count = 0;
    {
        std::shared_lock lock(*source->loaded.document);
        count = source->loaded.document->getPageCount();
    }
    std::vector<size_t> pages;
    if (!picked.isEmpty()) {
        for (int p: picked) {
            if (p >= 0 && static_cast<size_t>(p) < count) {
                pages.push_back(static_cast<size_t>(p));
            }
        }
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    } else {
        std::string error;
        pages = pagefiles::parseRange(range.trimmed().isEmpty() ? std::string("1-") : range.toStdString(), count,
                                      &error);
        if (pages.empty()) {
            Q_EMIT pagesFromFileInserted(0, QString::fromStdString(error));
            return false;
        }
    }
    if (pages.empty()) {
        Q_EMIT pagesFromFileInserted(0, tr("No pages chosen"));
        return false;
    }
    QPointer<AppController> self(this);
    QPointer<DocumentSession> target(s);
    appServices->jobs().start([self, target, source, pages, position] {
        // Copied as copied pages are: the PDF pages as a PDF in memory (with the file's password while it is held)
        auto copy = std::make_shared<PageClipboard>();
        copy->copy(*source->loaded.document, pages);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, target, source, copy, position] {
            if (!self) {
                return;
            }
            if (self->pageFile == source) {
                self->pageFile.reset();  // (inserted: the file is let go)
            }
            if (!target || copy->isEmpty()) {
                Q_EMIT self->pagesFromFileInserted(0, target ? tr("No pages could be read") : tr("The document was closed"));
                return;
            }
            DocumentSession& doc = *target;
            bool keptIn = false;
            auto copies = copy->pagesFor(doc, &keptIn);
            const size_t at = std::min(static_cast<size_t>(std::max(0, position)), doc.getDocument()->getPageCount());
            doc.clearSelectionEndText();
            doc.insertPages(copies, at);
            const int n = static_cast<int>(copies.size());
            if (&doc == self->session()) {
                QList<int> inserted;
                for (int i = 0; i < n; ++i) {
                    inserted.append(static_cast<int>(at) + i);
                }
                self->pages->selectPages(inserted);
            }
            doc.setCurrentPageNo(at);
            doc.getScrollHandler()->scrollToPage(at);
            const QString name = QString::fromStdString(source->file.filename().string());
            Q_EMIT self->pageActionDone(n == 1 ? tr("1 page inserted from “%1”").arg(name)
                                               : tr("%1 pages inserted from “%2”").arg(n).arg(name),
                                        true);
            Q_EMIT self->pagesFromFileInserted(n, QString());
        });
    }, BackgroundJobs::Priority::Normal);
    return true;
}

// --- extract and split (A7) -------------------------------------------------------------------------------------

fs::path AppController::pageFilesFolder(DocumentSession& s) const {
    const fs::path file = s.documentFile();
    std::error_code ec;
    if (!file.empty() && !VersionCache::instance().contains(file) && fs::is_directory(file.parent_path(), ec)) {
        return file.parent_path();
    }
    if (library->available()) {
        return fs::path(library->newDocumentPath(QStringLiteral("x")).toStdString()).parent_path();
    }
    const QString last = localPathOf(openFolder());
    return last.isEmpty() ? fs::path(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation).toStdString())
                          : fs::path(last.toStdString());
}

QVariantMap AppController::extractDraft(const QList<int>& list) const {
    QVariantMap draft;
    DocumentSession* s = session();
    const bool offered = s && !textPagesFixed() && !s->textFile();
    draft.insert(QStringLiteral("offered"), offered);
    if (!offered) {
        return draft;
    }
    const auto indices = pageList(list);
    const std::string range = pagefiles::rangeText(indices);
    const std::string stem = stemOf(*s);
    const QString name = indices.size() == 1
                                 ? tr("%1 (page %2)").arg(QString::fromStdString(stem)).arg(indices.front() + 1)
                                 : tr("%1 (pages %2)").arg(QString::fromStdString(stem), QString::fromStdString(range));
    draft.insert(QStringLiteral("name"), name);
    draft.insert(QStringLiteral("range"), QString::fromStdString(range));
    draft.insert(QStringLiteral("count"), static_cast<int>(indices.size()));
    draft.insert(QStringLiteral("folder"), qstr(pageFilesFolder(*s)));
    const bool isProtected = s->isProtected();
    draft.insert(QStringLiteral("xoppAllowed"), !isProtected);
    draft.insert(QStringLiteral("protectedDocument"), isProtected);
    // As new documents are (PDF files mode), and as the document is: a PDF stays a PDF
    std::string ext = s->documentFile().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    draft.insert(QStringLiteral("asPdf"), isProtected || pdfOnly() || ext == ".pdf");
    return draft;
}

void AppController::writePageFiles(DocumentSession& s, std::vector<std::pair<fs::path, std::vector<size_t>>> files,
                                   std::function<void(const QStringList&, const QString&)> then) {
    // The pages as they are now (pages pasted just now: once their PDF pages are in the merged PDF)
    s.clearSelectionEndText();
    s.waitForMerges();
    struct Job {
        fs::path target;
        std::shared_ptr<Document> doc;
    };
    std::vector<Job> jobs;
    for (auto& [target, pages]: files) {
        jobs.push_back({target, pagefiles::subset(*s.getDocument(), pages)});
    }
    size_t pdfPages = 0;
    {
        std::shared_lock lock(*s.getDocument());
        pdfPages = s.getDocument()->getPdfPageCount();
    }
    // (a protected document: encrypted with its password, also pages without a PDF page)
    const PdfEncryption::Encryption encryption = s.encryptionForSave();
    QPointer<AppController> self(this);
    appServices->jobs().start([self, jobs = std::move(jobs), pdfPages, encryption, then = std::move(then)] {
        QStringList written;
        QString error;
        for (const Job& job: jobs) {
            const auto r = pagefiles::write(*job.doc, pdfPages, job.target, encryption);
            if (!r.ok) {
                error = QString::fromStdString(r.error.empty() ? std::string("not written") : r.error);
                break;
            }
            written << qstr(job.target);
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, written, error, then] {
            if (self) {
                then(written, error);
            }
        });
    }, BackgroundJobs::Priority::Normal);
}

bool AppController::extractPages(const QList<int>& list, const QString& name, bool asPdf, bool remove) {
    DocumentSession* s = session();
    if (!s || !extractDraft(list).value(QStringLiteral("offered")).toBool()) {
        return false;
    }
    if (s->isProtected() && !asPdf) {
        Q_EMIT message(tr("Extract pages"),
                       tr("This document is protected with a password, and Xournal++ files (.xopp) cannot be. The "
                          "pages go into a PDF with notes, protected with the same password."),
                       true);
        return false;
    }
    const auto indices = pageList(list);
    if (remove && indices.size() >= s->getDocument()->getPageCount()) {
        Q_EMIT message(tr("Extract pages"), tr("A document keeps at least one page."), false);
        return false;
    }
    const fs::path folder = pageFilesFolder(*s);
    std::string stem = name.trimmed().toStdString();
    if (!DocumentFiles::validName(stem)) {
        stem = extractDraft(list).value(QStringLiteral("name")).toString().toStdString();
    }
    const fs::path target = folder / (DocumentFiles::uniqueName(folder, stem) + (asPdf ? ".pdf" : ".xopp"));
    const fs::path passwordFrom = s->isProtected() ? s->encryptionForSave().from : fs::path();
    QPointer<DocumentSession> source(s);
    writePageFiles(*s, {{target, indices}},
                   [this, source, indices, remove, target, passwordFrom](const QStringList& files, const QString& error) {
                       if (files.isEmpty()) {
                           Q_EMIT message(tr("Extract pages"),
                                          tr("The new document could not be written: %1").arg(error), true);
                           Q_EMIT pagesExtracted({}, error);
                           return;
                       }
                       if (remove && source) {
                           // Out of this document: one undo step
                           source->deletePages(indices);
                           if (source == session()) {
                               pages->clearSelection();
                           }
                       }
                       library->refresh();
                       if (!passwordFrom.empty()) {
                           PdfEncryption::derive(target, passwordFrom);  // (opened with the same password)
                       }
                       openPath(qstr(target));
                       const int n = static_cast<int>(indices.size());
                       const QString shown = QString::fromStdString(target.filename().string());
                       Q_EMIT pageActionDone(n == 1 ? tr("1 page extracted to “%1”").arg(shown)
                                                    : tr("%1 pages extracted to “%2”").arg(n).arg(shown),
                                             false);
                       Q_EMIT pagesExtracted(files, QString());
                   });
    return true;
}

std::vector<std::pair<std::string, std::vector<size_t>>> AppController::splitParts(const QString& mode, int every,
                                                                                  const QList<int>& list,
                                                                                  QString* error) const {
    std::vector<std::pair<std::string, std::vector<size_t>>> parts;
    DocumentSession* s = session();
    if (!s || textPagesFixed() || s->textFile()) {
        return parts;
    }
    const size_t count = s->getDocument()->getPageCount();
    const std::string stem = stemOf(*s);
    std::vector<std::vector<size_t>> pages;
    std::vector<std::string> titles;  // (chapters: by the page a part starts at)
    std::vector<size_t> titled;
    if (mode == QLatin1String("every")) {
        pages = pagefiles::splitEvery(count, static_cast<size_t>(std::max(1, every)));
    } else if (mode == QLatin1String("selected")) {
        std::vector<size_t> starts;
        for (int p: list) {
            if (p >= 0) {
                starts.push_back(static_cast<size_t>(p));
            }
        }
        pages = pagefiles::splitAt(count, starts);
    } else if (mode == QLatin1String("chapters")) {
        // The chapters of its table of contents (the PDF's outline, else the headings written in it): the top level
        std::vector<size_t> starts;
        for (int row = 0; outline && row < outline->rowCount(); ++row) {
            const QModelIndex i = outline->index(row);
            const int page = outline->data(i, OutlineModel::PageRole).toInt();
            if (outline->data(i, OutlineModel::LevelRole).toInt() == 0 && page >= 0 &&
                std::find(starts.begin(), starts.end(), static_cast<size_t>(page)) == starts.end()) {
                starts.push_back(static_cast<size_t>(page));
                titled.push_back(static_cast<size_t>(page));
                titles.push_back(outline->data(i, OutlineModel::TitleRole).toString().toStdString());
            }
        }
        if (starts.empty()) {
            if (error) {
                *error = tr("This document has no chapters.");
            }
            return parts;
        }
        pages = pagefiles::splitAt(count, starts);
    }
    if (pages.size() < 2) {
        if (error) {
            *error = tr("That gives only one part: nothing to split.");
        }
        return parts;
    }
    std::vector<std::string> used;
    for (size_t i = 0; i < pages.size(); ++i) {
        std::string name;
        if (auto it = std::find(titled.begin(), titled.end(), pages[i].front()); it != titled.end()) {
            if (const std::string t = fileNamePart(titles[static_cast<size_t>(it - titled.begin())]); !t.empty()) {
                name = stem + " - " + t;
            }
        }
        if (name.empty()) {
            name = tr("%1 (part %2)").arg(QString::fromStdString(stem)).arg(i + 1).toStdString();
        }
        // (two chapters of one title: two names)
        const std::string base = name;
        for (int n = 2; std::find(used.begin(), used.end(), name) != used.end(); ++n) {
            name = base + " (" + std::to_string(n) + ")";
        }
        used.push_back(name);
        parts.emplace_back(name, pages[i]);
    }
    return parts;
}

QVariantMap AppController::splitPlan(const QString& mode, int every, const QList<int>& list) const {
    QVariantMap plan;
    QString error;
    QVariantList parts;
    for (const auto& [name, pages]: splitParts(mode, every, list, &error)) {
        QVariantMap part;
        part.insert(QStringLiteral("name"), QString::fromStdString(name));
        part.insert(QStringLiteral("range"), QString::fromStdString(pagefiles::rangeText(pages)));
        part.insert(QStringLiteral("count"), static_cast<int>(pages.size()));
        parts.append(part);
    }
    plan.insert(QStringLiteral("parts"), parts);
    plan.insert(QStringLiteral("error"), error);
    return plan;
}

bool AppController::splitDocument(const QString& mode, int every, const QList<int>& list, bool asPdf) {
    DocumentSession* s = session();
    QString why;
    auto parts = splitParts(mode, every, list, &why);
    if (!s || parts.empty()) {
        if (!why.isEmpty()) {
            Q_EMIT message(tr("Split"), why, false);
        }
        return false;
    }
    if (s->isProtected() && !asPdf) {
        Q_EMIT message(tr("Split"),
                       tr("This document is protected with a password, and Xournal++ files (.xopp) cannot be. The "
                          "parts are PDFs with notes, protected with the same password."),
                       true);
        return false;
    }
    const fs::path folder = pageFilesFolder(*s);
    std::vector<std::pair<fs::path, std::vector<size_t>>> files;
    for (auto& [name, pages]: parts) {
        std::string stem = DocumentFiles::uniqueName(folder, name);
        files.emplace_back(folder / (stem + (asPdf ? ".pdf" : ".xopp")), std::move(pages));
    }
    const QString shownFolder = QString::fromStdString(folder.filename().string());
    writePageFiles(*s, std::move(files), [this, shownFolder](const QStringList& written, const QString& error) {
        if (!written.isEmpty()) {
            library->refresh();
        }
        if (!error.isEmpty()) {
            // (the parts written before stay: they are whole documents)
            Q_EMIT message(tr("Split"), tr("Not every part could be written: %1").arg(error), true);
            Q_EMIT pagesExtracted(written, error);
            return;
        }
        Q_EMIT pageActionDone(tr("Split into %1 documents in “%2”").arg(written.size()).arg(shownFolder), false);
        Q_EMIT pagesExtracted(written, QString());
    });
    return true;
}

// --- pages as pictures (A8) -------------------------------------------------------------------------------------

namespace {
/// Pages to draw off the UI thread: copies of them (the document may change or close meanwhile) with their PDF pages
struct PagePictures {
    std::shared_ptr<Document> copies;
    std::vector<XojPdfPageSPtr> pdfPages;
    std::vector<size_t> indices;  ///< their pages in the document
    size_t pageCount = 0;         ///< of the document
};

PagePictures picturesOf(DocumentSession& s, const std::vector<size_t>& wanted) {
    // (pages pasted just now: once their PDF pages are in the merged PDF)
    s.clearSelectionEndText();
    s.waitForMerges();
    PagePictures p;
    Document& doc = *s.getDocument();
    {
        std::shared_lock lock(doc);
        p.pageCount = doc.getPageCount();
        for (size_t i: wanted) {
            if (i < p.pageCount) {
                const PageRef page = doc.getPage(i);
                p.indices.push_back(i);
                p.pdfPages.push_back(page->getBackgroundType().isPdfPage() ? doc.getPdfPage(page->getPdfPageNr())
                                                                           : nullptr);
            }
        }
    }
    p.copies = pagefiles::subset(doc, p.indices);
    return p;
}

/// Page `i` of them as a picture at `dpi` (less when it would have more than `maxPixels`: `capped`), in the normal
/// colours; on white paper unless `paper` is false (then transparent where nothing is drawn). Any thread.
QImage drawPage(const PagePictures& p, size_t i, int dpi, bool paper, bool opaque, double maxPixels, bool* capped) {
    PageRef page;
    {
        std::shared_lock lock(*p.copies);
        page = p.copies->getPage(i);
    }
    region::Request request;
    request.area = xoj::util::Rectangle<double>(0, 0, page->getWidth(), page->getHeight());
    request.scale = region::scaleFor(request.area, dpi / 72.0, 0, maxPixels);
    if (capped) {
        *capped = request.scale < dpi / 72.0 - 1e-9;
    }
    request.forScreen = false;  // (as printed: the normal colours, also in dark mode)
    request.paper = paper;
    QImage image = region::renderImage(*p.copies, page, request, p.pdfPages[i]);
    if (image.isNull() || !paper) {
        return image;
    }
    // On white paper (a PDF page that paints none: white as on the screen; JPEG has no transparency)
    QImage onWhite(image.size(), opaque ? QImage::Format_RGB32 : QImage::Format_ARGB32_Premultiplied);
    onWhite.setDotsPerMeterX(image.dotsPerMeterX());
    onWhite.setDotsPerMeterY(image.dotsPerMeterY());
    onWhite.fill(Qt::white);
    QPainter painter(&onWhite);
    painter.drawImage(0, 0, image);
    painter.end();
    return onWhite;
}

QByteArray pngOf(const QImage& image) {
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}
}  // namespace

int AppController::pageImageDpi() const {
    int dpi = 0;
    app->getSettings()->getCustomElement(SETTINGS).getInt("pageImagesDpi", dpi);
    return dpi >= 36 && dpi <= 1200 ? dpi : DEFAULT_DPI;
}

void AppController::setPageImageDpi(int dpi) {
    dpi = std::clamp(dpi, 36, 1200);
    if (dpi == pageImageDpi()) {
        return;
    }
    app->getSettings()->getCustomElement(SETTINGS).setInt("pageImagesDpi", dpi);
    app->getSettings()->customSettingsChanged();
    Q_EMIT pageImageDpiChanged();
}

QVariantMap AppController::imageExportDraft() const {
    QVariantMap draft;
    DocumentSession* s = session();
    const bool offered = s && !s->textFile();
    draft.insert(QStringLiteral("offered"), offered);
    if (!offered) {
        return draft;
    }
    if (s->isProtected()) {
        draft.insert(QStringLiteral("refused"),
                     tr("This document is protected with a password, and pictures cannot be. Remove the password "
                        "first (⋮ → Document → Change or remove the password…) to export its pages as pictures."));
    }
    draft.insert(QStringLiteral("name"), QString::fromStdString(stemOf(*s)));
    std::string folder;
    app->getSettings()->getCustomElement(SETTINGS).getString("pageImagesFolder", folder);
    if (folder.empty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation).toStdString();
    }
    draft.insert(QStringLiteral("folder"), QUrl::fromLocalFile(QString::fromStdString(folder)));
    draft.insert(QStringLiteral("dpi"), pageImageDpi());
    return draft;
}

void AppController::putPageImage(const QImage& image, const QByteArray& png, int page, int dpi, int pages,
                                 bool capped, bool announce) {
    auto* mime = new QMimeData;
    mime->setImageData(image);  // (the formats of pictures Qt offers other apps)
    mime->setData(QStringLiteral("image/png"), png);
    QGuiApplication::clipboard()->setMimeData(mime);
    const int shownDpi = static_cast<int>(std::lround(image.dotsPerMeterX() * 0.0254));
    QString text = tr("Page %1 copied as an image (%2×%3)").arg(page + 1).arg(image.width()).arg(image.height());
    if (capped) {
        text = tr("Page %1 copied as an image (%2×%3, %4 dpi: the page is too large for %5 dpi)")
                       .arg(page + 1)
                       .arg(image.width())
                       .arg(image.height())
                       .arg(shownDpi)
                       .arg(dpi);
    } else if (pages > 1) {
        text = tr("Page %1 copied as an image (%2×%3; the first of the %4 pages)")
                       .arg(page + 1)
                       .arg(image.width())
                       .arg(image.height())
                       .arg(pages);
    }
    if (announce) {
        Q_EMIT pageActionDone(text, false);
    }
    Q_EMIT pageImageCopied(page, image.size(), shownDpi, QString());
}

bool AppController::copyPagesAsImage(const QList<int>& list) {
    DocumentSession* s = session();
    if (!s || s->textFile()) {
        return false;
    }
    const auto indices = pageList(list);
    if (indices.empty()) {
        return false;
    }
    // One picture on the clipboard: the first page (of a selection)
    PagePictures pictures = picturesOf(*s, {indices.front()});
    if (pictures.indices.empty()) {
        return false;
    }
    const int dpi = pageImageDpi();
    const int count = static_cast<int>(indices.size());
    QPointer<AppController> self(this);
    appServices->jobs().start([self, pictures, dpi, count] {
        bool capped = false;
        const QImage image = drawPage(pictures, 0, dpi, true, false, MAX_CLIPBOARD_PIXELS, &capped);
        const QByteArray png = image.isNull() ? QByteArray() : pngOf(image);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, image, png, pictures, dpi, count, capped] {
            if (!self) {
                return;
            }
            const int page = static_cast<int>(pictures.indices.front());
            if (image.isNull()) {
                const QString error = tr("Page %1 could not be drawn.").arg(page + 1);
                Q_EMIT self->pageActionDone(error, false);
                Q_EMIT self->pageImageCopied(page, {}, dpi, error);
                return;
            }
            self->putPageImage(image, png, page, dpi, count, capped);
        });
    }, BackgroundJobs::Priority::Normal);
    return true;
}

bool AppController::exportPageImages(const QList<int>& list, const QUrl& folderUrl, int dpi, bool transparent,
                                     const QString& format) {
    DocumentSession* s = session();
    const QVariantMap draft = imageExportDraft();
    if (!s || !draft.value(QStringLiteral("offered")).toBool()) {
        return false;
    }
    if (const QString refused = draft.value(QStringLiteral("refused")).toString(); !refused.isEmpty()) {
        Q_EMIT message(tr("Export pages as pictures"), refused, true);
        Q_EMIT pageImagesExported({}, refused);
        return false;
    }
    const QString folderPath = localPathOf(folderUrl);
    if (folderPath.isEmpty()) {
        return false;
    }
    const fs::path folder(folderPath.toStdString());
    dpi = std::clamp(dpi, 36, 1200);
    const bool jpeg = format.compare(QLatin1String("jpg"), Qt::CaseInsensitive) == 0 ||
                      format.compare(QLatin1String("jpeg"), Qt::CaseInsensitive) == 0;
    app->getSettings()->getCustomElement(SETTINGS).setString("pageImagesFolder", folder.string());
    app->getSettings()->customSettingsChanged();
    setPageImageDpi(dpi);  // (also for "Copy page as image")

    PagePictures pictures = picturesOf(*s, pageList(list));
    std::vector<std::string> names;
    for (size_t i: pictures.indices) {
        names.push_back(pagefiles::imageName(stemOf(*s), i, pictures.pageCount, jpeg ? ".jpg" : ".png"));
    }
    const bool paper = !(transparent && !jpeg);
    QPointer<AppController> self(this);
    appServices->jobs().start([self, pictures, names, folder, dpi, paper, jpeg] {
        QStringList written;
        QString error;
        QImage single;  // (one page: also on the clipboard)
        QByteArray singlePng;
        bool capped = false;
        std::error_code ec;
        fs::create_directories(folder, ec);
        if (!fs::is_directory(folder, ec)) {
            error = tr("The folder “%1” could not be made.").arg(qstr(folder));
        }
        for (size_t i = 0; error.isEmpty() && i < names.size(); ++i) {
            const QImage image = drawPage(pictures, i, dpi, paper, jpeg, MAX_IMAGE_PIXELS, &capped);
            if (image.isNull()) {
                error = tr("Page %1 could not be drawn.").arg(pictures.indices[i] + 1);
                break;
            }
            const fs::path target = folder / names[i];
            if (!image.save(qstr(target), jpeg ? "JPEG" : "PNG", jpeg ? 92 : -1)) {
                error = tr("“%1” could not be written.").arg(qstr(target));
                break;
            }
            written << qstr(target);
            if (names.size() == 1) {
                single = image;
                singlePng = jpeg ? pngOf(image) : QByteArray();
            }
        }
        if (!single.isNull() && singlePng.isEmpty()) {
            QFile file(written.front());
            if (file.open(QIODevice::ReadOnly)) {
                singlePng = file.readAll();  // (the PNG just written)
            }
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, written, error, folder, single, singlePng,
                                                                 pictures, dpi, capped] {
            if (!self) {
                return;
            }
            if (!error.isEmpty()) {
                Q_EMIT self->message(tr("Export pages as pictures"), error, true);
            } else if (!single.isNull()) {
                // One page: written, and on the clipboard too (to paste it somewhere right away)
                self->putPageImage(single, singlePng, static_cast<int>(pictures.indices.front()), dpi, 1, capped,
                                   false);
                Q_EMIT self->pageActionDone(
                        tr("Exported “%1” and copied it as an image (%2×%3)")
                                .arg(QString::fromStdString(fs::path(written.front().toStdString()).filename().string()))
                                .arg(single.width())
                                .arg(single.height()),
                        false);
            } else {
                Q_EMIT self->pageActionDone(
                        tr("%1 pictures exported to “%2”").arg(written.size()).arg(qstr(folder.filename())), false);
            }
            Q_EMIT self->pageImagesExported(written, error);
        });
    }, BackgroundJobs::Priority::Normal);
    return true;
}

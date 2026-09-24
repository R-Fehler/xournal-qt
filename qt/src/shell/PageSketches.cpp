#include "PageSketches.h"

#include <algorithm>
#include <shared_mutex>

#include <QCryptographicHash>
#include <QFile>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include "model/Document.h"
#include "pdf/base/XojPdfDocument.h"
#include "render/RenderService.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "util/PathUtil.h"

#include "CanvasMemory.h"
#include "DocumentFiles.h"
#include "DocumentPlaces.h"
#include "Library.h"
#include "Previews.h"
#include "Thumbnails.h"

namespace xqt {

namespace {
/// Workers below the others: the canvas and the sharp thumbnails go first.
QThreadPool& sketchPool(int workers) {
    static QThreadPool* p = [workers] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(workers);
        tp->setThreadPriority(QThread::LowestPriority);
        return tp;
    }();
    return *p;
}

/// Folder of the stored previews of a document as its files are now (empty: not saved)
fs::path folderOf(DocumentSession& session) {
    const fs::path file = session.documentFile();
    if (file.empty()) {
        return {};
    }
    fs::path pdf;
    {
        Document* doc = session.getDocument();
        std::shared_lock lock(*doc);
        if (doc->getPdfPageCount() > 0) {
            pdf = doc->getPdfFilepath();  // (also one that is not next to it)
        }
    }
    const QByteArray key = QByteArray::fromStdString(file.string()) + '\n' +
                           documentStamp(DocumentFiles::itemOf(file, DocumentFiles::TextFiles)).toUtf8() + '\n' +
                           QByteArray::fromStdString(pdf.string()) + '|' + fileStamp(pdf).toUtf8();
    const QString name =
            QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex().left(24));
    return Util::getCacheSubfolder("pages") / name.toStdString();
}

QImage readPage(const fs::path& file) {
    QImage img;
    std::error_code ec;
    if (!file.empty() && fs::exists(file, ec)) {
        img.load(QString::fromStdString(file.string()), "JPG");
    }
    return img;
}

void writePage(const fs::path& file, const QImage& image) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    // Written under another name first: a reader never sees half a file.
    const QString path = QString::fromStdString(file.string());
    if (image.convertToFormat(QImage::Format_RGB888).save(path + ".part", "JPG", 85)) {
        QFile::remove(path);
        QFile::rename(path + ".part", path);
    }
}

qint64 bytesAt(int width, double aspect) {
    return static_cast<qint64>(width) * std::max(1, static_cast<int>(width * aspect)) * 2;  // 16 bits per pixel
}
}  // namespace

PageSketches& PageSketches::instance() {
    static auto* sketches = new PageSketches;  // (never destroyed: workers may still offer while statics go)
    return *sketches;
}

// --- Tier (under mtx) ------------------------------------------------------------------------------------------------

auto PageSketches::Tier::find(quint64 session, quint64 pageId) const -> const Picture* {
    if (auto s = pictures.find(session); s != pictures.end()) {
        if (auto it = s->second.find(pageId); it != s->second.end()) {
            return &it->second;
        }
    }
    return nullptr;
}

void PageSketches::Tier::put(quint64 session, quint64 pageId, quint64 revision, QImage image) {
    auto s = pictures.find(session);
    if (s == pictures.end()) {
        return;  // closed
    }
    Picture& picture = s->second[pageId];
    used -= picture.image.sizeInBytes();
    picture = Picture{revision, std::move(image)};
    used += picture.image.sizeInBytes();
}

void PageSketches::Tier::dropSession(quint64 session) {
    if (auto it = pictures.find(session); it != pictures.end()) {
        for (const auto& [page, picture]: it->second) {
            used -= picture.image.sizeInBytes();
        }
        pictures.erase(it);
    }
}

void PageSketches::Tier::keepOnly(quint64 session, const std::set<quint64>& keep) {
    auto s = pictures.find(session);
    if (s == pictures.end()) {
        return;
    }
    for (auto it = s->second.begin(); it != s->second.end();) {
        if (!keep.count(it->first)) {  // a page that is gone, or beyond the budget
            used -= it->second.image.sizeInBytes();
            it = s->second.erase(it);
        } else {
            ++it;
        }
    }
}

// --- PageSketches ----------------------------------------------------------------------------------------------------

PageSketches::PageSketches() {
    sketches.budget = ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024 / 4;
    sketches.level = WIDTHS[0];
    previews.budget = CanvasMemory::instance().previewBudget();
    previews.level = PREVIEW_WIDTHS[0];
    planTimer.setSingleShot(true);
    connect(&planTimer, &QTimer::timeout, this, &PageSketches::plan);
    visibleTimer.setSingleShot(true);
    visibleTimer.setInterval(25);
    connect(&visibleTimer, &QTimer::timeout, this, &PageSketches::next);
    editTimer.setSingleShot(true);
    editTimer.setInterval(1500);
    connect(&editTimer, &QTimer::timeout, this, [this] { planSoon(0); });
    announceTimer.setSingleShot(true);
    announceTimer.setInterval(100);
    connect(&announceTimer, &QTimer::timeout, this, [this] {
        for (quint64 id: std::exchange(announced, {})) {
            Q_EMIT changed(id);
        }
    });
    // The previews take a part of the memory for rendered pages
    connect(&CanvasMemory::instance(), &CanvasMemory::limitChanged, this, [this] { planSoon(shownDelay); });
}

PageSketches::~PageSketches() = default;

void PageSketches::add(quint64 id, DocumentSession* session) {
    for (const auto& s: sessions) {
        if (s.id == id) {
            return;
        }
    }
    sessions.push_back(Session{id, session, 0});
    {
        std::lock_guard lock(mtx);
        sketches.pictures[id];  // (sharp thumbnails give pictures from now on)
        previews.pictures[id];
    }
    // Edited pages are drawn again when the edits paused (drawing does not keep the workers busy)
    connect(session, &DocumentSession::pageRevisionsChanged, this, [this] { editTimer.start(); });
    // (outwards from where the reader is now)
    connect(session, &DocumentSession::currentPageChanged, this, [this] { planSoon(shownDelay); });
    // As saved again (saved, or undone back): its pages go to disk
    connect(session, &DocumentSession::modifiedChanged, this, [this, id](bool modified) {
        if (!modified) {
            capture(id);
        }
    });
    connect(session, &DocumentSession::filePathChanged, this, [this, id] { capture(id); });
    capture(id);
    seedTitlePage(id);
    planSoon(shownDelay);
}

void PageSketches::capture(quint64 id) {
    DocumentSession* s = nullptr;
    for (const auto& e: sessions) {
        if (e.id == id) {
            s = e.session;
        }
    }
    if (!s || s->isModified()) {
        return;  // (the pages as saved are those captured before)
    }
    Disk disk;
    disk.folder = folderOf(*s);
    if (!disk.folder.empty()) {
        const auto stamps = s->pageStamps();
        for (size_t i = 0; i < stamps.size(); ++i) {
            disk.saved[stamps[i].id] = {stamps[i].revision, i};
        }
        std::error_code ec;
        if (fs::exists(disk.folder, ec)) {
            fs::last_write_time(disk.folder, fs::file_time_type::clock::now(), ec);  // (used now: kept longer)
        }
    }
    {
        std::lock_guard lock(mtx);
        if (auto old = disks.find(id); old != disks.end() && old->second.folder == disk.folder) {
            disk.stored = std::move(old->second.stored);  // (the same files: e.g. undone back to as saved)
        }
        disks[id] = std::move(disk);
    }
    planSoon(shownDelay);
}

void PageSketches::seedTitlePage(quint64 id) {
    DocumentSession* s = nullptr;
    for (const auto& e: sessions) {
        if (e.id == id) {
            s = e.session;
        }
    }
    if (!s || s->isModified() || s->documentFile().empty()) {
        return;
    }
    const DocumentItem item = DocumentFiles::itemOf(s->documentFile(), DocumentFiles::TextFiles);
    const size_t pages = s->getDocument()->getPageCount();
    if (!item.valid() || pages == 0) {
        return;
    }
    const auto title = std::min(static_cast<size_t>(std::max(0, DocumentPlaces::titlePage(DocumentPlaces::keyOf(item)))),
                                pages - 1);
    const quint64 pageId = s->pageId(title);
    {
        std::lock_guard lock(mtx);
        if (previews.find(id, pageId)) {
            return;
        }
    }
    if (const QImage img = PreviewCache::stored(item); !img.isNull()) {
        // (smaller than a preview: it is drawn later, the canvas shows this one meanwhile)
        store(id, pageId, s->pageRevision(title), img, true);
    }
}

fs::path PageSketches::diskFile(quint64 session, quint64 pageId, quint64 revision) const {
    auto d = disks.find(session);
    if (d == disks.end() || d->second.folder.empty()) {
        return {};
    }
    auto it = d->second.saved.find(pageId);
    if (it == d->second.saved.end() || it->second.first != revision) {
        return {};  // changed since
    }
    return d->second.folder / (std::to_string(it->second.second) + ".jpg");
}

void PageSketches::markStored(quint64 session, quint64 pageId) {
    std::lock_guard lock(mtx);
    if (auto d = disks.find(session); d != disks.end()) {
        if (auto it = d->second.saved.find(pageId); it != d->second.saved.end()) {
            d->second.stored.insert(it->second.second);
        }
    }
}

fs::path PageSketches::diskFolder(quint64 session) const {
    std::lock_guard lock(mtx);
    auto d = disks.find(session);
    return d == disks.end() ? fs::path() : d->second.folder;
}

void PageSketches::trimDisk(qint64 bytes) {
    struct Folder {
        fs::path path;
        fs::file_time_type used;
        qint64 size = 0;
    };
    std::vector<Folder> folders;
    qint64 total = 0;
    std::error_code ec;
    const fs::path root = Util::getCacheSubfolder("pages");
    for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        Folder f{it->path(), fs::last_write_time(it->path(), ec)};
        for (auto p = fs::directory_iterator(it->path(), ec); !ec && p != fs::directory_iterator(); p.increment(ec)) {
            f.size += static_cast<qint64>(p->file_size(ec));
        }
        total += f.size;
        folders.push_back(std::move(f));
    }
    std::sort(folders.begin(), folders.end(), [](const Folder& a, const Folder& b) { return a.used < b.used; });
    for (const Folder& f: folders) {
        if (total <= bytes) {
            break;
        }
        fs::remove_all(f.path, ec);
        total -= f.size;
    }
}

void PageSketches::remove(quint64 id) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->id == id) {
            if (it->session) {
                disconnect(it->session, nullptr, this, nullptr);
            }
            sessions.erase(it);
            break;
        }
    }
    {
        std::lock_guard lock(mtx);
        sketches.dropSession(id);
        previews.dropSession(id);
        pdfCopies.erase(id);
        disks.erase(id);
    }
    planSoon(shownDelay);  // the others may get bigger pictures now
}

void PageSketches::focus(quint64 id) {
    for (auto& s: sessions) {
        if (s.id == id) {
            s.shown = ++shownCounter;
            planSoon(shownDelay);
        }
    }
}

void PageSketches::planSoon(int ms) {
    if (!planTimer.isActive() || planTimer.remainingTime() > ms) {
        planTimer.start(ms);
    }
}

void PageSketches::plan() {
    struct Page {
        quint64 id;
        quint64 revision;
        double aspect;
    };
    // The documents shown last first; in each the current page first, then outwards from it
    std::vector<const Session*> order;
    for (const auto& s: sessions) {
        if (s.session) {
            order.push_back(&s);
        }
    }
    std::stable_sort(order.begin(), order.end(), [](const Session* a, const Session* b) { return a->shown > b->shown; });
    std::vector<std::pair<quint64, std::vector<Page>>> documents;
    for (const Session* s: order) {
        const auto stamps = s->session->pageStamps();
        std::vector<Page> pages;
        if (!stamps.empty()) {
            const auto current = static_cast<std::ptrdiff_t>(std::min(s->session->getCurrentPageNo(), stamps.size() - 1));
            const auto count = static_cast<std::ptrdiff_t>(stamps.size());
            auto take = [&](std::ptrdiff_t i) {
                const auto& st = stamps[static_cast<size_t>(i)];
                const double w = st.page->getWidth();
                pages.push_back(Page{st.id, st.revision, w > 0 ? st.page->getHeight() / w : 1.414});
            };
            take(current);
            for (std::ptrdiff_t d = 1; d < count; ++d) {
                if (current + d < count) {
                    take(current + d);
                }
                if (current - d >= 0) {
                    take(current - d);
                }
            }
        }
        documents.emplace_back(s->id, std::move(pages));
    }

    // The widths: the biggest at which all pages fit
    auto levelFor = [&](const auto& widths, qint64 budget) {
        for (int candidate: widths) {
            qint64 total = 0;
            for (const auto& [id, pages]: documents) {
                for (const Page& p: pages) {
                    total += bytesAt(candidate, p.aspect);
                }
            }
            if (total <= budget) {
                return candidate;
            }
        }
        return widths.back();
    };
    previews.budget = CanvasMemory::instance().previewBudget();
    const qint64 sb = sketches.budget, pb = previews.budget;
    const int sl = levelFor(WIDTHS, sb), pl = levelFor(PREVIEW_WIDTHS, pb);
    sketches.level = sl;
    previews.level = pl;

    // Once after the start: the stored previews used longest ago go
    static bool trimmed = false;
    if (!std::exchange(trimmed, true)) {
        QThreadPool::globalInstance()->start([] { trimDisk(DISK_LIMIT); });
    }

    // Who gets pictures: in that order, while the budget lasts; the others lose theirs
    std::vector<Job> missingSketch, missingPreview, outdated, toStore;
    qint64 plannedS = 0, plannedP = 0;
    {
        std::lock_guard lock(mtx);
        for (const auto& [id, pages]: documents) {
            std::set<quint64> keepS, keepP;
            for (const Page& p: pages) {
                const bool wantS = (plannedS += bytesAt(sl, p.aspect)) <= sb;
                const bool wantP = (plannedP += bytesAt(pl, p.aspect)) <= pb;
                if (wantS) {
                    keepS.insert(p.id);
                }
                if (wantP) {
                    keepP.insert(p.id);
                }
                const Picture* s = sketches.find(id, p.id);
                const Picture* v = previews.find(id, p.id);
                const bool sOk = s && s->revision == p.revision && s->image.width() == sl;
                const bool pOk = v && v->revision == p.revision && v->image.width() == pl;
                const Job job{id, p.id, p.revision, wantP};
                if (wantS && !s) {
                    missingSketch.push_back(job);
                } else if (wantP && !v) {
                    missingPreview.push_back(job);
                } else if ((wantS && !sOk) || (wantP && !pOk)) {
                    outdated.push_back(job);
                } else if (pOk && !diskFile(id, p.id, p.revision).empty()) {
                    // As saved, with its preview: on disk for the next opening (unless it is there)
                    const auto& d = disks.at(id);
                    if (!d.stored.count(d.saved.at(p.id).second)) {
                        toStore.push_back(Job{id, p.id, p.revision, true, true});
                    }
                }
            }
            sketches.keepOnly(id, keepS);
            previews.keepOnly(id, keepP);
        }
    }
    jobs.assign(missingSketch.begin(), missingSketch.end());
    jobs.insert(jobs.end(), missingPreview.begin(), missingPreview.end());
    jobs.insert(jobs.end(), outdated.begin(), outdated.end());
    jobs.insert(jobs.end(), toStore.begin(), toStore.end());
    next();
}

void PageSketches::next() {
    if (!jobs.empty() && RenderService::visiblePagesBusy()) {
        // The pages in view are being rendered: they go first (they are what the reader waits for)
        visibleTimer.start();
        return;
    }
    while (running < WORKERS && !jobs.empty()) {
        const Job job = jobs.front();
        jobs.pop_front();
        const int sl = sketches.level, pl = previews.level;
        const int target = job.preview ? pl : sl;
        QImage from;  // a picture of this revision at least as big
        fs::path file;  // its stored preview (the page is as saved)
        bool stored = false;
        {
            std::lock_guard lock(mtx);
            if (!sketches.pictures.count(job.session)) {
                continue;  // closed
            }
            file = diskFile(job.session, job.pageId, job.revision);
            if (!file.empty()) {
                const auto& d = disks.at(job.session);
                stored = d.stored.count(d.saved.at(job.pageId).second) > 0;
            }
            if (job.write) {
                const Picture* v = previews.find(job.session, job.pageId);
                if (file.empty() || stored || !v || v->revision != job.revision) {
                    continue;
                }
                ++running;
                QThreadPool::globalInstance()->start([this, job, file, image = v->image] {
                    writePage(file, image);
                    markStored(job.session, job.pageId);
                    QMetaObject::invokeMethod(
                            this,
                            [this] {
                                --running;
                                next();
                            },
                            Qt::QueuedConnection);
                });
                continue;
            }
            const Picture* s = sketches.find(job.session, job.pageId);
            const Picture* v = previews.find(job.session, job.pageId);
            const bool sOk = s && s->revision == job.revision && s->image.width() == sl;
            const bool pOk = v && v->revision == job.revision && v->image.width() == pl;
            if (sOk && (!job.preview || pOk)) {
                continue;  // made from a sharp thumbnail meanwhile
            }
            if (v && v->revision == job.revision && v->image.width() >= target) {
                from = v->image;
            } else if (s && s->revision == job.revision && s->image.width() >= target) {
                from = s->image;
            }
        }
        DocumentSession* session = ThumbnailProvider::acquireSession(job.session);
        if (!session) {
            continue;
        }
        fs::path pdfPath;
        size_t pdfPages = 0;
        {
            Document* doc = session->getDocument();
            std::shared_lock lock(*doc);
            if (doc->getPdfPageCount() > 0) {
                pdfPath = doc->getPdfFilepath();
                pdfPages = doc->getPdfPageCount();
            }
        }
        ++running;
        sketchPool(WORKERS).start(QRunnable::create([this, job, target, session, from, pdfPath, pdfPages, file, stored] {
            QImage img = from;
            if (img.isNull()) {
                img = ThumbnailProvider::keptImage(job.session, job.revision, target);
            }
            bool fromDisk = false;
            if (img.isNull() && !file.empty()) {
                img = readPage(file);  // (stored at the opening before)
                fromDisk = !img.isNull();
                reads += fromDisk;
            }
            if (img.isNull()) {
                if (auto stamp = session->pageOfRevision(job.revision)) {  // (else changed meanwhile)
                    auto pdf = takePdf(job.session, pdfPath, pdfPages);
                    img = ThumbnailProvider::renderPage(*session->getDocument(), stamp->page, target, pdf.get());
                    givePdf(job.session, pdfPath, std::move(pdf));
                    ++draws;
                }
            }
            if (!img.isNull()) {
                store(job.session, job.pageId, job.revision, img, job.preview);
                if (fromDisk) {
                    markStored(job.session, job.pageId);
                } else if (!file.empty() && !stored && job.preview && img.width() >= target) {
                    writePage(file, img.width() == target ? img : img.scaledToWidth(target, Qt::SmoothTransformation));
                    markStored(job.session, job.pageId);
                }
            }
            ThumbnailProvider::releaseSession(job.session);
            QMetaObject::invokeMethod(
                    this,
                    [this] {
                        --running;
                        next();
                        if (!running && jobs.empty()) {
                            std::lock_guard lock(mtx);
                            pdfCopies.clear();  // all drawn: the instances are not needed any more
                        }
                    },
                    Qt::QueuedConnection);
        }));
    }
}

std::unique_ptr<XojPdfDocument> PageSketches::takePdf(quint64 session, const fs::path& path, size_t pages) {
    if (path.empty()) {
        return nullptr;
    }
    {
        std::lock_guard lock(mtx);
        auto& copies = pdfCopies[session];
        if (copies.path == path && !copies.spare.empty()) {
            auto pdf = std::move(copies.spare.back());
            copies.spare.pop_back();
            return pdf;
        }
    }
    auto pdf = std::make_unique<XojPdfDocument>();
    GError* error = nullptr;
    const bool loaded = pdf->load(path, "", &error);
    if (error) {
        g_error_free(error);
    }
    // (not loadable, e.g. with a password, or changed on disk: the document's own instance)
    return loaded && pdf->getPageCount() == pages ? std::move(pdf) : nullptr;
}

void PageSketches::givePdf(quint64 session, const fs::path& path, std::unique_ptr<XojPdfDocument> pdf) {
    if (!pdf) {
        return;
    }
    std::lock_guard lock(mtx);
    if (auto it = pdfCopies.find(session); it != pdfCopies.end()) {  // (else closed meanwhile)
        if (it->second.path != path) {
            it->second = PdfCopies{path, {}};
        }
        it->second.spare.push_back(std::move(pdf));
    }
}

void PageSketches::store(quint64 session, quint64 pageId, quint64 revision, const QImage& image, bool preview) {
    auto scaled = [&image](int width) {  // (never bigger: a smaller one is drawn later)
        QImage img = image.width() <= width ? image : image.scaledToWidth(width, Qt::SmoothTransformation);
        return img.format() == QImage::Format_RGB16 ? img : img.convertToFormat(QImage::Format_RGB16);
    };
    QImage p = preview ? scaled(previews.level) : QImage();
    QImage s = scaled(sketches.level);
    {
        std::lock_guard lock(mtx);
        if (preview) {
            previews.put(session, pageId, revision, std::move(p));
        }
        sketches.put(session, pageId, revision, std::move(s));
    }
    QMetaObject::invokeMethod(this, [this, session] { announce(session); }, Qt::QueuedConnection);
}

void PageSketches::announce(quint64 session) {
    announced.insert(session);
    if (!announceTimer.isActive()) {
        announceTimer.start();
    }
}

void PageSketches::offer(quint64 session, quint64 pageId, quint64 revision, const QImage& sharp) {
    const int sl = sketches.level, pl = previews.level;
    if (sharp.isNull() || sharp.width() < sl) {
        return;
    }
    const double aspect = double(sharp.height()) / sharp.width();
    bool preview = false;
    {
        std::lock_guard lock(mtx);
        if (!sketches.pictures.count(session)) {
            return;
        }
        const Picture* s = sketches.find(session, pageId);
        const Picture* v = previews.find(session, pageId);
        const bool needS = !(s && s->revision == revision && s->image.width() == sl);
        preview = sharp.width() >= pl && !(v && v->revision == revision && v->image.width() == pl) &&
                  (v || previews.used + bytesAt(pl, aspect) <= previews.budget);
        if (!needS && !preview) {
            return;  // has them
        }
        if (!s && sketches.used + bytesAt(sl, aspect) > sketches.budget) {
            return;  // (the plan decides who gets one then)
        }
    }
    store(session, pageId, revision, sharp, preview);
}

QString PageSketches::url(quint64 session, quint64 pageId) const {
    std::lock_guard lock(mtx);
    if (const Picture* s = sketches.find(session, pageId)) {
        return QString("image://sketch/%1/%2/%3").arg(session).arg(pageId).arg(s->revision);
    }
    return {};
}

QImage PageSketches::image(quint64 session, quint64 pageId) const {
    std::lock_guard lock(mtx);
    const Picture* s = sketches.find(session, pageId);
    return s ? s->image : QImage();
}

QImage PageSketches::preview(quint64 session, quint64 pageId) const {
    std::lock_guard lock(mtx);
    const Picture* v = previews.find(session, pageId);
    return v ? v->image : QImage();
}

QImage PageSketches::imageOfRevision(quint64 session, quint64 revision) const {
    std::lock_guard lock(mtx);
    for (const Tier* tier: {&previews, &sketches}) {
        if (auto s = tier->pictures.find(session); s != tier->pictures.end()) {
            for (const auto& [page, picture]: s->second) {
                if (picture.revision == revision) {
                    return picture.image;
                }
            }
        }
    }
    return {};
}

void PageSketches::setBudget(qint64 bytes) {
    sketches.budget = std::max<qint64>(0, bytes);
    planSoon(shownDelay);
}

qint64 PageSketches::bytes() const {
    std::lock_guard lock(mtx);
    return sketches.used;
}

qint64 PageSketches::previewBytes() const {
    std::lock_guard lock(mtx);
    return previews.used;
}

bool PageSketches::idle() const {
    return !running && jobs.empty() && !planTimer.isActive() && !editTimer.isActive();
}

void PageSketches::setDelays(int shown, int edited) {
    shownDelay = shown;
    editTimer.setInterval(edited);
    if (planTimer.isActive()) {
        planTimer.start(shown);
    }
}

QImage SketchProvider::requestImage(const QString& id, QSize* size, const QSize&) {
    const QStringList parts = id.split('/');
    QImage img = PageSketches::instance().image(parts.value(0).toULongLong(), parts.value(1).toULongLong());
    if (img.isNull()) {  // (gone meanwhile: a white page, no warning)
        img = QImage(1, 1, QImage::Format_RGB16);
        img.fill(Qt::white);
    }
    if (size) {
        *size = img.size();
    }
    return img;
}

}  // namespace xqt

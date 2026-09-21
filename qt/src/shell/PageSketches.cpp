#include "PageSketches.h"

#include <algorithm>
#include <shared_mutex>

#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include "model/Document.h"
#include "pdf/base/XojPdfDocument.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"

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

qint64 bytesAt(int width, double aspect) {
    return static_cast<qint64>(width) * std::max(1, static_cast<int>(width * aspect)) * 2;  // 16 bits per pixel
}
}  // namespace

PageSketches& PageSketches::instance() {
    static auto* sketches = new PageSketches;  // (never destroyed: workers may still offer while statics go)
    return *sketches;
}

PageSketches::PageSketches(): budget(ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024 / 4) {
    planTimer.setSingleShot(true);
    connect(&planTimer, &QTimer::timeout, this, &PageSketches::plan);
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
        sketches[id];  // (sharp thumbnails give sketches from now on)
    }
    // Edited pages are sketched again when the edits paused (drawing does not keep the worker busy)
    connect(session, &DocumentSession::pageRevisionsChanged, this, [this] { editTimer.start(); });
    // (outwards from where the reader is now)
    connect(session, &DocumentSession::currentPageChanged, this, [this] { planSoon(shownDelay); });
    planSoon(shownDelay);
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
        if (auto it = sketches.find(id); it != sketches.end()) {
            for (const auto& [page, sketch]: it->second) {
                used -= sketch.image.sizeInBytes();
            }
            sketches.erase(it);
        }
        pdfCopies.erase(id);
    }
    planSoon(shownDelay);  // the others may get sharper sketches now
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

    // The width: the biggest at which all pages fit
    const qint64 b = budget;
    int w = WIDTHS.back();
    for (int candidate: WIDTHS) {
        qint64 total = 0;
        for (const auto& [id, pages]: documents) {
            for (const Page& p: pages) {
                total += bytesAt(candidate, p.aspect);
            }
        }
        if (total <= b) {
            w = candidate;
            break;
        }
    }
    level = w;

    // Who gets a sketch: in that order, while the budget lasts; the others lose theirs
    std::vector<Job> missing, outdated;
    qint64 planned = 0;
    {
        std::lock_guard lock(mtx);
        for (const auto& [id, pages]: documents) {
            auto& mine = sketches[id];
            std::set<quint64> keep;
            for (const Page& p: pages) {
                planned += bytesAt(w, p.aspect);
                if (planned > b) {
                    break;
                }
                keep.insert(p.id);
                auto it = mine.find(p.id);
                if (it == mine.end()) {
                    missing.push_back(Job{id, p.id, p.revision});
                } else if (it->second.revision != p.revision || it->second.image.width() != w) {
                    outdated.push_back(Job{id, p.id, p.revision});
                }
            }
            for (auto it = mine.begin(); it != mine.end();) {
                if (!keep.count(it->first)) {  // a page that is gone, or beyond the budget
                    used -= it->second.image.sizeInBytes();
                    it = mine.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    jobs.assign(missing.begin(), missing.end());
    jobs.insert(jobs.end(), outdated.begin(), outdated.end());
    next();
}

void PageSketches::next() {
    while (running < WORKERS && !jobs.empty()) {
        const Job job = jobs.front();
        jobs.pop_front();
        const int w = level;
        QImage from;  // its sketch of this revision, if bigger
        {
            std::lock_guard lock(mtx);
            auto s = sketches.find(job.session);
            if (s == sketches.end()) {
                continue;  // closed
            }
            if (auto it = s->second.find(job.pageId); it != s->second.end() && it->second.revision == job.revision) {
                if (it->second.image.width() == w) {
                    continue;  // made from a sharp thumbnail meanwhile
                }
                if (it->second.image.width() > w) {
                    from = it->second.image;
                }
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
        sketchPool(WORKERS).start(QRunnable::create([this, job, w, session, from, pdfPath, pdfPages] {
            QImage img;
            if (!from.isNull()) {
                img = from.scaledToWidth(w, Qt::SmoothTransformation);
            } else if (QImage kept = ThumbnailProvider::keptImage(job.session, job.revision, w); !kept.isNull()) {
                img = kept.scaledToWidth(w, Qt::SmoothTransformation);
            } else if (auto stamp = session->pageOfRevision(job.revision)) {  // (else changed meanwhile)
                auto pdf = takePdf(job.session, pdfPath, pdfPages);
                img = ThumbnailProvider::renderPage(*session->getDocument(), stamp->page, w, pdf.get());
                givePdf(job.session, pdfPath, std::move(pdf));
                ++draws;
            }
            if (!img.isNull()) {
                store(job.session, job.pageId, job.revision, img);
            }
            ThumbnailProvider::releaseSession(job.session);
            QMetaObject::invokeMethod(
                    this,
                    [this] {
                        --running;
                        next();
                        if (!running && jobs.empty()) {
                            std::lock_guard lock(mtx);
                            pdfCopies.clear();  // all sketched: the instances are not needed any more
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

void PageSketches::store(quint64 session, quint64 pageId, quint64 revision, const QImage& image) {
    QImage small = image.format() == QImage::Format_RGB16 ? image : image.convertToFormat(QImage::Format_RGB16);
    {
        std::lock_guard lock(mtx);
        auto s = sketches.find(session);
        if (s == sketches.end()) {
            return;
        }
        Sketch& sketch = s->second[pageId];
        used -= sketch.image.sizeInBytes();
        sketch = Sketch{revision, std::move(small)};
        used += sketch.image.sizeInBytes();
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
    const int w = level;
    if (sharp.isNull() || sharp.width() < w) {
        return;
    }
    {
        std::lock_guard lock(mtx);
        auto s = sketches.find(session);
        if (s == sketches.end()) {
            return;
        }
        auto it = s->second.find(pageId);
        if (it != s->second.end() && it->second.revision == revision && it->second.image.width() == w) {
            return;  // has it
        }
        if (it == s->second.end() && used + bytesAt(w, double(sharp.height()) / sharp.width()) > budget) {
            return;  // (the plan decides who gets one then)
        }
    }
    store(session, pageId, revision, sharp.scaledToWidth(w, Qt::SmoothTransformation));
}

QString PageSketches::url(quint64 session, quint64 pageId) const {
    std::lock_guard lock(mtx);
    if (auto s = sketches.find(session); s != sketches.end()) {
        if (auto it = s->second.find(pageId); it != s->second.end()) {
            return QString("image://sketch/%1/%2/%3").arg(session).arg(pageId).arg(it->second.revision);
        }
    }
    return {};
}

QImage PageSketches::image(quint64 session, quint64 pageId) const {
    std::lock_guard lock(mtx);
    if (auto s = sketches.find(session); s != sketches.end()) {
        if (auto it = s->second.find(pageId); it != s->second.end()) {
            return it->second.image;
        }
    }
    return {};
}

QImage PageSketches::imageOfRevision(quint64 session, quint64 revision) const {
    std::lock_guard lock(mtx);
    if (auto s = sketches.find(session); s != sketches.end()) {
        for (const auto& [page, sketch]: s->second) {
            if (sketch.revision == revision) {
                return sketch.image;
            }
        }
    }
    return {};
}

void PageSketches::setBudget(qint64 bytes) {
    budget = std::max<qint64>(0, bytes);
    planSoon(shownDelay);
}

qint64 PageSketches::bytes() const {
    std::lock_guard lock(mtx);
    return used;
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

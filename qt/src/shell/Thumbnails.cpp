#include "Thumbnails.h"

#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <cairo.h>

#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "pdf/base/XojPdfPage.h"
#include "render/RenderService.h"

#include "PageSketches.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"

namespace xqt {

namespace {
struct Registry {
    std::mutex mtx;
    std::condition_variable idle;
    quint64 nextId = 1;
    std::map<quint64, DocumentSession*> sessions;
    std::map<quint64, int> busy;  ///< running renders per session
};
Registry& registry() {
    static Registry r;
    return r;
}

class ThumbnailResponse final: public QQuickImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image);
    }
    /// QML does not want it any more (its item went away): it is not drawn
    void cancel() override { cancelled = true; }
    QImage image;
    std::atomic<bool> cancelled{false};
};

/// The drawn thumbnails of each page revision ("<session>/<revision>"), by width; the least recently used go first.
class Cache {
public:
    QImage find(const QString& key, int width) {
        std::lock_guard lock(mtx);
        auto it = pages.find(key);
        if (it == pages.end()) {
            return {};
        }
        // That width, or scaled down from the smallest bigger one
        auto w = it->second.lower_bound(width);
        if (w == it->second.end()) {
            return {};
        }
        lru.splice(lru.begin(), lru, w->second.used);
        if (w->first == width) {
            return w->second.image;
        }
        return w->second.image.scaledToWidth(width, Qt::SmoothTransformation);
    }
    /// The smallest one at least `width` wide, as it is (does not count as used)
    QImage peek(const QString& key, int width) {
        std::lock_guard lock(mtx);
        auto it = pages.find(key);
        if (it == pages.end()) {
            return {};
        }
        auto w = it->second.lower_bound(width);
        return w == it->second.end() ? QImage() : w->second.image;
    }
    void put(const QString& key, int width, const QImage& image) {
        if (image.isNull()) {
            return;
        }
        std::lock_guard lock(mtx);
        auto& widths = pages[key];
        if (widths.count(width)) {
            return;
        }
        lru.push_front({key, width});
        widths.emplace(width, Entry{image, lru.begin()});
        bytes += image.sizeInBytes();
        shrink();
    }
    void dropSession(quint64 session) {
        std::lock_guard lock(mtx);
        const QString prefix = QString::number(session) + '/';
        for (auto it = pages.begin(); it != pages.end();) {
            if (it->first.startsWith(prefix)) {
                for (auto& [w, e]: it->second) {
                    bytes -= e.image.sizeInBytes();
                    lru.erase(e.used);
                }
                it = pages.erase(it);
            } else {
                ++it;
            }
        }
    }
    void setLimit(qint64 l) {
        std::lock_guard lock(mtx);
        limit = std::max<qint64>(0, l);
        shrink();
    }
    qint64 size() {
        std::lock_guard lock(mtx);
        return bytes;
    }

private:
    using Use = std::list<std::pair<QString, int>>;  ///< key, width; front: used last
    struct Entry {
        QImage image;
        Use::iterator used;
    };
    void shrink() {
        while (bytes > limit && !lru.empty()) {
            const auto [key, width] = lru.back();
            lru.pop_back();
            auto it = pages.find(key);
            auto w = it->second.find(width);
            bytes -= w->second.image.sizeInBytes();
            it->second.erase(w);
            if (it->second.empty()) {
                pages.erase(it);
            }
        }
    }
    std::mutex mtx;
    std::unordered_map<QString, std::map<int, Entry>> pages;
    Use lru;
    qint64 bytes = 0;
    qint64 limit = ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024 * 3 / 4;
};
Cache& cache() {
    static Cache c;
    return c;
}
std::atomic<int> renders{0};

/// Workers of their own (not Qt's global pool, which others use too)
QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(std::max(1, std::min(4, QThread::idealThreadCount() - 1)));
        return tp;
    }();
    return *p;
}
}  // namespace

void ThumbnailProvider::setCacheLimit(qint64 bytes) {
    cache().setLimit(bytes - bytes / 4);
    PageSketches::instance().setBudget(bytes / 4);
}

QImage ThumbnailProvider::keptImage(quint64 session, quint64 revision, int width) {
    return cache().peek(QString("%1/%2").arg(session).arg(revision), width);
}
qint64 ThumbnailProvider::cacheBytes() { return cache().size(); }
int ThumbnailProvider::renderCount() { return renders.load(); }

quint64 ThumbnailProvider::registerSession(DocumentSession* session) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    for (auto& [id, s]: r.sessions) {
        if (s == session) {
            return id;
        }
    }
    const quint64 id = r.nextId++;
    r.sessions[id] = session;
    PageSketches::instance().add(id, session);
    return id;
}

void ThumbnailProvider::unregisterSession(DocumentSession* session) {
    auto& r = registry();
    std::unique_lock lock(r.mtx);
    for (auto it = r.sessions.begin(); it != r.sessions.end(); ++it) {
        if (it->second == session) {
            const quint64 id = it->first;
            r.sessions.erase(it);
            r.idle.wait(lock, [&] { return r.busy[id] == 0; });
            r.busy.erase(id);
            lock.unlock();
            cache().dropSession(id);
            PageSketches::instance().remove(id);
            return;
        }
    }
}

quint64 ThumbnailProvider::idOf(const DocumentSession* session) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    for (auto& [id, s]: r.sessions) {
        if (s == session) {
            return id;
        }
    }
    return 0;
}

DocumentSession* ThumbnailProvider::acquireSession(quint64 id) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    auto it = r.sessions.find(id);
    if (it == r.sessions.end()) {
        return nullptr;
    }
    ++r.busy[id];
    return it->second;
}

void ThumbnailProvider::releaseSession(quint64 id) {
    auto& r = registry();
    std::lock_guard lock(r.mtx);
    --r.busy[id];
    r.idle.notify_all();
}

QImage ThumbnailProvider::render(DocumentSession& session, size_t pageNo, int width) {
    ++renders;
    return renderDocument(*session.getDocument(), pageNo, width);
}

QImage ThumbnailProvider::renderDocument(Document& document, size_t pageNo, int width) {
    PageRef page;
    {
        std::shared_lock lock(document);
        if (pageNo >= document.getPageCount()) {
            return {};
        }
        page = document.getPage(pageNo);
    }
    return renderPage(document, page, width);
}

QImage ThumbnailProvider::renderPage(Document& doc, const PageRef& page, int width, const XojPdfDocument* pdf) {
    double zoom = 1;
    XojPdfPageSPtr pdfPage;
    QImage img;
    {
        std::shared_lock lock(doc);
        zoom = width / page->getWidth();
        img = QImage(width, std::max(1, static_cast<int>(page->getHeight() * zoom)), QImage::Format_ARGB32_Premultiplied);
        if (page->getBackgroundType().isPdfPage()) {
            pdfPage = pdf ? pdf->getPage(page->getPdfPageNr()) : doc.getPdfPage(page->getPdfPageNr());
        }
    }
    img.fill(Qt::white);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(img.bits(), CAIRO_FORMAT_ARGB32, img.width(),
                                                                   img.height(), static_cast<int>(img.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, zoom, zoom);

    // Like upstream's SaveJob::updatePreview / PreviewJob: without a PdfCache, render the PDF background directly.
    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
    if (page->getBackgroundType().isPdfPage()) {
        if (pdfPage) {
            pdfPage->render(cr);  // (the page keeps its PDF document)
        }
        flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
    } else {
        flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
    }
    {
        std::shared_lock lock(doc);
        DocumentView view;
        view.drawPage(page, cr, true, flags);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return img;
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize) {
    auto* response = new ThumbnailResponse;
    // id: <session>/<page>/<revision>[/<only for QML: ask again>]
    const QStringList parts = id.split('/');
    const quint64 sessionId = parts.value(0).toULongLong();
    const size_t page = parts.value(1).toULongLong();
    const quint64 revision = parts.value(2).toULongLong();
    const QString key = QString("%1/%2").arg(sessionId).arg(revision);
    const int asked = requestedSize.width() > 0 ? requestedSize.width() : 160;
    const int width = (asked + WIDTH_STEP - 1) / WIDTH_STEP * WIDTH_STEP;

    // Kept already (that width, or a bigger one to scale down), or small enough for the page's preview or sketch: no
    // drawing at all
    QImage kept = cache().find(key, width);
    if (kept.isNull() && width <= std::max(PageSketches::instance().previewWidth(), PageSketches::instance().width())) {
        if (QImage sketch = PageSketches::instance().imageOfRevision(sessionId, revision); sketch.width() >= width) {
            kept = sketch.width() == width ? sketch : sketch.scaledToWidth(width, Qt::SmoothTransformation);
        }
    }
    if (!kept.isNull()) {
        response->image = std::move(kept);
        QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
        return response;
    }

    {
        auto& r = registry();
        std::lock_guard lock(r.mtx);
        if (!r.sessions.count(sessionId)) {
            QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
            return response;
        }
    }
    // The one asked for last first: that is what is in view now
    static std::atomic<int> order{0};
    pool().start(QRunnable::create([response, sessionId, page, width, key, revision] {
        QImage img;
        if (!response->cancelled) {
            // The pages in view on the canvas first (a thumbnail draws with the document's PDF instance, which
            // renders one page at a time)
            RenderService::waitForVisiblePages(std::chrono::milliseconds(500));
        }
        // The session only now, and only if its document is still open: closing it waits for the thumbnails being
        // drawn, not for those still queued
        DocumentSession* session = response->cancelled ? nullptr : acquireSession(sessionId);
        if (session) {  // (else scrolled away or closed meanwhile: not drawn)
            ++renders;
            if (auto stamp = session->pageOfRevision(revision)) {
                img = renderPage(*session->getDocument(), stamp->page, width);
                cache().put(key, width, img);
                PageSketches::instance().offer(sessionId, stamp->id, revision, img);
            } else {
                img = renderDocument(*session->getDocument(), page, width);  // (an outdated address: not kept)
            }
            releaseSession(sessionId);
        }
        QMetaObject::invokeMethod(
                response,
                [response, img = std::move(img)]() mutable {
                    response->image = std::move(img);
                    Q_EMIT response->finished();
                },
                Qt::QueuedConnection);
    }), ++order);
    return response;
}

}  // namespace xqt

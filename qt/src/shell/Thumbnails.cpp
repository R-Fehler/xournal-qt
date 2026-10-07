#include "Thumbnails.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>

#include <cairo.h>

#include "model/Document.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "pdf/base/XojPdfPage.h"
#include "render/RenderService.h"

#include "AsyncImage.h"
#include "ImageWorkers.h"
#include "PageSketches.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"
#include "PageNoteSpace.h"
#include "DarkPages.h"

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

class ThumbnailResponse final: public AsyncImageResponse {
public:
    QQuickTextureFactory* textureFactory() const override {
        if (dark && !image.isNull()) {  // (dark pages: the thumbnail as the canvas shows the page)
            QImage shown = image;
            dark::apply(shown, dark::paperOfImage(shown));
            return QQuickTextureFactory::textureFactoryForImage(shown);
        }
        return AsyncImageResponse::textureFactory();
    }
    bool dark = false;
};

/// The drawn thumbnails of each page revision, by width (a smaller one is scaled from the smallest bigger one); the
/// least recently used go first.
struct Key {
    quint64 session = 0;
    quint64 revision = 0;
    int width = 0;
    auto operator<=>(const Key&) const = default;
};
LruImageCache<Key>& cache() {
    static LruImageCache<Key> c(ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024 * 3 / 4);
    return c;
}
/// The smallest kept thumbnail of a revision at least `width` wide (`touch`: it counts as used)
std::optional<std::pair<Key, QImage>> keptAtLeast(quint64 session, quint64 revision, int width, bool touch) {
    return cache().lowerBound(
            Key{session, revision, width},
            [&](const Key& k) { return k.session == session && k.revision == revision; }, touch);
}
std::atomic<int> renders{0};

}  // namespace

void ThumbnailProvider::setCacheLimit(qint64 bytes) {
    cache().setLimit(bytes - bytes / 4);
    PageSketches::instance().setBudget(bytes / 4);
}

QImage ThumbnailProvider::keptImage(quint64 session, quint64 revision, int width) {
    const auto kept = keptAtLeast(session, revision, width, false);
    return kept ? kept->second : QImage();
}
qint64 ThumbnailProvider::cacheBytes() { return cache().bytes(); }
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
            cache().removeIf([id](const Key& k) { return k.session == id; });
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
            notespace::renderPdf(cr, *page, *pdfPage);  // (the page keeps its PDF document; at its offset)
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
    // id: <session>/<page>/<revision>[/<only for QML: ask again>][~dark]
    QString plain = id;
    response->dark = dark::takeSuffix(plain);
    const QStringList parts = plain.split('/');
    const quint64 sessionId = parts.value(0).toULongLong();
    const size_t page = parts.value(1).toULongLong();
    const quint64 revision = parts.value(2).toULongLong();
    const int asked = requestedSize.width() > 0 ? requestedSize.width() : 160;
    const int width = (asked + WIDTH_STEP - 1) / WIDTH_STEP * WIDTH_STEP;

    // Kept already (that width, or a bigger one to scale down), or small enough for the page's preview or sketch: no
    // drawing at all
    QImage kept;
    if (const auto k = keptAtLeast(sessionId, revision, width, true)) {
        kept = k->first.width == width ? k->second : k->second.scaledToWidth(width, Qt::SmoothTransformation);
    }
    if (kept.isNull() && width <= std::max(PageSketches::instance().previewWidth(), PageSketches::instance().width())) {
        if (QImage sketch = PageSketches::instance().imageOfRevision(sessionId, revision); sketch.width() >= width) {
            kept = sketch.width() == width ? sketch : sketch.scaledToWidth(width, Qt::SmoothTransformation);
        }
    }
    if (!kept.isNull()) {
        response->finish(std::move(kept));
        return response;
    }

    {
        auto& r = registry();
        std::lock_guard lock(r.mtx);
        if (!r.sessions.count(sessionId)) {
            response->finish({});
            return response;
        }
    }
    // The one asked for last first: that is what is in view now
    static std::atomic<int> order{0};
    ImageWorkers::respond(
            ImageWorkers::Pool::Thumbnails, response,
            [response, sessionId, page, width, revision]() -> QImage {
                // The pages in view on the canvas first (a thumbnail draws with the document's PDF instance, which
                // renders one page at a time)
                RenderService::waitForVisiblePages(std::chrono::milliseconds(500));
                // The session only now, and only if its document is still open: closing it waits for the thumbnails
                // being drawn, not for those still queued
                DocumentSession* session = response->isCancelled() ? nullptr : acquireSession(sessionId);
                if (!session) {
                    return {};  // (scrolled away or closed meanwhile: not drawn)
                }
                ++renders;
                QImage img;
                if (auto stamp = session->pageOfRevision(revision)) {
                    img = renderPage(*session->getDocument(), stamp->page, width);
                    cache().put(Key{sessionId, revision, width}, img);
                    PageSketches::instance().offer(sessionId, stamp->id, revision, img);
                } else {
                    img = renderDocument(*session->getDocument(), page, width);  // (an outdated address: not kept)
                }
                releaseSession(sessionId);
                return img;
            },
            ++order);
    return response;
}

}  // namespace xqt

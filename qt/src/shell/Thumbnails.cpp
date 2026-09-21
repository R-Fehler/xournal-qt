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

/// The drawn thumbnails of each page ("<session>/<page>"), of its latest revision only, by width; the least recently
/// used go first.
class Cache {
public:
    QImage find(const QString& page, const QString& revision, int width) {
        std::lock_guard lock(mtx);
        auto it = pages.find(page);
        if (it == pages.end() || it->second.revision != revision) {
            return {};
        }
        // That width, or scaled down from the smallest bigger one
        auto w = it->second.widths.lower_bound(width);
        if (w == it->second.widths.end()) {
            return {};
        }
        lru.splice(lru.begin(), lru, w->second.used);
        if (w->first == width) {
            return w->second.image;
        }
        return w->second.image.scaledToWidth(width, Qt::SmoothTransformation);
    }
    void put(const QString& page, const QString& revision, int width, const QImage& image) {
        if (image.isNull()) {
            return;
        }
        std::lock_guard lock(mtx);
        auto& slot = pages[page];
        if (slot.revision != revision) {
            dropWidths(slot);  // an older picture of the page
            slot.revision = revision;
        }
        if (slot.widths.count(width)) {
            return;
        }
        lru.push_front({page, width});
        slot.widths.emplace(width, Entry{image, lru.begin()});
        bytes += image.sizeInBytes();
        shrink();
    }
    void dropSession(quint64 session) {
        std::lock_guard lock(mtx);
        const QString prefix = QString::number(session) + '/';
        for (auto it = pages.begin(); it != pages.end();) {
            if (it->first.startsWith(prefix)) {
                dropWidths(it->second);
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
    using Use = std::list<std::pair<QString, int>>;  ///< page, width; front: used last
    struct Entry {
        QImage image;
        Use::iterator used;
    };
    struct Slot {
        QString revision;
        std::map<int, Entry> widths;
    };
    void dropWidths(Slot& slot) {
        for (auto& [w, e]: slot.widths) {
            bytes -= e.image.sizeInBytes();
            lru.erase(e.used);
        }
        slot.widths.clear();
    }
    void shrink() {
        while (bytes > limit && !lru.empty()) {
            const auto [page, width] = lru.back();
            lru.pop_back();
            auto it = pages.find(page);
            auto w = it->second.widths.find(width);
            bytes -= w->second.image.sizeInBytes();
            it->second.widths.erase(w);
            if (it->second.widths.empty()) {
                pages.erase(it);
            }
        }
    }
    std::mutex mtx;
    std::unordered_map<QString, Slot> pages;
    Use lru;
    qint64 bytes = 0;
    qint64 limit = ThumbnailProvider::DEFAULT_CACHE_MB * 1024 * 1024;
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

void ThumbnailProvider::setCacheLimit(qint64 bytes) { cache().setLimit(bytes); }
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
            cache().dropSession(id);
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

QImage ThumbnailProvider::render(DocumentSession& session, size_t pageNo, int width) {
    ++renders;
    return renderDocument(*session.getDocument(), pageNo, width);
}

QImage ThumbnailProvider::renderDocument(Document& document, size_t pageNo, int width) {
    Document* doc = &document;
    std::shared_lock lock(*doc);
    if (pageNo >= doc->getPageCount()) {
        return {};
    }
    PageRef page = doc->getPage(pageNo);
    const double zoom = width / page->getWidth();
    QImage img(width, std::max(1, static_cast<int>(page->getHeight() * zoom)), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::white);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(img.bits(), CAIRO_FORMAT_ARGB32, img.width(),
                                                                   img.height(), static_cast<int>(img.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, zoom, zoom);

    // Like upstream's SaveJob::updatePreview / PreviewJob: without a PdfCache, render the PDF background directly.
    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
    if (page->getBackgroundType().isPdfPage()) {
        if (auto pdfPage = doc->getPdfPage(page->getPdfPageNr())) {
            pdfPage->render(cr);
        }
        flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
    } else {
        flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
    }
    DocumentView view;
    view.drawPage(page, cr, true, flags);
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
    const QString slot = parts.mid(0, 2).join('/');
    const QString revision = parts.value(2);
    const int asked = requestedSize.width() > 0 ? requestedSize.width() : 160;
    const int width = (asked + WIDTH_STEP - 1) / WIDTH_STEP * WIDTH_STEP;

    // Kept already (that width, or a bigger one to scale down): no drawing at all
    if (QImage kept = cache().find(slot, revision, width); !kept.isNull()) {
        response->image = std::move(kept);
        QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
        return response;
    }

    auto& r = registry();
    DocumentSession* session = nullptr;
    {
        std::lock_guard lock(r.mtx);
        if (auto it = r.sessions.find(sessionId); it != r.sessions.end()) {
            session = it->second;
            ++r.busy[sessionId];
        }
    }
    if (!session) {
        QMetaObject::invokeMethod(response, &QQuickImageResponse::finished, Qt::QueuedConnection);
        return response;
    }
    // The one asked for last first: that is what is in view now
    static std::atomic<int> order{0};
    pool().start(QRunnable::create([response, session, sessionId, page, width, slot, revision] {
        QImage img;
        if (!response->cancelled) {  // (scrolled away meanwhile: not drawn)
            img = render(*session, page, width);
            cache().put(slot, revision, width, img);
        }
        {
            auto& r = registry();
            std::lock_guard lock(r.mtx);
            --r.busy[sessionId];
            r.idle.notify_all();
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

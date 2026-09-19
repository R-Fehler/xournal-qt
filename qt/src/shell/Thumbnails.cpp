#include "Thumbnails.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <shared_mutex>

#include <QRunnable>
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
    QImage image;
};
}  // namespace

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
    Document* doc = session.getDocument();
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
    // id: <session>/<page>/<revision>
    const QStringList parts = id.split('/');
    const quint64 sessionId = parts.value(0).toULongLong();
    const size_t page = parts.value(1).toULongLong();
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 160;

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
    QThreadPool::globalInstance()->start([response, session, sessionId, page, width] {
        QImage img = render(*session, page, width);
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
    });
    return response;
}

}  // namespace xqt

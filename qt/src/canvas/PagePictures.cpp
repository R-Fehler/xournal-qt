/*
 * xournal-qt: where the pictures of a page are (see PagePictures.h).
 *
 * @license GNU GPLv2 or later
 */
#include "PagePictures.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <mutex>
#include <set>

#include <poppler.h>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentTextIndex.h"

namespace xqt {

std::vector<QRectF> imageRects(const XojPage& page) {
    std::vector<QRectF> out;
    for (const Layer* layer: page.getLayersView()) {
        if (!layer->isVisible()) {
            continue;
        }
        for (const Element* e: layer->getElementsView()) {
            if (e->getType() == ELEMENT_IMAGE) {
                const auto& b = e->getBoundingBox();
                out.emplace_back(b.x, b.y, b.width, b.height);
            }
        }
    }
    return out;
}

namespace {
QThreadPool& pool() {
    static QThreadPool* p = [] {
        auto* tp = new QThreadPool;
        tp->setMaxThreadCount(1);
        tp->setThreadPriority(QThread::LowestPriority);
        tp->setExpiryTimeout(5000);
        return tp;
    }();
    return *p;
}
}  // namespace

struct PdfPictures::Shared {
    std::mutex mtx;
    fs::path file;
    std::map<int, std::vector<QRectF>> done;
    std::set<int> queued;
    std::mutex readerMtx;  ///< the worker's poppler instance:
    std::unique_ptr<PdfLayoutReader> reader;
    fs::path readerFile;
    bool gone = false;
    int reads = 0;
};

PdfPictures::PdfPictures(QObject* parent): QObject(parent), shared(std::make_shared<Shared>()) {}

PdfPictures::~PdfPictures() {
    std::lock_guard lock(shared->mtx);
    shared->gone = true;
}

int PdfPictures::pagesRead() const {
    std::lock_guard lock(shared->mtx);
    return shared->reads;
}

std::vector<QRectF> PdfPictures::read(void* popplerPage) {
    auto* page = static_cast<PopplerPage*>(popplerPage);
    std::vector<QRectF> out;
    // (top left origin, PDF points: the page as poppler-glib's cairo output places it)
    GList* mapping = poppler_page_get_image_mapping(page);
    for (GList* l = mapping; l; l = l->next) {
        const auto* m = static_cast<PopplerImageMapping*>(l->data);
        out.emplace_back(QPointF(m->area.x1, m->area.y1), QPointF(m->area.x2, m->area.y2));
        out.back() = out.back().normalized();
    }
    poppler_page_free_image_mapping(mapping);
    return out;
}

std::optional<std::vector<QRectF>> PdfPictures::pictures(const fs::path& pdf, int pdfPage) {
    if (pdf.empty() || pdfPage < 0) {
        return std::vector<QRectF>{};
    }
    {
        std::lock_guard lock(shared->mtx);
        if (shared->file != pdf) {
            shared->file = pdf;
            shared->done.clear();
            shared->queued.clear();
        }
        if (auto it = shared->done.find(pdfPage); it != shared->done.end()) {
            return it->second;
        }
        if (!shared->queued.insert(pdfPage).second) {
            return std::nullopt;  // (being read)
        }
    }
    QPointer<PdfPictures> self(this);
    std::shared_ptr<Shared> s = shared;
    pool().start([s, self, pdf, pdfPage] {
        {
            std::lock_guard lock(s->mtx);
            if (s->gone || s->file != pdf) {
                return;
            }
        }
        std::vector<QRectF> found;
        {
            std::lock_guard readLock(s->readerMtx);
            if (!s->reader || s->readerFile != pdf) {
                s->reader = std::make_unique<PdfLayoutReader>(pdf);
                s->readerFile = pdf;
            }
            if (PopplerDocument* doc = s->reader->document(); doc && pdfPage < poppler_document_get_n_pages(doc)) {
                if (PopplerPage* page = poppler_document_get_page(doc, pdfPage)) {
                    found = read(page);
                    g_object_unref(page);
                }
            }
        }
        {
            std::lock_guard lock(s->mtx);
            if (s->gone || s->file != pdf) {
                return;
            }
            s->done[pdfPage] = std::move(found);
            s->queued.erase(pdfPage);
            ++s->reads;
        }
        QMetaObject::invokeMethod(
                QCoreApplication::instance(),
                [self] {
                    if (self) {
                        Q_EMIT self->known();
                    }
                },
                Qt::QueuedConnection);
    });
    return std::nullopt;
}

}  // namespace xqt

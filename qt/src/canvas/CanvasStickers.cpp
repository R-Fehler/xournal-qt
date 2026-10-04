/*
 * xournal-qt: stickers in a view (qt/docs/stickers.md): the selection as a sticker's content, the sticker written off
 * the UI thread (with the picture of the page's background behind it), and a sticker pasted on the current page.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <mutex>
#include <shared_mutex>

#include <QBuffer>
#include <QFile>
#include <QImage>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "render/RegionImage.h"
#include "session/StickerFile.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "StickyNotes.h"

using xoj::util::Rectangle;

namespace xqt {

namespace {
void unite(std::optional<Rectangle<double>>& all, const Rectangle<double>& r) {
    if (all) {
        all->unite(r);
    } else {
        all = r;
    }
}

bool isMarkdownText(const Element& e) {
    return e.getType() == ELEMENT_TEXT && static_cast<const Text&>(e).isMarkdown();
}
}  // namespace

void CanvasView::selectedWith(const PageRef& page, std::vector<xoj::util::Point<double>> shape) {
    lassoPage = shape.size() >= 3 ? page : PageRef();
    lasso = lassoPage ? std::move(shape) : std::vector<xoj::util::Point<double>>();
}

auto CanvasView::stickerSource() const -> std::optional<StickerSource> {
    StickerSource source;
    std::optional<Rectangle<double>> bounds;
    const auto addElement = [&](const Element* e) {
        unite(bounds, e->getBoundingBox());
        source.content.markdown.push_back(isMarkdownText(*e));
        source.content.elements.push_back(e->clone());
    };
    const auto addNote = [&](const Layer* layer) {
        if (const auto look = sticky::lookOf(*layer)) {
            unite(bounds, look->rect);
            source.content.notes.emplace_back(layer->clone());
        }
    };
    Document* doc = session.getDocument();
    std::shared_lock lock(*doc);
    if (selection) {
        // Upstream's selection holds its elements out of their layer where they were when it was made, and applies
        // its move, scale and rotation only when it ends (EditSelectionContents::makeMoveEffective): the copies get
        // them here, so the sticker is what the page shows
        const Rectangle<double> now = selection->getRect();
        const Rectangle<double> was = selection->getOriginalBounds();
        const Rectangle<double> snapped = selection->getSnappedBounds();
        const double rotation = selection->getRotation();
        const bool restoreLineWidth = session.getSettings()->getRestoreLineWidthEnabled();
        const double mx = now.x - was.x;
        const double my = now.y - was.y;
        const bool scale = now.width != was.width || now.height != was.height;
        for (const Element* e: selection->getElementsView()) {
            ElementPtr copy = e->clone();
            if (mx != 0 || my != 0) {
                copy->move(mx, my);
            }
            if (scale && was.width != 0 && was.height != 0) {
                copy->scale(now.x, now.y, now.width / was.width, now.height / was.height, 0, restoreLineWidth);
            }
            if (std::abs(rotation) > 1e-12) {
                copy->rotate(snapped.x + snapped.width / 2, snapped.y + snapped.height / 2, rotation);
            }
            unite(bounds, copy->getBoundingBox());
            source.content.markdown.push_back(isMarkdownText(*copy));
            source.content.elements.push_back(std::move(copy));
        }
        source.page = selection->getSourcePage();
    } else if (mixedSelection->active()) {
        source.page = mixedSelection->selectedPage()->getPage();
        for (const Layer* note: mixedSelection->notes()) {
            addNote(note);
        }
        // (the page's elements in their order on the page, as copying keeps them)
        std::vector<std::pair<std::pair<Layer::Index, Element::Index>, const Element*>> ordered;
        for (const auto& item: mixedSelection->items()) {
            ordered.push_back({{sticky::layerIdOf(*source.page, item.layer), item.layer->indexOf(item.element)},
                               item.element});
        }
        std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [where, e]: ordered) {
            addElement(e);
        }
    } else if (stickyNotes->hasSelection() && stickyNotes->selectedPage()) {
        source.page = stickyNotes->selectedPage()->getPage();
        addNote(stickyNotes->selectedLayer());
    }
    if (!bounds || !source.page) {
        return std::nullopt;
    }
    source.content.bounds = *bounds;
    source.paper = source.page->getBackgroundColor();
    source.pictureOffered = source.page->getBackgroundType().isPdfPage() ||
                            source.page->getBackgroundType().isImagePage();
    // The lasso it was made with, while everything still lies inside it (moved since: the rectangle)
    if (lassoPage == source.page && lasso.size() >= 3) {
        double minX = lasso.front().x, maxX = minX, minY = lasso.front().y, maxY = minY;
        for (const auto& p: lasso) {
            minX = std::min(minX, p.x);
            maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        constexpr double SLACK = 3;  // (a stroke's width reaches a little beyond the points the lasso took)
        if (bounds->x >= minX - SLACK && bounds->y >= minY - SLACK && bounds->x + bounds->width <= maxX + SLACK &&
            bounds->y + bounds->height <= maxY + SLACK) {
            source.outline = lasso;
        }
    }
    return source;
}

bool CanvasView::saveSticker(std::shared_ptr<StickerSource> source, bool withPicture, fs::path target,
                             std::function<void(const QString&, const std::string&)> done) {
    if (!source || (stickerJob.valid() && stickerJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)) {
        return false;
    }
    Document* doc = session.getDocument();
    std::optional<region::Request> request;
    XojPdfPageSPtr pending;
    if (withPicture && source->page) {
        std::shared_lock lock(*doc);
        const PageRef& page = source->page;
        if (const auto area = region::onPage(source->content.bounds, page->getWidth(), page->getHeight())) {
            request.emplace();
            request->area = *area;
            request->outline = source->outline;
            request->layers = false;     // (the background only: the content is in the sticker itself)
            request->forScreen = false;
            request->scale = region::scaleFor(*area, 0);  // (200 dpi, within the size limit)
            if (page->getBackgroundType().isPdfPage()) {
                pending = rasterPendingPdfPage(page->getPdfPageNr());  // (a pasted page whose PDF is not merged yet)
            }
        }
    }
    stickerJob = std::async(std::launch::async, [this, doc, source, request, pending, target = std::move(target),
                                                 done = std::move(done)] {
        std::optional<stickers::Picture> picture;
        if (request) {
            // (the document read under its shared lock, the PDF drawn without it: RegionRender.h)
            const QImage image = region::renderImage(*doc, source->page, *request, pending);
            if (!image.isNull()) {
                QByteArray png;
                QBuffer buffer(&png);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "PNG");
                picture = stickers::Picture{png.toStdString(), request->area};
            }
        }
        auto document = stickers::makeDocument(std::move(source->content), source->paper, picture);
        std::string error;
        std::string bytes;
        if (stickers::write(*document, target, &error)) {
            // (read back: what is on the clipboard is what was written)
            if (auto read = stickers::read(target, &error)) {
                bytes = stickers::clipboardBytes(*read);
            }
        } else if (error.empty()) {
            error = "could not write the file";
        }
        QMetaObject::invokeMethod(
                this, [done, error = QString::fromStdString(error), bytes = std::move(bytes)] { done(error, bytes); },
                Qt::QueuedConnection);
    });
    return true;
}

bool CanvasView::loadSticker(fs::path file,
                             std::function<void(const QString&, const std::string&, bool)> done) {
    if (stickerJob.valid() && stickerJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return false;
    }
    stickerJob = std::async(std::launch::async, [this, file = std::move(file), done = std::move(done)] {
        std::string error;
        std::string bytes;
        std::string ext = file.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
        const bool picture = ext != ".xopp";
        if (picture) {
            QFile f(QString::fromStdString(file.string()));
            if (f.open(QIODevice::ReadOnly)) {
                bytes = f.readAll().toStdString();
            }
            if (bytes.empty()) {
                error = "could not read the picture";
            }
        } else if (auto content = stickers::read(file, &error)) {
            bytes = stickers::clipboardBytes(*content);
        }
        QMetaObject::invokeMethod(
                this,
                [done, error = QString::fromStdString(error), bytes = std::move(bytes), picture] {
                    done(error, bytes, picture);
                },
                Qt::QueuedConnection);
    });
    return true;
}

bool CanvasView::pasteSticker(const std::string& bytes) {
    const size_t pNr = currentPageNo();
    if (pNr >= pages.size() || bytes.empty() || session.isReadOnly() || readingOnly) {
        return false;
    }
    const auto scope = actingScope(pNr);
    // In the middle of the visible part of the page
    const double zoom = viewController.zoom();
    const QRectF pageRect = layout.pageRect(pNr, zoom);
    QRectF visible = pageRect.intersected(viewController.visibleContentRect());
    if (visible.isEmpty()) {
        visible = pageRect;
    }
    const QPointF centre = (visible.center() - pageRect.topLeft()) / zoom;
    endTextEditing();
    return mixedSelection->pasteAt(pNr, bytes, centre, "Paste sticker");
}

}  // namespace xqt

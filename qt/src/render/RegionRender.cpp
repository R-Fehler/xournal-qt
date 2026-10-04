#include "RegionRender.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include "model/Document.h"
#include "model/NoteSpace.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "view/DocumentView.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/BackgroundView.h"

#include "PageRaster.h"

using xoj::util::Rectangle;

namespace xqt::region {

double scaleFor(const Rectangle<double>& area, double screenScale, double minDpi, double maxPixels) {
    double scale = std::max(screenScale, minDpi / 72.0);
    if (const double pixels = area.width * area.height * scale * scale; pixels > maxPixels && pixels > 0) {
        scale *= std::sqrt(maxPixels / pixels);
        // (the sides are rounded up: a little less, so that the pixels stay within the limit)
        while (scale > 0 && std::ceil(area.width * scale) * std::ceil(area.height * scale) > maxPixels) {
            scale *= 0.999;
        }
    }
    return scale;
}

std::optional<Rectangle<double>> onPage(const Rectangle<double>& area, double pageWidth, double pageHeight) {
    auto part = area.intersects(Rectangle<double>(0, 0, pageWidth, pageHeight));
    if (!part || part->width <= 0 || part->height <= 0) {
        return std::nullopt;
    }
    return part;
}

xoj::util::CairoSurfaceSPtr render(Document& doc, const PageRef& page, const Request& request, XojPdfPageSPtr pdf) {
    if (!page || !(request.scale > 0)) {
        return {};
    }
    double width = 0;
    double height = 0;
    bool pdfFirst = false;
    NoteSpace space;
    {
        std::shared_lock lock(doc);
        width = page->getWidth();
        height = page->getHeight();
        space = page->getNoteSpace();
        // (as PageRaster: a hidden background layer shows no PDF)
        pdfFirst = page->getBackgroundType().isPdfPage() && page->isLayerVisible(0);
        if (pdfFirst && !pdf) {
            pdf = doc.getPdfPage(page->getPdfPageNr());
        }
    }
    const auto area = onPage(request.area, width, height);
    if (!area) {
        return {};
    }
    const int w = std::max(1, static_cast<int>(std::ceil(area->width * request.scale - 1e-6)));
    const int h = std::max(1, static_cast<int>(std::ceil(area->height * request.scale - 1e-6)));
    xoj::util::CairoSurfaceSPtr surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h), xoj::util::adopt);
    if (cairo_surface_status(surface.get()) != CAIRO_STATUS_SUCCESS) {
        return {};
    }
    cairo_t* cr = cairo_create(surface.get());
    cairo_scale(cr, request.scale, request.scale);
    cairo_translate(cr, -area->x, -area->y);
    cairo_rectangle(cr, area->x, area->y, area->width, area->height);
    cairo_clip(cr);
    if (request.outline.size() >= 3) {
        cairo_move_to(cr, request.outline.front().x, request.outline.front().y);
        for (size_t i = 1; i < request.outline.size(); ++i) {
            cairo_line_to(cr, request.outline[i].x, request.outline[i].y);
        }
        cairo_close_path(cr);
        cairo_clip(cr);  // (antialiased: a soft edge, transparent outside)
    }

    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
    if (pdfFirst) {
        // The PDF page without the document's lock (PDF pages are immutable), at its offset (space for notes)
        if (pdf) {
            cairo_save(cr);
            if (!space.empty()) {
                cairo_set_source_rgb(cr, 1, 1, 1);
                cairo_paint(cr);
                cairo_translate(cr, space.left, space.top);
                cairo_rectangle(cr, 0, 0, width - space.left - space.right, height - space.top - space.bottom);
                cairo_clip(cr);
            }
            pdf->render(cr);
            cairo_restore(cr);
        }
        flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
    } else {
        // (a hidden background layer: the paper's color, not the screen's checkerboard)
        flags.forceBackgroundColor = xoj::view::FORCE_AT_LEAST_BACKGROUND_COLOR;
    }
    {
        std::shared_lock lock(doc);
        std::optional<PageRaster::ScreenScope> screen;
        if (request.forScreen) {
            screen.emplace();
        }
        if (request.layers) {
            DocumentView view;
            view.drawPage(page, cr, true, flags);
        } else {
            xoj::view::BackgroundView::createForPage(page, flags, nullptr)->draw(cr);
        }
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface.get());
    return surface;
}

}  // namespace xqt::region

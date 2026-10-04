/*
 * xournal-qt: the picture of an area of a page (RegionRender.h) as a QImage, for the Qt layers (header only: the
 * render library is Qt-free). The image shares the surface's pixels, and its resolution is the picture's (dots per
 * meter), so other apps paste it at the size it has on the page.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cmath>

#include <QImage>

#include "RegionRender.h"

namespace xqt::region {

inline QImage renderImage(Document& doc, const PageRef& page, const Request& request, XojPdfPageSPtr pdf = nullptr) {
    xoj::util::CairoSurfaceSPtr surface = render(doc, page, request, std::move(pdf));
    if (!surface) {
        return {};
    }
    cairo_surface_t* s = surface.release();
    QImage image(cairo_image_surface_get_data(s), cairo_image_surface_get_width(s), cairo_image_surface_get_height(s),
                 cairo_image_surface_get_stride(s), QImage::Format_ARGB32_Premultiplied,
                 [](void* data) { cairo_surface_destroy(static_cast<cairo_surface_t*>(data)); }, s);
    const int dotsPerMeter = static_cast<int>(std::lround(request.scale * 72.0 / 0.0254));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    return image;
}

}  // namespace xqt::region

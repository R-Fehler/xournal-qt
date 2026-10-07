/*
 * xournal-qt: a picture of an area of a page, as the screen shows it (qt/snip; qt/stickers reuses it for the picture
 * of the PDF behind a sticker).
 *
 * The area (page coordinates) is drawn at a scale of its own, not the view's: the background (paper, ruling, the PDF
 * page or the background image) and, unless only the background is asked for, every visible layer with what it holds
 * (ink, text, images, Markdown boxes through their renderer, sticky notes through theirs). What only lies over the
 * page on the screen is not part of it: the curtain, the hover pointer, a selection and its handles, the rectangle or
 * lasso being drawn. An outline (a lasso) cuts the picture to its shape: outside it the picture is transparent.
 *
 * Like PageRaster: the document is read under its shared lock, the PDF page is drawn without it (a slow PDF never
 * blocks the UI thread's exclusive lock). Any thread; poppler draws one page of an instance at a time.
 *
 * Qt-free (cairo); RegionImage.h makes a QImage of it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include "model/PageRef.h"
#include "pdf/base/XojPdfPage.h"
#include "util/Point.h"
#include "util/Rectangle.h"
#include "util/raii/CairoWrappers.h"

class Document;

namespace xqt::region {

/// The least resolution of a picture, in dots per inch (as PageClipboard::IMAGE_DPI: pages turned into pictures)
constexpr double MIN_DPI = 200;
/// The most pixels of a picture (about 4 megapixels, 16 MB)
constexpr double MAX_PIXELS = 4.0 * 1024 * 1024;

struct Request {
    xoj::util::Rectangle<double> area{0, 0, 0, 0};  ///< page coordinates (points); cut to the page
    /// A closed shape in page coordinates (a lasso): the picture is cut to it. Fewer than 3 points: the whole area.
    std::vector<xoj::util::Point<double>> outline;
    double scale = 1;        ///< pixels per point
    bool layers = true;      ///< false: the background only (paper, ruling, PDF, background image)
    bool forScreen = true;   ///< as the screen shows it (a covering sticky note that peeks: see through)
    /// false: no paper (its colour and ruling), transparent where nothing is drawn (pages exported as pictures,
    /// qt/docs/features/page-files.md); a PDF page and a background picture are still drawn
    bool paper = true;
};

/// Pixels per point for a picture of `area`: at least the screen's (`screenScale`: the view's zoom times its device
/// pixel ratio) and `minDpi`, but no more than `maxPixels` in all (then less, whatever the screen shows).
double scaleFor(const xoj::util::Rectangle<double>& area, double screenScale, double minDpi = MIN_DPI,
                double maxPixels = MAX_PIXELS);

/// The part of `area` on a page of this size (nullopt: none)
std::optional<xoj::util::Rectangle<double>> onPage(const xoj::util::Rectangle<double>& area, double pageWidth,
                                                   double pageHeight);

/// The picture: an ARGB32 image surface (premultiplied, cairo's: QImage::Format_ARGB32_Premultiplied) of the area cut
/// to the page, ceil(width × scale) by ceil(height × scale) pixels, transparent outside the outline. `pdf`: the PDF
/// page to draw instead of the document's (a pasted page whose PDF is not merged yet: RasterHost::rasterPendingPdfPage).
/// Null when nothing of the area is on the page.
xoj::util::CairoSurfaceSPtr render(Document& doc, const PageRef& page, const Request& request,
                                   XojPdfPageSPtr pdf = nullptr);

}  // namespace xqt::region

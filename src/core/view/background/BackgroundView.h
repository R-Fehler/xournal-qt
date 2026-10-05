/*
 * Xournal++
 *
 * Displays a background
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>  // xournal-qt: for backgroundDecorator
#include <memory>  // for unique_ptr

#include <cairo.h>  // for cairo_t

#include "model/PageRef.h"  // for ConstPageRef
#include "util/Color.h"     // for Color

#include "BackgroundFlags.h"

class PdfCache;
class PageType;
class XojPage;

namespace xoj {
namespace view {
class BackgroundView {
public:
    BackgroundView(double pageWidth, double pageHeight): pageWidth(pageWidth), pageHeight(pageHeight) {}
    virtual ~BackgroundView() = default;

    /**
     * @brief Draws the background on the entire mask represented by the cairo context cr
     *
     * Does nothing in the base class - used when the background drawing is suppressed.
     */
    virtual void draw(cairo_t* cr) const {}

    [[nodiscard]] static std::unique_ptr<BackgroundView> createRuled(double width, double height, Color backgroundColor,
                                                                     const PageType& pt, double lineWidthFactor = 1.0);

    [[nodiscard]] static std::unique_ptr<BackgroundView> createForPage(ConstPageRef page,
                                                                       xoj::view::BackgroundFlags bgFlags,
                                                                       PdfCache* pdfCache = nullptr);

protected:
    double pageWidth;
    double pageHeight;
};

/// xournal-qt: a frontend may draw over the background of a page with a pattern (textured paper,
/// qt/docs/dark-pages.md): createForPage hands it the background of every page that is not a PDF or an image page,
/// and draws what it returns. nullptr (upstream): as it is.
using BackgroundDecorator = std::unique_ptr<BackgroundView> (*)(std::unique_ptr<BackgroundView> view,
                                                                const XojPage& page);
inline std::atomic<BackgroundDecorator> backgroundDecorator{nullptr};
};  // namespace view
};  // namespace xoj

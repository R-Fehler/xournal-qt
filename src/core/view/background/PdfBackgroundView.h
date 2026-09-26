/*
 * Xournal++
 *
 * Displays a pdf background
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t

#include <cairo.h>  // for cairo_t

#include "BackgroundView.h"  // for BackgroundView

class PdfCache;

namespace xoj {
namespace view {

class PdfBackgroundView: public BackgroundView {
public:
    PdfBackgroundView(double pageWidth, double pageHeight, size_t pageNo, PdfCache* pdfCache = nullptr);
    /// xournal-qt: a page with space for notes (model/NoteSpace.h): the space is white, the PDF drawn at its offset
    PdfBackgroundView(double pageWidth, double pageHeight, size_t pageNo, PdfCache* pdfCache, double offsetX,
                      double offsetY, bool withSpace);
    virtual ~PdfBackgroundView() = default;

    /**
     * @brief Draws the background on the entire mask represented by the cairo context cr
     */
    void draw(cairo_t* cr) const override;

private:
    void drawPdf(cairo_t* cr) const;  // xournal-qt: upstream's draw()
    size_t pageNo;
    PdfCache* pdfCache = nullptr;
    double offsetX = 0;       // xournal-qt
    double offsetY = 0;       // xournal-qt
    bool withSpace = false;   // xournal-qt
};

};  // namespace view
};  // namespace xoj

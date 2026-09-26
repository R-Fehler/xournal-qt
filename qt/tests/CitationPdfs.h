/*
 * xournal-qt: test PDFs for the citation tests (qt/docs/citations.md): papers whose file names are numbers, with
 * their title in the PDF's /Title, or only as the largest text of the first page, and a reference list.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include <cairo-pdf.h>
#include <cairo.h>

namespace xqt::test {

struct PdfLine {
    std::string text;
    double size = 10;
    double y = 100;  ///< the baseline, from the top (points)
    double x = 60;
};

/// A one-page A4 PDF with these lines (Helvetica-like sans), `metaTitle` as its /Title ("": none) and, if given, an
/// arXiv stamp rotated in the left margin in 20 pt (as arXiv puts it on every paper, larger than the title).
inline void makePdf(const std::string& file, const std::string& metaTitle, const std::vector<PdfLine>& lines,
                    const std::string& arxivStamp = {}) {
    cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), 595, 842);
    if (!metaTitle.empty()) {
        cairo_pdf_surface_set_metadata(surface, CAIRO_PDF_METADATA_TITLE, metaTitle.c_str());
    }
    cairo_t* cr = cairo_create(surface);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    for (const PdfLine& l: lines) {
        cairo_set_font_size(cr, l.size);
        cairo_move_to(cr, l.x, l.y);
        cairo_show_text(cr, l.text.c_str());
    }
    if (!arxivStamp.empty()) {
        cairo_save(cr);
        cairo_set_font_size(cr, 20);
        cairo_move_to(cr, 30, 640);
        cairo_rotate(cr, -M_PI / 2);
        cairo_show_text(cr, arxivStamp.c_str());
        cairo_restore(cr);
    }
    cairo_show_page(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

/// A paper: its title in `titleSize` pt on top, authors, and some body text.
inline void makePaper(const std::string& file, const std::string& metaTitle, const std::string& shownTitle,
                      const std::string& authors, const std::string& arxivStamp = {}, double titleSize = 17) {
    makePdf(file, metaTitle,
            {{shownTitle, titleSize, 120, 80},
             {authors, 11, 150, 80},
             {"Abstract", 11, 190, 80},
             {"The dominant sequence transduction models are based on complex recurrent networks.", 10, 210, 80},
             {"We propose a new simple network architecture based solely on the mechanisms we study.", 10, 225, 80}},
            arxivStamp);
}

}  // namespace xqt::test

/*
 * xournal-qt: space for notes beside slides (qt/docs/note-space.md).
 *
 * A page gets blank space for notes on any of its four sides: it grows by the amounts, its PDF background is drawn at
 * (left, top) at its own scale, and everything on it moves by the change of (left, top), so the ink stays on the
 * slide. The model is upstream's XojPage with a NoteSpace (src/core/model/NoteSpace.h); this is the code that sets it
 * (one undo step for any number of pages) and the helpers that the renderers without a PdfCache use.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include <QPointF>
#include <QSizeF>

#include "model/NoteSpace.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"

#include <cairo.h>

class XojPage;
class XojPdfPage;

namespace xqt {

class DocumentSession;

namespace notespace {

/// The largest amount on a side (points; about 1.8 m).
inline constexpr double MAX_AMOUNT = 5000;

/// The slide of a page: its size without its space for notes.
QSizeF slideSize(const XojPage& page);
/// Where the page's PDF is drawn (the top left of the slide).
QPointF offsetOf(const XojPage& page);
/// A page can get space for notes: not an image background (upstream stretches the image over the page).
bool canHaveSpace(const XojPage& page);

/// Draw a PDF page on its page, at the page's offset (for renders without a PdfCache: thumbnails, previews). The
/// space around it is white.
void renderPdf(cairo_t* cr, const XojPage& page, const XojPdfPage& pdf, bool forPrinting = false);

/// Amounts for the four sides: points, or (relative) fractions of the slide's width (left, right) and height (top,
/// bottom), worked out per page.
struct Amounts {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool relative = false;
};
/// The space these amounts give a page (whole points, at most MAX_AMOUNT a side).
NoteSpace spaceFor(const XojPage& page, const Amounts& amounts);

/// Give pages (indices) the space `amounts` says (replacing what they had). One undo step. Returns how many pages
/// changed. UI thread.
size_t apply(DocumentSession& session, const std::vector<size_t>& pages, const Amounts& amounts);

/// Insert a blank page (plain, the size of the slide) after each of these pages. One undo step. Returns how many.
size_t insertBlankAfter(DocumentSession& session, std::vector<size_t> pages);

/// The undo of apply(): per page its space and size before and after; the elements move with (left, top).
class NoteSpaceUndoAction final: public UndoAction {
public:
    struct Change {
        PageRef page;
        NoteSpace from, to;
        double fromWidth = 0, fromHeight = 0, toWidth = 0, toHeight = 0;
    };
    explicit NoteSpaceUndoAction(std::vector<Change> changes);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override;

    /// Put the changes into effect (redo) or take them back (undo); fires the size change of each page.
    static void set(Control* control, const std::vector<Change>& changes, bool forward);

private:
    std::vector<Change> changes;
};

}  // namespace notespace
}  // namespace xqt

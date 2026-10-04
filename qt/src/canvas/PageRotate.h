/*
 * xournal-qt: turning pages by a quarter turn, to the left or to the right (qt/docs/page-rotation.md).
 *
 * The page's width and height swap and everything on it turns with it about the page:
 * - strokes, texts, images, TeX images and links turn (their points, their transformation: exact quarter turns, no
 *   rounding of the sine and cosine as with upstream's Element::rotate, which EditSelection uses for any angle);
 * - Markdown boxes and sticky notes (with what is on them) keep standing upright: their middle goes where the turned
 *   page puts it (a box is laid out and hit-tested unturned, MdBox.h; a note's text likewise);
 * - the page's own Markdown text stays at the page's margins and flows anew on the new size (as PageResize.h does);
 * - space for notes beside a slide turns with the page (the space on the left is above it after a turn to the right);
 * - the background: plain, ruled, graph, ... follow the new size; an image background is turned (a new picture,
 *   attached to the document); a PDF page is turned in a copy of it (PdfPages).
 * Any number of pages is one undo step. Undo puts every point and transformation back exactly as it was (they are
 * kept); redo turns again.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <QtGlobal>

#include "model/BackgroundImage.h"
#include "model/NoteSpace.h"
#include "model/PageRef.h"
#include "model/Point.h"
#include "undo/UndoAction.h"
#include "util/Matrix.h"

class Document;
class Element;
class XojPage;

namespace xqt {

class DocumentSession;

namespace pagerotate {

/// A quarter turn
enum class Turn {
    Left,   ///< counter-clockwise
    Right,  ///< clockwise
};

/// What happens to pages with a PDF background. The app turns them (InPdf) in every document (the author,
/// 2026-10-04); Kept is there for a document that must not get a merged PDF (none so far).
enum class PdfPages {
    /// Left as they are
    Kept,
    /// The PDF page itself is turned: a copy of it with its /Rotate changed goes into the document's merged PDF (as a
    /// pasted page does, PdfPageKeeper) and the page shows that. A PDF with notes then has it turned for every PDF
    /// app, with the embedded .xopp and our annotations to match (HybridPdf.h turns them with the page's /Rotate); a
    /// .xopp refers to the hidden ".name.pages.pdf" for it, which upstream Xournal++ reads as well. The PDF the
    /// document annotates is never changed.
    InPdf,
};

/// Why a page cannot be turned
enum class Blocked {
    None,        ///< it can
    PdfPage,     ///< a PDF page, and PdfPages::Kept
    PdfMissing,  ///< a PDF page whose PDF is not there (it cannot be turned with it)
};
/// `hasPdfPage`: whether the background PDF has a page of this number (also one on its way into the merged PDF).
/// Call under the document's lock.
Blocked blockedOf(const XojPage& page, PdfPages pdf, const std::function<bool(size_t)>& hasPdfPage);

/// What apply() would do to these pages.
struct Preview {
    size_t pages = 0;       ///< pages that turn
    size_t pdfPages = 0;    ///< left out: PDF pages (PdfPages::Kept)
    size_t missingPdf = 0;  ///< left out: their PDF page is missing
};
/// Takes the document's lock (shared).
Preview preview(DocumentSession& session, const std::vector<size_t>& pages, PdfPages pdf);

struct Result {
    size_t pages = 0;   ///< pages turned
    std::string error;  ///< nothing was turned because of it (the PDF pages could not be turned)
};
/// Turn these pages (indices) a quarter to the left or the right: one undo step. UI thread.
Result apply(DocumentSession& session, const std::vector<size_t>& pages, Turn turn, PdfPages pdf);

/// The undo of apply(): per page what it was before (its size, space for notes, background, the points and
/// transformations of its elements), and the steps of the page's Markdown text flowed anew.
class PageRotateUndoAction final: public UndoAction {
public:
    /// One element as it was
    struct ElementState {
        Element* element = nullptr;
        std::vector<Point> points;  ///< a stroke
        xoj::util::Matrix matrix;   ///< a text, image, TeX image or link
    };
    struct Change {
        PageRef page;
        double width = 0, height = 0;
        NoteSpace space;
        BackgroundImage image, turnedImage;  ///< an image background, and the picture turned
        bool pdf = false;       ///< its PDF page was turned too (PdfPages::InPdf)
        size_t fromPdfPage = 0, toPdfPage = 0;
        std::vector<ElementState> elements;
    };
    PageRotateUndoAction(DocumentSession& session, Turn turn, std::vector<Change> changes,
                         std::vector<UndoActionPtr> texts, quint64 pdfNumbering);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override;

private:
    DocumentSession& session;
    Turn turn;
    std::vector<Change> changes;
    std::vector<UndoActionPtr> texts;
    /// The numbers of the background PDF's pages were these then (DocumentSession::pdfNumbering): the PDF pages from
    /// before and after are still there under them
    quint64 pdfNumbering;
};

}  // namespace pagerotate
}  // namespace xqt

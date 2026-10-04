#include "PageRotate.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <glib.h>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/RectangularElement.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "session/PageMargins.h"
#include "session/StickyNote.h"
#include "util/Util.h"

#include "MdBox.h"
#include "PageResize.h"

namespace xqt::pagerotate {

namespace {

using xoj::util::Matrix;

/// The quarter turn of a page of this size, in page coordinates: what is at the top left goes to the top right
/// (Right) or the bottom left (Left). Exact: only 0, 1 and -1 in it.
Matrix turnOf(Turn turn, double width, double height) {
    return turn == Turn::Right ? Matrix{0, 1, -1, 0, {height, 0}} : Matrix{0, -1, 1, 0, {0, width}};
}

Turn opposite(Turn turn) { return turn == Turn::Right ? Turn::Left : Turn::Right; }

/// Space for notes turns with the page: the space on the left is at the top after a turn to the right
NoteSpace turned(const NoteSpace& s, Turn turn) {
    return turn == Turn::Right ? NoteSpace{s.bottom, s.left, s.top, s.right} : NoteSpace{s.top, s.right, s.bottom, s.left};
}

bool isPdf(const XojPage& page) { return page.getBackgroundType().isPdfPage(); }
bool isImage(const XojPage& page) { return page.getBackgroundType().isImagePage(); }

/// The page's own Markdown text (it is not turned: it flows anew at the margins; the document is locked)
const Text* pageBoxOf(const PageRef& page) { return pagesize::pageTextOf(page); }

/// The picture of an image background turned (attached to the document from then on); an empty one if it failed
BackgroundImage turnedImage(const BackgroundImage& image, Turn turn) {
    const GdkPixbuf* pixbuf = image.getPixbuf();
    if (!pixbuf) {
        return {};
    }
    GdkPixbuf* rotated = gdk_pixbuf_rotate_simple(
            pixbuf, turn == Turn::Right ? GDK_PIXBUF_ROTATE_CLOCKWISE : GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE);
    if (!rotated) {
        return {};
    }
    gchar* buffer = nullptr;
    gsize size = 0;
    GError* error = nullptr;
    const bool saved = gdk_pixbuf_save_to_buffer(rotated, &buffer, &size, "png", &error, nullptr);  // (lossless)
    g_object_unref(rotated);
    if (!saved) {
        g_warning("Could not turn the background picture: %s", error ? error->message : "");
        if (error) {
            g_error_free(error);
        }
        return {};
    }
    GBytes* bytes = g_bytes_new_take(buffer, size);
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    BackgroundImage out;
    out.loadFile(stream, fs::path("turned-background.png"), &error);
    g_object_unref(stream);
    if (error) {
        g_warning("Could not turn the background picture: %s", error->message);
        g_error_free(error);
        return {};
    }
    out.setAttach(true);  // (kept in the document: the user's picture file stays as it is)
    return out;
}

/// Copies of these pages of the background PDF (numbers), turned, added to the document's merged PDF (PdfPageKeeper):
/// the number of the first of them (npos: `error`). The PDF pages that are on their way into the merged PDF are
/// waited for first: a page turned twice in a row is read from it.
size_t turnPdfPages(DocumentSession& session, const std::vector<size_t>& numbers, Turn turn, std::string& error) {
    if (numbers.empty()) {
        return 0;
    }
    session.waitForMerges();
    fs::path bg;
    {
        std::shared_lock lock(*session.getDocument());
        bg = session.getDocument()->getPdfFilepath();
    }
    std::string pdf;
    const MergedPdf::Result r = MergedPdf::extract(bg, numbers, pdf, turn == Turn::Right ? 1 : -1);
    if (!r.ok) {
        error = r.error;
        return npos;
    }
    return session.addPdfPages(pdf, error);
}

/// The elements of a page as they are (not its own Markdown text: that flows anew). The document is locked.
std::vector<PageRotateUndoAction::ElementState> statesOf(const PageRef& page) {
    std::vector<PageRotateUndoAction::ElementState> states;
    const Text* flow = pageBoxOf(page);
    for (Layer* layer: page->getLayers()) {
        for (const auto& e: layer->getElements()) {
            if (e.get() == flow) {
                continue;
            }
            PageRotateUndoAction::ElementState s;
            s.element = e.get();
            if (const auto* stroke = dynamic_cast<const Stroke*>(e.get())) {
                s.points = stroke->getPointVector();
            } else if (const auto* r = dynamic_cast<const RectangularElement*>(e.get())) {
                s.matrix = r->getTransformation();
            } else {
                continue;
            }
            states.push_back(std::move(s));
        }
    }
    return states;
}

void restore(const std::vector<PageRotateUndoAction::ElementState>& states) {
    for (const auto& s: states) {
        if (auto* stroke = dynamic_cast<Stroke*>(s.element)) {
            stroke->setPointVector(s.points);
        } else if (auto* r = dynamic_cast<RectangularElement*>(s.element)) {
            r->setTransformation(s.matrix);
        }
    }
}

/// Turn what is on the page, its size and its space for notes (the document is locked). The background is the
/// caller's.
void turnContent(const PageRef& page, Turn turn) {
    const double width = page->getWidth(), height = page->getHeight();
    const Matrix m = turnOf(turn, width, height);
    const Text* flow = pageBoxOf(page);
    for (Layer* layer: page->getLayers()) {
        // A sticky note stays upright with what is on it: its middle goes where the page takes it
        if (const auto look = sticky::lookOf(*layer)) {
            const auto& r = look->rect;
            const auto c = m * xoj::util::Point<double>(r.x + r.width / 2, r.y + r.height / 2);
            sticky::Look to = *look;
            to.rect = {c.x - r.width / 2, c.y - r.height / 2, r.width, r.height};
            sticky::applyLook(*layer, *look, to);
            continue;
        }
        const bool boxes = md::isMarkdownLayer(*layer);
        for (const auto& e: layer->getElements()) {
            if (e.get() == flow) {
                continue;
            }
            if (auto* stroke = dynamic_cast<Stroke*>(e.get())) {
                std::vector<Point> points = stroke->getPointVector();
                for (Point& p: points) {
                    const auto q = m * xoj::util::Point<double>(p.x, p.y);
                    p.x = q.x;
                    p.y = q.y;
                }
                stroke->setPointVector(points);
            } else if (auto* text = dynamic_cast<Text*>(e.get()); text && (boxes || text->isMarkdown())) {
                // A Markdown box stays upright (it is laid out and tapped unturned): its middle moves
                const auto r = md::boxRect(*text);
                const auto c = m * xoj::util::Point<double>(r.x + r.width / 2, r.y + r.height / 2);
                text->move(c.x - (r.x + r.width / 2), c.y - (r.y + r.height / 2));
            } else if (auto* rect = dynamic_cast<RectangularElement*>(e.get())) {
                rect->setTransformation(m * rect->getTransformation());
            }
        }
    }
    page->setSize(height, width);
    page->setNoteSpace(turned(page->getNoteSpace(), turn));
}

/// Pages changed: drawn again, their thumbnails too (their revision)
void fireChanged(DocumentSession& session, const std::vector<PageRotateUndoAction::Change>& changes) {
    std::vector<size_t> indices;
    {
        Document* doc = session.getDocument();
        std::shared_lock lock(*doc);
        for (const auto& c: changes) {
            indices.push_back(doc->indexOf(c.page));
        }
    }
    for (size_t index: indices) {
        if (index != npos) {
            session.firePageSizeChanged(index);
            session.firePageChanged(index);  // (a square page keeps its size)
        }
    }
}

/// The PDF pages of these changes turned anew (redo, undo after the background PDF's pages were renumbered): their
/// new numbers, or npos for each if it failed
std::vector<size_t> turnPdfPagesOf(DocumentSession& session, const std::vector<PageRotateUndoAction::Change>& changes,
                                   Turn turn) {
    std::vector<size_t> numbers, out(changes.size(), npos);
    {
        std::shared_lock lock(*session.getDocument());
        for (const auto& c: changes) {
            if (c.pdf) {
                numbers.push_back(c.page->getPdfPageNr());
            }
        }
    }
    std::string error;
    const size_t first = turnPdfPages(session, numbers, turn, error);
    if (first == npos) {
        g_warning("Could not turn the PDF pages: %s", error.c_str());
        return out;
    }
    size_t k = 0;
    for (size_t i = 0; i < changes.size(); ++i) {
        if (changes[i].pdf) {
            out[i] = first + k++;
        }
    }
    return out;
}

}  // namespace

Blocked blockedOf(const XojPage& page, PdfPages pdf, const std::function<bool(size_t)>& hasPdfPage) {
    if (!isPdf(page)) {
        return Blocked::None;
    }
    if (pdf == PdfPages::Kept) {
        return Blocked::PdfPage;
    }
    return hasPdfPage(page.getPdfPageNr()) ? Blocked::None : Blocked::PdfMissing;
}

namespace {
/// Whether the background PDF has this page, or it is on its way into the merged PDF (the document is locked)
std::function<bool(size_t)> pdfPagesOf(DocumentSession& session) {
    const size_t count = session.getDocument()->getPdfPageCount();
    return [&session, count](size_t number) { return number < count || session.pendingPdfPage(number) != nullptr; };
}
}  // namespace

Preview preview(DocumentSession& session, const std::vector<size_t>& pages, PdfPages pdf) {
    Preview out;
    Document& doc = *session.getDocument();
    std::shared_lock lock(doc);
    const auto hasPdfPage = pdfPagesOf(session);
    std::set<size_t> seen;
    for (size_t index: pages) {
        if (index >= doc.getPageCount() || !seen.insert(index).second) {
            continue;
        }
        switch (blockedOf(*doc.getPage(index), pdf, hasPdfPage)) {
            case Blocked::None: ++out.pages; break;
            case Blocked::PdfPage: ++out.pdfPages; break;
            case Blocked::PdfMissing: ++out.missingPdf; break;
        }
    }
    return out;
}

Result apply(DocumentSession& session, const std::vector<size_t>& pages, Turn turn, PdfPages pdf) {
    Result result;
    Document* doc = session.getDocument();
    std::vector<PageRotateUndoAction::Change> changes;
    std::vector<PageRef> turning;
    std::vector<size_t> pdfNumbers;
    if (pdf == PdfPages::InPdf && session.mergingPdfPages()) {
        session.waitForMerges();  // (a page turned again right away: its PDF page is read from the merged PDF)
    }
    {
        std::shared_lock lock(*doc);
        const auto hasPdfPage = pdfPagesOf(session);
        std::set<size_t> seen;
        for (size_t index: pages) {
            if (index >= doc->getPageCount() || !seen.insert(index).second) {
                continue;
            }
            const PageRef page = doc->getPage(index);
            if (blockedOf(*page, pdf, hasPdfPage) != Blocked::None) {
                continue;
            }
            PageRotateUndoAction::Change c;
            c.page = page;
            c.pdf = isPdf(*page);
            if (c.pdf) {
                c.fromPdfPage = page->getPdfPageNr();
                pdfNumbers.push_back(c.fromPdfPage);
            }
            if (isImage(*page)) {
                c.image = page->getBackgroundImage();
            }
            changes.push_back(std::move(c));
            turning.push_back(page);
        }
    }
    if (changes.empty()) {
        return result;
    }
    session.clearSelectionEndText();
    for (auto& c: changes) {
        if (!c.image.isEmpty()) {
            c.turnedImage = turnedImage(c.image, turn);  // (not under the lock: a big picture takes a moment)
        }
    }

    // The PDF pages first: if they cannot be turned, nothing is
    const size_t firstPdf = turnPdfPages(session, pdfNumbers, turn, result.error);
    if (firstPdf == npos) {
        return result;
    }
    const quint64 numbering = session.pdfNumbering();  // (after: adding pages may have waited for a save)

    // The page's own texts are taken up as they are, and flow anew on the turned pages
    pagesize::TextReflow texts(session, turning);
    {
        std::unique_lock lock(*doc);
        size_t k = 0;
        for (auto& c: changes) {
            const PageRef& page = c.page;
            c.width = page->getWidth();
            c.height = page->getHeight();
            c.space = page->getNoteSpace();
            c.elements = statesOf(page);
            turnContent(page, turn);
            if (!c.turnedImage.isEmpty()) {
                page->setBackgroundImage(c.turnedImage);
            }
            if (c.pdf) {
                c.toPdfPage = firstPdf + k++;
                page->setBackgroundPdfPageNr(c.toPdfPage);
            }
        }
    }
    fireChanged(session, changes);
    std::vector<UndoActionPtr> textSteps = texts.finish();
    result.pages = changes.size();
    session.addPageUndoAction(
            std::make_unique<PageRotateUndoAction>(session, turn, std::move(changes), std::move(textSteps), numbering));
    return result;
}

// --- undo ----------------------------------------------------------------------------------------------------------

PageRotateUndoAction::PageRotateUndoAction(DocumentSession& session, Turn turn, std::vector<Change> changes,
                                           std::vector<UndoActionPtr> texts, quint64 pdfNumbering):
        UndoAction("PageRotateUndoAction"),
        session(session),
        turn(turn),
        changes(std::move(changes)),
        texts(std::move(texts)),
        pdfNumbering(pdfNumbering) {}

bool PageRotateUndoAction::undo(Control*) {
    // (the texts first: they flowed on the turned pages; the last first)
    bool ok = true;
    for (auto it = texts.rbegin(); it != texts.rend(); ++it) {
        ok = (*it)->undo(&session) && ok;
    }
    // The PDF pages from before are still there, unless a save renumbered the background PDF's pages since: then the
    // turned ones are turned back
    const bool sameNumbers = session.pdfNumbering() == pdfNumbering;
    const std::vector<size_t> back =
            sameNumbers ? std::vector<size_t>{} : turnPdfPagesOf(session, changes, opposite(turn));
    {
        std::unique_lock lock(*session.getDocument());
        for (size_t i = 0; i < changes.size(); ++i) {
            const Change& c = changes[i];
            restore(c.elements);
            c.page->setSize(c.width, c.height);
            c.page->setNoteSpace(c.space);
            if (!c.turnedImage.isEmpty()) {
                c.page->setBackgroundImage(c.image);
            }
            if (c.pdf) {
                const size_t number = sameNumbers ? c.fromPdfPage : back[i];
                if (number != npos) {
                    c.page->setBackgroundPdfPageNr(number);
                }
            }
        }
    }
    fireChanged(session, changes);
    return ok;
}

bool PageRotateUndoAction::redo(Control*) {
    const bool sameNumbers = session.pdfNumbering() == pdfNumbering;
    const std::vector<size_t> again = sameNumbers ? std::vector<size_t>{} : turnPdfPagesOf(session, changes, turn);
    {
        std::unique_lock lock(*session.getDocument());
        for (size_t i = 0; i < changes.size(); ++i) {
            const Change& c = changes[i];
            turnContent(c.page, turn);  // (from the very same state as the first time: the same result)
            if (!c.turnedImage.isEmpty()) {
                c.page->setBackgroundImage(c.turnedImage);
            }
            if (c.pdf) {
                const size_t number = sameNumbers ? c.toPdfPage : again[i];
                if (number != npos) {
                    c.page->setBackgroundPdfPageNr(number);
                }
            }
        }
    }
    fireChanged(session, changes);
    bool ok = true;
    for (auto& t: texts) {
        ok = t->redo(&session) && ok;
    }
    return ok;
}

std::string PageRotateUndoAction::getText() {
    return changes.size() == 1 ? "Rotate page" : "Rotate " + std::to_string(changes.size()) + " pages";
}

std::vector<PageRef> PageRotateUndoAction::getPages() {
    std::vector<PageRef> pages;
    pages.reserve(changes.size());
    for (const Change& c: changes) {
        pages.push_back(c.page);
    }
    return pages;
}

}  // namespace xqt::pagerotate

#include "PageNoteSpace.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "util/Util.h"

#include "DocumentSession.h"
#include "PageOrderUndoAction.h"

namespace xqt::notespace {

QSizeF slideSize(const XojPage& page) {
    const NoteSpace& s = page.getNoteSpace();
    return {page.getWidth() - s.left - s.right, page.getHeight() - s.top - s.bottom};
}

QPointF offsetOf(const XojPage& page) { return {page.getNoteSpace().left, page.getNoteSpace().top}; }

bool canHaveSpace(const XojPage& page) { return !page.getBackgroundType().isImagePage(); }

void renderPdf(cairo_t* cr, const XojPage& page, const XojPdfPage& pdf, bool forPrinting) {
    const NoteSpace& s = page.getNoteSpace();
    cairo_save(cr);
    if (!s.empty()) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr, 0, 0, page.getWidth(), page.getHeight());
        cairo_fill(cr);
        cairo_translate(cr, s.left, s.top);
        // (poppler's render() paints white over everything it may draw on: only the slide)
        cairo_rectangle(cr, 0, 0, page.getWidth() - s.left - s.right, page.getHeight() - s.top - s.bottom);
        cairo_clip(cr);
    }
    forPrinting ? pdf.renderForPrinting(cr) : pdf.render(cr);
    cairo_restore(cr);
}

NoteSpace spaceFor(const XojPage& page, const Amounts& a) {
    const QSizeF slide = slideSize(page);
    auto amount = [](double v, double of) {
        const double pts = std::round(v * of);
        return std::isfinite(pts) ? std::clamp(pts, 0.0, MAX_AMOUNT) : 0.0;
    };
    const double w = a.relative ? slide.width() : 1;
    const double h = a.relative ? slide.height() : 1;
    return NoteSpace{amount(a.left, w), amount(a.top, h), amount(a.right, w), amount(a.bottom, h)};
}

size_t apply(DocumentSession& session, const std::vector<size_t>& pages, const Amounts& amounts) {
    Document* doc = session.getDocument();
    std::vector<NoteSpaceUndoAction::Change> changes;
    {
        std::shared_lock lock(*doc);
        changes.reserve(pages.size());
        std::vector<bool> seen(doc->getPageCount(), false);
        for (size_t index: pages) {
            if (index >= doc->getPageCount() || seen[index]) {
                continue;
            }
            seen[index] = true;
            PageRef page = doc->getPage(index);
            if (!canHaveSpace(*page)) {
                continue;
            }
            NoteSpaceUndoAction::Change c;
            c.page = page;
            c.from = page->getNoteSpace();
            c.to = spaceFor(*page, amounts);
            if (c.to == c.from) {
                continue;
            }
            const QSizeF slide = slideSize(*page);
            c.fromWidth = page->getWidth();
            c.fromHeight = page->getHeight();
            // (the slide's size as it is: the new space around it)
            c.toWidth = c.to.left + slide.width() + c.to.right;
            c.toHeight = c.to.top + slide.height() + c.to.bottom;
            changes.push_back(std::move(c));
        }
    }
    if (changes.empty()) {
        return 0;
    }
    session.clearSelectionEndText();
    NoteSpaceUndoAction::set(&session, changes, true);
    const size_t n = changes.size();
    session.addPageUndoAction(std::make_unique<NoteSpaceUndoAction>(std::move(changes)));
    return n;
}

size_t insertBlankAfter(DocumentSession& session, std::vector<size_t> pages) {
    std::sort(pages.begin(), pages.end());
    pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    const auto before = session.pageOrder();
    std::vector<PageRef> after;
    after.reserve(before.size() + pages.size());
    size_t added = 0;
    {
        std::shared_lock lock(*session.getDocument());
        for (size_t i = 0, k = 0; i < before.size(); ++i) {
            after.push_back(before[i]);
            if (k < pages.size() && pages[k] == i) {
                ++k;
                const QSizeF slide = slideSize(*before[i]);
                auto blank = std::make_shared<XojPage>(slide.width(), slide.height());
                blank->setBackgroundType(PageType(PageTypeFormat::Plain));
                blank->setBackgroundColor(Colors::white);
                after.push_back(std::move(blank));
                ++added;
            }
        }
    }
    if (added == 0) {
        return 0;
    }
    session.clearSelectionEndText();
    session.applyPageOrder(after, {});
    session.addPageUndoAction(std::make_unique<PageOrderUndoAction>(
            before, after, std::vector<PageRef>{},
            added == 1 ? "Insert page" : "Insert " + std::to_string(added) + " pages"));
    return added;
}

// --- undo ----------------------------------------------------------------------------------------------------------

NoteSpaceUndoAction::NoteSpaceUndoAction(std::vector<Change> changes):
        UndoAction("NoteSpaceUndoAction"), changes(std::move(changes)) {}

void NoteSpaceUndoAction::set(Control* control, const std::vector<Change>& changes, bool forward) {
    Document* doc = control->getDocument();
    std::vector<size_t> indices;
    indices.reserve(changes.size());
    {
        std::unique_lock lock(*doc);
        for (const Change& c: changes) {
            const NoteSpace& to = forward ? c.to : c.from;
            const NoteSpace& from = forward ? c.from : c.to;
            const double dx = to.left - from.left;
            const double dy = to.top - from.top;
            if (dx != 0 || dy != 0) {
                for (Layer* layer: c.page->getLayers()) {
                    for (auto& e: layer->getElements()) {
                        e->move(dx, dy);
                    }
                }
            }
            c.page->setNoteSpace(to);
            c.page->setSize(forward ? c.toWidth : c.fromWidth, forward ? c.toHeight : c.fromHeight);
            indices.push_back(doc->indexOf(c.page));
        }
    }
    for (size_t index: indices) {
        if (index != npos) {
            control->firePageSizeChanged(index);
        }
    }
}

bool NoteSpaceUndoAction::undo(Control* control) {
    set(control, changes, false);
    return true;
}

bool NoteSpaceUndoAction::redo(Control* control) {
    set(control, changes, true);
    return true;
}

std::string NoteSpaceUndoAction::getText() {
    return changes.size() == 1 ? "Space for notes" : "Space for notes on " + std::to_string(changes.size()) + " pages";
}

std::vector<PageRef> NoteSpaceUndoAction::getPages() {
    std::vector<PageRef> pages;
    pages.reserve(changes.size());
    for (const Change& c: changes) {
        pages.push_back(c.page);
    }
    return pages;
}

}  // namespace xqt::notespace

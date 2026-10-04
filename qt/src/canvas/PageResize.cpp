#include "PageResize.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PageMargins.h"
#include "session/StickyNote.h"
#include "undo/GroupUndoAction.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPaginate.h"

namespace xqt::pagesize {

namespace {
/// How far something may reach beyond the edge and still count as on the page (points)
constexpr double TOLERANCE = 0.5;

bool sameSize(double a, double b) { return std::abs(a - b) < 0.01; }

size_t outsideOf(const PageRef& page, double width, double height) {
    const auto beyond = [&](double right, double bottom) {
        return right > width + TOLERANCE || bottom > height + TOLERANCE;
    };
    const Text* flow = pageTextOf(page);  // (flows anew: never beyond)
    size_t n = 0;
    for (const Layer* layer: page->getLayersView()) {
        if (const auto look = sticky::lookOf(*layer)) {
            n += beyond(look->rect.x + look->rect.width, look->rect.y + look->rect.height) ? 1 : 0;
            continue;
        }
        for (const Element* e: layer->getElementsView()) {
            if (e == flow) {
                continue;
            }
            const auto& box = e->getBoundingBox();
            n += beyond(box.x + box.width, box.y + box.height) ? 1 : 0;
        }
    }
    return n;
}
}  // namespace

const Text* pageTextOf(const PageRef& page) {
    const Layer* layer = md::markdownLayer(page);
    const Text* box = layer ? PageMargins::pageBox(*layer, page) : nullptr;
    return box && !box->getText().empty() ? box : nullptr;
}

TextReflow::TextReflow(DocumentSession& session, const std::vector<PageRef>& pages) {
    Document* doc = session.getDocument();
    std::vector<size_t> starts;  // the first page of each page's text on these pages
    {
        std::shared_lock lock(*doc);
        std::set<size_t> seen;
        for (const PageRef& page: pages) {
            if (!pageTextOf(page)) {
                continue;
            }
            const size_t index = doc->indexOf(page);
            if (index == npos) {
                continue;
            }
            // (from the page it starts on, as MarkdownSession::begin takes it up)
            size_t first = index;
            while (first > 0) {
                const Text* t = pageTextOf(doc->getPage(first));
                if (!t || !md::continues(t->getText())) {
                    break;
                }
                --first;
            }
            if (seen.insert(first).second) {
                starts.push_back(first);
            }
        }
    }
    for (size_t index: starts) {
        auto group = std::make_unique<GroupUndoAction>();
        auto text = std::make_unique<MarkdownSession>(session);
        text->recordInto(group.get());
        text->begin(index, md::Style{});
        texts.push_back(std::move(text));
        groups.push_back(std::move(group));
    }
}

TextReflow::~TextReflow() = default;

std::vector<UndoActionPtr> TextReflow::finish() {
    std::vector<UndoActionPtr> steps;
    for (size_t i = 0; i < texts.size(); ++i) {
        texts[i]->reflow();
        texts[i]->finish();
        steps.push_back(std::move(groups[i]));  // (empty when the text stayed as it was: nothing to do)
    }
    texts.clear();
    groups.clear();
    return steps;
}

bool canResize(const XojPage& page) { return !page.getBackgroundType().isPdfPage(); }

Preview preview(Document& doc, const std::vector<size_t>& pages, double width, double height) {
    Preview out;
    if (!(width > 0 && height > 0 && std::isfinite(width) && std::isfinite(height))) {
        return out;
    }
    std::shared_lock lock(doc);
    std::set<size_t> seen;
    for (size_t index: pages) {
        if (index >= doc.getPageCount() || !seen.insert(index).second) {
            continue;
        }
        const PageRef page = doc.getPage(index);
        if (!canResize(*page)) {
            ++out.pdfPages;
            continue;
        }
        if (sameSize(page->getWidth(), width) && sameSize(page->getHeight(), height)) {
            continue;
        }
        ++out.pages;
        if (width < page->getWidth() || height < page->getHeight()) {
            out.outside += outsideOf(page, width, height);
        }
    }
    return out;
}

size_t apply(DocumentSession& session, const std::vector<size_t>& pages, double width, double height) {
    if (!(width > 0 && height > 0 && std::isfinite(width) && std::isfinite(height))) {
        return 0;
    }
    Document* doc = session.getDocument();
    std::vector<PageSizeUndoAction::Change> changes;
    std::vector<PageRef> changed;
    {
        std::shared_lock lock(*doc);
        std::set<size_t> seen;
        for (size_t index: pages) {
            if (index >= doc->getPageCount() || !seen.insert(index).second) {
                continue;
            }
            const PageRef page = doc->getPage(index);
            if (!canResize(*page) ||
                (sameSize(page->getWidth(), width) && sameSize(page->getHeight(), height))) {
                continue;
            }
            changes.push_back({page, page->getWidth(), page->getHeight(), width, height, page->getNoteSpace()});
            changed.push_back(page);
        }
    }
    if (changes.empty()) {
        return 0;
    }
    session.clearSelectionEndText();

    // The page's texts are taken up at their places on the pages as they are, flowed anew on the new sizes
    TextReflow texts(session, changed);
    PageSizeUndoAction::set(&session, changes, true);
    std::vector<UndoActionPtr> textSteps = texts.finish();
    const size_t n = changes.size();
    session.addPageUndoAction(std::make_unique<PageSizeUndoAction>(std::move(changes), std::move(textSteps)));
    return n;
}

// --- undo ----------------------------------------------------------------------------------------------------------

PageSizeUndoAction::PageSizeUndoAction(std::vector<Change> changes, std::vector<UndoActionPtr> texts):
        UndoAction("PageSizeUndoAction"), changes(std::move(changes)), texts(std::move(texts)) {}

void PageSizeUndoAction::set(Control* control, const std::vector<Change>& changes, bool forward) {
    Document* doc = control->getDocument();
    std::vector<size_t> indices;
    indices.reserve(changes.size());
    {
        std::unique_lock lock(*doc);
        for (const Change& c: changes) {
            c.page->setSize(forward ? c.toWidth : c.fromWidth, forward ? c.toHeight : c.fromHeight);
            c.page->setNoteSpace(forward ? NoteSpace{} : c.fromSpace);
            indices.push_back(doc->indexOf(c.page));
        }
    }
    for (size_t index: indices) {
        if (index != npos) {
            control->firePageSizeChanged(index);
        }
    }
}

bool PageSizeUndoAction::undo(Control* control) {
    // (the texts first: they flowed on the new sizes; one after another, the last first)
    bool ok = true;
    for (auto it = texts.rbegin(); it != texts.rend(); ++it) {
        ok = (*it)->undo(control) && ok;
    }
    set(control, changes, false);
    return ok;
}

bool PageSizeUndoAction::redo(Control* control) {
    set(control, changes, true);
    bool ok = true;
    for (auto& t: texts) {
        ok = t->redo(control) && ok;
    }
    return ok;
}

std::string PageSizeUndoAction::getText() {
    return changes.size() == 1 ? "Page size" : "Page size of " + std::to_string(changes.size()) + " pages";
}

std::vector<PageRef> PageSizeUndoAction::getPages() {
    std::vector<PageRef> pages;
    pages.reserve(changes.size());
    for (const Change& c: changes) {
        pages.push_back(c.page);
    }
    return pages;
}

}  // namespace xqt::pagesize

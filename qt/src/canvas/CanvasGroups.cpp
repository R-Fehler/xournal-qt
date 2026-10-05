// xournal-qt: groups of elements in the canvas (qt/docs/groups.md): grouping and ungrouping what is selected.
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Element.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasPage.h"
#include "CanvasView.h"

namespace xqt {

namespace {
/// The selected elements; `oneLayer`: they are all in one layer (an element selection always is)
std::vector<Element*> selectedElements(const EditSelection* selection, const MixedSelection& mixed, bool& oneLayer) {
    std::vector<Element*> elements;
    oneLayer = true;
    if (selection) {
        for (const Element* e: selection->getElementsView()) {
            elements.push_back(const_cast<Element*>(e));  // (the selection's own)
        }
    } else if (mixed.active()) {
        const Layer* layer = mixed.items().empty() ? nullptr : mixed.items().front().layer;
        for (const auto& item: mixed.items()) {
            elements.push_back(item.element);
            oneLayer = oneLayer && item.layer == layer;
        }
    }
    return elements;
}
}  // namespace

groups::State CanvasView::groupState() const {
    if (isReadingOnly() || session.isReadOnly()) {
        return {};
    }
    bool oneLayer = true;
    std::vector<const Element*> elements;
    {
        std::shared_lock lock(*session.getDocument());
        for (const Element* e: selectedElements(selection.get(), *mixedSelection, oneLayer)) {
            elements.push_back(e);
        }
        groups::State s = groups::stateOf(elements);
        s.canGroup = s.canGroup && oneLayer;
        return s;
    }
}

bool CanvasView::groupSelection() {
    if (isReadingOnly() || session.isReadOnly()) {
        return false;
    }
    std::unique_ptr<UndoAction> undo;
    {
        Document* doc = session.getDocument();
        std::unique_lock lock(*doc);
        bool oneLayer = true;
        const std::vector<Element*> elements = selectedElements(selection.get(), *mixedSelection, oneLayer);
        if (!oneLayer) {
            return false;  // (a group is in one layer)
        }
        const PageRef page = selection ? selection->getSourcePage()
                                       : mixedSelection->active() ? mixedSelection->selectedPage()->getPage() : PageRef();
        undo = groups::group(elements, page, *doc);
    }
    if (!undo) {
        return false;
    }
    session.getUndoRedoHandler()->addUndoAction(std::move(undo));
    ++selectionRev;
    Q_EMIT selectionChanged(true);
    Q_EMIT updateRequested();
    return true;
}

bool CanvasView::ungroupSelection() {
    if (isReadingOnly() || session.isReadOnly()) {
        return false;
    }
    std::unique_ptr<UndoAction> undo;
    {
        Document* doc = session.getDocument();
        std::unique_lock lock(*doc);
        bool oneLayer = true;
        const std::vector<Element*> elements = selectedElements(selection.get(), *mixedSelection, oneLayer);
        const PageRef page = selection ? selection->getSourcePage()
                                       : mixedSelection->active() ? mixedSelection->selectedPage()->getPage() : PageRef();
        undo = groups::ungroup(elements, page);
    }
    if (!undo) {
        return false;
    }
    session.getUndoRedoHandler()->addUndoAction(std::move(undo));
    ++selectionRev;
    Q_EMIT selectionChanged(true);
    Q_EMIT updateRequested();
    return true;
}

}  // namespace xqt

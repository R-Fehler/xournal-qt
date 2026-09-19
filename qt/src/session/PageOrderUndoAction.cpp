#include "PageOrderUndoAction.h"

#include <algorithm>

#include "DocumentSession.h"

namespace xqt {

PageOrderUndoAction::PageOrderUndoAction(std::vector<PageRef> before, std::vector<PageRef> after,
                                         std::vector<PageRef> moved, std::string text):
        UndoAction("PageOrderUndoAction"),
        before(std::move(before)),
        after(std::move(after)),
        moved(std::move(moved)),
        text(std::move(text)) {}

bool PageOrderUndoAction::undo(Control* control) {
    auto* session = dynamic_cast<DocumentSession*>(control);
    if (!session) {
        return false;
    }
    session->applyPageOrder(before, moved);
    return true;
}

bool PageOrderUndoAction::redo(Control* control) {
    auto* session = dynamic_cast<DocumentSession*>(control);
    if (!session) {
        return false;
    }
    session->applyPageOrder(after, moved);
    return true;
}

std::vector<PageRef> PageOrderUndoAction::getPages() {
    // The pages that appear, disappear or move (their thumbnails and search results change).
    std::vector<PageRef> pages = moved;
    for (const auto& p: before) {
        if (std::find(after.begin(), after.end(), p) == after.end()) {
            pages.push_back(p);
        }
    }
    for (const auto& p: after) {
        if (std::find(before.begin(), before.end(), p) == before.end()) {
            pages.push_back(p);
        }
    }
    return pages;
}

}  // namespace xqt

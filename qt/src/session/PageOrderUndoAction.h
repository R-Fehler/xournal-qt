/*
 * xournal-qt: undo of page operations on several pages at once (delete, paste, move by drag and drop).
 *
 * Stores the page order before and after; undo/redo restore one of them (DocumentSession::applyPageOrder). Upstream
 * only has InsertDeletePageUndoAction and SwapUndoAction for one page each.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include "model/PageRef.h"
#include "undo/UndoAction.h"

namespace xqt {

class PageOrderUndoAction final: public UndoAction {
public:
    /// `moved`: pages that are in both orders but at another place (moved by drag and drop).
    PageOrderUndoAction(std::vector<PageRef> before, std::vector<PageRef> after, std::vector<PageRef> moved,
                        std::string text);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override { return text; }
    std::vector<PageRef> getPages() override;

private:
    std::vector<PageRef> before, after, moved;
    std::string text;
};

}  // namespace xqt

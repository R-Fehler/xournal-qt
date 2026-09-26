/*
 * xournal-qt: changing the size of existing pages (the page menu's "Page size…").
 *
 * The content stays where it is, measured from the top left: nothing moves, and what a smaller page no longer holds
 * is kept beyond its edge (preview() counts it, for the dialog's warning). The page's own Markdown text flows anew
 * over its pages at their new width and margins (MarkdownSession::reflow), pages added or removed included. Pages
 * with a PDF background keep the PDF's size (Space for notes enlarges them: PageNoteSpace.h); another page that had
 * space for notes is the new size as a whole (its space goes; undo brings it back). Any number of pages is
 * one undo step; only the pages in view are rendered again (CanvasView::pageSizeChanged), the others when they come
 * into view.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "model/NoteSpace.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"

class Document;
class XojPage;

namespace xqt {

class DocumentSession;

namespace pagesize {

/// A page can take another size: not one with a PDF background (it has the PDF page's size).
bool canResize(const XojPage& page);

/// What apply() would do to these pages.
struct Preview {
    size_t pages = 0;     ///< pages whose size changes
    size_t pdfPages = 0;  ///< pages left out: a PDF background
    size_t outside = 0;   ///< elements that would reach beyond the new page (a sticky note counts once)
};
/// Takes the document's lock (shared).
Preview preview(Document& doc, const std::vector<size_t>& pages, double width, double height);

/// Give these pages (indices) the size width × height (points). Returns how many pages changed. UI thread.
size_t apply(DocumentSession& session, const std::vector<size_t>& pages, double width, double height);

/// The undo of apply(): per page its size before and after, and the page's text flowed anew (MarkdownSession's
/// changes, after the sizes).
class PageSizeUndoAction final: public UndoAction {
public:
    struct Change {
        PageRef page;
        double fromWidth = 0, fromHeight = 0, toWidth = 0, toHeight = 0;
        NoteSpace fromSpace;  ///< space for notes it had (a page that is no PDF page: the new size replaces it)
    };
    PageSizeUndoAction(std::vector<Change> changes, std::vector<UndoActionPtr> texts);

    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override;

    /// The sizes after (forward) or before; fires the size change of each page.
    static void set(Control* control, const std::vector<Change>& changes, bool forward);

private:
    std::vector<Change> changes;
    std::vector<UndoActionPtr> texts;
};

}  // namespace pagesize
}  // namespace xqt

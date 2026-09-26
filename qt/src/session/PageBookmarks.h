/*
 * xournal-qt: bookmarks on pages (qt/docs/bookmarks.md).
 *
 * A bookmark is part of the document: a page's optional label (XojPage::getBookmark, an upstream seam), saved in a
 * .xopp as the page attribute xqt-bookmark="label" and in a PDF with notes also as the children of a top-level outline
 * item "Bookmarks" (HybridPdf writes it), so every PDF viewer lists them. An empty label is the automatic one: the page
 * is called "Page N" by its place now (it follows the page when pages come, go or move).
 *
 * Because the label lives on the page object, a bookmark follows its page through insertions, deletions, moves and
 * their undo; a copy of a page (duplicate, paste) starts without one.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <QString>

#include "model/DocumentOutline.h"
#include "model/PageRef.h"
#include "undo/UndoAction.h"

class Document;

namespace xqt::PageBookmarks {

/// The title of our item in a PDF's outline.
constexpr const char* OUTLINE_TITLE = "Bookmarks";

/// What a bookmark is called: its label, or "Page N" for the automatic one (`page` 0-based).
QString displayLabel(const std::string& label, size_t page);
std::string displayLabelUtf8(const std::string& label, size_t page);
/// A title read back (a PDF outline) is the automatic label of that page: "Page N" (also untranslated).
bool isAutomaticLabel(const std::string& title, size_t page);

/// This top-level outline entry is our "Bookmarks" item: that title, children that all go to a page and have no
/// children of their own. The table of contents leaves it out; its children are the bookmarks of a PDF without our
/// data.
bool isOutlineItem(const DocumentOutlineEntry& entry);

/// A document read from a PDF without our data (no page has a bookmark): the pages that our outline item's entries go
/// to get their bookmarks (the first page showing that PDF page). Returns how many. Called by DocumentSession::loadFile
/// before anyone else sees the document.
size_t adoptOutline(Document& doc);

struct Mark {
    size_t page = 0;     ///< 0-based
    std::string label;   ///< "": the automatic one
};
/// The bookmarks of the document in page order (locked by the caller, shared).
std::vector<Mark> of(const Document& doc);

/// One undo step: a page's bookmark set, renamed or removed. `apply` sets a label on the page (and tells the views).
class BookmarkUndoAction final: public UndoAction {
public:
    using Apply = std::function<void(const PageRef&, const std::optional<std::string>&)>;
    BookmarkUndoAction(PageRef page, std::optional<std::string> before, std::optional<std::string> after, Apply apply);
    bool undo(Control* control) override;
    bool redo(Control* control) override;
    std::string getText() override;
    std::vector<PageRef> getPages() override { return {}; }  // (no page's picture changes)

private:
    PageRef target;
    std::optional<std::string> before, after;
    Apply apply;
};

}  // namespace xqt::PageBookmarks

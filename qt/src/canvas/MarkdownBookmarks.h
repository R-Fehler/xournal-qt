/*
 * xournal-qt: bookmarking a page of a Markdown text (qt/docs/bookmarks.md, "Markdown").
 *
 * The pages of a .md, and of the text of a PDF text document, have their bookmarks in their text: a comment
 * "<!-- xqt:bookmark label -->" before the block it marks (MdBookmarks.h), read into XojPage::bookmark whenever the
 * text changes (TextDocument::syncBookmarks). "Bookmark this page", its removal and renaming are therefore edits of
 * the text: in the text being written when it is (MarkdownEditor::applyEdit, a step of its undo; the cursor stays
 * where it is in the text), else as an edit of their own (one undo step, as MarkdownFile::setText). Saving writes
 * them as any typed text.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <string>

class Document;

namespace xqt {
class CanvasView;
class DocumentSession;
}  // namespace xqt

namespace xqt::MarkdownBookmarks {

/// Whether a page's bookmark is a comment in its text (a page of the text that starts on page 1, not a plain text).
/// The caller holds the document's lock (shared).
bool isTextPage(Document& doc, size_t page);

enum class Change { Add, Remove, Rename };

struct Result {
    bool changed = false;
    /// Add: the page is all inside one block (a long code block, list, ...), whose page got the bookmark
    bool earlier = false;
    size_t page = 0;  ///< Add: the page that has the bookmark now
};

/// Add a bookmark to a page of the text (the automatic one: `<!-- xqt:bookmark -->`), remove its bookmarks, or give its
/// (first) bookmark the label `label` ("" or the automatic label: the automatic one). `view`: the canvas of the
/// session (its text being written, if any; nullptr: none).
Result edit(DocumentSession& session, CanvasView* view, size_t page, Change change, const std::string& label = {});

}  // namespace xqt::MarkdownBookmarks

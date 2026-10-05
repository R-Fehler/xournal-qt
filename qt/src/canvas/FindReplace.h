/*
 * xournal-qt: find and replace in a document (qt/docs/md-editor.md, "Find and replace").
 *
 * What it changes is the text that can be written: the page's Markdown text (a .md or .txt edited, a PDF text
 * document, the page texts of notes; each flow over its pages once), the Markdown text boxes and the sticky notes'
 * Markdown texts, on layers that are shown. It changes their source (session/TextReplace.h); PDF text, plain text
 * elements and handwriting are searched but never replaced.
 *
 * - replaceAll: every match in all of them, as one undo step. While a text is written on the page (MarkdownEditor)
 *   and only it has matches, the step is one of that text (its own Ctrl+Z); otherwise the writing ends first.
 * - replaceCurrent: the current hit of the document's search (DocumentSearch) is replaced when a match of the source
 *   is drawn there, as one undo step, and the hit after it becomes the current one. The search's hits are what the
 *   page shows, the matches are in the source; they are told apart by where they are drawn.
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <vector>

#include <QString>

#include "session/TextReplace.h"

class Document;
class Text;

namespace xqt {

class CanvasView;
class DocumentSession;

namespace replace {

/// A text that find and replace changes.
struct Target {
    size_t page = 0;            ///< the page's text: the first page it flows over; a box: its page
    bool pageText = true;
    const Text* box = nullptr;  ///< the page's text: its box on `page`; else the text box or the note's text
};
/// The texts of a document find and replace changes, in page order (none for a text file that is read-only). Takes
/// the document's lock (shared).
std::vector<Target> targets(DocumentSession& session);

/// The document has text find and replace can change.
bool canReplace(DocumentSession& session);

/// Every match replaced, as one undo step (see above; `view`: the view that may be writing a text, may be null).
/// Returns how many were replaced.
int replaceAll(DocumentSession& session, CanvasView* view, const QString& query, const QString& with,
               const Options& options);

enum class Step {
    Replaced,  ///< the current hit was replaced, the one after it is current
    Skipped,   ///< the current hit cannot be replaced (PDF text, a plain text, handwriting): the next one is current
    Shown,     ///< there was no current hit: the first from the current page on is current now
    None,      ///< no hits
};
/// The current hit replaced (see above).
Step replaceCurrent(DocumentSession& session, CanvasView* view, const QString& query, const QString& with,
                    const Options& options);

}  // namespace replace
}  // namespace xqt

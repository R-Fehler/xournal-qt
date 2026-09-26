/*
 * xournal-qt: Markdown written on the page, as in Typora or Obsidian's live preview.
 *
 * The text is shown formatted while it is typed. The block with the cursor shows its Markdown, the marks dimmed (a
 * heading keeps its size, bold stays bold). The cursor and the selection are places in the source. The page's text
 * flows over its pages (MarkdownSession), and the cursor goes with it from page to page.
 *
 * The box with the cursor is drawn by this editor (at once, as typed); the renderer leaves it out while it is
 * edited (Text::setInEditing), the other pages' boxes are drawn as always.
 *
 * Keys: typing, Enter (a new paragraph; in a list the next item, on an empty item the list ends; in code a line),
 * Shift+Enter (a line of the same paragraph), Backspace / Delete, the arrows (with Ctrl: words; Up / Down on the
 * lines as drawn), Home / End (the line; with Ctrl: the text), Shift for selecting, Ctrl+A / C / X / V, Ctrl+Z /
 * Ctrl+Shift+Z (in the text being written; afterwards the whole edit is one undo step), Ctrl+B / I / E / K (bold,
 * italic, code, link), Ctrl+1 / 2 / 3 / 0 (headings), Tab / Shift+Tab (list levels), Escape (done). The formatting
 * keys and the formatting bar's tools are md::format's (applyEdit).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <cairo.h>

#include "CanvasTextInput.h"
#include "MarkdownSession.h"
#include "MdFormat.h"

namespace xoj::view {
class OverlayView;
}

namespace xqt {

class CanvasView;
class DocumentSession;

class MarkdownEditor final: public CanvasTextInput {
public:
    /// Start writing on a page (page coordinates): the page's text (`pageText`), or the text box drawn at (x, y),
    /// or a new one there. The cursor goes where the page was tapped.
    MarkdownEditor(CanvasView& view, DocumentSession& session, size_t page, bool pageText, double x, double y,
                   const md::Style& style);
    /// Done: the edit is one undo step.
    ~MarkdownEditor() override;
    /// Back to the text as it was, instead.
    void cancel();

    CanvasPage& getPage() const override;
    bool contains(double x, double y) const override;
    /// A press on a page (page coordinates): on the text, the cursor goes there (true); elsewhere nothing (false).
    bool tap(CanvasPage& page, double x, double y);
    /// A press anywhere on a page of the text (a text file edited: TextFile), not only on the text: the cursor goes
    /// to the nearest place of the text on that page (true); false if the text is not on that page.
    bool tapAnywhere(CanvasPage& page, double x, double y);
    /// A press on a check box of the text being written (page coordinates): it is switched (the cursor stays).
    bool toggleCheckBox(CanvasPage& page, double x, double y);
    void mousePressed(double x, double y) override;
    void mouseMoved(double x, double y) override;
    bool keyPressed(const QKeyEvent* e, bool& finish) override;
    bool wantsKeyEvent(const QKeyEvent* e) const override;
    void inputMethodEvent(const QInputMethodEvent* e) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    QRectF cursorRectOnPage() const override;

    /// Draws the box with the cursor, the selection and the cursor (page coordinates of getPage()).
    void paint(cairo_t* cr) const;
    void setFontSize(double size);
    double fontSize() const { return md.fontSize(); }

    /// What is written, to open it beside the page: a page (the page's text: its first page) and, for a text box, a
    /// point on it.
    struct Target {
        size_t page = 0;
        bool pageText = true;
        double x = 0;
        double y = 0;
    };
    Target target() const;
    const std::string& text() const { return md.text(); }
    /// The cursor and the other end of the selection (source offsets).
    size_t cursorPosition() const { return caret; }
    size_t anchorPosition() const { return anchor; }
    /// Put the cursor at a source offset (e.g. where it was before the text was read again).
    void setCursorPosition(size_t offset);
    /// A change made by a formatting tool (md::format, the formatting bar): text[from, to) is replaced, then the
    /// selection is set. One undo step in the text being written.
    void applyEdit(const md::format::Edit& change);
    /// Plain text (a .txt): no Markdown formatting.
    bool isPlain() const { return plain; }
    /// Undo and redo in the text being written (Ctrl+Z / Ctrl+Shift+Z).
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    void undo() { undoEdit(false); }
    void redo() { undoEdit(true); }

private:
    using Part = MarkdownSession::PagePart;

    // --- places ---------------------------------------------------------------------------------------------------
    size_t partOf(size_t offset) const;
    size_t localOf(size_t part, size_t offset) const;
    size_t sourceOf(size_t part, size_t local) const;
    /// The box's text as drawn (with the input method's text not yet typed, at the cursor).
    std::string shownText(size_t part) const;
    md::Style styleOf(size_t part) const;
    QPointF originOf(size_t part) const;
    /// The layout of a part's box (the box with the cursor: its block as source). Valid until the next layout.
    const md::Layout& layoutOf(size_t part) const;
    /// The source offset at a point of a part's page (page coordinates).
    size_t hit(size_t part, double x, double y) const;
    QRectF caretRect() const;
    QRectF boxRect(size_t part) const;

    // --- changes --------------------------------------------------------------------------------------------------
    enum class EditKind { Typing, Other };
    /// Replace source[from, to) by `with`; the cursor goes after it.
    void edit(size_t from, size_t to, const std::string& with, EditKind kind = EditKind::Other);
    void insert(const std::string& s, EditKind kind = EditKind::Typing);
    void removeSelection();
    bool hasSelection() const { return caret != anchor; }
    /// The text changed (or the cursor): the pages follow, the cursor is shown (and scrolled to).
    void changed(bool textChanged);
    void moveCursor(size_t to, bool keepAnchor);
    void setCurrent(size_t part);
    /// A page's box is drawn otherwise (another block as its source, or formatted again): searched again.
    void drawnAsWrittenChanged(const PageRef& page);

    // --- keys -----------------------------------------------------------------------------------------------------
    void newLine(bool soft);
    /// The cursor at the end of a list item or quote line with nothing but its mark: where the mark begins (npos:
    /// not so).
    size_t emptyItemMark() const;
    void indent(bool in);
    size_t verticalMove(bool down) const;
    size_t prevChar(size_t pos) const;
    size_t nextChar(size_t pos) const;
    size_t wordBoundary(size_t pos, bool forward) const;
    void undoEdit(bool redo);

    CanvasView& view;
    DocumentSession& session;
    MarkdownSession md;
    std::vector<Part> parts;
    size_t current = 0;           ///< the part with the cursor
    Text* editing = nullptr;      ///< its box, left out by the renderer while it is edited
    CanvasPage* page = nullptr;   ///< its page (with this editor's view)
    size_t rawBegin = md::NO_SOURCE;  ///< where the block drawn as its source begins (in the box's text)
    size_t caret = 0;
    size_t anchor = 0;
    std::string preedit;          ///< the input method's text not yet typed
    /// A change of the text: source[at, at + removed.size()) was `removed` and is `inserted` since (whole texts
    /// would be too much for a long file).
    struct Change {
        size_t at = 0;
        std::string removed;
        std::string inserted;
        size_t caretBefore = 0;
    };
    std::vector<Change> undoStack;
    std::vector<Change> redoStack;
    bool lastWasTyping = false;
    bool plain = false;  ///< plain text (a .txt): no Markdown keys
    QRectF lastArea;
};

}  // namespace xqt

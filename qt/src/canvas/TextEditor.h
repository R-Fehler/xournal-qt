/*
 * xournal-qt: editing a text element on the canvas (text tool).
 *
 * Qt port of upstream's control/tools/TextEditor (which is built on GtkTextBuffer and GtkIMContext): the same model
 * behavior — a tap edits the text under it or creates a new one with the tool's font and color; the original element
 * stays in its layer marked "in editing" (not rendered) while a copy is edited; finishing adds InsertUndoAction,
 * TextBoxUndoAction or DeleteUndoAction (empty text) — with Qt keys and input methods (dead keys, the on-screen
 * keyboard). The text is laid out with Pango, like the renderer, so it looks the same while and after editing.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "model/Layer.h"

#include <memory>
#include <optional>

#include <QRectF>
#include <QString>
#include <QVariant>

#include <cairo.h>

#include "model/OverlayBase.h"

#include "CanvasTextInput.h"
#include "model/PageRef.h"
#include "util/Rectangle.h"

class QInputMethodEvent;
class QKeyEvent;
class Text;
class XojFont;

namespace xoj::view {
class OverlayView;
}

namespace xqt {

class CanvasPage;
class DocumentSession;

/// How the text tool makes a new text.
struct NewTextOptions {
    /// A Markdown text (a text box in the page's layer "Markdown", drawn formatted when not being edited): with
    /// this font size, as wide as there is room up to the right margin.
    bool markdown = false;
    double markdownSize = 10;
};

class TextEditor final: public CanvasTextInput {
public:
    using NewText = NewTextOptions;
    /// Start editing at a page position (points): the text there (a Markdown text drawn there, or a text of the
    /// selected layer), or a new one.
    TextEditor(DocumentSession& session, CanvasPage& page, double x, double y, const NewText& how = {});
    /// Finishes the edition (undo action; an empty text is removed).
    ~TextEditor() override;

    CanvasPage& getPage() const override { return page; }
    /// The view drawing the text being edited, for the page's overlays.
    std::unique_ptr<xoj::view::OverlayView> createView();

    /// The point (page coordinates) is on the edited text box (upstream isEventInEditor).
    bool contains(double x, double y) const override;
    void mousePressed(double x, double y) override;
    void mouseMoved(double x, double y) override;

    /// Returns false if the key is not for the editor. `finish` is set for Escape.
    bool keyPressed(const QKeyEvent* e, bool& finish) override;
    /// Whether the editor wants this key instead of an application shortcut.
    static bool wantsKey(const QKeyEvent* e);
    bool wantsKeyEvent(const QKeyEvent* e) const override { return wantsKey(e); }
    void inputMethodEvent(const QInputMethodEvent* e) override;
    /// `cursorRect`: the cursor in page coordinates.
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    QRectF cursorRectOnPage() const override;
    std::string textBeforeCursor() const override;
    void replaceBeforeCursor(size_t bytes, const std::string& text) override;

    void setFont(const XojFont& font);
    void setColor(uint32_t argb);
    const QString& text() const { return content; }
    /// A Markdown text is edited (its source).
    bool isMarkdown() const { return markdown; }
    double fontSize() const;

    /// Draws the text, the selection, the preedit text and the cursor (page coordinates).
    void paint(cairo_t* cr) const;
    /// Area to repaint (page coordinates).
    xoj::util::Rectangle<double> area() const;

private:
    void changed(bool textChanged);
    void insert(const QString& s);
    void removeSelection();
    bool hasSelection() const { return anchor != cursor; }
    void moveCursor(int to, bool keepAnchor);
    int wordBoundary(int from, bool forward) const;
    int lineMove(int from, int lines) const;
    int lineEdge(int from, bool end) const;
    int indexAt(double x, double y) const;
    int toUtf8(int qIndex) const;
    int fromUtf8(int byteIndex) const;
    void finalize();
    void finalizeText();
    void useMarkdownLayer();

    DocumentSession& session;
    CanvasPage& page;
    PageRef pageRef;
    Layer* layer = nullptr;             ///< where the text is / goes
    bool markdown = false;
    bool createdLayer = false;          ///< the Markdown layer was made for this text
    Layer::Index selectedBefore = 0;
    std::unique_ptr<Text> textElement;  ///< the copy being edited
    Text* original = nullptr;           ///< the element in the layer (nullptr: new text)
    QString content;
    QString preedit;
    int cursor = 0;
    int anchor = 0;
    xoj::util::Rectangle<double> lastArea;
};

}  // namespace xqt

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

#include <memory>
#include <optional>

#include <QRectF>
#include <QString>
#include <QVariant>

#include <cairo.h>

#include "model/OverlayBase.h"
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

class TextEditor final: public OverlayBase {
public:
    /// Start editing at a page position (points): the text there, or a new one.
    TextEditor(DocumentSession& session, CanvasPage& page, double x, double y);
    /// Finishes the edition (undo action; an empty text is removed).
    ~TextEditor() override;

    CanvasPage& getPage() const { return page; }
    /// The view drawing the text being edited, for the page's overlays.
    std::unique_ptr<xoj::view::OverlayView> createView();

    /// The point (page coordinates) is on the edited text box (upstream isEventInEditor).
    bool contains(double x, double y) const;
    void mousePressed(double x, double y);
    void mouseMoved(double x, double y);

    /// Returns false if the key is not for the editor. `finish` is set for Escape.
    bool keyPressed(const QKeyEvent* e, bool& finish);
    /// Whether the editor wants this key instead of an application shortcut.
    static bool wantsKey(const QKeyEvent* e);
    void inputMethodEvent(const QInputMethodEvent* e);
    /// `cursorRect`: the cursor in page coordinates.
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const;
    QRectF cursorRectOnPage() const;

    void setFont(const XojFont& font);
    void setColor(uint32_t argb);
    const QString& text() const { return content; }

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

    DocumentSession& session;
    CanvasPage& page;
    PageRef pageRef;
    std::unique_ptr<Text> textElement;  ///< the copy being edited
    Text* original = nullptr;           ///< the element in the layer (nullptr: new text)
    QString content;
    QString preedit;
    int cursor = 0;
    int anchor = 0;
    xoj::util::Rectangle<double> lastArea;
};

}  // namespace xqt

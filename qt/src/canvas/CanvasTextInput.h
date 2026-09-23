/*
 * xournal-qt: text typed on the canvas (the text tool): a text box (TextEditor) or Markdown written on the page
 * (MarkdownEditor). The canvas item gives it the keys and the input method, the page the presses and drags.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QRectF>
#include <QVariant>

#include "model/OverlayBase.h"

class QInputMethodEvent;
class QKeyEvent;

namespace xqt {

class CanvasPage;

class CanvasTextInput: public OverlayBase {
public:
    ~CanvasTextInput() override = default;
    /// The page with the cursor.
    virtual CanvasPage& getPage() const = 0;
    /// The point (page coordinates, of getPage()) is on the text.
    virtual bool contains(double x, double y) const = 0;
    virtual void mousePressed(double x, double y) = 0;
    virtual void mouseMoved(double x, double y) = 0;
    /// Returns false if the key is not for the editor. `finish` is set for Escape.
    virtual bool keyPressed(const QKeyEvent* e, bool& finish) = 0;
    /// Whether the editor wants this key instead of an application shortcut.
    virtual bool wantsKeyEvent(const QKeyEvent* e) const = 0;
    virtual void inputMethodEvent(const QInputMethodEvent* e) = 0;
    virtual QVariant inputMethodQuery(Qt::InputMethodQuery query) const = 0;
    /// The cursor, in page coordinates (of getPage()).
    virtual QRectF cursorRectOnPage() const = 0;
};

}  // namespace xqt

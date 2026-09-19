/*
 * xournal-qt: shadow of upstream gui/XournalView.h.
 * Abstract per-tab document view as seen by reused upstream code; implemented by the Qt canvas (qt/src/canvas).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t

class Control;
class EditSelection;
class Layout;
class XournalppCursor;

class XournalView {
public:
    virtual ~XournalView() = default;

    virtual size_t getCurrentPage() const = 0;
    /// The layers of the given page changed (visibility, order, active layer, ...).
    virtual void layerChanged(size_t page) = 0;
    /// The background PDF was replaced: drop cached PDF renderings.
    virtual void recreatePdfCache() = 0;

    // --- selection (upstream XournalView; the view owns the selection) ---
    virtual EditSelection* getSelection() const { return nullptr; }
    /// Takes ownership. Replaces (clears) a previous selection.
    virtual void setSelection(EditSelection* /*selection*/) {}
    /// The selected elements go back to their layer.
    virtual void clearSelection() {}
    /// Delete the selected elements (undoable).
    virtual void deleteSelection(EditSelection* /*sel*/ = nullptr) {}
    virtual void repaintSelection(bool /*evenWithoutSelection*/ = false) {}

    // --- used by the selection ---
    virtual double getZoom() const { return 1.0; }
    virtual XournalppCursor* getCursor() const { return nullptr; }
    virtual Control* getControl() const { return nullptr; }
    virtual Layout* getLayout() const { return nullptr; }
    virtual void ensureRectIsVisible(int /*x*/, int /*y*/, int /*width*/, int /*height*/) {}
    virtual void pageSelected(size_t /*page*/) {}
};

/*
 * xournal-qt: shadow of upstream gui/XournalView.h.
 * Abstract per-tab document view as seen by reused upstream code; implemented by the Qt canvas (qt/src/canvas).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t

class XournalView {
public:
    virtual ~XournalView() = default;

    virtual size_t getCurrentPage() const = 0;
    /// The layers of the given page changed (visibility, order, active layer, ...).
    virtual void layerChanged(size_t page) = 0;
    /// The background PDF was replaced: drop cached PDF renderings.
    virtual void recreatePdfCache() = 0;
};

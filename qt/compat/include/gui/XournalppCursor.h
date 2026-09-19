/*
 * xournal-qt: shadow of upstream gui/XournalppCursor.h.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class XournalppCursor {
public:
    virtual ~XournalppCursor() = default;
    virtual void setCursorBusy(bool busy) = 0;
    /// Recompute the cursor from the current tool and state.
    virtual void updateCursor() = 0;
};

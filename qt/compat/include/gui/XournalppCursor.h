/*
 * xournal-qt: shadow of upstream gui/XournalppCursor.h.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "control/tools/CursorSelectionType.h"
#include "gui/inputdevices/InputEvents.h"  // (upstream's header includes it; reused code relies on it for KeyEvent)

class XournalppCursor {
public:
    virtual ~XournalppCursor() = default;
    virtual void setCursorBusy(bool busy) = 0;
    /// Recompute the cursor from the current tool and state.
    virtual void updateCursor() = 0;
    // Cursor shape hints of upstream's tools (the Qt canvas shows a hover dot; not needed so far).
    virtual void setMouseSelectionType(CursorSelectionType) {}
    virtual void setMouseDown(bool) {}
    virtual void setInvisible(bool) {}
    virtual void setInsidePage(bool) {}
    virtual void setIsLinkHighlighted(bool) {}
    virtual void activateDrawDirCursor(bool /*enable*/, bool /*shift*/ = false, bool /*ctrl*/ = false) {}
    virtual void setInputDeviceClass(InputDeviceClass) {}
    virtual void setRotationAngle(double) {}
    virtual void setMirror(bool) {}
};

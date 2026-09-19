/*
 * xournal-qt: shadow of upstream gui/PageView.h (XojPageView).
 *
 * The page view as seen by reused upstream code (selection: EditSelection, EditSelectionContents). Implemented by
 * the Qt canvas' CanvasPage. Same bases and method names as upstream.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "gui/LegacyRedrawable.h"
#include "model/PageListener.h"
#include "model/PageRef.h"
#include "util/Point.h"
#include "view/Repaintable.h"

class XournalView;

class XojPageView: public LegacyRedrawable, public PageListener, public xoj::view::Repaintable {
public:
    ~XojPageView() override = default;

    virtual XournalView* getXournal() const = 0;
    virtual const PageRef getPage() const = 0;
    /// Position of the page in the layout (content pixels, independent of scrolling).
    virtual xoj::util::Point<int> getPixelPosition() const = 0;
};

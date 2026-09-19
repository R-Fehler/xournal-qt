/*
 * xournal-qt: shadow of upstream gui/Layout.h: the page layout as seen by reused upstream code (selection moves
 * between pages, scrolls near the edges). Implemented by the Qt canvas (CanvasView). Coordinates are content pixels
 * like upstream's (the same as XojPageView::getPixelPosition).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "util/Rectangle.h"

class XojPageView;

class Layout {
public:
    virtual ~Layout() = default;
    virtual XojPageView* getPageViewAt(int x, int y) const = 0;
    virtual int getTotalPixelWidth() const = 0;
    virtual int getTotalPixelHeight() const = 0;
    virtual xoj::util::Rectangle<double> getVisibleRect() = 0;
    virtual void scrollRelative(double x, double y) = 0;
};

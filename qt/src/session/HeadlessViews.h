/*
 * xournal-qt: default implementations of the view-side shadow interfaces for a document session that is not (yet)
 * shown by a canvas: tests, the CLI, background tabs. The Qt canvas replaces them with real implementations.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <functional>

#include "control/ScrollHandler.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/XournalppCursor.h"

namespace xqt {

class HeadlessXournalView final: public XournalView {
public:
    size_t currentPage = 0;
    size_t getCurrentPage() const override { return currentPage; }
    void layerChanged(size_t) override {}
    void recreatePdfCache() override {}
};

/// `control->getWindow()` of a session: gives access to whatever view currently shows the session.
class SessionWindow final: public MainWindow {
public:
    XournalView* view = nullptr;
    XournalView* getXournal() const override { return view; }
};

class HeadlessCursor final: public XournalppCursor {
public:
    void setCursorBusy(bool) override {}
    void updateCursor() override {}
};

/// Scrolling requests from reused upstream code (e.g. undoing a page deletion scrolls to that page).
class SessionScrollHandler final: public ScrollHandler {
public:
    std::function<void(size_t page, XojPdfRectangle rect)> onScrollToPage;
    std::function<void(const LinkDestination& dest)> onScrollToLinkDest;
    std::function<size_t(const PageRef& page)> indexOf;

    void scrollToPage(const PageRef& page, XojPdfRectangle rect = {0, 0, -1, -1}) override {
        if (indexOf) {
            scrollToPage(indexOf(page), rect);
        }
    }
    void scrollToPage(size_t page, XojPdfRectangle rect = {0, 0, -1, -1}) override {
        if (onScrollToPage) {
            onScrollToPage(page, rect);
        }
    }
    void scrollToLinkDest(const LinkDestination& dest) override {
        if (onScrollToLinkDest) {
            onScrollToLinkDest(dest);
        }
    }
};

}  // namespace xqt

/*
 * xournal-qt: default implementations of the view-side shadow interfaces for a document session that is not (yet)
 * shown by a canvas: tests, the CLI, background tabs. The Qt canvas replaces them with real implementations.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <functional>
#include <vector>

#include "control/ScrollHandler.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "gui/XournalppCursor.h"

class ZoomControl;

namespace xqt {

class HeadlessXournalView final: public XournalView {
public:
    size_t currentPage = 0;
    size_t getCurrentPage() const override { return currentPage; }
    void layerChanged(size_t) override {}
    void recreatePdfCache() override {}
};

/// The views showing a session, as one XournalView for reused upstream code (qt/self-reference: a tab's view and a
/// second view of the same document beside it). What is about the document goes to all of them (a layer changed, the
/// PDF replaced); the rest (the selection, the zoom, the current page) to the active one: the first view (the
/// primary one), or while a second view acts (DocumentSession::ViewScope) that one. Without views: the headless one.
class SessionViews final: public XournalView {
public:
    explicit SessionViews(XournalView& headless): headless(headless) {}
    struct Entry {
        XournalView* view;
        ZoomControl* zoom;
    };
    std::vector<Entry> views;
    /// The view acting now instead of the primary one (nullptr: none)
    XournalView* scoped = nullptr;

    XournalView& active() const { return scoped ? *scoped : views.empty() ? headless : *views.front().view; }
    /// The zoom of the active view (nullptr: none, headless)
    ZoomControl* activeZoom() const {
        const XournalView* a = &active();
        for (const Entry& e: views) {
            if (e.view == a) {
                return e.zoom;
            }
        }
        return nullptr;
    }

    size_t getCurrentPage() const override { return active().getCurrentPage(); }
    void layerChanged(size_t page) override {
        if (views.empty()) {
            headless.layerChanged(page);
        }
        for (const Entry& e: views) {
            e.view->layerChanged(page);
        }
    }
    void recreatePdfCache() override {
        if (views.empty()) {
            headless.recreatePdfCache();
        }
        for (const Entry& e: views) {
            e.view->recreatePdfCache();
        }
    }
    EditSelection* getSelection() const override { return active().getSelection(); }
    void setSelection(EditSelection* selection) override { active().setSelection(selection); }
    void clearSelection() override { active().clearSelection(); }
    void deleteSelection(EditSelection* sel = nullptr) override { active().deleteSelection(sel); }
    void repaintSelection(bool evenWithoutSelection = false) override { active().repaintSelection(evenWithoutSelection); }
    double getZoom() const override { return active().getZoom(); }
    XournalppCursor* getCursor() const override { return active().getCursor(); }
    Control* getControl() const override { return active().getControl(); }
    Layout* getLayout() const override { return active().getLayout(); }
    void ensureRectIsVisible(int x, int y, int width, int height) override {
        active().ensureRectIsVisible(x, y, width, height);
    }
    void pageSelected(size_t page) override { active().pageSelected(page); }

private:
    XournalView& headless;
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

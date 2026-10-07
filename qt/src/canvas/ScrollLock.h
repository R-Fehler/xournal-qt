/*
 * xournal-qt: two views scrolled together (the reference view's locked scrolling, qt/docs/features/reference-view.md).
 *
 * Generic for any two views: two different documents, a document beside itself, a version beside now. While locked,
 * scrolling, a jump to a page and zooming in either view move the other one:
 * - **Where:** by page, with the page offset the two had when they were locked (page 3 here beside page 1 there stays
 *   so), and by the place within the page relative to its size, so pages of other sizes still line up. The point
 *   that is kept together is the middle of the top edge of each view (sideways: of the left edge): two documents
 *   shown from their beginnings are on their first pages, whatever the size of their pages.
 * - **Zoom:** relative to the width that fits (ViewController::fitWidthZoom of the page in view): the two keep the
 *   ratio they had when locked, so a page that fills its half keeps filling it on the other side too, whatever the
 *   size of the pages or of the halves.
 * - A view whose size changed (the divider, the window) follows the other one; it never moves the other. A view
 *   locked before it had a size (a reference just opened) keeps where it opens: the pair is taken from there.
 * - No loops: while one view moves the other, the other's signals are not followed (`syncing`), and a view that is
 *   where it should be is not moved again. Nothing is rendered beyond what scrolling the other view needs.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QSizeF>

#include <cstddef>

namespace xqt {

class ViewController;

class ScrollLock final: public QObject {
    Q_OBJECT
public:
    explicit ScrollLock(QObject* parent = nullptr);
    ~ScrollLock() override;

    /// Keep `b` with `a` from now on: the page offset is the difference of the pages in the middle of the two views
    /// now, and `b` comes to `a`'s place within the page (the zoom of each stays as it is).
    void lock(ViewController* a, ViewController* b);
    void unlock();
    bool locked() const { return !a.isNull() && !b.isNull(); }
    ViewController* first() const;
    ViewController* second() const;
    /// Page of `b` minus page of `a`
    int pageOffset() const { return offset; }
    /// Show page `pageA` in `a` and `pageB` in `b`, which become the pair from now on (the next or previous change
    /// of a comparison). Also while not locked.
    static void showPages(ViewController& a, size_t pageA, ViewController& b, size_t pageB);
    /// The same, keeping the lock: the offset becomes pageB - pageA.
    void showPages(size_t pageA, size_t pageB);
    /// The point of a view that is kept together with the other's: the middle of its top edge (sideways: of its left
    /// edge), a little in from it
    static QPointF anchorOf(const ViewController& v);

Q_SIGNALS:
    void lockedChanged();

private:
    /// `from` moved: `to` follows (`toOffset`: the page of `to` minus the page of `from`)
    void follow(ViewController& from, ViewController& to, int toOffset);
    void zoomed(ViewController& from, ViewController& to, bool fromIsA);
    void moved(ViewController& from, ViewController& to, bool fromIsA);
    /// The offset and the ratio of the zooms from where the two views are now
    void recapture();
    /// zoom / the zoom that fits the width of the page in view (0: not known yet)
    static double relativeZoom(const ViewController& v);

    QPointer<ViewController> a, b;
    int offset = 0;
    /// relativeZoom(b) / relativeZoom(a) when locked (none: not known, zoom by the same factor then)
    std::optional<double> zoomRatio;
    double lastZoomA = 0, lastZoomB = 0;
    QSizeF lastSizeA, lastSizeB;
    bool syncing = false;
    bool settling = false;  ///< a view got its first size: the pair is taken once it has settled (moving nothing)
    std::vector<QMetaObject::Connection> connections;
};

}  // namespace xqt

/*
 * xournal-qt: the memory for rendered canvas pages, shared by all open documents (all tabs of all windows).
 *
 * Rendering a page (poppler, thousands of strokes, big images) is what costs time and energy; keeping it costs only
 * memory. So the canvas keeps as many rendered pages as the limit allows (Settings, default a quarter of the RAM)
 * and renders pages in advance:
 *  - the document used last may take 70 % of the limit while other documents are open, all of it when it is the
 *    only one. Of its part, 35 % go to the pages before the visible ones and 65 % to the pages after them (reading
 *    goes forward); where one side has no more pages the other gets the rest. The visible pages always stay;
 *  - its pages in that window are rendered in advance by the background workers of the RenderService (idle
 *    priority, a PDF instance of their own), nearest first, once scrolling paused. A page rendered at another zoom
 *    is shown scaled meanwhile; near ones are rendered again at the new zoom, far ones when they come near;
 *  - the other documents keep what they have while the rest of the limit holds it; when it does not, the one used
 *    longest ago gives up its pages first, those farthest from where it was read first. Documents in sight (the
 *    reference beside the notes, a document in another window) come before those in the background, and never give
 *    up their visible pages.
 * A plan is made shortly after the reader stopped scrolling or zooming, switched documents or the limit changed.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QObject>
#include <QTimer>

namespace xqt {

class CanvasView;

class CanvasMemory final: public QObject {
    Q_OBJECT
public:
    static CanvasMemory& instance();

    /// Physical memory of the machine (bytes)
    static qint64 systemMemory();
    /// Default limit: a quarter of the RAM; the setting goes up to a third
    static qint64 defaultLimit();
    static qint64 maxLimit();
    static constexpr double CURRENT_SHARE = 0.7;
    static constexpr double BEFORE_SHARE = 0.35;
    /// Pages at most this far away that have a buffer at another zoom are rendered again in advance
    static constexpr int NEAR_PAGES = 10;
    /// A tenth of the limit is for the previews of all pages (PageSketches), the rest for rendered pages
    static constexpr double PREVIEW_SHARE = 0.1;

    void setLimit(qint64 bytes);
    qint64 limit() const { return max; }
    qint64 previewBudget() const { return static_cast<qint64>(static_cast<double>(max) * PREVIEW_SHARE); }
    qint64 pagesLimit() const { return max - previewBudget(); }

    void add(CanvasView* view);
    void remove(CanvasView* view);
    /// The reader uses this view (scrolled, zoomed, switched to it): it becomes the current one; a plan follows.
    void used(CanvasView* view);
    /// Plan right away (tests)
    void planNow();
    /// Rendered pages of all views (bytes)
    qint64 bytes() const;
    void setPlanDelay(int ms) { planTimer.setInterval(ms); }

Q_SIGNALS:
    void limitChanged();

private:
    CanvasMemory();
    void plan();

    struct View {
        CanvasView* view;
        quint64 used;
    };
    std::vector<View> views;
    quint64 counter = 0;
    qint64 max;
    QTimer planTimer;
};

}  // namespace xqt

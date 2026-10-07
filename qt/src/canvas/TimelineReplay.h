/*
 * xournal-qt: a document view shows its document as of a moment of its timeline (qt/docs/features/timeline.md,
 * "Replay").
 *
 * Read-only: the view is for reading while it replays (CanvasView::isReadingOnly); the document is never changed.
 *
 * Drawing, so that playing stays smooth and the pictures of the pages are kept:
 * - The pages' pictures (PageRaster) are drawn with a filter that shows the first N elements of the timeline (the
 *   "committed" frame). Changing it draws the pages whose content differs again, in the background, as any change
 *   does; the old picture stays until the new one is there.
 * - What came since (a few elements while playing) and the stroke being written are drawn over the picture, as the
 *   stroke being written with the pen is (CanvasPage::composeTile asks drawOverlay): only the tiles they touch are
 *   composed again, nothing is rendered. The overlay draws exactly what the picture shown lacks (it knows the filter
 *   that picture was drawn with), so nothing is drawn twice or missing while a new picture is on its way.
 * - The frame is committed when the overlay would hold too much (MAX_OVERLAY elements, or it is older than
 *   COMMIT_AFTER_MS), when it goes back (a picture cannot be drawn smaller), when a sticky note comes (its paper is
 *   drawn by its own drawer), and when playing pauses (settle()).
 *
 * UI thread, except rasterFilter (any thread).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QElapsedTimer>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <cairo.h>

#include "session/Timeline.h"

class Element;

namespace xqt {
namespace render {
class ElementFilter;
}

class CanvasView;

class TimelineReplay {
public:
    /// At most this many elements are drawn over a page's picture before it is drawn again
    static constexpr size_t MAX_OVERLAY = 48;
    /// ... and for at most this long
    static constexpr int64_t COMMIT_AFTER_MS = 2500;

    TimelineReplay(CanvasView& view, std::shared_ptr<const timeline::Timeline> timeline, int64_t at);
    TimelineReplay(const TimelineReplay&) = delete;
    TimelineReplay& operator=(const TimelineReplay&) = delete;

    const timeline::Timeline& timeline() const { return *line; }
    int64_t position() const { return at; }
    const timeline::Frame& frame() const { return now; }

    /// Shows the document as of bar time `t`
    void seek(int64_t t);
    /// The pictures of the pages catch up with the frame (playing paused, scrubbing ended)
    void settle();

    /// The filter the pages' pictures are drawn with (any thread)
    std::shared_ptr<const render::ElementFilter> rasterFilter() const;
    /// Draws over the picture of page `page` what it lacks at the frame (page coordinates; `drawn`: the filter that
    /// picture was drawn with, nullptr: all elements)
    void drawOverlay(cairo_t* cr, size_t page, const render::ElementFilter* drawn) const;
    /// The element shown nearest to (x, y) on page `page` (page coordinates), closer than `radius`: its event
    std::optional<size_t> shownAt(size_t page, double x, double y, double radius) const;
    /// The number of elements the committed frame shows (tests)
    size_t committedShown() const;

private:
    void commit(size_t shown);
    /// The pages' areas where what the overlay draws changed from `before` to `now`
    void flagChanges(const timeline::Frame& before);
    size_t shownBy(const render::ElementFilter* drawn) const;
    void drawEvent(cairo_t* cr, size_t index, double fraction) const;

    CanvasView& view;
    std::shared_ptr<const timeline::Timeline> line;
    int64_t at = 0;
    timeline::Frame now;
    mutable std::mutex filterMutex;
    std::shared_ptr<const render::ElementFilter> committed;
    size_t committedCount = 0;
    QElapsedTimer sinceCommit;
};

}  // namespace xqt

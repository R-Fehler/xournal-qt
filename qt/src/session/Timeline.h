/*
 * xournal-qt: the timeline of a document (qt/docs/features/timeline.md, "The timeline"): its elements in the order they
 * were made, and its recordings, on one clock. The replay (CanvasView::setReplay, app.timeline) shows the document as
 * of a moment of it.
 *
 * - **One clock**: absolute times (ms since 1970 UTC). An element is at the time it was made (xqt-created,
 *   ElementTimes.h). An element tied to a recording (upstream's fn/ts) is at the recording's start plus its ts, so it
 *   appears exactly when it is heard. A recording starts when its elements say (their xqt-created minus their ts, the
 *   earliest: pauses of the recorder only make that difference larger), else at the time in its name
 *   ("2026-10-04_14-03-22.ogg", local time, as Xournal++ and xournal-qt name them; so Xournal++ files with a recording
 *   are placed too), else nowhere (its elements count as having no time). A recording is a track on the bar from its
 *   start for its length.
 * - **The bar** (what the play bar shows, ms from 0): the absolute times with long pauses taken out. A pause longer
 *   than IDLE_GAP in which nothing is written and nothing recorded becomes PAUSE_ON_BAR; one longer than SESSION_GAP
 *   also starts a new session, a mark on the bar. Shorter pauses play as they were.
 * - **Elements without a time** (older files, files saved by Xournal++, elements of a recording placed nowhere) come
 *   first, page by page in the order of their layers, a moment each (a prelude of at most PRELUDE_MAX).
 * - A stroke is drawn on over a while (its length at handwriting speed, at most until the next element comes): points
 *   have no times, so it grows evenly along its length. Other elements appear at once.
 * - Elements of hidden layers are not on it (they are not drawn).
 *
 * Pure model: no Qt widgets, no drawing. The caller holds the document's lock (shared) while building; the timeline
 * keeps pointers to the elements, valid while the document is not changed (the replay is read-only and ends when the
 * document changes).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Document;
class Element;
class Layer;

namespace xqt::timeline {

/// A pause longer than this (nothing written, nothing recorded) is shortened to PAUSE_ON_BAR
constexpr int64_t IDLE_GAP = 8000;
/// What is left of such a pause on the bar
constexpr int64_t PAUSE_ON_BAR = 1500;
/// A pause longer than this starts a new session (a mark on the bar)
constexpr int64_t SESSION_GAP = 20 * 60 * 1000;
/// The elements without a time: a moment each, together at most this long
constexpr int64_t PRELUDE_STEP = 120;
constexpr int64_t PRELUDE_MAX = 6000;
/// Handwriting speed (points a second) and the limits of the time a stroke takes to be drawn on
constexpr double STROKE_SPEED = 160;
constexpr int64_t STROKE_MIN = 120;
constexpr int64_t STROKE_MAX = 2500;

/// The start of a recording from its name ("2026-10-04_14-03-22.ogg", "…-2.ogg", a path to one: local time, as
/// Xournal++ names them), ms since 1970 UTC; nullopt: the name has no time
std::optional<int64_t> startFromName(const std::string& name);

/// An element on the bar
struct Event {
    const Element* element = nullptr;
    const Layer* layer = nullptr;  ///< the layer it is in
    size_t page = 0;               ///< its page (0-based)
    int64_t at = 0;                ///< when it starts to appear (bar ms)
    int64_t length = 0;            ///< a stroke is drawn on over this long (bar ms); 0: at once
    int64_t when = 0;              ///< the absolute time (ms since 1970 UTC); 0: it has none (the prelude)
};

/// A recording on the bar
struct Track {
    std::string name;    ///< as the elements and memos name it
    int64_t at = 0;      ///< where it starts on the bar
    int64_t length = 0;  ///< ms
    int64_t start = 0;   ///< absolute start (ms since 1970 UTC)
    bool found = false;  ///< its file was found (it can be heard)
};

/// The start of a session (or of the prelude: `when` 0)
struct Mark {
    int64_t at = 0;
    int64_t when = 0;
};

/// The document at a moment: the first `shown` events are shown whole; `drawing` (when set: event `shown`) is being
/// drawn on, `fraction` of it
struct Frame {
    size_t shown = 0;
    std::optional<size_t> drawing;
    double fraction = 0;
    bool operator==(const Frame& o) const { return shown == o.shown && drawing == o.drawing && fraction == o.fraction; }
};

/// What is heard at a moment: the track and the position in it (ms)
struct Heard {
    size_t track = 0;
    int64_t position = 0;
};

class Timeline {
public:
    /// The length of a recording in ms (nullopt: its file is nowhere)
    using LengthOf = std::function<std::optional<int64_t>(const std::string& name)>;

    Timeline() = default;
    /// The caller holds the document's lock (shared)
    static Timeline build(const Document& doc, const LengthOf& lengthOf);

    const std::vector<Event>& events() const { return list; }
    const std::vector<Track>& tracks() const { return recordings; }
    const std::vector<Mark>& marks() const { return sessions; }
    /// The length of the bar (ms)
    int64_t duration() const { return total; }
    bool empty() const { return list.empty() && recordings.empty(); }

    /// The document at bar time `t`
    Frame frameAt(int64_t t) const;
    /// The index of an element's event (nullopt: not on the timeline)
    std::optional<size_t> indexOf(const Element* e) const;
    /// The absolute time at bar time `t` (nullopt: in the prelude, before any time)
    std::optional<int64_t> wallTime(int64_t t) const;
    /// The recording heard at bar time `t` (of those found; the one started last when they overlap)
    std::optional<Heard> heardAt(int64_t t) const;
    /// The bar time of an absolute time (the nearest place on the bar for a time in a shortened pause)
    int64_t barOf(int64_t when) const;

private:
    struct Segment {
        int64_t abs = 0;  ///< from this absolute time on ...
        int64_t bar = 0;  ///< ... the bar goes with the clock from here
    };
    std::vector<Event> list;
    std::vector<Track> recordings;
    std::vector<Mark> sessions;
    std::vector<Segment> segments;
    std::unordered_map<const Element*, size_t> index;
    int64_t total = 0;
};

}  // namespace xqt::timeline

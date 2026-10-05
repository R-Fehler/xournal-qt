/*
 * xournal-qt: gestures of the pen while it writes (qt/docs/pen-gestures.md).
 *
 * - Hold to straighten: a stroke of the pen or the highlighter, finished and held still for a moment before the pen is
 *   lifted, becomes what upstream's ShapeRecognizer makes of it (a line, a triangle, a rectangle, a circle or an
 *   ellipse); when it finds nothing, the stroke stays as drawn and the pen writes on. CanvasInput notices the pen at
 *   rest and asks the page (CanvasPage::straightenStroke), whose GestureStrokeHandler replaces the stroke the way
 *   upstream's shape recognizer mode does on release: two undo steps, the first one brings the freehand stroke back.
 *   The shape is there at once, while the pen still rests; lifting the pen ends it (what the pen does meanwhile is
 *   not drawn).
 * - Scratch out to erase (opt-in): a quick zigzag of the pen over ink deletes the strokes it covers, in one undo step,
 *   and is not kept itself (ScratchOut.h). A zigzag over nothing, or drawn slowly, stays a stroke.
 *
 * The settings, in the "xournalQt" part of settings.xml: "holdToStraighten" (on by default),
 * "holdToStraightenTime" (ms, 500 by default) and "scratchOut" (off by default).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include "control/tools/StrokeHandler.h"

class Settings;

namespace xqt {

namespace pengestures {
constexpr int DEFAULT_HOLD_MS = 500;
constexpr int MIN_HOLD_MS = 250;
constexpr int MAX_HOLD_MS = 2000;
/// A pen counts as resting while it stays this close to where it stopped (view pixels: a hand shakes a little)
constexpr double HOLD_SLOP_PX = 6.0;

bool holdToStraighten(Settings& settings);
void setHoldToStraighten(Settings& settings, bool on);
/// How long the pen rests before the stroke is straightened (ms)
int holdTime(Settings& settings);
void setHoldTime(Settings& settings, int ms);
bool scratchOut(Settings& settings);
void setScratchOut(Settings& settings, bool on);
}  // namespace pengestures

/// Upstream's StrokeHandler (freehand, and its shape recognizer mode) with the gestures of the pen.
class GestureStrokeHandler: public StrokeHandler {
public:
    GestureStrokeHandler(Control* control, const PageRef& page);

    /// The pen rests: the stroke so far becomes the shape the recogniser sees in it, in the document with its undo
    /// steps (as upstream's shape recognizer mode on release). False: no shape found, the stroke goes on as it was.
    /// `restRadius`: the points this close to the last one are the hand shaking at rest (page coordinates).
    bool straighten(double restRadius);
    /// The stroke was straightened: the pen does nothing more until it is lifted.
    bool straightened() const { return done; }

    void onButtonPressEvent(const PositionInputData& pos, double zoom) override;
    void onButtonReleaseEvent(const PositionInputData& pos, double zoom) override;

private:
    /// The stroke is a quick zigzag over strokes: they are deleted (one undo step), the zigzag goes. False: it stays.
    bool scratchOut(const PositionInputData& release, double zoom);

    bool done = false;
    guint32 pressTime = 0;
};

}  // namespace xqt

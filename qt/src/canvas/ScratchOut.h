/*
 * xournal-qt: scratch out to erase (qt/docs/pen-gestures.md): a quick zigzag of the pen over ink deletes the strokes
 * it covers. The shape test and the strokes it covers, without the canvas (GestureStrokeHandler uses them on release).
 *
 * A zigzag: drawn back and forth along a mostly horizontal axis (its main axis, within 40° of the horizontal), at
 * least three times turning back over most of its width, each sweep nearly straight and along the axis. Handwriting
 * moves on along the line (a "www" or "mmm" turns up and down, not back), loops ("eee") are curved, digits like "3"
 * or "8" turn back on slanted, curved paths; the handwriting fixture (test/files/benchmark/handwritten-text.xopp)
 * has no stroke that passes (PenGesturesTest).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include "model/Point.h"

class Element;
class Layer;

namespace xqt::scratchout {

/// How a stroke is shaped (page coordinates)
struct Shape {
    bool zigzag = false;
    int reversals = 0;     ///< turns back over at least TURN_FRACTION of the width
    double angle = 0;      ///< of the main axis, radians (0: horizontal)
    double width = 0;      ///< extent along the main axis
    double length = 0;     ///< of the whole path
    double straightness = 0;  ///< the sweeps' path length over their extent along the axis (1: straight)
    double steepest = 0;   ///< the steepest sweep's angle to the axis, radians
    double sweepShare = 0;  ///< the median sweep's extent along the axis over the width
    // the band it covers: its centre, and its extent along the axis (u) and across it (v) from there
    double cx = 0, cy = 0;
    double uMin = 0, uMax = 0, vMin = 0, vMax = 0;
};

/// Thresholds (tested on the handwriting fixture)
constexpr int MIN_POINTS = 8;
constexpr double MIN_WIDTH = 8.0;            ///< points (about 3 mm)
constexpr double MAX_AXIS_DEGREES = 40.0;    ///< the main axis to the horizontal
constexpr double TURN_FRACTION = 0.4;        ///< a turn goes back at least this share of the width
constexpr int MIN_REVERSALS = 3;
constexpr double MAX_SWEEP_DEGREES = 30.0;   ///< each sweep to the main axis
constexpr double MAX_STRAIGHTNESS = 1.3;     ///< the sweeps nearly straight (loops: 1.5 and more)
constexpr double MIN_SWEEP_SHARE = 0.55;     ///< the median sweep covers most of the width
/// Drawn quickly: at least this many view pixels per second along the path (handwriting is slower on average)
constexpr double MIN_SPEED_PX_PER_S = 200.0;
/// A stroke is covered when this share of its points lies within the zigzag's band (and the zigzag crosses it)
constexpr double MIN_COVERED = 0.6;

Shape analyse(const std::vector<Point>& points);
/// Fast enough? `durationMs` <= 0: not known (counts as fast).
bool quick(double length, double durationMs, double zoom);
/// The strokes of the layer the zigzag covers: it crosses them, and most of each lies within its band.
std::vector<const Element*> covered(const Layer& layer, const std::vector<Point>& zigzag, const Shape& shape,
                              double zigzagWidth);

}  // namespace xqt::scratchout

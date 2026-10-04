/*
 * xournal-qt: what the pointer looks like over the page (qt/docs/hover-cursors.md).
 *
 * The mouse and the hovering pen show the tool's pointer: a small dot (the default) or the crosshair, a setting. With
 * the eraser in hand (also the pen's eraser end, or a side button set to erase) it is the eraser itself: its real
 * size at the current zoom, gray, square as upstream's eraser (round for the whiteout eraser, whose ink is a round
 * brush), dashed when it deletes whole strokes.
 *
 * Both are cursors of the platform (QCursor from these pictures), which the compositor moves with the pointer at no
 * cost to the app. Where the platform shows no cursor for the pen, or the eraser is too big for a cursor, the canvas
 * draws the same picture itself (DocumentCanvasItem).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <QColor>
#include <QImage>
#include <QPointF>

class QPainter;
class Settings;
class ToolHandler;

namespace xqt::hover {

/// The tool's pointer over the page ("hoverPointer" in the xournalQt part of the settings: "dot" or "crosshair").
enum class Pointer { Dot, Crosshair };
Pointer pointerSetting(Settings& settings);
void setPointerSetting(Settings& settings, Pointer pointer);

/// The eraser as the pointer shows it.
struct EraserMark {
    double size = 0;            ///< logical pixels: the square's side or the circle's diameter
    bool round = false;         ///< the whiteout eraser (a round brush)
    bool wholeStrokes = false;  ///< deletes whole strokes: the outline is dashed
    bool operator==(const EraserMark& o) const {
        return size == o.size && round == o.round && wholeStrokes == o.wholeStrokes;
    }
};

/// The eraser a press would use now, at this zoom (logical pixels per point): the eraser end's tool (`eraserEnd`,
/// the tool of its button setting), else the active tool (the tool bar's, or a side button's while it is held).
/// None if that press would not erase.
std::optional<EraserMark> eraserMark(ToolHandler& tools, Settings& settings, bool eraserEnd, double zoom);

/// How the eraser is drawn (the cursor's picture and the canvas's own outline alike): a faint gray inside, a light
/// halo around the line (it shows on dark paper), the gray line, dashed for whole strokes (logical pixels)
inline const QColor ERASER_FILL(128, 128, 128, 46);
inline const QColor ERASER_LINE(90, 90, 90);
inline const QColor HALO(255, 255, 255, 210);
constexpr double HALO_WIDTH = 3;
constexpr double LINE_WIDTH = 1;
constexpr double DASH = 3;

/// The smallest outline drawn (an eraser smaller than this at the zoom is still shown at this size), logical pixels
constexpr double MIN_ERASER_PX = 5;
/// The largest cursor (device pixels a side) asked of the platform; a bigger eraser is drawn by the canvas
constexpr int MAX_CURSOR_PX = 256;

/// The side of the pictures (logical pixels, even: the middle is a whole pixel, the cursor's hot spot)
int dotSide();
int eraserSide(const EraserMark& mark);
/// The eraser's picture fits a cursor at this device pixel ratio
bool eraserFitsCursor(const EraserMark& mark, double dpr);

/// Paint the dot / the eraser with its middle at `center` (logical pixels).
void paintDot(QPainter& p, QPointF center);
void paintEraser(QPainter& p, const EraserMark& mark, QPointF center);
/// The pictures (for cursors): side × side logical pixels at this device pixel ratio, the shape in the middle.
QImage dotImage(double dpr);
QImage eraserImage(const EraserMark& mark, double dpr);

/// The platform shows a cursor for a hovering pen (the window's cursor where the pen is): X11 (the pen moves the
/// pointer), Windows Ink, macOS, and Wayland's tablet protocol from Qt 6.7 on. Not Android, iOS, nor off-screen.
/// XQT_PEN_CURSOR=0/1 overrides it (to check on a device).
bool platformShowsPenCursor();
/// For tests: as if the platform did (true) / did not (false); nullopt: as it is.
void setPlatformShowsPenCursorForTests(std::optional<bool> shows);

}  // namespace xqt::hover

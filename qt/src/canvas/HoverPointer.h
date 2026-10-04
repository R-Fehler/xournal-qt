/*
 * xournal-qt: what the pointer looks like over the page (qt/docs/hover-cursors.md).
 *
 * The mouse and the hovering pen show the tool's pointer: a small dot (the default) or the crosshair, a setting.
 *
 * It is a cursor of the platform (QCursor from this picture), which the compositor moves with the pointer at no
 * cost to the app. Where the platform shows no cursor for the pen, the canvas draws the same picture itself
 * (DocumentCanvasItem).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <QImage>
#include <QPointF>

class QPainter;
class Settings;

namespace xqt::hover {

/// The tool's pointer over the page ("hoverPointer" in the xournalQt part of the settings: "dot" or "crosshair").
enum class Pointer { Dot, Crosshair };
Pointer pointerSetting(Settings& settings);
void setPointerSetting(Settings& settings, Pointer pointer);

/// The side of the pictures (logical pixels, even: the middle is a whole pixel, the cursor's hot spot)
int dotSide();

/// Paint the dot with its middle at `center` (logical pixels).
void paintDot(QPainter& p, QPointF center);
/// The picture (for the cursor): side × side logical pixels at this device pixel ratio, the dot in the middle.
QImage dotImage(double dpr);

/// The platform shows a cursor for a hovering pen (the window's cursor where the pen is): X11 (the pen moves the
/// pointer), Windows Ink, macOS, and Wayland's tablet protocol from Qt 6.7 on. Not Android, iOS, nor off-screen.
/// XQT_PEN_CURSOR=0/1 overrides it (to check on a device).
bool platformShowsPenCursor();
/// For tests: as if the platform did (true) / did not (false); nullopt: as it is.
void setPlatformShowsPenCursorForTests(std::optional<bool> shows);

}  // namespace xqt::hover

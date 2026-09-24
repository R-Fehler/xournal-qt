/*
 * xournal-qt: XQT_LOG_INPUT=1 writes what the pen, touch and mouse send to stderr (qt/docs/windows.md, "Pen input").
 *
 * Per press: the event, its device (name, type, pointer type, capabilities), position, pressure, tilt, buttons, and
 * what the canvas makes of it; then the first few moves of each stroke, the release, and the pen's proximity. Off by
 * default; then each call costs one test of a static flag.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

class QEvent;

namespace xqt::inputlog {

/// Whether XQT_LOG_INPUT is set (read once).
bool enabled();

/// An input event the window (or, for proximity, the application) got; logged once however many canvases see it.
void event(const QEvent* e);

/// What a canvas did with it: taken (it draws or scrolls) or passed on (to the controls, or unclaimed). `as` says how
/// the canvas treats it, e.g. "pen, pressure used" or "mouse (synthesized from the pen: ignored)".
void decision(const QEvent* e, bool taken, const char* as);

}  // namespace xqt::inputlog

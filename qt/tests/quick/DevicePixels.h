/*
 * xournal-qt: the quick tests at any scale factor (QT_SCALE_FACTOR=1.25, 1.5, 2; qt/docs/features/hidpi.md).
 *
 * The tests think in the window's logical pixels. Two things Qt hands them are in device pixels:
 * - QWindowSystemInterface takes the positions of the events it is given as the platform's (native, device pixels),
 *   as a real tablet or mouse delivers them;
 * - QQuickWindow::grabWindow() returns the window's device pixels.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>
#include <QPointF>
#include <QWindow>

#include <private/qhighdpiscaling_p.h>

namespace xqt::test {

/// A point of the window (logical pixels) as the platform gives it to QWindowSystemInterface
inline QPointF nativeLocal(const QWindow* window, QPointF pos) { return QHighDpi::toNativeLocalPosition(pos, window); }
/// ... and its global position
inline QPointF nativeGlobal(const QWindow* window, QPointF pos) {
    return QHighDpi::toNativeGlobalPosition(window->mapToGlobal(pos), window);
}

/// The device pixel of a grabbed window under a point of the window (logical pixels)
inline QPoint devicePixel(const QWindow* window, QPointF pos) { return (pos * window->devicePixelRatio()).toPoint(); }
/// The colour of a grabbed window at a point of the window (logical pixels)
inline QRgb pixelAt(const QImage& shot, const QWindow* window, QPointF pos) {
    return shot.pixel(devicePixel(window, pos));
}

}  // namespace xqt::test

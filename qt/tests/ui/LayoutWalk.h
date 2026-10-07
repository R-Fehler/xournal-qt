/*
 * xournal-qt: what the UI tests of window sizes share (AdaptiveAuditTest, AdaptiveLayoutTest): the sizes of the audit
 * (qt/docs/history/README.md), and a walk over the visible controls of a window that finds those outside it, those
 * hidden in a scrolled area and those smaller than a finger.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QRectF>
#include <QString>
#include <QStringList>

class QQuickItem;
class QQuickWindow;

namespace xqt::uitest {

struct WindowSize {
    int w;
    int h;
    const char* label;
};
/// The audit's sizes (logical pixels; tablets and 2-in-1s: the screen minus a 48 px task bar)
extern const WindowSize auditSizes[18];

/// An item's name for a report: its objectName, else its icon, text or tip, else its type
QString labelOf(QQuickItem* item);

struct LayoutFindings {
    int buttons = 0;
    /// Enabled controls (buttons, fields, combo boxes) whose visible part lies outside the window:
    /// "name@right,bottom"
    QStringList outside;
    /// ... cut by a scrolling (clipping) area inside the window: hidden until scrolled
    QStringList hidden;
    /// Buttons smaller than `minTarget` either way: "name(WxH)"
    QStringList small;
    /// `outside` by name only (without the place), for comparing with lists of known ones
    QStringList outsideNames() const;
};

/// Walks the visible items under the window's content item (and its header / footer: they are its children too).
LayoutFindings walkLayout(QQuickWindow* window, int minTarget = 40);

}  // namespace xqt::uitest

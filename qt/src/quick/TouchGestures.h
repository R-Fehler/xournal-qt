/*
 * xournal-qt: gestures with four or five fingers on the touch screen, for the whole window.
 *
 * The window's touch events are watched (they are only looked at, never taken away), so the gestures work over the
 * pages, the page grid and the document overview alike: a tap with four or five fingers, and pinching four or five
 * fingers together ("zoom out") or apart. What they do is decided in QML.
 *
 * Note: a touch pad reports its gestures to the compositor, not to the application (KWin uses three and four finger
 * swipes itself), so these gestures are for the touch screen.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>

#include <QObject>
#include <QPointF>
#include <QPointer>

#include <QQuickWindow>

namespace xqt {

class TouchGestures: public QObject {  // (not final: QML derives from it)
    Q_OBJECT
    Q_PROPERTY(QQuickWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
public:
    explicit TouchGestures(QObject* parent = nullptr);
    ~TouchGestures() override;

    QQuickWindow* window() const { return watched.data(); }
    void setWindow(QQuickWindow* w);
    bool enabled() const { return on; }
    void setEnabled(bool e);

    bool eventFilter(QObject* watched, QEvent* event) override;

Q_SIGNALS:
    void windowChanged();
    void enabledChanged();
    /// A short tap with `fingers` (4 or 5) fingers.
    void tapped(int fingers);
    /// The fingers (4 or 5) were moved together ("zoom out") or apart; once per touch.
    void pinchedIn(int fingers);
    void pinchedOut(int fingers);

private:
    void reset();
    /// Mean distance of the points from their centre.
    double spreadOf() const;

    QPointer<QQuickWindow> watched;
    bool on = true;
    std::map<int, QPointF> points;
    int maxPoints = 0;
    double startMs = 0;
    double startSpread = 0;  ///< when the last finger came down
    QPointF startCentroid;
    double travel = 0;
    bool fired = false;  ///< a pinch was reported for this touch
};

}  // namespace xqt

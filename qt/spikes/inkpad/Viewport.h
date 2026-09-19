/*
 * xournal-qt M0 spike: view transform with anchor-based pinch zoom and iOS-like momentum.
 *
 * view = page * scale + offset   (logical pixels of the canvas)
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QTimer>

class Viewport: public QObject {
    Q_OBJECT
public:
    explicit Viewport(QObject* parent = nullptr);

    double scale() const { return s; }
    QPointF offset() const { return off; }
    QPointF toView(QPointF page) const { return page * s + off; }
    QPointF toPage(QPointF view) const { return (view - off) / s; }

    void setViewSize(QSizeF size);
    QSizeF viewSize() const { return view; }

    void fitWidth();
    void panBy(QPointF deltaView);
    void zoomAt(QPointF viewAnchor, double factor);

    /// Pinch: the page point under the initial centroid stays under the current centroid.
    void pinchBegin(QPointF centroid, double distance);
    void pinchUpdate(QPointF centroid, double distance);
    void pinchEnd();

    /// Momentum after a fling. Velocity in logical px per ms.
    void fling(QPointF velocity);
    void stopMomentum();
    bool hasMomentum() const { return momentumTimer.isActive(); }

Q_SIGNALS:
    void changed();
    /// Emitted ~300 ms after the last scale change (like Xournal++'s blockRerenderZoom).
    void zoomSettled(double scale);

private:
    void setScaleAround(QPointF viewAnchorPage, QPointF viewAnchor, double newScale);
    void clampOffset();
    void stepMomentum();

    double s = 1.0;
    QPointF off;
    QSizeF view{800, 600};

    QPointF pinchPageAnchor;
    double pinchStartDistance = 1.0;
    double pinchStartScale = 1.0;

    QTimer momentumTimer;
    QElapsedTimer momentumClock;
    qint64 lastMomentumMs = 0;
    QPointF velocity;

    QTimer settleTimer;
};

/*
 * xournal-qt: how high the pen is above the screen, for pens that tell it.
 *
 * Some pens report their distance while they hover (Wayland's tablet protocol passes it on as the z of the tablet
 * events: 0 touching, 65535 as far as the pen can tell). With it, the pen can count as "near" only up to a height of
 * one's own choice: a pen that is noticed 4 cm above the screen need not block touch all that way up. One object for
 * the whole application: it watches the tablet events of every window, so the settings can show the height as well.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>

class QTabletEvent;

namespace xqt {

class PenHover final: public QObject {
    Q_OBJECT
    /// The pen has told its height at least once (it may never, then heights are unknown).
    Q_PROPERTY(bool reportsHeight READ reportsHeight NOTIFY changed)
    /// How high it is now, 0 (on the screen) to 1 (as far as it can tell); only meaningful while it is near.
    Q_PROPERTY(double height READ height NOTIFY changed)
    Q_PROPERTY(bool inProximity READ inProximity NOTIFY changed)
public:
    static PenHover& instance();

    bool reportsHeight() const { return heightReported; }
    double height() const { return currentHeight; }
    bool inProximity() const { return proximity; }

    /// A tablet event (also those the canvas gets directly).
    void record(const QTabletEvent& e);
    void setProximity(bool near);
    /// Forget everything (a new start, e.g. between tests).
    void reset();

    /// The largest z a pen reports (Wayland's tablet protocol).
    static constexpr double MAX_Z = 65535;

Q_SIGNALS:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    PenHover();
    bool heightReported = false;
    double currentHeight = 0;
    bool proximity = false;
};

}  // namespace xqt

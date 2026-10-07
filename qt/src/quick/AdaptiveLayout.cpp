#include "AdaptiveLayout.h"

#include <array>
#include <cmath>
#include <vector>

#include <QGuiApplication>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>

namespace xqt {

namespace adaptive {

SizeClass classOf(double w, double h) {
    if (w < 360 || h < 360) {
        return SizeClass::Tiny;
    }
    if (w < 600) {
        return SizeClass::PhonePortrait;
    }
    if (h < 560) {
        return SizeClass::PhoneShort;
    }
    if (w <= 1280 && h >= 900 && h > w) {
        return SizeClass::TabletPortrait;
    }
    if (w >= 1280) {
        return SizeClass::DesktopWide;
    }
    // 840 <= w < 1280 in landscape, and what is left (800x600, a portrait window under 900 px high)
    return SizeClass::DesktopNarrow;
}

SizeClass classAfter(SizeClass current, double w, double h) {
    const SizeClass exact = classOf(w, h);
    if (exact == current) {
        return exact;
    }
    // The class stays while a size within the hysteresis would still have it (the thresholds are straight lines, so
    // the corners and the middles of the square around the size are enough)
    for (const double dw: {-HYSTERESIS_PX, 0.0, HYSTERESIS_PX}) {
        for (const double dh: {-HYSTERESIS_PX, 0.0, HYSTERESIS_PX}) {
            if (classOf(w + dw, h + dh) == current) {
                return current;
            }
        }
    }
    return exact;
}

QString nameOf(SizeClass c) {
    switch (c) {
        case SizeClass::DesktopWide: return QStringLiteral("desktopWide");
        case SizeClass::DesktopNarrow: return QStringLiteral("desktopNarrow");
        case SizeClass::TabletPortrait: return QStringLiteral("tabletPortrait");
        case SizeClass::PhonePortrait: return QStringLiteral("phonePortrait");
        case SizeClass::PhoneShort: return QStringLiteral("phoneShort");
        case SizeClass::Tiny: return QStringLiteral("tiny");
    }
    return {};
}

}  // namespace adaptive

namespace {
constexpr std::array<double, 3> WIDTH_STEPS{600, 840, 1280};

template <size_t N>
int stepOf(double v, const std::array<double, N>& limits) {
    int step = 0;
    for (const double limit: limits) {
        if (v >= limit) {
            ++step;
        }
    }
    return step;
}

/// The step of `v` among `limits`, keeping `current` while `v` is within the hysteresis of it
template <size_t N>
int stepAfter(int current, double v, const std::array<double, N>& limits, bool known) {
    const int exact = stepOf(v, limits);
    if (!known || exact == current) {
        return exact;
    }
    const double h = adaptive::HYSTERESIS_PX;
    return stepOf(v - h, limits) == current || stepOf(v + h, limits) == current ? current : exact;
}
}  // namespace

namespace {
std::vector<AdaptiveLayout*>& instances() {
    static std::vector<AdaptiveLayout*> all;
    return all;
}
}  // namespace

// The window's pointer events are watched through the application, before anyone else: the canvas takes the
// events of the pages for itself (its filter of the application returns them as taken), and a stroke is what must
// hold the class most of all. The filter installed last is asked first, so each canvas puts these filters in front
// again once it has installed its own (watchBeforeCanvases).
AdaptiveLayout::AdaptiveLayout(QObject* parent): QObject(parent) {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    fingerLast = fingerSeen = true;
#endif
    qApp->installEventFilter(this);
    instances().push_back(this);
}

AdaptiveLayout::~AdaptiveLayout() {
    std::erase(instances(), this);
    qApp->removeEventFilter(this);
}

void AdaptiveLayout::setMobilePlatform(bool on) {
    if (on != mobile) {
        mobile = on;
        Q_EMIT platformChanged();
    }
}

void AdaptiveLayout::watchBeforeCanvases() {
    for (AdaptiveLayout* a: instances()) {
        qApp->installEventFilter(a);  // (installed again: moved to the front)
    }
}

void AdaptiveLayout::setWindow(QWindow* w) {
    if (w == watched) {
        return;
    }
    disconnect(widthConnection);
    disconnect(heightConnection);
    watched = w;
    mouseDown = penDown = fingersDown = false;
    if (w) {
        widthConnection = connect(w, &QWindow::widthChanged, this, &AdaptiveLayout::update);
        heightConnection = connect(w, &QWindow::heightChanged, this, &AdaptiveLayout::update);
    }
    Q_EMIT windowChanged();
    update();
}

void AdaptiveLayout::setAdaptive(bool on) {
    if (on != adapting) {
        adapting = on;
        Q_EMIT changed();
    }
}

QString AdaptiveLayout::widthClass() const {
    static const std::array<QString, 4> names{QStringLiteral("compact"), QStringLiteral("medium"),
                                              QStringLiteral("expanded"), QStringLiteral("wide")};
    return names[static_cast<size_t>(widthStep)];
}

bool AdaptiveLayout::phone() const {
    return current == SizeClass::PhonePortrait || current == SizeClass::PhoneShort || current == SizeClass::Tiny;
}

bool AdaptiveLayout::roomForSidebar() const {
    if (!adapting) {
        return true;
    }
    return sidebarRoom && (current == SizeClass::DesktopWide || current == SizeClass::DesktopNarrow);
}

void AdaptiveLayout::setHold(bool on) {
    if (on == holding) {
        return;
    }
    const bool was = held();
    holding = on;
    pointerStateChanged(was);
}

void AdaptiveLayout::setTouchSetting(const QString& mode) {
    const QString m = mode == "on" || mode == "off" ? mode : QStringLiteral("auto");
    if (m != touchMode) {
        touchMode = m;
        Q_EMIT touchChanged();
    }
}

bool AdaptiveLayout::touchProfile() const {
    if (touchMode == "on") {
        return true;
    }
    if (touchMode == "off") {
        return false;
    }
    return fingerLast;
}

QString AdaptiveLayout::classify(double width, double height) {
    return adaptive::nameOf(adaptive::classOf(width, height));
}

void AdaptiveLayout::update() {
    if (!watched || held()) {
        return;  // (after the pointer is let go)
    }
    const double w = watched->width(), h = watched->height();
    if (w <= 0 || h <= 0 || (known && w == width && h == height)) {
        return;
    }
    // The hysteresis is for a window edge being dragged (many small steps). A jump - the device turned, the window
    // maximized or snapped to a side, the pointer let go after a while - takes the class of the new size as it is: a
    // Surface turned upright (1280 wide) is a tablet at once.
    const double hyst = adaptive::HYSTERESIS_PX;
    const bool smooth = known && std::abs(w - width) <= 2 * hyst && std::abs(h - height) <= 2 * hyst;
    const SizeClass c = smooth ? adaptive::classAfter(current, w, h) : adaptive::classOf(w, h);
    const int ws = stepAfter(widthStep, w, WIDTH_STEPS, smooth);
    const bool p = !smooth ? h > w : (portrait ? h + hyst > w : h > w + hyst);
    const bool room = !smooth ? w >= adaptive::SIDEBAR_ROOM_PX
                              : (sidebarRoom ? w + hyst >= adaptive::SIDEBAR_ROOM_PX
                                             : w - hyst >= adaptive::SIDEBAR_ROOM_PX);
    known = true;
    width = w;
    height = h;
    current = c;
    widthStep = ws;
    portrait = p;
    sidebarRoom = room;
    Q_EMIT changed();
}

void AdaptiveLayout::pointerStateChanged(bool wasHeld) {
    const bool now = held();
    if (now != wasHeld) {
        Q_EMIT heldChanged();
        if (!now) {
            // What the window became while it was held, and the pointer that was used: after the release has reached
            // the item it was for (a layout that changed before would move the item away under the pointer)
            QMetaObject::invokeMethod(this, &AdaptiveLayout::settle, Qt::QueuedConnection);
        }
    }
}

void AdaptiveLayout::settle() {
    if (held()) {
        return;
    }
    update();
    if (fingerSeen != fingerLast) {
        const bool before = touchProfile();
        fingerLast = fingerSeen;
        if (touchProfile() != before) {
            Q_EMIT touchChanged();
        }
    }
}

bool AdaptiveLayout::eventFilter(QObject* object, QEvent* event) {
    if (object != watched) {
        return false;
    }
    const bool was = held();
    switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease: {
            auto* e = static_cast<QMouseEvent*>(event);
            mouseDown = e->buttons() != Qt::NoButton;
            const auto type = e->device() ? e->device()->type() : QInputDevice::DeviceType::Mouse;
            if (event->type() == QEvent::MouseButtonPress &&
                (type == QInputDevice::DeviceType::Mouse || type == QInputDevice::DeviceType::TouchPad)) {
                fingerSeen = false;
            }
            break;
        }
        case QEvent::TabletPress: penDown = true; break;
        case QEvent::TabletRelease: penDown = false; break;
        case QEvent::TouchBegin:
            fingersDown = true;
            fingerSeen = true;
            break;
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: fingersDown = false; break;
        case QEvent::FocusOut:
            // (a press whose release goes elsewhere, e.g. to a window that opened meanwhile, must not keep the
            // class for ever)
            mouseDown = penDown = fingersDown = false;
            break;
        default: return false;
    }
    pointerStateChanged(was);
    return false;
}

}  // namespace xqt

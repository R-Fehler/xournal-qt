/*
 * xournal-qt: the size class of a window, and the touch profile (qt/docs/adaptive-layout.md).
 *
 * One place that knows how much room a window has, instead of a threshold in every QML file. The class follows the
 * window's size with a hysteresis (HYSTERESIS_PX: dragging a window edge does not make the layout flicker; a jump of
 * more than twice that, such as turning the device, takes the new class at once) and never changes while a pointer is
 * held in the window (a stroke, a drag): the change waits until it is let go.
 *
 *   desktopWide     w >= 1280, h >= 560                    1920x1080, 1366x768, 1280x800, 1440x912
 *   desktopNarrow   840 <= w < 1280, h >= 560, w >= h      1024x700; and whatever no other class takes (800x600)
 *   tabletPortrait  600 <= w <= 1280, h >= 900, h > w      960x1392, 720x1232, 1280x1872, 900x1000 (Fold 7 open)
 *   phonePortrait   w < 600                                412x915
 *   phoneShort      h < 560                                915x412, 1280x500
 *   tiny            w < 360 or h < 360                     split screen, a pop-up window
 * The first that matches, in the order tiny, phone portrait, phone short, tablet portrait, desktop wide, desktop
 * narrow. (Tablet portrait takes w = 1280 too: a Surface Pro in portrait at 150 % is 1280 wide.)
 *
 * The touch profile is separate from the size: fingers need bigger targets (minTarget: 48 px, else 40). Automatic,
 * it is on on Android and iOS, and elsewhere once a finger touched the screen, until the mouse (or touch pad) is used
 * again; the pen changes nothing. The setting "touchProfile" ("auto", "on", "off") overrides it.
 *
 * QML: `AdaptiveLayout { window: win; adaptive: ...; touchSetting: ... }`, one per window.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QWindow>

namespace xqt {

enum class SizeClass { DesktopWide, DesktopNarrow, TabletPortrait, PhonePortrait, PhoneShort, Tiny };

namespace adaptive {
constexpr double HYSTERESIS_PX = 32;
/// The page sidebar is shown beside the page when the window is at least this wide (the page keeps ~900 px)
constexpr double SIDEBAR_ROOM_PX = 1110;
constexpr int MIN_TARGET = 40;        ///< the smallest button for the mouse and the pen
constexpr int MIN_TARGET_TOUCH = 48;  ///< ... for a finger

/// The class of a window of this size (logical pixels), without hysteresis.
SizeClass classOf(double width, double height);
/// The class after a resize from `current`: `current` stays while the size is within HYSTERESIS_PX of it.
SizeClass classAfter(SizeClass current, double width, double height);
QString nameOf(SizeClass c);
}  // namespace adaptive

class AdaptiveLayout: public QObject {  // (not final: QML derives from it)
    Q_OBJECT
    Q_PROPERTY(QWindow* window READ window WRITE setWindow NOTIFY windowChanged)
    /// The setting "Adapt the layout to the window size": off, the window keeps the desktop layout (layoutClass is
    /// desktopWide, roomForSidebar true)
    Q_PROPERTY(bool adaptive READ adaptive WRITE setAdaptive NOTIFY changed)
    /// "desktopWide", "desktopNarrow", "tabletPortrait", "phonePortrait", "phoneShort" or "tiny"
    Q_PROPERTY(QString sizeClass READ sizeClass NOTIFY changed)
    /// The class the layout follows: sizeClass, or desktopWide when adapting is off. The choices made by hand are kept
    /// per layoutClass.
    Q_PROPERTY(QString layoutClass READ layoutClass NOTIFY changed)
    /// Width: "compact" (< 600), "medium" (< 840), "expanded" (< 1280), "wide"; "portrait" (h > w) or "landscape".
    /// Each with the same hysteresis.
    Q_PROPERTY(QString widthClass READ widthClass NOTIFY changed)
    Q_PROPERTY(QString orientation READ orientation NOTIFY changed)
    /// A phone class (phonePortrait, phoneShort, tiny): the compact layouts
    Q_PROPERTY(bool phone READ phone NOTIFY changed)
    /// The layout of a phone class: `phone`, unless "Adapt the layout" is off (the layout class is desktopWide then).
    /// What the QML lays out by (the app bar, the dock, menus as sheets); `phone` is the window's size alone.
    Q_PROPERTY(bool phoneLayout READ phoneLayout NOTIFY changed)
    /// Room for the page sidebar beside the page (window >= SIDEBAR_ROOM_PX, not portrait, not a phone)
    Q_PROPERTY(bool roomForSidebar READ roomForSidebar NOTIFY changed)
    /// The size the class was taken from (it may lag behind the window while a pointer is held)
    Q_PROPERTY(double classWidth READ classWidth NOTIFY changed)
    Q_PROPERTY(double classHeight READ classHeight NOTIFY changed)
    /// A pointer is held in the window (mouse button, pen, finger), or QML holds the class (`hold`)
    Q_PROPERTY(bool held READ held NOTIFY heldChanged)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY heldChanged)
    Q_PROPERTY(QString touchSetting READ touchSetting WRITE setTouchSetting NOTIFY touchChanged)
    Q_PROPERTY(bool touchProfile READ touchProfile NOTIFY touchChanged)
    /// The smallest target for the pointer in use: 48 with the touch profile, else 40
    Q_PROPERTY(int minTarget READ minTarget NOTIFY touchChanged)
    /// Android or iOS (the platform, not the size): one window, so no tab is dragged out into a window of its own and
    /// no entry offers a new window. Written by the tests only (to try the phone's platform on the desktop).
    Q_PROPERTY(bool mobilePlatform READ mobilePlatform WRITE setMobilePlatform NOTIFY platformChanged)
public:
    explicit AdaptiveLayout(QObject* parent = nullptr);
    ~AdaptiveLayout() override;

    QWindow* window() const { return watched.data(); }
    void setWindow(QWindow* w);
    bool adaptive() const { return adapting; }
    void setAdaptive(bool on);
    QString sizeClass() const { return adaptive::nameOf(current); }
    QString layoutClass() const { return adapting ? sizeClass() : adaptive::nameOf(SizeClass::DesktopWide); }
    QString widthClass() const;
    QString orientation() const { return portrait ? QStringLiteral("portrait") : QStringLiteral("landscape"); }
    bool phone() const;
    bool phoneLayout() const { return adapting && phone(); }
    bool roomForSidebar() const;
    double classWidth() const { return width; }
    double classHeight() const { return height; }
    bool held() const { return holding || pointerHeld(); }
    bool hold() const { return holding; }
    void setHold(bool on);
    QString touchSetting() const { return touchMode; }
    void setTouchSetting(const QString& mode);
    bool touchProfile() const;
    int minTarget() const { return touchProfile() ? adaptive::MIN_TARGET_TOUCH : adaptive::MIN_TARGET; }
    bool mobilePlatform() const { return mobile; }
    void setMobilePlatform(bool on);

    /// The class of a size, for QML and tests (no hysteresis)
    Q_INVOKABLE static QString classify(double width, double height);

    bool eventFilter(QObject* object, QEvent* event) override;
    /// Puts the filters of all AdaptiveLayouts in front of the application's other event filters again (a canvas
    /// installs its own, which takes the events of its pages: then these would no longer see a stroke)
    static void watchBeforeCanvases();

Q_SIGNALS:
    void windowChanged();
    void changed();
    void heldChanged();
    void touchChanged();
    void platformChanged();

private:
    bool pointerHeld() const { return mouseDown || penDown || fingersDown; }
    /// The window's size now, unless a pointer is held (then later)
    void update();
    void pointerStateChanged(bool wasHeld);
    /// Once nothing is held: the class of the window's size, the touch profile of the pointer used last
    void settle();

    QPointer<QWindow> watched;
    QMetaObject::Connection widthConnection, heightConnection;
    bool adapting = true;
    bool known = false;  ///< a size was taken (the first one is taken without hysteresis)
    SizeClass current = SizeClass::DesktopWide;
    int widthStep = 3;   ///< 0 compact, 1 medium, 2 expanded, 3 wide
    bool portrait = false;
    bool sidebarRoom = true;
    double width = 0;
    double height = 0;
    bool holding = false;
    bool mouseDown = false;
    bool penDown = false;
    bool fingersDown = false;
    QString touchMode = QStringLiteral("auto");
    bool fingerLast = false;  ///< the last pointer that touched was a finger (not the mouse; the pen changes nothing)
    bool fingerSeen = false;  ///< ... as seen while it is still held (fingerLast follows once it is let go)
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    bool mobile = true;
#else
    bool mobile = false;
#endif
};

}  // namespace xqt

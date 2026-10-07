/*
 * xournal-qt: the presenter view on a second screen (qt/docs/features/presenter-view.md), as in Okular and PowerPoint.
 *
 * While presenting with two screens, the audience's screen shows only the slide (full screen, without its space for
 * notes, qt/docs/features/note-space.md) and the window being presented from becomes the presenter's console on the
 * other screen: the current page large with its space for notes (the tab's own view, written on with the toolbox as
 * when presenting on one screen), the next page smaller, a clock, the time since the start (paused, resumed, reset),
 * the page number. With one screen, presenting is as before.
 *
 * The audience's screen is a second CanvasView of the presented document (as the same document beside itself,
 * qt/docs/features/reference-view.md: one session, one undo history, the rendered pages of both under CanvasMemory's
 * one limit). It takes no input; it follows the presenter's page, and shows what the presenter's view shows only for a
 * moment (CanvasView::setMirror): a stroke while it is written, the laser pointer's ink, the curtain and the
 * spotlight. The keys that go from page to page work in its window too (AudienceWindow.qml).
 *
 * Which screen is the audience's: the one that is not the primary screen (the laptop's, where the console goes), or
 * the primary one when "swap screens" is on (a setting, also a button of the console). With more than two screens,
 * the first one that is not the console's.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTimer>

class QScreen;
class QWindow;

namespace xqt {

class AppContext;
class CanvasView;

class PresenterConsole final: public QObject {
    Q_OBJECT
    /// The presenter view is on (setting "presenterView", on by default) and there are two screens: presenting opens
    /// the audience's window and the console
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// The setting "presenterView"
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY availableChanged)
    /// Presenting with the presenter view now: the audience's view exists (its window is shown)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    /// The CanvasView of the audience's screen (nullptr while not active)
    Q_PROPERTY(QObject* audienceView READ audienceViewObject NOTIFY activeChanged)
    /// The setting "presenterSwapScreens": the audience's screen is the primary one
    Q_PROPERTY(bool swapScreens READ swapScreens WRITE setSwapScreens NOTIFY screensChanged)
    /// The screens' names, for the console ("" when there is no second screen)
    Q_PROPERTY(QString audienceScreenName READ audienceScreenName NOTIFY screensChanged)
    /// The page shown (0-based) and how many there are
    Q_PROPERTY(int page READ page NOTIFY pageChanged)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY pageChanged)
    /// The slide of the current page (the page without its space for notes), in points: the audience's window shows
    /// the slide at this shape
    Q_PROPERTY(QSizeF slideSize READ slideSize NOTIFY pageChanged)
    /// The current page has space for notes (shown on the console only, unless showNotes)
    Q_PROPERTY(bool pageHasNotes READ pageHasNotes NOTIFY pageChanged)
    /// The setting "presenterShowNotes" (off by default): the audience sees the whole page with its space for notes
    /// (and the ink written there), not only the slide
    Q_PROPERTY(bool showNotes READ showNotes WRITE setShowNotes NOTIFY optionsChanged)
    /// What the audience's window shows of the current page, in points (the slide, or the whole page with showNotes):
    /// its canvas has this shape, as large as the screen allows
    Q_PROPERTY(QSizeF shownSize READ shownSize NOTIFY shownChanged)
    /// The setting "presenterFollowView" (on by default): zoomed in on the console, the audience sees the same part of
    /// the page (fitted to its screen's shape, never less than the presenter sees); off, it always sees the whole slide
    Q_PROPERTY(bool followView READ followView WRITE setFollowView NOTIFY optionsChanged)
    /// The audience's window's size (AudienceWindow tells it; the audience's screen's until then): the shape the
    /// part the audience sees is widened to
    Q_PROPERTY(QSizeF audienceSize READ audienceSize WRITE setAudienceSize NOTIFY audienceSizeChanged)
    /// Following a presenter zoomed in: what the audience sees, in the presenter's canvas (its view coordinates) for
    /// the frame drawn there; empty when the audience sees the whole slide (or page)
    Q_PROPERTY(QRectF audienceFrame READ audienceFrame NOTIFY audienceFrameChanged)
    /// The picture of the next page (a thumbnail's URL; "" after the last page) and its shape (height / width)
    Q_PROPERTY(QString nextPicture READ nextPicture NOTIFY pageChanged)
    Q_PROPERTY(qreal nextAspect READ nextAspect NOTIFY pageChanged)
    /// The time since the presentation started: running or paused, in whole seconds (told every second)
    Q_PROPERTY(bool timerRunning READ timerRunning NOTIFY timerChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY timerChanged)

public:
    explicit PresenterConsole(AppContext& app, QObject* parent = nullptr);
    ~PresenterConsole() override;

    bool available() const;
    bool enabled() const;
    void setEnabled(bool on);
    bool active() const { return audience != nullptr; }
    CanvasView* audienceView() const { return audience.get(); }
    QObject* audienceViewObject() const;
    bool swapScreens() const;
    void setSwapScreens(bool on);
    /// The audience's screen and the console's (nullptr: not two screens)
    QScreen* audienceScreen() const;
    QScreen* consoleScreen() const;
    QString audienceScreenName() const;

    int page() const { return shownPage; }
    int pageCount() const;
    QSizeF slideSize() const;
    bool pageHasNotes() const;
    bool showNotes() const;
    void setShowNotes(bool on);
    /// The part of the current page the audience's view shows (page points)
    QRectF shownRect() const { return shown; }
    QSizeF shownSize() const { return shown.isEmpty() ? slideSize() : shown.size(); }
    bool followView() const;
    void setFollowView(bool on);
    QSizeF audienceSize() const;
    void setAudienceSize(QSizeF size);
    QRectF audienceFrame() const { return frameOnConsole; }
    /// Both screens back to the whole slide (or page): the presenter's view fitted as when presenting starts
    Q_INVOKABLE void fitPage();
    QString nextPicture() const;
    qreal nextAspect() const;

    bool timerRunning() const { return running; }
    int elapsedSeconds() const { return static_cast<int>(elapsedMs() / 1000); }
    Q_INVOKABLE qint64 elapsedMs() const;
    /// The time: on again, paused, back to 0 (it goes on running if it ran)
    Q_INVOKABLE void startTimer();
    Q_INVOKABLE void pauseTimer();
    Q_INVOKABLE void toggleTimer() { running ? pauseTimer() : startTimer(); }
    Q_INVOKABLE void resetTimer();

    /// The view presenting now (the current tab's while presenting; nullptr: not presenting). With two screens the
    /// audience's view is made for it (and goes with it).
    void setPresented(CanvasView* view);

    /// The windows on their screens: the console's window onto the console's screen when it is on the audience's,
    /// the audience's window full screen on the audience's screen. Called when the audience's window is to show, and
    /// again when the screens change.
    Q_INVOKABLE void placeWindows(QWindow* audienceWindow, QWindow* consoleWindow);
    /// Only the console's window (before it goes full screen for presenting): off the audience's screen
    Q_INVOKABLE void placeConsole(QWindow* consoleWindow);

Q_SIGNALS:
    void availableChanged();
    void activeChanged();
    void screensChanged();
    void pageChanged();
    void timerChanged();
    void optionsChanged();
    void shownChanged();
    void audienceSizeChanged();
    void audienceFrameChanged();

private:
    void update();
    void tearDown();
    /// The audience's view to the presenter's page, the slide (or the whole page with showNotes) filling it, or the
    /// part the presenter sees when zoomed in (followView). `force`: fitted again even when the part is the same (the
    /// page or the pages changed).
    void place(bool force);
    void follow() { place(true); }
    /// The presenter's view zoomed in further than the page filling it (presenting's fit)
    bool presenterZoomedIn(size_t page) const;
    /// What the audience sees of a page when it shows all of it: the slide, or the whole page with showNotes
    QRectF frameOf(size_t page) const;

    AppContext& app;
    QPointer<CanvasView> presented;
    std::unique_ptr<CanvasView> audience;
    std::vector<QMetaObject::Connection> connections;
    int shownPage = 0;
    QRectF shown;  ///< the part of the page the audience's view shows (points)
    bool notesShown = false;  ///< showNotes as the audience's view was last fitted
    bool followed = true;     ///< followView as last seen
    bool placing = false;     ///< in place() (fitting the presenter's view there calls it again)
    QSizeF windowSize;        ///< the audience's window's (audienceSize)
    QRectF frameOnConsole;    ///< audienceFrame
    bool wasAvailable = false;

    bool running = false;
    qint64 banked = 0;  ///< ms before the last start
    QElapsedTimer clock;
    QTimer tick;
};

}  // namespace xqt

/*
 * xournal-qt: the presenter view on a second screen (see PresenterConsole.h, qt/docs/features/presenter-view.md).
 *
 * @license GNU GPLv2 or later
 */
#include "PresenterConsole.h"

#include <QGuiApplication>
#include <QScopedValueRollback>
#include <QScreen>
#include <QWindow>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "AudienceRegion.h"
#include "CanvasView.h"
#include "PageSketches.h"
#include "SessionRegistry.h"
#include "ViewController.h"

namespace xqt {

namespace {
constexpr const char* ENABLED_KEY = "presenterView";
constexpr const char* SWAP_KEY = "presenterSwapScreens";
constexpr const char* NOTES_KEY = "presenterShowNotes";
constexpr const char* FOLLOW_KEY = "presenterFollowView";

bool setting(Settings& s, const char* key, bool fallback) {
    bool on = fallback;
    s.getCustomElement("xournalQt").getBool(key, on);
    return on;
}

/// The slide of a page: the page without its space for notes (qt/docs/features/note-space.md), in page points
QRectF slideOf(const XojPage& page) {
    const NoteSpace& n = page.getNoteSpace();
    const QRectF whole(0, 0, page.getWidth(), page.getHeight());
    const QRectF slide(n.left, n.top, page.getWidth() - n.left - n.right, page.getHeight() - n.top - n.bottom);
    return slide.isEmpty() ? whole : slide;
}
}  // namespace

PresenterConsole::PresenterConsole(AppContext& app, QObject* parent): QObject(parent), app(app) {
    tick.setInterval(1000);
    connect(&tick, &QTimer::timeout, this, &PresenterConsole::timerChanged);
    auto screens = [this] {
        Q_EMIT screensChanged();
        update();
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, screens);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, screens);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, screens);
    connect(&app, &AppContext::settingsChanged, this, [this] {
        Q_EMIT screensChanged();  // (swap screens)
        update();
        if (showNotes() != notesShown || followView() != followed) {
            notesShown = showNotes();
            followed = followView();
            Q_EMIT optionsChanged();
            follow();  // (at once: the audience sees the space for notes now, or not any more; follows, or not)
        }
    });
    notesShown = showNotes();
    followed = followView();
    wasAvailable = available();
}

PresenterConsole::~PresenterConsole() { tearDown(); }

bool PresenterConsole::enabled() const { return setting(*app.getSettings(), ENABLED_KEY, true); }

void PresenterConsole::setEnabled(bool on) {
    if (on != enabled()) {
        app.getSettings()->getCustomElement("xournalQt").setBool(ENABLED_KEY, on);
        app.getSettings()->customSettingsChanged();
        Q_EMIT app.settingsChanged();
    }
}

bool PresenterConsole::swapScreens() const { return setting(*app.getSettings(), SWAP_KEY, false); }

void PresenterConsole::setSwapScreens(bool on) {
    if (on != swapScreens()) {
        app.getSettings()->getCustomElement("xournalQt").setBool(SWAP_KEY, on);
        app.getSettings()->customSettingsChanged();
        Q_EMIT app.settingsChanged();
    }
}

bool PresenterConsole::showNotes() const { return setting(*app.getSettings(), NOTES_KEY, false); }

void PresenterConsole::setShowNotes(bool on) {
    if (on != showNotes()) {
        app.getSettings()->getCustomElement("xournalQt").setBool(NOTES_KEY, on);
        app.getSettings()->customSettingsChanged();
        Q_EMIT app.settingsChanged();
    }
}

bool PresenterConsole::followView() const { return setting(*app.getSettings(), FOLLOW_KEY, true); }

void PresenterConsole::setFollowView(bool on) {
    if (on != followView()) {
        app.getSettings()->getCustomElement("xournalQt").setBool(FOLLOW_KEY, on);
        app.getSettings()->customSettingsChanged();
        Q_EMIT app.settingsChanged();
    }
}

QSizeF PresenterConsole::audienceSize() const {
    if (!windowSize.isEmpty()) {
        return windowSize;
    }
    if (QScreen* s = audienceScreen()) {
        return s->geometry().size();
    }
    return QSizeF(16, 9);
}

void PresenterConsole::setAudienceSize(QSizeF size) {
    if (size != windowSize) {
        windowSize = size;
        Q_EMIT audienceSizeChanged();
        place(false);
    }
}

void PresenterConsole::fitPage() {
    if (presented && presented->pageCount() > 0) {
        presented->resetRotation();
        presented->getViewController().fitPresentedPage(std::min(presented->currentPageNo(), presented->pageCount() - 1));
    }
}

bool PresenterConsole::available() const { return enabled() && QGuiApplication::screens().size() >= 2; }

QScreen* PresenterConsole::audienceScreen() const {
    const auto screens = QGuiApplication::screens();
    QScreen* primary = QGuiApplication::primaryScreen();
    if (screens.size() < 2 || !primary) {
        return nullptr;
    }
    if (swapScreens()) {
        return primary;
    }
    for (QScreen* s: screens) {
        if (s != primary) {
            return s;
        }
    }
    return nullptr;
}

QScreen* PresenterConsole::consoleScreen() const {
    QScreen* audienceOn = audienceScreen();
    if (!audienceOn) {
        return nullptr;
    }
    if (!swapScreens()) {
        return QGuiApplication::primaryScreen();
    }
    for (QScreen* s: QGuiApplication::screens()) {
        if (s != audienceOn) {
            return s;
        }
    }
    return nullptr;
}

QString PresenterConsole::audienceScreenName() const {
    QScreen* s = audienceScreen();
    return s ? s->name() : QString();
}

QObject* PresenterConsole::audienceViewObject() const { return audience.get(); }

// --- the audience's view -------------------------------------------------------------------------------------------

void PresenterConsole::setPresented(CanvasView* view) {
    if (view == presented) {
        return;
    }
    tearDown();
    presented = view;
    update();
}

void PresenterConsole::update() {
    const bool can = available();
    if (can != wasAvailable) {
        wasAvailable = can;
        Q_EMIT availableChanged();
    }
    const bool wanted = can && presented;
    if (wanted == active()) {
        return;
    }
    if (!wanted) {
        tearDown();
        return;
    }
    DocumentSession& session = presented->getSession();
    // A second view of the presented document (as the same document beside itself): its own page and zoom, the
    // session's pages, stand-ins and memory limit
    audience = std::make_unique<CanvasView>(session);
    const quint64 id = SessionRegistry::idOf(&session);
    audience->setStandInSource([id, s = &session](size_t page) {
        return PageSketches::instance().standIn(id, s->pageId(page));
    });
    audience->setReadingOnly(true);
    presented->setMirror(audience.get());
    CanvasView* a = audience.get();
    connections.push_back(connect(presented.data(), &CanvasView::currentPageChanged, this, &PresenterConsole::follow));
    // (the presenter zooms or scrolls: the audience follows, without rendering anything for it but what it shows)
    connections.push_back(connect(&presented->getViewController(), &ViewController::changed, this, [this] {
        place(false);
    }));
    // (pages inserted, deleted, resized, their space for notes changed: the slide's place on its page)
    connections.push_back(connect(a, &CanvasView::pagesChanged, this, &PresenterConsole::follow));
    connections.push_back(connect(&session, &DocumentSession::pageRevisionsChanged, this, [this] { place(false); }));
    connections.push_back(connect(&session, &DocumentSession::pageRevisionsChanged, this, &PresenterConsole::pageChanged));
    connections.push_back(connect(&PageSketches::instance(), &PageSketches::changed, this, [a, id](qulonglong of) {
        if (of == id) {
            a->standInsChanged();
        }
    }));
    follow();
    // The time starts with the presentation
    banked = 0;
    running = false;
    startTimer();
    tick.start();
    Q_EMIT activeChanged();
    Q_EMIT pageChanged();
}

void PresenterConsole::tearDown() {
    for (const auto& c: std::exchange(connections, {})) {
        disconnect(c);
    }
    if (presented) {
        presented->setMirror(nullptr);
    }
    if (!frameOnConsole.isEmpty()) {
        frameOnConsole = {};
        Q_EMIT audienceFrameChanged();
    }
    if (!audience) {
        return;
    }
    tick.stop();
    std::unique_ptr<CanvasView> goes = std::move(audience);
    Q_EMIT activeChanged();  // (the audience's window lets go of it first)
    goes.reset();
}

void PresenterConsole::place(bool force) {
    if (!audience || !presented || placing) {
        return;
    }
    const size_t count = presented->pageCount();
    if (count == 0) {
        return;
    }
    QScopedValueRollback guard(placing, true);
    const size_t page = std::min(presented->currentPageNo(), count - 1);
    const bool moved = static_cast<int>(page) != shownPage;
    ViewController& presenterView = presented->getViewController();
    const bool follows = followView();
    if (moved && follows && presenterZoomedIn(page)) {
        // Another page: both screens show it whole (the audience is never left on a part of the page before)
        presenterView.fitPresentedPage(page);
    }
    const QRectF frame = frameOf(page);
    QRectF region = frame;
    if (follows && presenterZoomedIn(page)) {
        // What the presenter sees of the page (the bounding box of it, were the canvas turned), widened to the
        // audience's screen's shape, within the slide (or the page)
        const auto seen = presented->viewOnPage(page);
        region = presenter::audienceRegion(frame, QRectF(seen.x, seen.y, seen.width, seen.height), audienceSize());
    }
    if (force || moved || region != shown) {
        audience->setCurrentPageNo(page);
        audience->getViewController().fitPageRect(page, region);
    }
    shownPage = static_cast<int>(page);
    if (region != shown) {
        shown = region;
        Q_EMIT shownChanged();
    }
    // The frame on the console: what the audience sees, while it is a part of the page
    QRectF onConsole;
    if (region != frame && !region.isEmpty()) {
        const double z = presenterView.zoom();
        const QRectF p = presented->pageViewRect(page);
        onConsole = QRectF(p.topLeft() + region.topLeft() * z, region.size() * z);
    }
    if (onConsole != frameOnConsole) {
        frameOnConsole = onConsole;
        Q_EMIT audienceFrameChanged();
    }
    if (moved) {
        Q_EMIT pageChanged();
    }
}

bool PresenterConsole::presenterZoomedIn(size_t page) const {
    if (!presented->isPresenting()) {
        return false;  // (not yet, or not any more: its zoom is the editor's)
    }
    const ViewController& v = presented->getViewController();
    return v.keptFit() != ViewController::Fit::Page && v.zoom() > v.presentedZoom(page) * 1.01;
}

QRectF PresenterConsole::frameOf(size_t page) const {
    if (!presented) {
        return {};
    }
    if (const PageRef p = presented->getSession().getDocument()->getPage(page)) {
        return showNotes() ? QRectF(0, 0, p->getWidth(), p->getHeight()) : slideOf(*p);
    }
    return {};
}

int PresenterConsole::pageCount() const { return presented ? static_cast<int>(presented->pageCount()) : 0; }

QSizeF PresenterConsole::slideSize() const {
    if (presented && shownPage < pageCount()) {
        if (const PageRef p = presented->getSession().getDocument()->getPage(static_cast<size_t>(shownPage))) {
            return slideOf(*p).size();
        }
    }
    return QSizeF(16, 9);
}

bool PresenterConsole::pageHasNotes() const {
    if (presented && shownPage < pageCount()) {
        if (const PageRef p = presented->getSession().getDocument()->getPage(static_cast<size_t>(shownPage))) {
            return !p->getNoteSpace().empty();
        }
    }
    return false;
}

QString PresenterConsole::nextPicture() const {
    const int next = shownPage + 1;
    if (!presented || next >= pageCount()) {
        return {};
    }
    // (the thumbnail of the page, named by its revision: drawn again when the page changes)
    const DocumentSession& s = presented->getSession();
    return QStringLiteral("image://thumbnail/%1/%2/%3")
            .arg(SessionRegistry::idOf(&s))
            .arg(next)
            .arg(s.pageRevision(static_cast<size_t>(next)));
}

qreal PresenterConsole::nextAspect() const {
    const int next = shownPage + 1;
    if (presented && next < pageCount()) {
        if (const PageRef p = presented->getSession().getDocument()->getPage(static_cast<size_t>(next));
            p && p->getWidth() > 0) {
            return p->getHeight() / p->getWidth();
        }
    }
    return 0.5625;
}

// --- the time ------------------------------------------------------------------------------------------------------

qint64 PresenterConsole::elapsedMs() const { return banked + (running ? clock.elapsed() : 0); }

void PresenterConsole::startTimer() {
    if (running) {
        return;
    }
    running = true;
    clock.start();
    Q_EMIT timerChanged();
}

void PresenterConsole::pauseTimer() {
    if (!running) {
        return;
    }
    banked += clock.elapsed();
    running = false;
    Q_EMIT timerChanged();
}

void PresenterConsole::resetTimer() {
    banked = 0;
    if (running) {
        clock.start();
    }
    Q_EMIT timerChanged();
}

// --- the windows ---------------------------------------------------------------------------------------------------

void PresenterConsole::placeConsole(QWindow* consoleWindow) {
    QScreen* console = consoleScreen();
    if (consoleWindow && console && consoleWindow->screen() == audienceScreen()) {
        // (the window was on the projector: the console goes to the other screen, the slide comes there)
        consoleWindow->setScreen(console);
        if (consoleWindow->visibility() == QWindow::FullScreen) {
            consoleWindow->setGeometry(console->geometry());
        } else {
            const QRect g = console->availableGeometry();
            const QSize size = consoleWindow->size().boundedTo(g.size());
            consoleWindow->setGeometry(QRect(g.topLeft() + QPoint((g.width() - size.width()) / 2,
                                                                  (g.height() - size.height()) / 2),
                                             size));
        }
    }
}

void PresenterConsole::placeWindows(QWindow* audienceWindow, QWindow* consoleWindow) {
    placeConsole(consoleWindow);
    QScreen* audienceOn = audienceScreen();
    if (!audienceWindow || !audienceOn) {
        return;
    }
    if (audienceWindow->screen() != audienceOn) {
        audienceWindow->setScreen(audienceOn);
    }
    audienceWindow->setGeometry(audienceOn->geometry());
    audienceWindow->showFullScreen();
    if (consoleWindow) {
        consoleWindow->requestActivate();  // (the keys stay with the console)
    }
}

}  // namespace xqt

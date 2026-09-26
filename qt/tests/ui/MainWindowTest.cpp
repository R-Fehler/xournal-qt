/*
 * xournal-qt: the real main window (Main.qml) off-screen: keyboard shortcuts, settings sheet, tab overview.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <filesystem>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>

#include <QGuiApplication>
#include <QStyleHints>
#include <QCoreApplication>
#include <cmath>

#include <QDir>
#include <fstream>

#include <QElapsedTimer>
#include <iostream>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <shared_mutex>

#include <QClipboard>
#include <QMimeData>
#include <QPointer>
#include <QSignalSpy>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTabletEvent>
#include <QWheelEvent>
#include <gtest/gtest.h>
#include <cairo-pdf.h>
#include <poppler.h>
#include <qpdf/DLL.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "model/Text.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "model/NoteSpace.h"
#include "canvas/CanvasView.h"
#include "canvas/CanvasMemory.h"
#include "canvas/CanvasPage.h"
#include "canvas/StickyNotes.h"
#include "markdown/MdBox.h"
#include "markdown/MdImages.h"
#include "session/TextFile.h"
#include "canvas/PenHover.h"
#include "canvas/ScreenCalibration.h"
#include "canvas/MarkdownEditor.h"
#include "canvas/TextEditor.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/StickyNote.h"
#include "session/FuzzyQuery.h"
#include "session/IncrementalPdf.h"
#include "session/DocumentMode.h"
#include "session/HybridPdf.h"
#include "session/TextDocument.h"
#include "session/PdfPageKeeper.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/PagesModel.h"
#include "shell/RecentFiles.h"
#include "shell/Previews.h"
#include "shell/SettingsModel.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "shell/PageSketches.h"
#include "shell/Thumbnails.h"
#include "util/PathUtil.h"

#include "AppController.h"
#include "TextFlow.h"
#include "MarkdownFile.h"
#include "../SearchHits.h"
#include "config-test.h"

namespace {
class MainWindowTest: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        prepareController();
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        window->requestActivate();
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));  // (the pointer rests outside: nothing hovered, no tool tips)
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();  // first the image workers, then the engine that owns their providers
        engine.reset();
        controller.reset();
    }

    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    template <typename T = QObject>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
    }
    /// Waits until the popup is fully open (or closed).
    static bool waitOpened(QObject* popup, bool opened, int timeoutMs = 5000) {
        auto done = [&] {
            return popup->property("opened").toBool() == opened && popup->property("visible").toBool() == opened;
        };
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return done();
    }
    void key(Qt::Key k, Qt::KeyboardModifiers m = Qt::NoModifier) {
        QTest::keyClick(window, k, m);
        wait(20);
    }
    /// Also items without a QObject parent (made by a Repeater): through the item tree.
    QQuickItem* findItem(const char* name) const {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == name) {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        return walk(window->contentItem());
    }
    /// Waits until something is true (animations, delegates of a list, a popup fading out).
    /// (returns as soon as it is true: the time is for a machine slowed down by other work)
    void until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    void click(QQuickItem* item, Qt::KeyboardModifiers m = Qt::NoModifier) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, m,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    /// Scrolls the flickable (a ScrollView's) that holds `item` so that the item is shown (to be clicked).
    void scrollIntoView(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QQuickItem* flick = item->parentItem();
        while (flick && !flick->inherits("QQuickFlickable")) {
            flick = flick->parentItem();
        }
        if (!flick) {
            return;
        }
        auto* content = flick->property("contentItem").value<QQuickItem*>();
        if (!content) {
            return;
        }
        const qreal y = item->mapToItem(content, QPointF(0, 0)).y();
        const qreal maxY = std::max(0.0, flick->property("contentHeight").toReal() - flick->height());
        flick->setProperty("contentY", std::clamp(y - flick->height() / 3, 0.0, maxY));
        nextFrame();
    }
    /// Until the window drew its next frame: layouts changed meanwhile are done, the items are where they are shown
    void nextFrame() {
        QSignalSpy drawn(window, &QQuickWindow::frameSwapped);
        window->update();
        drawn.wait(5000);
    }
    /// Types text into the focused item (QTest::keyClicks is for widgets only).
    void type(const char* text) {
        for (const char* c = text; *c; ++c) {
            QTest::keyClick(window, *c);
        }
        wait(20);
    }

    /// Before the window is loaded. Most tests are about a document (the app starts on the home screen).
    virtual void prepareController() { controller->newDocument(); }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(MainWindowTest, ctrlTabSwitchesTabs) {
    controller->newDocument();
    controller->newDocument();
    ASSERT_EQ(controller->tabCount(), 3);
    ASSERT_EQ(controller->currentTab(), 2);
    key(Qt::Key_Tab, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 0) << "Ctrl+Tab wraps around to the first tab";
    key(Qt::Key_Tab, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 1);
    key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(controller->currentTab(), 0) << "Ctrl+Shift+Tab goes back";
    key(Qt::Key_PageDown, Qt::ControlModifier);
    EXPECT_EQ(controller->currentTab(), 1);
}

// Beside the overview button: the previous and the next document, like Ctrl+PgUp / Ctrl+PgDown (round the ends)
TEST_F(MainWindowTest, arrowsBesideTheOverviewSwitchTabs) {
    auto* previous = findItem("previousTabButton");
    auto* next = findItem("nextTabButton");
    auto* overview = findItem("overviewButton");
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    ASSERT_NE(overview, nullptr);
    ASSERT_EQ(controller->tabCount(), 1);
    EXPECT_FALSE(previous->isVisible()) << "one document: nothing to switch to";
    EXPECT_FALSE(next->isVisible());

    controller->newDocument();
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabCount(), 3);
    ASSERT_EQ(controller->currentTab(), 2);
    EXPECT_TRUE(previous->isVisible());
    EXPECT_TRUE(next->isVisible());
    const auto sceneX = [](QQuickItem* i) { return i->mapToScene(QPointF(0, 0)).x(); };
    EXPECT_LT(std::abs(sceneX(next) - sceneX(overview)), 120) << "next to the overview button";
    EXPECT_LT(sceneX(previous), sceneX(next)) << "previous on the left";

    click(next);
    EXPECT_EQ(controller->currentTab(), 0) << "past the last one: the first, as Ctrl+PgDown";
    click(previous);
    EXPECT_EQ(controller->currentTab(), 2) << "before the first one: the last, as Ctrl+PgUp";
    click(previous);
    EXPECT_EQ(controller->currentTab(), 1);
}

TEST_F(MainWindowTest, tabOverviewSwitchesAndCloses) {
    controller->newDocument();
    controller->newDocument();
    ASSERT_EQ(controller->tabCount(), 3);
    QObject* overview = find("tabOverview");
    ASSERT_NE(overview, nullptr);

    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* grid = find<QQuickItem>("tabGrid");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 3);

    // Tap the first card.
    QQuickItem* first = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0));
    ASSERT_NE(first, nullptr);
    const QPointF center = first->mapToScene(QPointF(first->width() / 2, first->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    EXPECT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 0);

    // Keyboard: open, go right, Enter.
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_Right);
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);

    // Delete closes the highlighted (unmodified) document.
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_Delete);
    EXPECT_EQ(controller->tabCount(), 2);

    // Escape closes it, and types nothing into the search (its text is a control character)
    auto* search = find<QQuickItem>("overviewSearchField");
    ASSERT_NE(search, nullptr);
    key(Qt::Key_Escape);
    EXPECT_TRUE(waitOpened(overview, false)) << "Escape closes the overview";
    EXPECT_EQ(search->property("text").toString(), QString());
    // A letter still starts the search
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    key(Qt::Key_A);
    EXPECT_EQ(search->property("text").toString(), QStringLiteral("a"));
}

// Palm rejection: touch does not wait after the pen by default; the height up to which the pen counts as near can
// be chosen, but is only offered once the pen has told its height.
TEST_F(MainWindowTest, penHeightIsOfferedOnceThePenTellsIt) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    EXPECT_EQ(settings->get("palmRejectionTimeout").toInt(), 0) << "no wait after the pen by default";
    EXPECT_EQ(settings->get("palmNearHeight").toInt(), 100) << "all of the pen's range by default";
    xqt::PenHover::instance().reset();
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("touchTab"));
    auto* row = findItem("palmNearHeightRow");
    ASSERT_NE(row, nullptr);
    EXPECT_FALSE(row->isVisible()) << "a pen that has not told its height: not offered";

    // The pen hovers at a quarter of its range
    static QPointingDevice pen("test pen", 3001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position | QInputDevice::Capability::ZPosition, 1, 3);
    xqt::PenHover::instance().setProximity(true);
    QTabletEvent hover(QEvent::TabletMove, &pen, QPointF(10, 10), QPointF(10, 10), 0.0, 0.f, 0.f, 0.f, 0.0,
                       static_cast<float>(0.25 * xqt::PenHover::MAX_Z), Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    xqt::PenHover::instance().record(hover);
    until([&] { return row->isVisible(); });
    EXPECT_TRUE(row->isVisible()) << "offered now";
    ASSERT_TRUE(settings->set("palmNearHeight", 40));
    EXPECT_EQ(settings->get("palmNearHeight").toInt(), 40);
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    xqt::PenHover::instance().reset();
}

// Drawing with the finger: a toggle in the tool bar and the same setting in Settings -> Touch (off on the desktop).
TEST_F(MainWindowTest, fingerDrawingIsAToggleInTheToolBarAndASetting) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    EXPECT_FALSE(settings->get("touchDrawing").toBool()) << "off by default";
    controller->newDocument();
    auto* button = findItem("touchDrawingButton");
    ASSERT_NE(button, nullptr);
    until([&] { return button->isVisible(); });
    EXPECT_FALSE(button->property("checked").toBool());
    click(button);
    EXPECT_TRUE(settings->get("touchDrawing").toBool());
    EXPECT_TRUE(button->property("checked").toBool());

    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("touchTab"));
    auto* row = findItem("touchDrawingSwitch");
    ASSERT_NE(row, nullptr);
    until([&] { return row->isVisible(); });
    QQuickItem* toggle = nullptr;
    for (QQuickItem* child: row->childItems()) {
        if (QString(child->metaObject()->className()).contains("Switch")) {
            toggle = child;
        }
    }
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->property("checked").toBool()) << "the same setting";
    click(toggle);
    EXPECT_FALSE(settings->get("touchDrawing").toBool());
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    EXPECT_FALSE(button->property("checked").toBool());

    // The first start on a phone without a pen turns it on, once; the user's choice stays after that
    controller->setFingerDrawingDefault(true);
    EXPECT_TRUE(settings->get("touchDrawing").toBool());
    settings->set("touchDrawing", false);
    controller->setFingerDrawingDefault(true);
    EXPECT_FALSE(settings->get("touchDrawing").toBool()) << "only once";
}

TEST_F(MainWindowTest, documentsOpenWithTheHandIfSoSet) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    EXPECT_FALSE(settings->get("handWhenOpening").toBool()) << "off on the desktop (on by default on Android)";
    controller->selectTool("pen");
    controller->newDocument();
    EXPECT_EQ(controller->tool(), "pen") << "the tool stays as it was";

    settings->set("handWhenOpening", true);
    controller->newDocument();
    EXPECT_EQ(controller->tool(), "hand") << "one finger scrolls in a document just opened";
    controller->selectTool("pen");  // the user chooses the pen to write
    controller->tabManager().setCurrentIndex(0);
    EXPECT_EQ(controller->tool(), "pen") << "switching tabs opens nothing: the pen stays";
    controller->tabManager().closeTab(0);
    EXPECT_EQ(controller->tool(), "pen") << "neither does closing one";
    controller->newDocument();
    EXPECT_EQ(controller->tool(), "hand");

    // The switch in Settings -> Touch
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("touchTab"));
    auto* row = findItem("handWhenOpeningSwitch");
    ASSERT_NE(row, nullptr);
    until([&] { return row->isVisible(); });
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

TEST_F(MainWindowTest, settingsSheetAppliesAndSavesOnClose) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    QObject* sheet = find("settingsPage");
    ASSERT_NE(sheet, nullptr);
    const QString file = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
                         "/xournal-qt/settings.xml";

    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    ASSERT_TRUE(settings->set("pressureMultiplier", 2.5));
    EXPECT_DOUBLE_EQ(settings->get("pressureMultiplier").toDouble(), 2.5) << "applies immediately";
    QFile::remove(file);
    ASSERT_TRUE(settings->set("autosaveMinutes", 7));
    EXPECT_FALSE(QFile::exists(file)) << "saved only when the sheet closes";

    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    QFile f(file);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly)) << file.toStdString();
    const QByteArray xml = f.readAll();
    EXPECT_TRUE(xml.contains("name=\"pressureMultiplier\" value=\"2.5")) << xml.left(400).toStdString();
    EXPECT_TRUE(xml.contains("name=\"autosaveTimeout\" value=\"7\""));
}

// Settings -> Display: a ruler on the screen that the slider (or a drag, with the mouse or a finger) stretches until
// it matches a real one; saved, it calibrates this screen (only this one), and 100 % (Ctrl+1) is the real size from
// then on.
TEST_F(MainWindowTest, theScreenIsCalibratedInTheSettings) {
    namespace SC = xqt::ScreenCalibration;
    Settings& settings = *controller->context().getSettings();
    const auto display = SC::displayOf(window->screen(), window->devicePixelRatio());
    ASSERT_FALSE(display.key.isEmpty());
    xqt::CanvasView* view = controller->tabManager().currentView();
    ASSERT_NE(view, nullptr);
    auto& vc = view->getViewController();
    const double defaultDpi = SC::defaultPpi(display) / display.dpr;
    EXPECT_DOUBLE_EQ(vc.zoom100(), defaultDpi / 72.0) << "not calibrated: what the screen says (or 96 dpi)";
    EXPECT_FALSE(SC::storedPpi(settings, display.key).has_value());

    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("displayTab"));
    QQuickItem* ruler = findItem("calibrationRuler");
    QQuickItem* slider = findItem("calibrationSlider");
    ASSERT_NE(ruler, nullptr);
    ASSERT_NE(slider, nullptr);
    until([&] { return ruler->isVisible() && slider->isVisible(); });
    nextFrame();
    EXPECT_NEAR(ruler->property("dpi").toDouble(), defaultDpi, 1e-6);
    EXPECT_NEAR(ruler->property("pixelsPerCm").toDouble(), defaultDpi / 2.54, 1e-6);

    // The slider stretches the ruler
    const double before = ruler->property("dpi").toDouble();
    click(slider);  // (the middle of the slider: (40 + 400) / 2 dpi)
    const double moved = ruler->property("dpi").toDouble();
    EXPECT_NE(moved, before);
    EXPECT_NEAR(moved, slider->property("value").toDouble(), 1e-6);
    EXPECT_NEAR(moved, 220.0, 15.0);
    EXPECT_NEAR(ruler->property("pixelsPerCm").toDouble(), moved / 2.54, 1e-6);

    // ... and so does a drag on the ruler, from its 0 mark: 10 % further out is 10 % more pixels per inch
    const double zeroX = ruler->property("zeroX").toDouble();
    const QPointF grab = ruler->mapToScene(QPointF(zeroX + 200, ruler->height() / 2));
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, grab.toPoint());
    for (int step = 1; step <= 5; ++step) {
        QTest::mouseMove(window, (grab + QPointF(4 * step, 0)).toPoint());
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, (grab + QPointF(20, 0)).toPoint());
    wait(50);
    const double dragged = ruler->property("dpi").toDouble();
    EXPECT_NEAR(dragged, moved * 1.1, moved * 0.01);

    // ... and a finger (Android): from 220 px to 200 px, a tenth shorter
    static QPointingDevice* finger = QTest::createTouchDevice();
    const QPoint touch = ruler->mapToScene(QPointF(zeroX + 220, ruler->height() / 2)).toPoint();
    QTest::touchEvent(window, finger).press(1, touch);
    for (int step = 1; step <= 5; ++step) {
        QTest::touchEvent(window, finger).move(1, touch - QPoint(4 * step, 0));
    }
    QTest::touchEvent(window, finger).release(1, touch - QPoint(20, 0));
    wait(50);
    const double touched = ruler->property("dpi").toDouble();
    EXPECT_NEAR(touched, dragged * 200.0 / 220.0, dragged * 0.01);
    EXPECT_FALSE(SC::storedPpi(settings, display.key).has_value()) << "nothing is stored before it is saved";
    EXPECT_DOUBLE_EQ(vc.zoom100(), defaultDpi / 72.0);

    // Saved: for this screen, as the panel's pixels per inch; the view takes it at once
    const double zoom = vc.zoom();
    QQuickItem* save = findItem("calibrationSave");
    scrollIntoView(save);
    click(save);
    ASSERT_TRUE(SC::storedPpi(settings, display.key).has_value());
    EXPECT_NEAR(*SC::storedPpi(settings, display.key), touched * display.dpr, 0.01);
    std::string stored;
    settings.getCustomElement("xournalQt").getString("screenCalibration", stored);
    EXPECT_EQ(QString::fromStdString(stored).count('='), 1) << "one screen: " << stored;
    EXPECT_NEAR(vc.zoom100(), touched / 72.0, 1e-3);
    EXPECT_DOUBLE_EQ(vc.zoom(), zoom) << "the page stays as large as it was; only what is called 100 % changes";
    EXPECT_EQ(controller->zoomPercent(), static_cast<int>(std::lround(zoom / vc.zoom100() * 100)));

    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/xournal-qt/settings.xml");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("screenCalibration")) << "written when the sheet closes";

    // Real size: Ctrl+1
    key(Qt::Key_1, Qt::ControlModifier);
    EXPECT_EQ(controller->zoomPercent(), 100);
    EXPECT_NEAR(vc.zoom(), touched / 72.0, 1e-3);

    // Back to what the screen says
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    QQuickItem* reset = findItem("calibrationReset");
    scrollIntoView(reset);
    click(reset);
    EXPECT_FALSE(SC::storedPpi(settings, display.key).has_value());
    EXPECT_NEAR(ruler->property("dpi").toDouble(), defaultDpi, 1e-6);
    EXPECT_DOUBLE_EQ(vc.zoom100(), defaultDpi / 72.0);
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

namespace {
QString fixturePath(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}
bool waitFor(const std::function<bool()>& done, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return done();
}
}  // namespace

TEST_F(MainWindowTest, searchBarFindsAndSteps) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* bar = find<QQuickItem>("searchBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible());
    key(Qt::Key_F, Qt::ControlModifier);
    EXPECT_TRUE(bar->isVisible());
    type("p1");
    key(Qt::Key_Return);  // search now
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 3 && !controller->searchRunning(); }));
    EXPECT_EQ(controller->searchCurrent(), 1);
    key(Qt::Key_Return);
    EXPECT_EQ(controller->searchCurrent(), 2);
    EXPECT_EQ(controller->pageNumber(), 10);
    key(Qt::Key_Return, Qt::ShiftModifier);
    EXPECT_EQ(controller->searchCurrent(), 1);
    key(Qt::Key_Escape);
    EXPECT_FALSE(bar->isVisible());
    EXPECT_EQ(controller->searchQuery(), "");
}

TEST_F(MainWindowTest, tabOverviewSearchesAllDocuments) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/strokes.xopp")));
    ASSERT_EQ(controller->tabCount(), 3);
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));

    // Typing in the grid starts the search in all documents.
    type("page");
    key(Qt::Key_Return);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto hits = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchHitsRole).toInt(); };
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1) && !running(2); }));
    EXPECT_EQ(hits(0), 0) << "pages.xopp: only p1..p11";
    EXPECT_GE(hits(1), 1) << "old.xopp: \"Page 2\" in the PDF";
    EXPECT_EQ(hits(2), 0);

    // Opening the document with hits shows its search, at its first hit.
    auto* grid = find<QQuickItem>("tabGrid");
    QQuickItem* card = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, card), Q_ARG(int, 1));
    ASSERT_NE(card, nullptr);
    const QPointF center = card->mapToScene(QPointF(card->width() / 2, card->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    ASSERT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);
    EXPECT_EQ(controller->searchQuery(), "page");
    EXPECT_EQ(controller->searchCurrent(), 1);
    EXPECT_TRUE(find<QQuickItem>("searchBar")->isVisible());
    EXPECT_EQ(controller->pageNumber(), 2) << "scrolled to the hit on page 2";
}

TEST_F(MainWindowTest, pageGridZoomsAndJumpsToAPage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* gridPanel = find<QQuickItem>("pageGrid");
    ASSERT_NE(gridPanel, nullptr);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(gridPanel->isVisible());
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 11);

    const int columns = gridPanel->property("columns").toInt();
    ASSERT_GE(columns, 2);
    key(Qt::Key_Plus);  // bigger previews: one column less
    EXPECT_EQ(gridPanel->property("columns").toInt(), columns - 1);
    key(Qt::Key_Minus);
    key(Qt::Key_Minus);
    EXPECT_EQ(gridPanel->property("columns").toInt(), columns + 1);

    wait(50);
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 4));
    ASSERT_NE(cell, nullptr);
    const QPointF center = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    wait(50);
    EXPECT_FALSE(gridPanel->isVisible());
    EXPECT_EQ(controller->pageNumber(), 5);
}

// Tapping pages in the overview with a finger must select exactly the page that was tapped.
TEST_F(MainWindowTest, tappingPagesInTheOverviewSelectsThem) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* gridPanel = find<QQuickItem>("pageGrid");
    ASSERT_NE(gridPanel, nullptr);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(gridPanel->isVisible());
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    gridPanel->setProperty("selectionMode", true);  // "Select" in the overview: taps select
    wait(50);
    auto* pagesModel = controller->property("pages").value<QObject*>();
    ASSERT_NE(pagesModel, nullptr);
    const auto isSelected = [&](int page) {
        bool result = false;
        QMetaObject::invokeMethod(pagesModel, "isSelected", Q_RETURN_ARG(bool, result), Q_ARG(int, page));
        return result;
    };
    const auto cellCenter = [&](int index) {
        QQuickItem* cell = nullptr;
        QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, index));
        return cell ? cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2)).toPoint() : QPoint();
    };

    static QPointingDevice* finger = QTest::createTouchDevice();
    for (int page: {0, 3, 7}) {
        const QPoint at = cellCenter(page);
        ASSERT_FALSE(at.isNull());
        QTest::touchEvent(window, finger).press(1, at);
        QTest::touchEvent(window, finger).release(1, at);
        wait(60);
        EXPECT_TRUE(isSelected(page)) << "tapped page " << page + 1;
    }
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 3) << "and no others";

    // A finger never holds perfectly still: a tap that slides a few pixels still selects that page
    const QPoint at = cellCenter(5);
    QTest::touchEvent(window, finger).press(1, at);
    QTest::touchEvent(window, finger).move(1, at + QPoint(4, 5));
    QTest::touchEvent(window, finger).release(1, at + QPoint(4, 5));
    wait(60);
    EXPECT_TRUE(isSelected(5)) << "a tap with a little movement still selects";
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 4);

    // A slow tap (a finger easily rests longer than the press-and-hold time) must not throw the selection away:
    // dragging pages only starts once the finger really moves.
    const QPoint slow = cellCenter(9);
    QTest::touchEvent(window, finger).press(1, slow);
    wait(600);
    QTest::touchEvent(window, finger).release(1, slow);
    wait(80);
    EXPECT_TRUE(isSelected(9)) << "the page that was held is selected";
    EXPECT_EQ(pagesModel->property("selectionCount").toInt(), 5) << "the pages selected before stay selected";
    EXPECT_FALSE(controller->canUndoPages()) << "holding a page still must not move any page";
}

TEST_F(MainWindowTest, pageGridCanShowOnlyPagesWithHits) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->setSearchQuery("p1");
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 3 && !controller->searchRunning(); }));
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    EXPECT_EQ(grid->property("count").toInt(), 11);

    // The filter chip of the grid (the sidebar has one too: take the visible one inside the grid).
    QQuickItem* chip = nullptr;
    for (auto* c: find<QQuickItem>("pageGrid")->findChildren<QQuickItem*>("searchFilterChip")) {
        if (c->isVisible()) {
            chip = c;
        }
    }
    ASSERT_NE(chip, nullptr);
    const QPointF p = chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p.toPoint());
    wait(50);
    EXPECT_EQ(grid->property("count").toInt(), 3);
    auto* sidebarList = find<QQuickItem>("sidebarList");
    ASSERT_NE(sidebarList, nullptr);
    EXPECT_EQ(sidebarList->property("count").toInt(), 3) << "the sidebar shares the filter";

    // The second page shown is page 10.
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, 1));
    ASSERT_NE(cell, nullptr);
    const QPointF c = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, c.toPoint());
    wait(50);
    EXPECT_EQ(controller->pageNumber(), 10);
}

TEST_F(MainWindowTest, theHitsFilterLeavesTheWindowAsItIs) {
    // The author: pressing "N pages with hits" (sidebar or page grid) made the maximized window half as high
    window->showMaximized();
    until([&] { return window->visibility() == QWindow::Maximized; });
    wait(1700);  // (past the settling of the window state)
    const QRect before = window->geometry();
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->setSearchQuery("p1");
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 3 && !controller->searchRunning(); }));
    wait(100);
    ASSERT_EQ(window->visibility(), QWindow::Maximized);
    const auto clickChip = [&](QQuickItem* root) {
        QQuickItem* chip = nullptr;
        for (auto* c: root->findChildren<QQuickItem*>("searchFilterChip")) {
            if (c->isVisible()) {
                chip = c;
            }
        }
        if (!chip) {
            return;  // (the sidebar is hidden in a narrow window)
        }
        const QPointF p = chip->mapToScene(QPointF(chip->width() / 2, chip->height() / 2));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, p.toPoint());
        wait(200);
    };
    clickChip(window->contentItem());  // the sidebar's
    EXPECT_EQ(window->visibility(), QWindow::Maximized) << "sidebar chip";
    EXPECT_EQ(window->geometry(), before) << "sidebar chip";
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    wait(200);
    clickChip(find<QQuickItem>("pageGrid"));
    EXPECT_EQ(window->visibility(), QWindow::Maximized) << "grid chip";
    EXPECT_EQ(window->geometry(), before) << "grid chip";
}

TEST_F(MainWindowTest, pageGridKeepsScrollingAfterTouchpadLift) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    for (int i = 0; i < 80; ++i) {
        controller->addPageAfterCurrent();  // a long document: room to keep scrolling
    }
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    wait(50);
    static QPointingDevice touchpad("test touchpad", 2003, QInputDevice::DeviceType::TouchPad,
                                    QPointingDevice::PointerType::Finger,
                                    QInputDevice::Capability::Position | QInputDevice::Capability::Scroll, 2, 0);
    const QPointF pos = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));
    auto wheel = [&](int dy, Qt::ScrollPhase phase) {
        QWheelEvent e(pos, window->mapToGlobal(pos), QPoint(0, dy), QPoint(0, dy * 4), Qt::NoButton, Qt::NoModifier,
                      phase, false, Qt::MouseEventNotSynthesized, &touchpad);
        QCoreApplication::sendEvent(window, &e);
    };
    const double start = grid->property("contentY").toDouble();
    wheel(-20, Qt::ScrollBegin);
    for (int i = 0; i < 8; ++i) {
        wait(10);
        wheel(-20, Qt::ScrollUpdate);
    }
    const double atLift = grid->property("contentY").toDouble();
    EXPECT_GT(atLift, start + 100) << "two-finger scrolling moves the grid";
    const double end = grid->property("contentHeight").toDouble() - grid->height();
    ASSERT_LT(atLift, end - 200) << "not at the end of the grid already";
    wheel(0, Qt::ScrollEnd);
    // Momentum: the grid keeps moving after the fingers are lifted (a loaded machine is slow, and a short flick
    // may already be over when we look, so the flick itself counts too)
    auto moved = [&] {
        return grid->property("contentY").toDouble() > atLift + 50 || grid->property("flicking").toBool();
    };
    until(moved, 3000);
    EXPECT_TRUE(moved()) << "no momentum after lifting the fingers";
}

namespace {
QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}
/// The visible items at the center of `target` (diagnostics).
std::string itemsAt(QQuickWindow* window, QQuickItem* target) {
    const QPointF p = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
    std::string list;
    for (QQuickItem* c: window->contentItem()->childItems()) {
        if (c->isVisible() && c->contains(c->mapFromScene(p))) {
            list += std::string(c->metaObject()->className()) + "(" + c->objectName().toStdString() +
                    ", z " + std::to_string(c->z()) + ") ";
        }
    }
    return list;
}
QPoint centerOf(QQuickItem* item) { return item->mapToScene(QPointF(item->width() / 2, item->height() / 3)).toPoint(); }
}  // namespace

TEST_F(MainWindowTest, sidebarSelectCopyPasteDeleteWithPageUndo) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    wait(100);
    auto* pagesModel = qobject_cast<xqt::PagesModel*>(controller->pagesModel());

    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(itemAt(list, 0)));
    QTest::mouseClick(window, Qt::LeftButton, Qt::ShiftModifier, centerOf(itemAt(list, 1)));
    EXPECT_EQ(pagesModel->selectedPages(), QList<int>({0, 1})) << "Shift+click from the clicked page";
    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, centerOf(itemAt(list, 1)));  // off
    QTest::mouseClick(window, Qt::LeftButton, Qt::ControlModifier, centerOf(itemAt(list, 2)));  // on
    ASSERT_EQ(pagesModel->selectedPages(), QList<int>({0, 2}));

    key(Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(controller->copiedPages(), 2);
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), 13) << "pasted after the selection";

    key(Qt::Key_Delete);
    EXPECT_EQ(controller->pageCount(), 11) << "the pasted (selected) pages are deleted";
    auto* snackbar = find<QQuickItem>("snackbar");
    ASSERT_NE(snackbar, nullptr);
    EXPECT_TRUE(snackbar->isVisible());
    key(Qt::Key_Z, Qt::ControlModifier);  // page undo (the sidebar has the focus)
    EXPECT_EQ(controller->pageCount(), 13);

    // On the canvas, Ctrl+Z is the annotation undo again.
    auto* canvas = find<QQuickItem>("canvas");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 2)).toPoint());
    EXPECT_TRUE(canvas->hasActiveFocus()) << "under the click: " << itemsAt(window, canvas);
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), 13) << "no page undo from the canvas";
}

TEST_F(MainWindowTest, pageGridDragAndDropMovesSelectedPages) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* session = controller->tabManager().currentSession();
    const auto original = session->pageOrder();
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    wait(100);
    auto* pagesModel = qobject_cast<xqt::PagesModel*>(controller->pagesModel());
    pagesModel->selectPages({0, 1});

    // Press and hold on page 1, drag to the right half of page 4 (index 3), drop.
    const QPoint from = centerOf(itemAt(grid, 0));
    QQuickItem* targetCell = itemAt(grid, 3);
    const QPoint to = targetCell->mapToScene(QPointF(targetCell->width() * 0.8, targetCell->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    wait(600);  // press and hold
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, from + (to - from) * i / 10);
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    wait(50);
    const auto order = session->pageOrder();
    EXPECT_EQ(order[2], original[0]);
    EXPECT_EQ(order[3], original[1]);
    EXPECT_EQ(order[1], original[3]);
    EXPECT_EQ(pagesModel->selectedPages(), QList<int>({2, 3}));
    EXPECT_TRUE(find<QQuickItem>("pageGrid")->isVisible()) << "dragging does not open the page";

    controller->undoPages();
    EXPECT_EQ(session->pageOrder(), original);
}

// XQT_SHOTS=<folder>: writes the pictures for the README (off-screen, so no display is needed).
//   XQT_SHOTS=/tmp/shots ./xqt-ui-tests --gtest_filter='*shot*'
namespace {
bool wantShots() { return qEnvironmentVariableIsSet("XQT_SHOTS"); }

void saveShot(QQuickWindow* window, const char* name) {
    const QString folder = qEnvironmentVariable("XQT_SHOTS");
    QDir().mkpath(folder);
    const QImage picture = window->grabWindow();
    ASSERT_FALSE(picture.isNull());
    ASSERT_TRUE(picture.save(folder + '/' + QLatin1String(name) + ".png"));
    std::cerr << "shot " << name << ": " << picture.width() << "x" << picture.height() << "\n";
}
/// No tool tip over the tool bar in the picture: the pointer leaves the buttons and comes to rest on the page
void restPointer(QQuickWindow* window) {
    for (const QPoint& p: {QPoint(window->width() / 2, 300), QPoint(window->width() / 2, window->height() - 60),
                           QPoint(window->width() / 2, window->height() - 30)}) {
        QTest::mouseMove(window, p);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

/// A curve as a hand draws it, from `origin` to the right
void drawCurve(QQuickWindow* window, QPoint origin, int width, int height, double turns) {
    auto at = [&](double t) {
        return origin + QPoint(static_cast<int>(t * width),
                               static_cast<int>(-std::sin(t * turns * 2 * M_PI) * height / 2 * (1 - 0.6 * t)));
    };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(0));
    for (int i = 1; i <= 60; ++i) {
        QTest::mouseMove(window, at(static_cast<double>(i) / 60));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(1));
}
}  // namespace

// A page of notes: Markdown written on the page, a curve drawn by hand, the pages beside it.
TEST_F(MainWindowTest, shotOfTheCanvas) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    window->setProperty("sidebarShown", true);
    controller->goToPage(7);  // squared paper
    wait(600);

    // The note where the page is seen, and the curve under it
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "openBox", Q_ARG(QVariant, 7), Q_ARG(QVariant, 60.0), Q_ARG(QVariant, 300.0));
    wait(200);
    find<QQuickItem>("markdownArea")
            ->setProperty("text", QStringLiteral("## Damped oscillation\n\n"
                                                 "Measured on the shaker at 12 Hz. The envelope follows `exp(-t/tau)`, "
                                                 "with tau about 0.8 s.\n\n"
                                                 "- [x] set up the sensor\n- [ ] repeat it with the heavier mass\n"));
    wait(500);
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, true));
    wait(1200);  // (closing puts the zoom back to what it was before it opened)
    controller->goToPage(7);
    wait(600);

    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    const QPoint origin = canvas->mapToScene(QPointF(canvas->width() * 0.32, canvas->height() * 0.42)).toPoint();
    drawCurve(window, origin, 430, 150, 2.5);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, origin - QPoint(30, 0));  // the axis
    QTest::mouseMove(window, origin + QPoint(460, 0));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, origin + QPoint(460, 0));
    restPointer(window);
    wait(900);  // (the thumbnails of the pages follow)
    saveShot(window, "canvas");
}

// Markdown written on a page, with its source beside it.
TEST_F(MainWindowTest, shotOfMarkdown) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    controller->newDocument();
    while (controller->tabCount() > 1) {
        controller->closeTab(0);
    }
    window->setProperty("sidebarShown", false);
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
    wait(200);
    find<QQuickItem>("markdownArea")->setProperty("text", QStringLiteral(
            "# Seminar, week 3\n\n"
            "Wave equation, **separation of variables**. The ansatz `u(x,t) = X(x)T(t)` gives two problems that are\n"
            "each of one variable.\n\n"
            "## To do\n\n"
            "- [x] read chapter 4\n"
            "- [ ] exercise 4.2 (the boundary conditions!)\n"
            "- [ ] ask about the third eigenvalue\n\n"
            "> The eigenvalues are what the boundary asks for, not what the equation gives.\n\n"
            "```python\n"
            "def modes(n, L):\n"
            "    return [k * pi / L for k in range(1, n + 1)]\n"
            "```\n\n"
            "| mode | n | note |\n| --- | --- | --- |\n| fundamental | 1 | drawn below |\n| first | 2 | node in the middle |\n"));
    restPointer(window);
    wait(900);
    saveShot(window, "markdown");
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, true));
}

// All pages of a document at once.
TEST_F(MainWindowTest, shotOfThePageGrid) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(400);
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    restPointer(window);
    wait(1200);
    saveShot(window, "page-grid");
}

// The open documents, and the search over all of them with the pages that have hits.
TEST_F(MainWindowTest, shotOfTheOverview) {
    if (!wantShots()) {
        GTEST_SKIP() << "set XQT_SHOTS";
    }
    window->resize(1280, 820);
    restPointer(window);  // (no tool tip of a hovered button in the picture)
    wait(200);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/strokes.xopp")));
    wait(600);
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    QObject* overview = find("tabOverview");
    ASSERT_TRUE(waitOpened(overview, true));
    type("page");
    key(Qt::Key_Return);
    wait(800);
    if (!overview->property("extendedView").toBool()) {
        click(find<QQuickItem>("overviewExtendedButton"));
    }
    restPointer(window);
    wait(1200);
    saveShot(window, "overview");
}

// --- the home screen: library and recent documents ---
namespace {
class HomeScreenTest: public MainWindowTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        const fs::path pdf = fs::path(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"));
        fs::create_directories(root / "Physics");
        fs::copy_file(pdf, root / "Physics" / "sheet.pdf");
        fs::copy_file(pdf, root / "lecture.pdf");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/strokes.xopp")), root / "notes.xopp");
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();  // (tests share the config folder)
    }
    QQuickItem* grid() const { return find<QQuickItem>("libraryGrid"); }
    int gridCount() const { return grid()->property("count").toInt(); }
    QQuickItem* card(int row) const { return itemAt(grid(), row); }
    int rowOf(const char* name) const {
        return qobject_cast<xqt::LibraryModel*>(controller->libraryModel())
                ->rowOf(QString::fromStdString((root / name).string()));
    }
    void click(QQuickItem* item, Qt::KeyboardModifiers m = Qt::NoModifier) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, m,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        wait(50);
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(HomeScreenTest, startsOnTheLibraryAndOpensDocuments) {
    EXPECT_EQ(controller->tabCount(), 0);
    auto* home = find<QQuickItem>("homeView");
    ASSERT_NE(home, nullptr);
    EXPECT_TRUE(home->isVisible());
    ASSERT_NE(grid(), nullptr);
    ASSERT_EQ(gridCount(), 3);  // Physics, lecture, notes

    click(card(rowOf("Physics")));  // into the folder
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "Physics");
    EXPECT_EQ(gridCount(), 1);
    key(Qt::Key_Backspace);  // up again
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "");
    ASSERT_EQ(gridCount(), 3);

    click(card(rowOf("notes.xopp")));  // opens it
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_FALSE(home->isVisible());
    EXPECT_EQ(controller->title(), "notes.xopp");

    click(find<QQuickItem>("homeTab"));  // back to the library; the document stays open
    EXPECT_TRUE(home->isVisible());
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_EQ(controller->recentModel()->property("count").toInt(), 1);
}

TEST_F(HomeScreenTest, newMarkdownAndTextFilesAreMadeInTheFolderAndOpened) {
    for (const char* item: {"newMarkdownItem", "newTextItem"}) {
        click(find<QQuickItem>("newDocumentButton"));
        QObject* menu = find("newMenu");
        ASSERT_TRUE(waitOpened(menu, true));
        click(findItem(item));
        QObject* dialog = find("textFileDialog");
        ASSERT_NE(dialog, nullptr);
        ASSERT_TRUE(waitOpened(dialog, true));
        type("Ideas");
        key(Qt::Key_Return);
        EXPECT_TRUE(waitOpened(dialog, false));
        wait(50);
        type("First line");
        ASSERT_TRUE(controller->save());
        controller->setHomeVisible(true);
        wait(50);
    }
    EXPECT_EQ(controller->tabCount(), 2);
    for (const char* name: {"Ideas.md", "Ideas.txt"}) {
        std::ifstream in(root / name, std::ios::binary);
        ASSERT_TRUE(in) << name;
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()), "First line")
                << name;
    }
    // A name that is taken: the next free one
    EXPECT_TRUE(controller->createTextFile("Ideas", ".md"));
    EXPECT_TRUE(fs::exists(root / "Ideas (2).md"));
    EXPECT_EQ(controller->title(), "Ideas (2).md");
    EXPECT_EQ(controller->textDocument(), "markdown");
}

// A new text document in PDF files mode (qt/docs/md-pdf.md): the menu offers "New text document…", which makes
// "name.pdf", a PDF with notes, and opens it with the cursor in its text and the formatting bar; typing writes into
// the text, the pen stays the pen, and ⋮ offers "Export as Markdown".
TEST_F(HomeScreenTest, aNewTextDocumentIsAPdfInPdfFilesMode) {
    Settings& settings = *controller->context().getSettings();
    struct Back {  // (the tests share the config folder)
        Settings& s;
        ~Back() { xqt::DocumentMode::store(s, xqt::DocumentMode::Mode::Unset); }
    } back{settings};
    xqt::DocumentMode::store(settings, xqt::DocumentMode::Mode::Pdf);
    Q_EMIT controller->context().settingsChanged();  // (as Settings do)
    click(find<QQuickItem>("newDocumentButton"));
    QObject* menu = find("newMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* item = findItem("newMarkdownItem");
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->property("text").toString(), QString::fromUtf8("New text document…"));
    click(item);
    QObject* dialog = find("textFileDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(dialog->property("title").toString(), "New text document");
    type("Report");
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(dialog, false));
    wait(50);
    EXPECT_EQ(controller->title(), "Report.pdf");
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(root / "Report.pdf"));
    EXPECT_FALSE(fs::exists(root / "Report.md"));
    EXPECT_TRUE(controller->textNotes());
    auto* bar = findItem("markdownFormatBar");
    ASSERT_NE(bar, nullptr);
    until([&] { return bar->isVisible(); });
    EXPECT_TRUE(bar->isVisible());
    const QString tool = controller->tool();
    type("Hello");
    key(Qt::Key_Escape);  // (done writing)
    EXPECT_EQ(controller->tool(), tool) << "the tools stay the notes' tools";
    type(" world");  // (again, without a click)
    key(Qt::Key_Escape);
    EXPECT_TRUE(bar->isVisible()) << "shown while nothing is written, as for a .md";
    EXPECT_TRUE(controller->modified());
    ASSERT_TRUE(controller->save());
    auto loaded = xqt::DocumentSession::loadFile(root / "Report.pdf");
    ASSERT_TRUE(loaded.document);
    {
        std::shared_lock lock(*loaded.document);
        EXPECT_EQ(xqt::TextDocument::flowText(*loaded.document), "Hello world");
    }
    // ⋮ → Export as Markdown is there (a .md's "Open as PDF document" is not)
    QObject* more = find("moreMenu");
    ASSERT_NE(more, nullptr);
    QMetaObject::invokeMethod(more, "open");
    ASSERT_TRUE(waitOpened(more, true));
    auto* exportItem = find<QQuickItem>("exportMarkdownItem");
    ASSERT_NE(exportItem, nullptr);
    until([&] { return exportItem->isVisible(); });
    EXPECT_TRUE(exportItem->isVisible());
    EXPECT_FALSE(find<QQuickItem>("openAsPdfDocumentItem")->isVisible());
    key(Qt::Key_Escape);
}

TEST_F(HomeScreenTest, severalDocumentsAreSelectedAndMovedIntoAFolder) {
    ASSERT_EQ(gridCount(), 3);
    click(card(rowOf("lecture.pdf")), Qt::ControlModifier);
    click(card(rowOf("notes.xopp")), Qt::ControlModifier);
    auto* bar = find<QQuickItem>("homeSelectionBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    EXPECT_EQ(controller->libraryModel()->property("selectionCount").toInt(), 2);
    EXPECT_EQ(controller->tabCount(), 0) << "Ctrl+click selects, it does not open";

    click(find<QQuickItem>("moveSelectedButton"));
    QObject* dialog = find("transferDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* folders = find<QQuickItem>("transferFolders");
    ASSERT_NE(folders, nullptr);
    wait(50);
    ASSERT_EQ(folders->property("count").toInt(), 2);  // the library, Physics
    click(itemAt(folders, 1));
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_TRUE(fs::exists(root / "Physics" / "lecture.pdf"));
    EXPECT_TRUE(fs::exists(root / "Physics" / "notes.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    EXPECT_EQ(gridCount(), 1);
    EXPECT_FALSE(bar->isVisible());
}

TEST_F(HomeScreenTest, newDocumentIsSavedInTheLibrary) {
    click(find<QQuickItem>("newDocumentButton"));
    QObject* menu = find("newMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitOpened(menu, true));
    click(findItem("newDocumentItem"));
    QObject* dialog = find("newDocumentDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    type("Week");
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_TRUE(fs::exists(root / "Week.xopp"));
    EXPECT_EQ(controller->title(), "Week.xopp");
}

// A phone-wide window (411 px, the Fold 7 folded): nothing of the home screen is wider than the window. The header
// scrolls sideways, the search has a row of its own, the buttons of an empty folder stand one below the other, and
// the new document dialog fits.
TEST_F(HomeScreenTest, aPhoneWideWindowFitsTheHomeScreenAndTheNewDocumentDialog) {
    auto* search = find<QQuickItem>("librarySearchField");
    auto* header = find<QQuickItem>("homeHeader");
    auto* settingsButton = find<QQuickItem>("homeSettingsButton");
    ASSERT_NE(search, nullptr);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(settingsButton, nullptr);
    const double wideSearchY = search->mapToScene(QPointF(0, 0)).y();
    EXPECT_LE(header->width(), window->width()) << "(a header wider than the window scrolls)";

    window->resize(411, 820);
    wait(150);
    auto rightEdge = [](QQuickItem* item) { return item->mapToScene(QPointF(item->width(), 0)).x(); };
    EXPECT_LE(rightEdge(search), window->width()) << "the search field fits";
    EXPECT_GT(search->mapToScene(QPointF(0, 0)).y(), wideSearchY + 30) << "in a row of its own, below";
    EXPECT_LE(rightEdge(header), window->width() + 0.5);
    EXPECT_TRUE(header->property("interactive").toBool()) << "the header scrolls sideways";
    EXPECT_GT(rightEdge(settingsButton), window->width()) << "(its last buttons are further right)";
    if (wantShots()) {
        saveShot(window, "phone-home");
    }

    // An empty folder: its three buttons one below the other, all inside the window
    fs::create_directories(root / "Empty");
    controller->libraryModel()->setProperty("folder", "Empty");
    wait(100);
    ASSERT_EQ(controller->libraryModel()->property("folder").toString(), "Empty");
    ASSERT_EQ(gridCount(), 0);
    QList<QQuickItem*> emptyButtons;
    for (const char* name: {"emptyNewDocument", "emptyImportFiles", "emptyImportFolder"}) {
        auto* b = findItem(name);
        ASSERT_NE(b, nullptr) << name;
        EXPECT_TRUE(b->isVisible()) << name;
        emptyButtons << b;
    }
    ASSERT_EQ(emptyButtons.size(), 3);
    for (QQuickItem* b: emptyButtons) {
        EXPECT_LE(rightEdge(b), window->width()) << b->property("text").toString().toStdString();
        EXPECT_GE(b->mapToScene(QPointF(0, 0)).x(), 0);
    }
    EXPECT_NE(emptyButtons[0]->mapToScene(QPointF(0, 0)).y(), emptyButtons[1]->mapToScene(QPointF(0, 0)).y());
    if (wantShots()) {
        saveShot(window, "phone-empty-folder");
    }

    // The new document dialog: nothing in it is wider than the window
    QObject* dialog = find("newDocumentDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitOpened(dialog, true));
    wait(400);
    for (const char* name: {"newDocumentName", "landscapeButton", "saveInLibrary", "paperBox"}) {
        auto* item = findItem(name);
        ASSERT_NE(item, nullptr) << name;
        EXPECT_LE(rightEdge(item), window->width()) << name;
    }
    if (wantShots()) {
        saveShot(window, "phone-new-document");
    }
    key(Qt::Key_Escape);
    EXPECT_TRUE(waitOpened(dialog, false));
    window->resize(1280, 900);
}

TEST_F(MainWindowTest, tabStripUsesTheWholeWidthForManyTabs) {
    auto* list = find<QQuickItem>("tabList");
    auto* plus = find<QQuickItem>("newTabButton");
    ASSERT_NE(list, nullptr);
    ASSERT_NE(plus, nullptr);
    const double few = list->width();
    EXPECT_GT(few, 0);
    for (int i = 0; i < 20; ++i) {
        controller->newDocument();
    }
    wait(100);
    EXPECT_GT(list->property("contentWidth").toDouble(), window->width()) << "more tabs than room";
    EXPECT_GT(plus->mapToScene(QPointF(plus->width(), 0)).x(), window->width() - 60)
            << "the + button follows the tabs to the right end";
    EXPECT_GT(list->width(), window->width() * 0.75);
}

TEST_F(MainWindowTest, shortSearchTextsWaitForEnter) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    key(Qt::Key_F, Qt::ControlModifier);
    type("p1");
    wait(400);
    EXPECT_EQ(controller->searchQuery(), "") << "fewer than 4 characters: not while typing";
    key(Qt::Key_Return);
    EXPECT_EQ(controller->searchQuery(), "p1");
    EXPECT_GT(controller->searchHitCount() + (controller->searchRunning() ? 1 : 0), 0);
    type("0 x");  // "p10 x": long enough
    until([&] { return controller->searchQuery() == "p10 x"; });  // (once the typing paused)
    EXPECT_EQ(controller->searchQuery(), "p10 x");
}

namespace {
/// A PDF of `pages` pages full of text, "search" three times per page: its search takes a while.
void makeLongTextPdf(const std::string& file, int pages) {
    cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), 595, 842);
    cairo_t* cr = cairo_create(surface);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10);
    for (int p = 0; p < pages; ++p) {
        for (int line = 0; line < 60; ++line) {
            cairo_move_to(cr, 40, 40 + line * 12.5);
            cairo_show_text(cr, line % 20 == 7 ? "a line to search for in the long text of this page"
                                               : "lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do");
        }
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
}  // namespace

// The author: with a search, the page grid kept jumping back to the start while scrolling down. The hit places of
// the pages scrolled into view arrive as search updates, and each one moved the grid to the current hit's page.
TEST_F(MainWindowTest, thePageGridStaysWhereItIsScrolledWhileSearchResultsArrive) {
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("long.pdf").toStdString();
    makeLongTextPdf(pdf, 120);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf)));
    controller->setSearchQuery("search");
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 120 * 3 && !controller->searchRunning(); }, 8000));
    ASSERT_GT(controller->searchCurrent(), 0) << "a current hit, near the start";
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    auto* grid = find<QQuickItem>("pageGridView");
    ASSERT_NE(grid, nullptr);
    until([&] { return grid->isVisible(); });
    wait(300);
    const qreal far = grid->property("contentHeight").toReal() * 0.6;
    ASSERT_GT(far, grid->height());
    grid->setProperty("contentY", far);  // scrolled down, away from the current hit
    wait(1200);  // the pages now in view get their hit places
    EXPECT_NEAR(grid->property("contentY").toReal(), far, 1.0) << "the grid stays where it was scrolled";
}

// Typing on while the search for what was typed before still runs: the field used to be set back to the text of
// that search whenever its results came in (the field's text was bound to the query), and the letters typed
// meanwhile were gone. Whatever is typed stays; the results are those of the text in the field.
TEST_F(MainWindowTest, theSearchFieldsKeepWhatIsTypedWhileASearchRuns) {
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("long.pdf").toStdString();
    makeLongTextPdf(pdf, 400);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf)));
    wait(50);
    key(Qt::Key_F, Qt::ControlModifier);
    auto* field = find<QQuickItem>("searchField");
    ASSERT_NE(field, nullptr);
    until([&] { return field->hasActiveFocus(); });
    type("sear");
    ASSERT_TRUE(waitFor([&] { return controller->searchQuery() == "sear"; }));
    // The search for "sear" has started; typing goes on, key by key
    for (const char c: std::string("ch fo")) {
        QTest::keyClick(window, c);
        wait(15);
        EXPECT_TRUE(field->property("text").toString().endsWith(QChar(c == ' ' ? ' ' : c)))
                << "typed '" << c << "', the field has \"" << field->property("text").toString().toStdString() << '"';
    }
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("search fo"));
    ASSERT_TRUE(waitFor([&] { return controller->searchQuery() == "search fo" && !controller->searchRunning(); },
                        20000));
    wait(100);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("search fo")) << "nothing typed is lost";
    EXPECT_EQ(controller->searchHitCount(), 400 * 3) << "the hits of the text in the field";
    key(Qt::Key_Escape);

    // The search over all open documents
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* overviewField = find<QQuickItem>("overviewSearchField");
    ASSERT_NE(overviewField, nullptr);
    overviewField->forceActiveFocus();
    type("line");
    wait(350);
    for (const char c: std::string(" to s")) {
        QTest::keyClick(window, c);
        wait(15);
    }
    EXPECT_EQ(overviewField->property("text").toString(), QStringLiteral("line to s"));
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    const auto hits = [&] { return tabs->index(0, 0).data(xqt::TabManager::SearchHitsRole).toInt(); };
    const auto running = [&] { return tabs->index(0, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };
    ASSERT_TRUE(waitFor([&] { return controller->searchQuery() == "line to s" && !running(); }, 20000));
    EXPECT_EQ(overviewField->property("text").toString(), QStringLiteral("line to s"));
    EXPECT_EQ(hits(), 400 * 3);
}

namespace {
/// A PDF of three pages; "turbine" on the first and the third.
void makeTurbinePdf(const std::string& file) {
    cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), 595, 842);
    cairo_t* cr = cairo_create(surface);
    cairo_select_font_face(cr, "sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 14);
    for (const char* line: {"The turbine blades turn in the wind.", "Nothing to see on this page.",
                            "A second turbine stands by the river."}) {
        cairo_move_to(cr, 60, 100);
        cairo_show_text(cr, line);
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}
}  // namespace

// "Fuzzy" in the search bar of a document (the author, 2026-09-26): the text is read with the fuzzy search's syntax,
// typos tolerated; a tap searches it again in the other mode and sets the app-wide setting. A search handed over from
// the library or the tab overview with Fuzzy on shows it on (the bar used to show no sign of it). Its help opens on a
// long press, as in the library.
TEST_F(MainWindowTest, theSearchBarHasAFuzzyToggle) {
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setFuzzySearch(false);  // (the tests share the config folder)
    QTemporaryDir tmp;
    const QString pdf = tmp.filePath("turbines.pdf");
    makeTurbinePdf(pdf.toStdString());
    ASSERT_TRUE(controller->openPath(pdf));
    key(Qt::Key_F, Qt::ControlModifier);
    auto* toggle = find<QQuickItem>("searchFuzzy");
    ASSERT_NE(toggle, nullptr);
    until([&] { return toggle->isVisible(); });
    EXPECT_FALSE(toggle->property("checked").toBool());

    // Misspelled: nothing plain, the pages with "turbine" fuzzy
    type("tbine");
    ASSERT_TRUE(waitFor([&] { return controller->searchQuery() == "tbine" && !controller->searchRunning(); }, 8000));
    EXPECT_EQ(controller->searchHitCount(), 0) << "plain: no such text";
    click(toggle);
    EXPECT_TRUE(toggle->property("checked").toBool());
    EXPECT_TRUE(library->fuzzySearch()) << "the app-wide setting";
    EXPECT_TRUE(controller->property("searchFuzzy").toBool());
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 2 && !controller->searchRunning(); }))
            << "searched again, fuzzy";
    EXPECT_EQ(controller->searchQuery(), "tbine");
    EXPECT_EQ(controller->searchHitPageCount(), 2) << "pages 1 and 3";
    EXPECT_EQ(controller->searchCurrent(), 1);
    EXPECT_TRUE(find<QQuickItem>("searchField")->hasActiveFocus()) << "the field keeps the keys";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(800);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // The syntax, as in the library; typing on keeps the mode
    struct Case {
        const char* query;
        int hits;
    };
    for (const Case& c: {Case{"turbnie", 2},      // a typo
                         Case{"'tbine", 0},       // exactly these letters
                         Case{"'turbine'", 2},    // the whole word
                         Case{"^turb", 2},        // a word that starts with it
                         Case{"bine$", 2},        // a word that ends with it
                         Case{"tbine river", 3},  // every hit of both terms
                         Case{"tbine !river", 2}}) {
        controller->setSearchQuery(QString::fromUtf8(c.query));
        ASSERT_TRUE(waitFor([&] { return !controller->searchRunning(); })) << c.query;
        EXPECT_TRUE(controller->property("searchFuzzy").toBool()) << c.query;
        EXPECT_EQ(controller->searchHitCount(), c.hits) << c.query;
    }
    auto* hint = find<QQuickItem>("searchSyntaxHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());
    controller->setSearchQuery("(tbine");
    until([&] { return hint->isVisible(); });
    EXPECT_TRUE(hint->isVisible()) << "a ( not closed: searched as plain text, and it says so";

    // Off again: plain
    controller->setSearchQuery("tbine");
    click(toggle);
    EXPECT_FALSE(toggle->property("checked").toBool());
    EXPECT_FALSE(library->fuzzySearch());
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 0 && !controller->searchRunning(); }));
    EXPECT_FALSE(hint->isVisible());

    // Handed over from the library with Fuzzy on: the bar shows it on
    key(Qt::Key_Escape);
    library->setFuzzySearch(true);
    ASSERT_TRUE(controller->openSearchHit(pdf, "tbine"));
    library->setFuzzySearch(false);  // turned off in the library meanwhile: this document's search stays fuzzy
    until([&] { return find<QQuickItem>("searchBar")->isVisible(); });
    EXPECT_TRUE(toggle->property("checked").toBool()) << "the handed-over search is fuzzy";
    ASSERT_TRUE(waitFor([&] { return controller->searchHitCount() == 2 && !controller->searchRunning(); }));
    // ... and from the tab overview
    controller->clearSearch();
    EXPECT_FALSE(toggle->property("checked").toBool()) << "no search: the setting";
    library->setFuzzySearch(true);
    controller->searchAllTabs("tbine");
    EXPECT_TRUE(toggle->property("checked").toBool());
    library->setFuzzySearch(false);
    controller->searchAllTabs("tbine");
    EXPECT_FALSE(toggle->property("checked").toBool());
    EXPECT_EQ(controller->searchHitCount(), 0);

    // The help: a long press (not toggled)
    key(Qt::Key_F, Qt::ControlModifier);
    QObject* help = find("searchFuzzyHelp");
    ASSERT_NE(help, nullptr);
    const QPoint center = toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
    wait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 300);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
    ASSERT_TRUE(waitOpened(help, true)) << "opened by a long press";
    EXPECT_FALSE(library->fuzzySearch()) << "not toggled by it";
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(help, false));
    library->setFuzzySearch(false);

    // A phone-wide window: the bar fits (its field gives way)
    window->resize(420, 800);
    nextFrame();
    auto* bar = find<QQuickItem>("searchBar");
    const QRectF shown = bar->mapRectToScene(QRectF(0, 0, bar->width(), bar->height()));
    EXPECT_GE(shown.left(), 0);
    EXPECT_LE(shown.right(), window->width());
    const QRectF toggleShown = toggle->mapRectToScene(QRectF(0, 0, toggle->width(), toggle->height()));
    EXPECT_LE(toggleShown.right(), shown.right());
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT").replace(".png", "-narrow.png"));
    }
}

// Typing on the library starts a search, but keys that only have a control character as their text do not type it.
TEST_F(HomeScreenTest, onlyVisibleCharactersStartTheLibrarySearch) {
    auto* grid = find<QQuickItem>("libraryGrid");
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(field, nullptr);
    grid->forceActiveFocus();
    key(Qt::Key_Escape);
    key(Qt::Key_Delete);
    key(Qt::Key_Tab);
    grid->forceActiveFocus();
    EXPECT_EQ(field->property("text").toString(), QString()) << "no control characters in the search";
    key(Qt::Key_L);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("l")) << "a letter starts the search";
}

// As the search in a document: typing on while the search for the text before runs keeps every letter.
TEST_F(HomeScreenTest, theLibrarySearchFieldKeepsWhatIsTyped) {
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    QObject* library = controller->libraryModel();
    field->forceActiveFocus();
    type("lect");
    ASSERT_TRUE(waitFor([&] { return library->property("searchQuery").toString() == "lect"; }));
    for (const char c: std::string("ure")) {
        QTest::keyClick(window, c);
        wait(15);
    }
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("lecture"));
    ASSERT_TRUE(waitFor([&] { return library->property("searchQuery").toString() == "lecture"; }));
    wait(50);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("lecture"));
}

// "Names" next to the library search: the reduced search over names only.
TEST_F(HomeScreenTest, theLibrarySearchCanBeLimitedToNames) {
    auto* button = find<QQuickItem>("searchNamesOnly");
    ASSERT_NE(button, nullptr);
    QObject* library = controller->libraryModel();
    EXPECT_FALSE(library->property("namesOnly").toBool());
    click(button);
    EXPECT_TRUE(library->property("namesOnly").toBool()) << "names only";
    EXPECT_TRUE(button->property("checked").toBool());
    click(button);
    EXPECT_FALSE(library->property("namesOnly").toBool()) << "the full search again";
}

// "Fuzzy" in the library search: fzf's syntax, names matched fuzzily with their matched letters highlighted, a hint
// for an expression that is not valid. An app-wide setting, off by default.
TEST_F(HomeScreenTest, theLibrarySearchHasAFuzzyToggle) {
    auto* button = find<QQuickItem>("librarySearchFuzzy");
    ASSERT_NE(button, nullptr);
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setFuzzySearch(false);  // (the tests share the config folder)
    EXPECT_FALSE(button->property("checked").toBool());
    library->setSearchQuery("lctr");
    EXPECT_EQ(gridCount(), 0) << "not fuzzy: no such text";

    click(button);
    EXPECT_TRUE(library->fuzzySearch());
    EXPECT_TRUE(button->property("checked").toBool());
    ASSERT_TRUE(waitFor([&] { return gridCount() == 1; }));
    QQuickItem* name = nullptr;
    until([&] {
        QQuickItem* c = card(0);
        if (!c) {
            return false;
        }
        for (auto* item: c->findChildren<QQuickItem*>()) {
            if (item->objectName() == "cardName") {
                name = item;
            }
        }
        return name != nullptr;
    });
    ASSERT_NE(name, nullptr);
    EXPECT_TRUE(name->property("text").toString().contains("<font")) << name->property("text").toString().toStdString();
    EXPECT_EQ(name->property("textFormat").toInt(), 4) << "Text.StyledText";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(800);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    auto* hint = find<QQuickItem>("librarySyntaxHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());
    library->setSearchQuery("(lecture");
    until([&] { return hint->isVisible(); });
    EXPECT_TRUE(hint->isVisible()) << "a ( that is not closed";
    EXPECT_FALSE(hint->property("text").toString().isEmpty());

    click(button);
    EXPECT_FALSE(library->fuzzySearch()) << "off again";
    until([&] { return !hint->isVisible(); });
    EXPECT_FALSE(hint->isVisible());
    library->setSearchQuery("");
}

// The fuzzy search's help: a long press on the "Fuzzy" button (which does not toggle it then), a right click, or the
// help button in Settings → Search; it tells the typo tolerance as it is set, and closes with Escape.
TEST_F(HomeScreenTest, theFuzzySearchButtonOpensItsHelp) {
    auto* button = find<QQuickItem>("librarySearchFuzzy");
    ASSERT_NE(button, nullptr);
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setFuzzySearch(false);  // (the tests share the config folder)
    QObject* help = find("librarySearchFuzzyHelp");
    ASSERT_NE(help, nullptr);

    // A long press
    const QPoint center = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, center);
    wait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 300);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, center);
    ASSERT_TRUE(waitOpened(help, true)) << "opened by a long press";
    EXPECT_FALSE(library->fuzzySearch()) << "not toggled by it";
    auto* rows = findItem("fuzzyHelpRows");
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->property("count").toInt(), 11) << "the syntax, a row each";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(800);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    settings->set("fuzzyTypos", 0);
    EXPECT_TRUE(help->property("typoText").toString().contains("not tolerated"));
    settings->set("fuzzyTypos", 1);
    EXPECT_TRUE(help->property("typoText").toString().contains("5 or more"));
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(help, false));

    // A right click
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, center);
    ASSERT_TRUE(waitOpened(help, true)) << "opened by a right click";
    EXPECT_FALSE(library->fuzzySearch());
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(help, false));
    click(button);
    EXPECT_TRUE(library->fuzzySearch()) << "a tap still toggles";
    EXPECT_FALSE(help->property("visible").toBool());
    library->setFuzzySearch(false);

    // From Settings → Search
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("searchTab"));
    auto* helpButton = findItem("fuzzyHelpButton");
    ASSERT_NE(helpButton, nullptr);
    until([&] { return helpButton->isVisible(); });
    click(helpButton);
    QObject* fromSettings = find("settingsFuzzyHelp");
    ASSERT_NE(fromSettings, nullptr);
    ASSERT_TRUE(waitOpened(fromSettings, true));
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(fromSettings, false));
    EXPECT_TRUE(sheet->property("visible").toBool()) << "back in the settings";
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

// The library has a button for the settings (no tool bar there, and not everybody has a keyboard at hand).
TEST_F(HomeScreenTest, theSettingsOpenFromTheLibrary) {
    auto* button = find<QQuickItem>("homeSettingsButton");
    ASSERT_NE(button, nullptr);
    QObject* sheet = find("settingsPage");
    ASSERT_NE(sheet, nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(800);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(button);
    EXPECT_TRUE(waitOpened(sheet, true));
}

// Settings → Storage: what the library's cache takes, moving it into the app's cache folder and back, and removing
// all its cache folders (the window closes then, so the app does not build them again right away).
TEST_F(HomeScreenTest, theStorageSettingsMoveAndRemoveTheLibraryCache) {
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->searchIndex()->flush();
    ASSERT_TRUE(fs::exists(root / "Physics" / ".xournal_library" / "notes.pack"));
    QObject* sheet = find("settingsPage");
    ASSERT_NE(sheet, nullptr);
    click(find<QQuickItem>("homeSettingsButton"));
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("storageTab"));
    auto* size = findItem("cacheSizeLabel");
    ASSERT_NE(size, nullptr);
    until([&] { return size->property("text").toString().contains("files"); });
    EXPECT_TRUE(size->property("text").toString().contains("files")) << size->property("text").toString().toStdString();
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    auto* toApp = findItem("cacheInAppSwitch");
    ASSERT_NE(toApp, nullptr);
    click(toApp);
    EXPECT_TRUE(library->cacheInAppCache());
    EXPECT_FALSE(fs::exists(root / "Physics" / ".xournal_library")) << "moved into the app cache";
    EXPECT_TRUE(toApp->property("checked").toBool());
    click(toApp);
    EXPECT_FALSE(library->cacheInAppCache());
    EXPECT_TRUE(fs::exists(root / "Physics" / ".xournal_library" / "notes.pack")) << "and back";

    click(findItem("removeCachesButton"));
    QObject* dialog = find("removeCachesDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    click(findItem("removeCachesConfirm"));
    EXPECT_TRUE(library->cacheRemoved());
    EXPECT_FALSE(fs::exists(root / "Physics" / ".xournal_library"));
    EXPECT_FALSE(fs::exists(root / ".xournal_library"));
    until([&] { return !window->isVisible(); });
    EXPECT_FALSE(window->isVisible()) << "the window closes";
}

// A document read in the app shows on its card when and at which page; "Last read first" sorts by that.
TEST_F(HomeScreenTest, cardsShowWhenAndWhereADocumentWasLastRead) {
    const QString lecture = QString::fromStdString((root / "lecture.pdf").string());
    ASSERT_TRUE(controller->openPath(lecture));
    controller->goToPage(1);
    wait(50);
    controller->closeTab(controller->currentTab());
    controller->setHomeVisible(true);
    wait(80);
    const int row = rowOf("lecture.pdf");
    ASSERT_GE(row, 0);
    until([&] { return card(row) != nullptr; });
    QQuickItem* tag = nullptr;
    QQuickItem* text = nullptr;
    for (auto* c: card(row)->findChildren<QQuickItem*>()) {
        if (c->objectName() == "lastReadTag") tag = c;
        if (c->objectName() == "lastReadText") text = c;
    }
    ASSERT_NE(tag, nullptr);
    ASSERT_NE(text, nullptr);
    until([&] { return tag->isVisible(); });
    EXPECT_TRUE(tag->isVisible()) << "read just now";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1000);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    EXPECT_TRUE(text->property("text").toString().endsWith("p.2")) << text->property("text").toString().toStdString();
    QQuickItem* notesTag = nullptr;
    for (auto* c: card(rowOf("notes.xopp"))->findChildren<QQuickItem*>()) {
        if (c->objectName() == "lastReadTag") notesTag = c;
    }
    ASSERT_NE(notesTag, nullptr);
    EXPECT_FALSE(notesTag->isVisible()) << "never read in the app";

    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setSortBy("read");
    EXPECT_EQ(library->data(library->index(0), xqt::LibraryModel::IsFolderRole).toBool(), true) << "Physics";
    EXPECT_EQ(library->data(library->index(1), xqt::LibraryModel::NameRole).toString(), "lecture")
            << "the one read last comes first";
    library->setSortBy("name");
}

TEST_F(HomeScreenTest, shortLibrarySearchWaitsForEnter) {
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("le");
    wait(400);
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "");
    key(Qt::Key_Return);
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "le");
}

TEST_F(HomeScreenTest, searchFindsFoldersAndOpensThem) {
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("physics");
    until([&] { return controller->libraryModel()->property("searchQuery").toString() == "physics"; });
    ASSERT_EQ(controller->libraryModel()->property("searchQuery").toString(), "physics");
    ASSERT_GE(gridCount(), 1);
    auto* first = card(0);
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->property("isFolder").toBool());
    click(first);
    EXPECT_EQ(controller->libraryModel()->property("folder").toString(), "Physics");
    EXPECT_EQ(controller->libraryModel()->property("searchQuery").toString(), "");
    EXPECT_EQ(field->property("text").toString(), "");
    EXPECT_EQ(gridCount(), 1);  // sheet.pdf
}

// Library menu → "Export library as archive…": the dialog explains it, never into the library, then in the background
// with progress, and a summary at the end.
TEST_F(HomeScreenTest, exportLibraryAsArchive) {
    ASSERT_NE(find("exportLibraryArchiveItem"), nullptr);
    QObject* dialog = find("libraryArchiveDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitOpened(dialog, true));
    const QString text = findItem("libraryArchiveExplanation")->property("text").toString();
    EXPECT_TRUE(text.contains("PDF/A-3") && text.contains("copied") && text.contains("not changed")) << text.toStdString();
    EXPECT_TRUE(findItem("archiveWholeLibrary")->property("checked").toBool());
    EXPECT_FALSE(findItem("archiveThisFolder")->property("enabled").toBool()) << "at the library's top";
    QMetaObject::invokeMethod(dialog, "close");
    ASSERT_TRUE(waitOpened(dialog, false));

    EXPECT_FALSE(controller->exportLibraryArchive(QUrl::fromLocalFile(QString::fromStdString((root / "Physics").string())),
                                                  false))
            << "never into the library";
    QObject* messageDialog = find("messageDialog");
    ASSERT_TRUE(waitOpened(messageDialog, true));
    QMetaObject::invokeMethod(messageDialog, "close");
    ASSERT_TRUE(waitOpened(messageDialog, false));

    QTemporaryDir out;
    QObject* summary = find("libraryArchiveSummary");
    ASSERT_NE(summary, nullptr);
    ASSERT_TRUE(controller->exportLibraryArchive(QUrl::fromLocalFile(out.path()), false));
    EXPECT_TRUE(find("libraryArchiveProgress")->property("visible").toBool());
    EXPECT_NE(findItem("libraryArchiveCancel"), nullptr);
    ASSERT_TRUE(waitOpened(summary, true, 60000));
    EXPECT_FALSE(find("libraryArchiveProgress")->property("visible").toBool());
    const QVariantMap result = summary->property("summary").toMap();
    EXPECT_EQ(result["archived"].toInt(), 3) << "sheet, lecture, notes";
    EXPECT_TRUE(findItem("libraryArchiveSummaryText")->property("text").toString().startsWith("3 archived"));
    const fs::path target(result["target"].toString().toStdString());
    EXPECT_EQ(target.parent_path(), fs::path(out.path().toStdString()));
    for (const char* f: {"Physics/sheet.pdf", "lecture.pdf", "notes.pdf", "README.txt"}) {
        EXPECT_TRUE(fs::exists(target / f)) << f;
    }
    EXPECT_TRUE(xqt::HybridPdf::isArchive(target / "notes.pdf"));
    QMetaObject::invokeMethod(summary, "close");
    ASSERT_TRUE(waitOpened(summary, false));
}

TEST_F(HomeScreenTest, libraryMenuMarksThisLibraryWithoutToggles) {
    click(find<QQuickItem>("libraryMenuButton"));
    QObject* menu = find("libraryMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitOpened(menu, true));
    int current = 0;
    const int n = menu->property("count").toInt();
    for (int i = 0; i < n; ++i) {
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, i));
        ASSERT_NE(item, nullptr);
        EXPECT_FALSE(item->property("checkable").toBool()) << "no toggles: " << item->property("text").toString().toStdString();
        if (item->property("current").toBool()) {
            ++current;
            EXPECT_TRUE(item->property("text").toString().contains(QString::fromStdString(root.filename().string())));
            click(item);  // this library: stays in this window
        }
    }
    EXPECT_EQ(current, 1) << "this window's library is marked";
    EXPECT_TRUE(waitOpened(menu, false));
    EXPECT_TRUE(controller->homeVisible());
}

TEST_F(HomeScreenTest, extendedSearchShowsHitPagesAndOpensThePage) {
    auto* lib = controller->libraryModel();
    QElapsedTimer t;
    t.start();
    while (lib->property("indexing").toBool() && t.elapsed() < 5000) {
        wait(20);
    }
    click(find<QQuickItem>("extendedSearchButton"));
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("page 2");
    key(Qt::Key_Return);
    wait(100);
    const int row = rowOf("lecture.pdf");
    ASSERT_GE(row, 0);
    QQuickItem* lecture = card(row);
    ASSERT_NE(lecture, nullptr);
    QQuickItem* strip = nullptr;
    for (auto* c: lecture->findChildren<QQuickItem*>()) {
        if (c->objectName() == "hitPageStrip") {
            strip = c;
        }
    }
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isVisible());
    ASSERT_EQ(strip->property("count").toInt(), 1) << "page 2 has the hit";
    wait(100);
    QQuickItem* page = itemAt(strip, 0);
    ASSERT_NE(page, nullptr);
    EXPECT_GT(grid()->property("cellHeight").toDouble(), grid()->property("cellWidth").toDouble())
            << "taller cells for the page row";

    click(page);  // opens the document at that page, with the search
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_EQ(controller->title(), "lecture.pdf");
    EXPECT_EQ(controller->pageNumber(), 2);
    EXPECT_EQ(controller->searchQuery(), "page 2");

    // Zoom: fewer, bigger cells
    click(find<QQuickItem>("homeTab"));
    const int columns = grid()->property("columns").toInt();
    click(find<QQuickItem>("zoomInButton"));
    EXPECT_EQ(grid()->property("columns").toInt(), std::max(1, columns - 1));
}

namespace {
/// A library with a Markdown file: "needle" in a paragraph under "Lecture 3 › Kalman filter" and far down in it.
class HomeScreenMarkdownTest: public HomeScreenTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        std::string text = "# Lecture 3\n\n## Kalman filter\n\nThe needle is here.\n\n";
        for (int i = 0; i < 100; ++i) {
            text += "Paragraph " + std::to_string(i) + " about the prediction step of the filter.\n\n";
        }
        text += "## Update\n\nAnother needle at the end.\n";
        std::ofstream(root / "kalman.md") << text;
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
    }
};
}  // namespace

TEST_F(HomeScreenMarkdownTest, extendedSearchShowsSnippetCardsAndOpensTheFileThere) {
    auto* lib = controller->libraryModel();
    QElapsedTimer t;
    t.start();
    while (lib->property("indexing").toBool() && t.elapsed() < 5000) {
        wait(20);
    }
    ASSERT_EQ(gridCount(), 1);
    click(find<QQuickItem>("extendedSearchButton"));
    auto* field = find<QQuickItem>("librarySearchField");
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    type("needle");
    key(Qt::Key_Return);
    wait(100);
    QQuickItem* md = card(rowOf("kalman.md"));
    ASSERT_NE(md, nullptr);
    QQuickItem* strip = nullptr;
    QQuickItem* pages = nullptr;
    for (auto* c: md->findChildren<QQuickItem*>()) {
        if (c->objectName() == "hitPassageStrip") {
            strip = c;
        } else if (c->objectName() == "hitPageStrip") {
            pages = c;
        }
    }
    ASSERT_NE(strip, nullptr);
    EXPECT_TRUE(strip->isVisible());
    EXPECT_FALSE(pages->isVisible()) << "cards, not pages";
    ASSERT_EQ(strip->property("count").toInt(), 2);
    wait(100);
    QQuickItem* first = itemAt(strip, 0);
    ASSERT_NE(first, nullptr);
    QString headings;
    for (auto* c: first->findChildren<QQuickItem*>()) {
        if (c->objectName() == "hitPassageHeadings") {
            headings = c->property("text").toString();
        }
    }
    EXPECT_EQ(headings, "Lecture 3 › Kalman filter");

    // The second card: the file opens at its page, with the search on its hit, to be edited (no read-only note)
    QMetaObject::invokeMethod(strip, "positionViewAtIndex", Q_ARG(int, 1), Q_ARG(int, 0));  // (ListView.Beginning)
    wait(100);
    QQuickItem* second = itemAt(strip, 1);
    ASSERT_NE(second, nullptr);
    click(second);
    EXPECT_FALSE(controller->homeVisible());
    EXPECT_EQ(controller->title(), "kalman.md");
    EXPECT_GT(controller->pageNumber(), 1);
    EXPECT_EQ(controller->searchQuery(), "needle");
    wait(50);
    auto* note = find<QQuickItem>("shownFileNote");
    ASSERT_NE(note, nullptr);
    EXPECT_FALSE(note->isVisible());
    EXPECT_EQ(controller->textDocument(), "markdown");
    EXPECT_TRUE(controller->textEditable());
}

TEST_F(HomeScreenMarkdownTest, aMarkdownFileIsNotWrittenOn) {
    auto draw = [&] {
        auto* canvas = find<QQuickItem>("canvas");
        const QPoint from = canvas->mapToScene(QPointF(canvas->width() * 0.4, canvas->height() * 0.4)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(window, from + QPoint(8 * i, 5 * i));
            wait(10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + QPoint(80, 50));
        wait(50);
    };
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(100);
    controller->selectTool("pen");  // (the tool is app-wide: an earlier test in the same process may have left another)
    ASSERT_EQ(controller->tool(), "pen");
    draw();
    EXPECT_FALSE(controller->modified()) << "a text file: the pen puts the cursor into the text, it does not write";
    EXPECT_EQ(controller->beginMarkdown(0), "") << "(it is written on its pages, not beside them)";
    controller->newDocument();
    wait(100);
    draw();
    EXPECT_TRUE(controller->modified()) << "(a new document is written on)";
}

TEST_F(HomeScreenMarkdownTest, aMarkdownFileIsWrittenInAndSavedBack) {
    const fs::path file = root / "kalman.md";
    std::string original;
    {
        std::ifstream in(file, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    EXPECT_FALSE(findItem("eraserButton")->isVisible()) << "no ink tools for a text file";
    auto* canvas = find<QQuickItem>("canvas");
    click(canvas);  // the cursor goes where the page was clicked
    type("Hello");
    EXPECT_TRUE(controller->modified());
    ASSERT_TRUE(controller->save());
    std::string saved;
    {
        std::ifstream in(file, std::ios::binary);
        saved.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const size_t at = saved.find("Hello");
    ASSERT_NE(at, std::string::npos);
    EXPECT_EQ(saved.substr(0, at) + saved.substr(at + 5), original) << "only the typed text is new";
    EXPECT_FALSE(controller->modified());
    EXPECT_FALSE(fs::exists(root / "kalman.xopp"));

    // Changed by another app while it has changes here: asked, and reloaded
    type("X");
    std::ofstream(file, std::ios::binary) << "# Changed elsewhere\n";
    controller->checkTextFiles();
    auto* dialog = find<QObject>("textChangedDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    click(find<QQuickItem>("textReloadButton"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_FALSE(controller->modified());
    EXPECT_EQ(controller->tabManager().currentSession()->currentText(), "# Changed elsewhere\n");
    // Without changes here it is read again without asking
    std::ofstream(file, std::ios::binary) << "# Third\n";
    controller->checkTextFiles();
    wait(50);
    EXPECT_FALSE(dialog->property("visible").toBool());
    EXPECT_EQ(controller->tabManager().currentSession()->currentText(), "# Third\n");
}

TEST_F(HomeScreenMarkdownTest, aTxtFileIsEditedAsPlainText) {
    const fs::path file = root / "todo.txt";
    std::ofstream(file, std::ios::binary) << "# not a heading\n**not bold**\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    EXPECT_EQ(controller->textDocument(), "plain");
    EXPECT_TRUE(controller->textEditable());
    EXPECT_FALSE(find<QQuickItem>("shownFileNote")->isVisible());
    click(find<QQuickItem>("canvas"));
    key(Qt::Key_End, Qt::ControlModifier);
    type("done");
    ASSERT_TRUE(controller->save());
    std::ifstream in(file, std::ios::binary);
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
              "# not a heading\n**not bold**\ndone");
}

TEST_F(HomeScreenMarkdownTest, textFilesAreOnPagesOrOnOneContinuousPage) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(100);
    EXPECT_FALSE(controller->textContinuous()) << "pages by default";
    EXPECT_GT(controller->pageCount(), 2);
    auto* layout = find<QQuickItem>("layoutButton");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      layout->mapToScene(QPointF(layout->width() / 2, layout->height() / 2)).toPoint());
    auto* menu = find<QObject>("layoutMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* continuous = nullptr;
    for (auto* c: menu->findChildren<QQuickItem*>()) {
        if (c->objectName() == "textContinuousItem") {
            continuous = c;
        }
    }
    ASSERT_NE(continuous, nullptr);
    ASSERT_TRUE(continuous->isVisible());
    click(continuous);
    wait(50);
    EXPECT_TRUE(controller->textContinuous());
    EXPECT_EQ(controller->pageCount(), 1);
    EXPECT_FALSE(controller->modified());
    // Another text file opens the same way (a setting); notes are not affected
    const fs::path other = root / "other.md";
    std::ofstream(other, std::ios::binary) << "# Other\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(other.string())));
    wait(50);
    EXPECT_TRUE(controller->textContinuous());
    controller->setTextContinuous(false);
    EXPECT_FALSE(controller->textContinuous());
    controller->setCurrentTab(0);
    wait(50);
    EXPECT_TRUE(controller->textContinuous()) << "the other tab stays as it was laid out";
    controller->setTextContinuous(false);
    EXPECT_GT(controller->pageCount(), 2);
}

TEST_F(HomeScreenMarkdownTest, otherTextFilesAreEditedOnlyAfterAWarning) {
    const fs::path file = root / "script.py";
    std::ofstream(file, std::ios::binary) << "def f():\r\n    return 1\r\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(100);
    EXPECT_EQ(controller->textDocument(), "") << "read-only, as before";
    EXPECT_TRUE(controller->canEditAnyway());
    auto* button = find<QQuickItem>("editAnywayButton");
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisible());
    auto* dialog = find<QObject>("editAnywayDialog");
    ASSERT_NE(dialog, nullptr);
    // Cancel: it stays read-only
    click(button);
    ASSERT_TRUE(waitOpened(dialog, true));
    QMetaObject::invokeMethod(dialog, "reject");
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(controller->textDocument(), "");
    // OK: edited as plain text, in the same tab
    click(button);
    ASSERT_TRUE(waitOpened(dialog, true));
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(controller->textDocument(), "plain");
    EXPECT_TRUE(controller->textEditable());
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_EQ(controller->title(), "script.py");
    click(find<QQuickItem>("canvas"));
    key(Qt::Key_End, Qt::ControlModifier);
    type("# end");
    ASSERT_TRUE(controller->save());
    {
        std::ifstream in(file, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()),
                  "def f():\r\n    return 1\r\n# end");
    }
    // Opened again later: edited at once, no warning (once per file)
    controller->closeTab(0);
    ASSERT_TRUE(controller->openPath(QString::fromStdString(file.string())));
    wait(50);
    EXPECT_EQ(controller->textDocument(), "plain");
    EXPECT_FALSE(dialog->property("visible").toBool());
    // Another file is asked about again
    const fs::path other = root / "data.json";
    std::ofstream(other, std::ios::binary) << "{}\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(other.string())));
    wait(50);
    EXPECT_EQ(controller->textDocument(), "");
    EXPECT_FALSE(controller->editAnyway());
    ASSERT_TRUE(waitOpened(dialog, true));
    QMetaObject::invokeMethod(dialog, "reject");
}

namespace {
/// Records what would be handed to the system (nothing is started).
struct FakeSystemApps: xqt::SystemApps {
    QStringList opened, shown, libraries, trashed, shared;
    bool share(const QStringList& files) override {
        shared << files;
        return true;
    }
    /// (removed instead: never the user's trash)
    bool moveToTrash(const QString& path) override {
        trashed << path;
        return QFileInfo(path).isDir() ? QDir(path).removeRecursively() : QFile::remove(path);
    }
    bool openWithSystemApp(const QString& path) override {
        opened << path;
        return true;
    }
    bool showInFileManager(const QString& path) override {
        shown << path;
        return true;
    }
    bool startLibraryWindow(const QString& folder) override {
        libraries << folder;
        return true;
    }
};

/// A library with a text file and an Office file next to the documents.
class HomeScreenFilterTest: public HomeScreenTest {
protected:
    void prepareController() override {
        xqt::SystemApps::setInstance(&fake);
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        std::ofstream(root / "report.docx") << "PK";
        std::ofstream(root / "kalman.py") << "def predict(state):\n    return state\n";
        HomeScreenTest::prepareController();
    }
    void TearDown() override {
        HomeScreenTest::TearDown();
        xqt::SystemApps::setInstance(nullptr);
    }
    /// A child of an item or popup by its objectName
    static QQuickItem* child(QObject* parent, const char* name) {
        for (auto* c: parent->findChildren<QQuickItem*>()) {
            if (c->objectName() == name) {
                return c;
            }
        }
        return nullptr;
    }
    FakeSystemApps fake;
};
}  // namespace

// A sync app's conflict copy is a badge on its document's card (not a card of its own); the badge opens "compare /
// keep one", and keeping the document moves the copy to the trash.
TEST_F(HomeScreenFilterTest, aSyncConflictIsABadgeOnItsDocument) {
    const fs::path copy = root / "notes.sync-conflict-20240312-101530-ABCDEFG.xopp";
    fs::copy_file(root / "notes.xopp", copy);
    controller->libraryModel()->setProperty("folder", QString());
    QMetaObject::invokeMethod(controller->libraryModel(), "refresh");
    wait(100);
    EXPECT_EQ(gridCount(), 3) << "no card of its own";
    QQuickItem* notes = card(rowOf("notes.xopp"));
    ASSERT_NE(notes, nullptr);
    QQuickItem* badge = child(notes, "conflictBadge");
    ASSERT_NE(badge, nullptr);
    EXPECT_TRUE(badge->isVisible());
    EXPECT_FALSE(child(card(rowOf("lecture.pdf")), "conflictBadge")->isVisible());
    click(badge);
    auto* dialog = find<QObject>("conflictDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(controller->tabCount(), 0) << "the badge does not open the document";
    EXPECT_EQ(dialog->property("items").toList().size(), 2);
    // (the buttons are delegates of a Repeater: found in the tree of items, not of objects)
    std::function<QQuickItem*(QQuickItem*)> button = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == "conflictKeepDocumentButton" && item->isVisible()) {
            return item;
        }
        for (QQuickItem* c: item->childItems()) {
            if (QQuickItem* found = button(c)) {
                return found;
            }
        }
        return nullptr;
    };
    click(button(window->contentItem()));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(fake.trashed, QStringList{QString::fromStdString(copy.string())});
    EXPECT_FALSE(fs::exists(copy));
    EXPECT_TRUE(fs::exists(root / "notes.xopp"));
    wait(50);
    EXPECT_FALSE(child(card(rowOf("notes.xopp")), "conflictBadge")->isVisible());
}

TEST_F(HomeScreenFilterTest, theShowButtonChoosesTheKindsOfFilesShown) {
    ASSERT_EQ(gridCount(), 3) << "Physics, lecture, notes: text and other files are not shown by default";
    auto* button = find<QQuickItem>("showButton");
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->property("checked").toBool());
    click(button);
    QObject* popup = find("showPopup");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(waitOpened(popup, true));
    EXPECT_TRUE(child(popup, "showNotes")->property("checked").toBool());
    EXPECT_FALSE(child(popup, "showOther")->property("checked").toBool());
    EXPECT_FALSE(child(popup, "showOnlyPdfsWithNotes")->property("checked").toBool());

    click(child(popup, "showOther"));
    EXPECT_EQ(gridCount(), 4);
    EXPECT_TRUE(button->property("checked").toBool()) << "marked while not the default";
    click(child(popup, "showText"));
    EXPECT_EQ(gridCount(), 5);
    EXPECT_TRUE(popup->property("opened").toBool()) << "stays open for more toggles";
    // The Office file: an icon of its type, its extension, its size
    wait(100);
    QQuickItem* docx = card(rowOf("report.docx"));
    ASSERT_NE(docx, nullptr);
    EXPECT_TRUE(child(docx, "fileTypeIcon")->isVisible());
    EXPECT_EQ(child(docx, "kindBadgeText")->property("text").toString(), "DOCX");
    EXPECT_EQ(child(docx, "cardName")->property("text").toString(), "report.docx");
    EXPECT_EQ(child(card(rowOf("kalman.py")), "kindBadgeText")->property("text").toString(), "PY");

    // Only PDFs with notes: the lone PDF goes
    click(child(popup, "showOnlyPdfsWithNotes"));
    EXPECT_EQ(gridCount(), 4);
    EXPECT_LT(rowOf("lecture.pdf"), 0);
    click(child(popup, "showDefaults"));
    EXPECT_EQ(gridCount(), 3);
    EXPECT_FALSE(button->property("checked").toBool());
}

namespace {
/// The library of HomeScreenFilterTest with a PDF with notes ("memo.pdf") and a PDF text document ("essay.pdf") next to
/// the plain "lecture.pdf"
class HomeScreenKindsTest: public HomeScreenFilterTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        {
            DocumentHandler handler;
            Document doc(&handler);
            auto page = std::make_shared<XojPage>(595, 842);
            auto stroke = std::make_unique<Stroke>();
            stroke->setWidth(2);
            stroke->addPoint(Point(100, 100, 1.0));
            stroke->addPoint(Point(300, 200, 1.0));
            page->getSelectedLayer()->addElement(std::move(stroke));
            doc.addPage(page);
            ASSERT_TRUE(xqt::HybridPdf::write(doc, root / "memo.pdf").ok);
        }
        auto text = xqt::MarkdownFile::notesDocument("# An essay\n\nWritten, not drawn.\n");
        ASSERT_TRUE(xqt::HybridPdf::write(*text, root / "essay.pdf").ok);
        HomeScreenFilterTest::prepareController();
    }
    QString badgeOf(const char* file) const {
        QQuickItem* c = card(rowOf(file));
        return c ? child(c, "kindBadgeText")->property("text").toString() : QString("<no card>");
    }
};
}  // namespace

// The kinds of PDFs at a glance (qt/docs/library.md, "Kinds of PDFs"): a plain PDF, a PDF with notes and a PDF text
// document have badges of their own (the text document's in another colour), said in words in the tooltip; "Only PDF
// text documents" shows only those.
TEST_F(HomeScreenKindsTest, theCardsTellPlainPdfsPdfsWithNotesAndTextDocumentsApart) {
    ASSERT_TRUE(waitFor([&] { return badgeOf("essay.pdf") == "PDF MD"; }, 20000)) << badgeOf("essay.pdf").toStdString();
    EXPECT_EQ(badgeOf("lecture.pdf"), "PDF");
    EXPECT_EQ(badgeOf("memo.pdf"), "PDF ✎");
    EXPECT_FALSE(child(card(rowOf("notes.xopp")), "kindBadge")->isVisible()) << "notes alone: no badge";
    QQuickItem* plain = child(card(rowOf("lecture.pdf")), "kindBadge");
    QQuickItem* notes = child(card(rowOf("memo.pdf")), "kindBadge");
    QQuickItem* text = child(card(rowOf("essay.pdf")), "kindBadge");
    EXPECT_EQ(text->property("description").toString(), "PDF text document");
    EXPECT_EQ(notes->property("description").toString(), "PDF with notes");
    EXPECT_EQ(plain->property("description").toString(), "PDF");
    EXPECT_NE(text->property("color"), notes->property("color")) << "told apart by colour too";
    EXPECT_EQ(notes->property("color"), plain->property("color")) << "(both red: PDFs)";
    if (qEnvironmentVariableIsSet("XQT_SHOTS")) {
        saveShot(window, "library-kinds");
    }

    // Only PDF text documents
    auto* button = find<QQuickItem>("showButton");
    click(button);
    QObject* popup = find("showPopup");
    ASSERT_TRUE(waitOpened(popup, true));
    click(child(popup, "showOnlyTextDocuments"));
    EXPECT_GE(rowOf("essay.pdf"), 0);
    EXPECT_LT(rowOf("memo.pdf"), 0);
    EXPECT_LT(rowOf("lecture.pdf"), 0);
    EXPECT_GE(rowOf("notes.xopp"), 0) << "notes are not PDFs";
    click(child(popup, "showDefaults"));
    EXPECT_GE(rowOf("lecture.pdf"), 0);
}

TEST_F(HomeScreenFilterTest, anOtherFileOpensWithItsAppAndIsShownInTheFileManager) {
    QMetaObject::invokeMethod(controller->libraryModel(), "setShown", Q_ARG(QString, "other"), Q_ARG(bool, true));
    wait(50);
    ASSERT_EQ(gridCount(), 4);
    const QString docx = QString::fromStdString((root / "report.docx").string());
    click(card(rowOf("report.docx")));
    EXPECT_EQ(fake.opened, QStringList{docx}) << "a tap hands it to its app";
    EXPECT_EQ(controller->tabCount(), 0);
    EXPECT_TRUE(controller->homeVisible());

    // Its menu: open with the system app, show in the file manager
    click(child(card(rowOf("report.docx")), "cardMenuButton"));
    QObject* menu = find("homeItemMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* openWith = child(menu, "openWithSystemAppItem");
    ASSERT_NE(openWith, nullptr);
    EXPECT_TRUE(openWith->isVisible());
    click(child(menu, "showInFileManagerItem"));
    EXPECT_EQ(fake.shown, QStringList{docx});
    ASSERT_TRUE(waitOpened(menu, false)) << "the menu closes (Qt 6.8: a tap while it is closing goes to it)";
    // A document has no "Open externally"
    click(child(card(rowOf("notes.xopp")), "cardMenuButton"));
    ASSERT_TRUE(waitOpened(menu, true));
    EXPECT_FALSE(child(menu, "openWithSystemAppItem")->isVisible());
}

TEST_F(HomeScreenFilterTest, textFilesAndImagesOpenExternally) {
    // A read-only text file: the button in the tool bar hands it over
    const QString py = QString::fromStdString((root / "kalman.py").string());
    ASSERT_TRUE(controller->openPath(py));
    wait(50);
    auto* button = findItem("openExternallyButton");
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isVisible());
    click(button);
    EXPECT_EQ(fake.opened, QStringList{py});

    // A .md with unsaved changes: asked to save first; saved, then handed over
    const fs::path md = root / "draft.md";
    std::ofstream(md, std::ios::binary) << "# Draft\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(md.string())));
    wait(50);
    click(find<QQuickItem>("canvas"));
    type("x");
    ASSERT_TRUE(controller->modified());
    click(findItem("openExternallyButton"));
    auto* dialog = find<QObject>("externalSaveDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(fake.opened.size(), 1) << "not before it is saved";
    click(find<QQuickItem>("externalSaveButton"));
    until([&] { return fake.opened.size() == 2; });
    ASSERT_EQ(fake.opened.size(), 2);
    EXPECT_EQ(fake.opened.last(), QString::fromStdString(md.string()));
    EXPECT_FALSE(controller->modified());
    // The other app changes it: shown as it is when the window is looked at again
    std::ofstream(md, std::ios::binary) << "# Draft, edited elsewhere\n";
    controller->checkTextFiles();
    EXPECT_EQ(controller->tabManager().currentSession()->currentText(), "# Draft, edited elsewhere\n");

    // Notes have no "Open externally"
    controller->newDocument();
    wait(50);
    EXPECT_FALSE(findItem("openExternallyButton")->isVisible());
    EXPECT_EQ(controller->externalFileOf(QString::fromStdString((root / "notes.xopp").string())), "");
    EXPECT_EQ(controller->externalFileOf(QString::fromStdString((root / "lecture.pdf").string())), "");
    EXPECT_EQ(controller->externalFileOf(py), py);
    EXPECT_EQ(controller->externalFileOf(QString::fromStdString(md.string())), QString::fromStdString(md.string()));
}

TEST_F(HomeScreenFilterTest, aTextDocumentIsSharedAsTheFileItself) {
    const fs::path md = root / "share.md";
    std::ofstream(md, std::ios::binary) << "# Share\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(md.string())));
    wait(50);
    click(find<QQuickItem>("canvas"));
    type("x");
    ASSERT_TRUE(controller->modified());
    auto* dialog = find<QObject>("shareDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QString()));
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_FALSE(findItem("sharePdfChoice")->isVisible()) << "no PDF with notes for a text file";
    EXPECT_FALSE(findItem("shareXournalChoice")->isVisible());
    ASSERT_TRUE(findItem("shareTextFileChoice")->isVisible());
    click(findItem("shareTextFileChoice"));
    until([&] { return !fake.shared.isEmpty(); });
    EXPECT_EQ(fake.shared, QStringList{QString::fromStdString(md.string())});
    EXPECT_FALSE(controller->modified()) << "saved first";
    EXPECT_FALSE(fs::exists(root / "share.pdf"));
    EXPECT_FALSE(fs::exists(root / "share.xopp"));
    EXPECT_EQ(controller->shareStep(), "text");
    EXPECT_FALSE(controller->sharePdfCopy(QUrl(), true));
    // No Save as (with its file types) for a text file: it is saved as itself
    EXPECT_FALSE(controller->saveAs(QUrl::fromLocalFile(QString::fromStdString((root / "share.pdf").string()))));
    EXPECT_FALSE(fs::exists(root / "share.pdf"));
}

TEST_F(HomeScreenFilterTest, aFolderOpensAsALibraryInAWindowOfItsOwn) {
    // The menu of a folder card: "Open as library" opens it in a window of its own
    click(child(card(rowOf("Physics")), "cardMenuButton"));
    QObject* menu = find("homeItemMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* openAsLibrary = child(menu, "openAsLibraryItem");
    ASSERT_NE(openAsLibrary, nullptr);
    ASSERT_TRUE(openAsLibrary->isVisible());
    click(openAsLibrary);
    EXPECT_EQ(fake.libraries, QStringList{QString::fromStdString((root / "Physics").string())});
    EXPECT_TRUE(waitOpened(menu, false));
    // A document's menu has none
    click(child(card(rowOf("notes.xopp")), "cardMenuButton"));
    ASSERT_TRUE(waitOpened(menu, true));
    EXPECT_FALSE(child(menu, "openAsLibraryItem")->isVisible());
    QMetaObject::invokeMethod(menu, "close");
}

TEST_F(HomeScreenFilterTest, recentLibrariesOpenAgain) {
    QObject* menu = find("homeItemMenu");
    // A library opened before is in the Recent grid: a folder with the library mark; a tap opens it again
    QTemporaryDir other;
    const QString otherPath = other.path();
    auto* recent = qobject_cast<xqt::RecentFiles*>(controller->recentModel());
    recent->addLibrary(fs::path(otherPath.toStdString()));
    find<QQuickItem>("homeView")->setProperty("page", 1);
    wait(100);
    auto* recentGrid = find<QQuickItem>("recentGrid");
    ASSERT_EQ(recentGrid->property("count").toInt(), 1);
    QQuickItem* libraryCard = itemAt(recentGrid, 0);
    ASSERT_NE(libraryCard, nullptr);
    EXPECT_TRUE(child(libraryCard, "libraryMark")->isVisible());
    EXPECT_EQ(child(libraryCard, "cardName")->property("text").toString(), QFileInfo(otherPath).fileName());
    click(libraryCard);
    EXPECT_EQ(fake.libraries, QStringList{otherPath});
    // Its menu: no rename, copy, move or trash of a whole library from here
    click(child(libraryCard, "cardMenuButton"));
    ASSERT_TRUE(waitOpened(menu, true));
    EXPECT_FALSE(child(menu, "renameItem")->isVisible());
    EXPECT_FALSE(child(menu, "trashItem")->isVisible());
    EXPECT_FALSE(child(menu, "moveToItem")->isVisible());
    QMetaObject::invokeMethod(menu, "close");
    recent->clear();
}

// Windows (2026-09-24): the Downloads entry of the library menu made its URL as "file://" + path, which with a drive
// letter is a network path ("cannot open c//"). The entry hands over the path itself now.
TEST_F(HomeScreenFilterTest, theLibraryMenuOpensTheDownloadsFolderByItsPath) {
    QTemporaryDir dl;
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    fs::create_directories(config.toStdString());
    {
        QFile dirs(config + "/user-dirs.dirs");
        ASSERT_TRUE(dirs.open(QIODevice::WriteOnly));
        dirs.write(("XDG_DOWNLOAD_DIR=\"" + dl.path().toStdString() + "\"\n").c_str());
    }
    QObject* menu = find("libraryMenu");
    ASSERT_NE(menu, nullptr);
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitOpened(menu, true));
    QObject* downloads = nullptr;
    for (auto* item: menu->findChildren<QQuickItem*>()) {
        if (item->objectName() == "libraryMenuEntry" &&
            item->property("modelData").toMap().value("downloads").toBool()) {
            downloads = item;
        }
    }
    ASSERT_NE(downloads, nullptr) << "the Downloads folder is in the menu";
    QMetaObject::invokeMethod(downloads, "triggered");
    ASSERT_FALSE(fake.libraries.isEmpty());
    EXPECT_EQ(fake.libraries.last(), dl.path());
    QMetaObject::invokeMethod(menu, "close");
    QFile::remove(config + "/user-dirs.dirs");
}

TEST_F(MainWindowTest, tabsCloseOnlyOnPurposeAndAllAtOnce) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabManager().count(), 2);

    // A tap (or a middle click) on a tab only shows it; it must not close it
    auto* tab = findItem("tabList");
    ASSERT_NE(tab, nullptr);
    QQuickItem* first = itemAt(tab, 0);
    ASSERT_NE(first, nullptr);
    QTest::mouseClick(window, Qt::MiddleButton, Qt::NoModifier,
                      first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint());
    wait(50);
    EXPECT_EQ(controller->tabManager().count(), 2) << "the middle button does not close tabs";
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint p = first->mapToScene(QPointF(first->width() / 2, first->height() / 2)).toPoint();
    { auto touch = QTest::touchEvent(window, screen); touch.press(0, p); }
    wait(30);
    { auto touch = QTest::touchEvent(window, screen); touch.release(0, p); }
    wait(80);
    EXPECT_EQ(controller->tabManager().count(), 2) << "a finger on a tab does not close it";
    EXPECT_EQ(controller->currentTab(), 0) << "it shows that document";

    // All at once, from the overview (more than one: it asks first)
    auto* overview = find<QObject>("tabOverview");
    QMetaObject::invokeMethod(overview, "open");
    until([&] { return overview->property("visible").toBool(); });
    click(findItem("closeAllButton"));
    auto* dialog = find<QObject>("closeAllDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool()) << "it asks before closing several documents";
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return controller->tabManager().count() == 0; });
    EXPECT_EQ(controller->tabManager().count(), 0);
    EXPECT_TRUE(controller->homeVisible()) << "no documents left: the home screen";
}

TEST_F(MainWindowTest, draggingATabOffTheStripSaysWhatHappens) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    AppController::setWindowFactory({});  // no window is made in this test
    auto* list = findItem("tabList");
    ASSERT_NE(list, nullptr);
    QQuickItem* tab = itemAt(list, 0);
    ASSERT_NE(tab, nullptr);
    auto* hint = find<QQuickItem>("tabDragHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());

    const QPoint start = tab->mapToScene(QPointF(tab->width() / 2, tab->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    for (int dy = 10; dy <= 160; dy += 15) {
        QTest::mouseMove(window, start + QPoint(0, dy));
        wait(20);
    }
    EXPECT_TRUE(hint->isVisible()) << "the window says what letting go does";
    EXPECT_TRUE(hint->property("willMove").toBool());

    const int before = controller->tabManager().count();
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(0, 160));
    wait(100);
    EXPECT_EQ(controller->tabManager().count(), before - 1) << "the document went to a window of its own";
    EXPECT_FALSE(hint->isVisible());
}

TEST_F(MainWindowTest, aTabGetsAWindowOfItsOwn) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabManager().count(), 2);

    // The windows are made like in main(): the same QML with the new controller as "app"
    std::vector<QPointer<QQuickWindow>> made;
    AppController::setWindowFactory([this, &made](AppController* w) {
        auto* context = new QQmlContext(engine->rootContext(), w);
        context->setContextProperty("app", w);
        auto* component = new QQmlComponent(engine.get(), QStringLiteral("XournalQt"), QStringLiteral("Main"), w);
        QObject* object = component->create(context);
        if (!object) {
            qWarning("%s", qPrintable(component->errorString()));
            return;
        }
        object->setParent(w);
        made.push_back(qobject_cast<QQuickWindow*>(object));
    });

    controller->undockTab(0);
    wait(200);
    ASSERT_EQ(made.size(), 1u);
    ASSERT_NE(made[0], nullptr);
    EXPECT_EQ(controller->tabManager().count(), 1) << "the document left the main window";
    ASSERT_EQ(controller->documentWindows().size(), 1u);
    AppController* second = controller->documentWindows().front();
    EXPECT_EQ(second->tabManager().count(), 1);
    // It shows documents only: no home tab
    auto* homeTab = made[0]->findChild<QQuickItem*>("homeTab");
    ASSERT_NE(homeTab, nullptr);
    EXPECT_FALSE(homeTab->isVisible());
    EXPECT_FALSE(second->homeVisible());

    // Back to the main window: the second window asks to be closed
    QSignalSpy closing(second, &AppController::closeWindowRequested);
    second->dockTab(0);
    wait(50);
    EXPECT_EQ(controller->tabManager().count(), 2) << "the document is back in the main window";
    EXPECT_EQ(closing.count(), 1);
    wait(100);  // the window without documents closes itself, with its controller
    EXPECT_TRUE(controller->documentWindows().empty());
    EXPECT_TRUE(made[0].isNull()) << "the window is gone";
    AppController::setWindowFactory({});
}

TEST_F(MainWindowTest, fourOrFiveFingersShowThePagesOrTheDocuments) {
    static QPointingDevice* screen = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    auto* grid = find<QQuickItem>("pageGrid");
    auto* overview = find<QObject>("tabOverview");  // a Popup is no Item
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(overview, nullptr);
    auto overviewShown = [&] { return overview->property("visible").toBool(); };
    auto tap = [&](int fingers) {
        {
            auto down = QTest::touchEvent(window, screen);
            for (int i = 0; i < fingers; ++i) {
                down.press(i, QPoint(300 + i * 40, 300));
            }
        }
        wait(30);
        {
            auto up = QTest::touchEvent(window, screen);
            for (int i = 0; i < fingers; ++i) {
                up.release(i, QPoint(300 + i * 40, 300));
            }
        }
        wait(80);
    };

    tap(4);  // the pages of the document
    EXPECT_TRUE(grid->isVisible());
    tap(4);
    until([&] { return !grid->isVisible(); });
    EXPECT_FALSE(grid->isVisible()) << "the same gesture closes it again";

    tap(5);  // all open documents
    until([&] { return overviewShown(); });
    EXPECT_TRUE(overviewShown());
    tap(5);
    until([&] { return !overviewShown(); });
    EXPECT_FALSE(overviewShown());

    tap(3);  // fewer fingers are for the canvas (undo / redo), not for the overviews
    EXPECT_FALSE(grid->isVisible());
    EXPECT_FALSE(overviewShown());
}

TEST_F(MainWindowTest, fiveWidthsInTheToolBar) {
    window->setWidth(1920);  // room for the whole tool bar (full HD)
    wait(100);
    auto* fifth = findItem("customSizeButton");
    ASSERT_NE(fifth, nullptr);
    click(findItem("sizeButton4"));
    EXPECT_EQ(controller->size(), 4) << "the fourth width (very thick)";

    click(fifth);  // the fifth: the tool's own width
    EXPECT_EQ(controller->size(), 5);
    const double before = controller->customWidth();
    EXPECT_GT(before, 0);

    click(fifth);  // again: change it
    auto* popup = find<QObject>("customWidthPopup");  // a Popup is no Item
    ASSERT_NE(popup, nullptr);
    EXPECT_TRUE(popup->property("visible").toBool());
    click(findItem("customWidthMore"));
    EXPECT_GT(controller->customWidth(), before);
    click(findItem("customWidthLess"));
    click(findItem("customWidthLess"));
    EXPECT_LT(controller->customWidth(), before);
    EXPECT_EQ(controller->size(), 5);
}

TEST_F(MainWindowTest, toolbarMovesToTheLeftOrRight) {
    auto* gridButton = find<QQuickItem>("contentsButton");  // a tool bar button
    auto* canvas = find<QQuickItem>("canvas");
    ASSERT_NE(gridButton, nullptr);
    auto sceneX = [](QQuickItem* i) { return i->mapToScene(QPointF(0, 0)).x(); };
    const double canvasWidthOnTop = canvas->width();

    controller->setToolbarPosition("left");
    wait(50);
    EXPECT_LT(sceneX(gridButton), 110);
    EXPECT_GT(gridButton->mapToScene(QPointF(0, 0)).y(), 0) << "below the tab strip, not in the header";
    EXPECT_LT(canvas->width(), canvasWidthOnTop);
    EXPECT_GE(sceneX(find<QQuickItem>("sidebar")), 100) << "the page sidebar right of the tools";

    controller->setToolbarPosition("right");
    wait(50);
    EXPECT_GT(sceneX(gridButton), window->width() - 110);
    EXPECT_LE(sceneX(canvas) + canvas->width(), window->width() - 100);

    controller->setToolbarPosition("top");
    wait(50);
    EXPECT_DOUBLE_EQ(canvas->width(), canvasWidthOnTop);
    EXPECT_EQ(controller->toolbarPosition(), "top");
}

TEST_F(MainWindowTest, fullScreenShowsOnlyTheCurrentTool) {
    auto* square = find<QQuickItem>("quickToolSquare");
    ASSERT_NE(square, nullptr);
    EXPECT_FALSE(square->isVisible());
    key(Qt::Key_F11);
    EXPECT_TRUE(window->property("fullScreenMode").toBool());
    EXPECT_TRUE(square->isVisible());
    EXPECT_FALSE(find<QQuickItem>("sidebar")->isVisible());
    auto* gridButton = find<QQuickItem>("contentsButton");  // a tool bar button
    EXPECT_FALSE(gridButton->isVisible()) << "the tools are hidden";
    EXPECT_TRUE(find<QQuickItem>("viewPill")->isVisible()) << "page number, zoom and the page grid stay";

    // Tap the square: all tools; choosing one closes them
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, square->mapToScene(QPointF(28, 28)).toPoint());
    QObject* tools = find("quickTools");
    ASSERT_TRUE(waitOpened(tools, true));
    QQuickItem* eraser = nullptr;
    for (auto* i: window->contentItem()->window()->findChildren<QQuickItem*>()) {
        if (i->property("iconName").toString() == "xopp-tool-eraser" && i->isVisible()) {
            eraser = i;
        }
    }
    ASSERT_NE(eraser, nullptr);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      eraser->mapToScene(QPointF(eraser->width() / 2, eraser->height() / 2)).toPoint());
    EXPECT_EQ(controller->tool(), "eraser");
    EXPECT_TRUE(waitOpened(tools, false));

    key(Qt::Key_Escape);  // leaves full screen
    EXPECT_FALSE(window->property("fullScreenMode").toBool());
    EXPECT_TRUE(gridButton->isVisible());
    controller->selectTool("pen");
}

// Full screen from a button in the tool bar, and back out with the finger alone (no F11, no Escape)
TEST_F(MainWindowTest, fullScreenButtonAndBackByTouch) {
    window->setWidth(2000);  // room for the whole tool bar (it scrolls in a narrower window)
    wait(100);
    auto* button = findItem("fullScreenButton");
    ASSERT_NE(button, nullptr);
    EXPECT_TRUE(button->isVisible());
    ASSERT_LT(button->mapToScene(QPointF(button->width(), 0)).x(), window->width()) << "in sight";
    static QPointingDevice* finger = QTest::createTouchDevice();
    const auto tap = [&](QQuickItem* item) {
        const QPoint at = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
        QTest::touchEvent(window, finger).press(1, at);
        QTest::touchEvent(window, finger).release(1, at);
        wait(80);
    };
    tap(button);
    until([&] { return window->property("fullScreenMode").toBool(); });
    ASSERT_TRUE(window->property("fullScreenMode").toBool()) << "the button goes full screen";

    // The way back: the tool square, then "Leave full screen"
    auto* square = find<QQuickItem>("quickToolSquare");
    ASSERT_NE(square, nullptr);
    ASSERT_TRUE(square->isVisible());
    tap(square);
    QObject* tools = find("quickTools");
    ASSERT_TRUE(waitOpened(tools, true));
    EXPECT_FALSE(button->isVisible()) << "not twice: the tools offer \"Leave full screen\" right below";
    auto* leave = findItem("leaveFullScreenButton");
    if (!leave) {
        leave = find<QQuickItem>("leaveFullScreenButton");
    }
    ASSERT_NE(leave, nullptr);
    ASSERT_TRUE(leave->isVisible());
    tap(leave);
    until([&] { return !window->property("fullScreenMode").toBool(); });
    EXPECT_FALSE(window->property("fullScreenMode").toBool()) << "left full screen by touch";
    until([&] { return button->isVisible(); });
    EXPECT_TRUE(button->isVisible()) << "back in the tool bar";
}

TEST_F(MainWindowTest, leavingFullScreenGoesBackToTheWindowState) {
    // Maximized before: maximized after Escape (it came back as a small window)
    window->showMaximized();
    until([&] { return window->visibility() == QWindow::Maximized; });
    ASSERT_EQ(window->visibility(), QWindow::Maximized);
    window->setProperty("fullScreenMode", true);
    until([&] { return window->visibility() == QWindow::FullScreen; });
    ASSERT_EQ(window->visibility(), QWindow::FullScreen);
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return window->visibility() == QWindow::Maximized; });
    EXPECT_FALSE(window->property("fullScreenMode").toBool());
    EXPECT_EQ(window->visibility(), QWindow::Maximized) << "back to maximized";

    // A compositor that gives back the size from before it was maximized when full screen ends (KWin on Wayland
    // was seen doing so): the window asks for maximized once more
    window->setProperty("fullScreenMode", true);
    until([&] { return window->visibility() == QWindow::FullScreen; });
    QTest::keyClick(window, Qt::Key_Escape);
    window->showNormal();  // what the compositor did
    until([&] { return window->visibility() == QWindow::Maximized; });
    EXPECT_EQ(window->visibility(), QWindow::Maximized) << "maximized again after the compositor's normal size";

    // Not maximized before (made smaller by the user): the same size after
    window->showNormal();
    until([&] { return window->visibility() == QWindow::Windowed; });
    until([&] { return !window->property("leavingFullScreen").toBool(); }, 2000);
    ASSERT_EQ(window->property("windowedVisibility").toInt(), int(QWindow::Windowed)) << "the user's choice is kept";
    window->setProperty("fullScreenMode", true);
    until([&] { return window->visibility() == QWindow::FullScreen; });
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return window->visibility() == QWindow::Windowed; });
    EXPECT_EQ(window->visibility(), QWindow::Windowed) << "back to the window as it was";
}

TEST_F(MainWindowTest, contentsInTheSidebarAndTheOverview) {
    // A 6-page PDF with an outline: "Chapter 1" p.1 (with "Section 1.1" p.3), "Chapter 2" p.5
    QTemporaryDir tmp;
    const std::string pdf = tmp.filePath("book.pdf").toStdString();
    {
        cairo_surface_t* s = cairo_pdf_surface_create(pdf.c_str(), 300, 400);
        cairo_t* cr = cairo_create(s);
        for (int p = 0; p < 6; ++p) {
            cairo_show_page(cr);
        }
        const int ch1 = cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 1", "page=1", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_pdf_surface_add_outline(s, ch1, "Section 1.1", "page=3", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_pdf_surface_add_outline(s, CAIRO_PDF_OUTLINE_ROOT, "Chapter 2", "page=5", CAIRO_PDF_OUTLINE_FLAG_OPEN);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    }
    ASSERT_TRUE(controller->openPath(QString::fromStdString(pdf)));
    wait(100);

    // Sidebar: Contents
    click(findItem("sidebarContentsButton"));
    auto* outlineList = find<QQuickItem>("outlineList");
    ASSERT_NE(outlineList, nullptr);
    until([&] { return outlineList->isVisible() && outlineList->property("count").toInt() == 3; });
    EXPECT_TRUE(outlineList->isVisible());
    ASSERT_EQ(outlineList->property("count").toInt(), 3);
    until([&] { return itemAt(outlineList, 2) != nullptr; });
    click(itemAt(outlineList, 2));  // Chapter 2
    until([&] { return controller->pageNumber() == 5; });
    EXPECT_EQ(controller->pageNumber(), 5);

    // Overview: the pages of each heading; a page opens there. One button opens it, the one in the tool bar, and
    // it shows whether the overview is open.
    auto* contentsButton = find<QQuickItem>("contentsButton");
    ASSERT_NE(contentsButton, nullptr);
    EXPECT_FALSE(contentsButton->property("checked").toBool());
    int tocButtonsInTheSidebar = 0;
    for (auto* i: find<QQuickItem>("sidebar")->findChildren<QQuickItem*>()) {
        if (i->property("iconName").toString() == QStringLiteral("xqt-toc")) {
            ++tocButtonsInTheSidebar;
        }
    }
    EXPECT_EQ(tocButtonsInTheSidebar, 0) << "not a second button for it beside the pages";
    click(contentsButton);
    auto* overview = find<QQuickItem>("contentsOverview");
    ASSERT_NE(overview, nullptr);
    until([&] { return overview->isVisible(); });
    ASSERT_TRUE(overview->isVisible());
    EXPECT_TRUE(contentsButton->property("checked").toBool()) << "the button shows that it is open";
    auto* list = find<QQuickItem>("contentsList");
    until([&] { return itemAt(list, 1) != nullptr; });
    QQuickItem* section = itemAt(list, 1);  // Section 1.1: pages 3, 4
    ASSERT_NE(section, nullptr);
    QQuickItem* strip = nullptr;
    for (auto* c: section->findChildren<QQuickItem*>()) {
        if (c->objectName() == "contentsPages") {
            strip = c;
        }
    }
    ASSERT_NE(strip, nullptr);
    EXPECT_EQ(strip->property("count").toInt(), 2);
    until([&] { return itemAt(strip, 1) != nullptr; });
    QQuickItem* page4 = itemAt(strip, 1);
    ASSERT_NE(page4, nullptr);
    click(page4);
    until([&] { return !overview->isVisible() && controller->pageNumber() == 4; });
    EXPECT_FALSE(overview->isVisible());
    EXPECT_EQ(controller->pageNumber(), 4);
}

TEST_F(MainWindowTest, pageAndLayoutShortcutsInThePill) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int pages = controller->pageCount();

    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages + 1) << "Ctrl+N adds a page after the current one";

    // A tap on the layout button switches between one page and two side by side
    auto* layout = find<QQuickItem>("layoutButton");
    ASSERT_NE(layout, nullptr);
    EXPECT_FALSE(controller->pairedPages());
    click(layout);
    EXPECT_TRUE(controller->pairedPages());
    click(layout);
    EXPECT_FALSE(controller->pairedPages()) << "and back";

    // Fitting: the whole page is smaller than the width of one
    controller->fitWidth();
    wait(30);
    const int wide = controller->zoomPercent();
    controller->fitHeight();
    wait(30);
    EXPECT_LT(controller->zoomPercent(), wide) << "the height of a portrait page needs less zoom";
    controller->fitPage();
    wait(30);
    EXPECT_LE(controller->zoomPercent(), wide);
    // A quick tap with a finger must switch the layout, not open the menu
    auto* menuOnTouch = find<QObject>("layoutMenu");
    ASSERT_NE(menuOnTouch, nullptr);
    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint onLayout = layout->mapToScene(QPointF(layout->width() / 2, layout->height() / 2)).toPoint();
    const bool paired = controller->pairedPages();
    { auto down = QTest::touchEvent(window, finger); down.press(0, onLayout); }
    wait(30);
    { auto up = QTest::touchEvent(window, finger); up.release(0, onLayout); }
    wait(120);
    EXPECT_NE(controller->pairedPages(), paired) << "a tap switches the layout";
    EXPECT_FALSE(menuOnTouch->property("visible").toBool()) << "and opens no menu";
    click(layout);  // back

    // Right click does what press and hold does
    auto* layoutMenu = find<QObject>("layoutMenu");
    ASSERT_NE(layoutMenu, nullptr);
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      layout->mapToScene(QPointF(layout->width() / 2, layout->height() / 2)).toPoint());
    until([&] { return layoutMenu->property("visible").toBool(); });
    EXPECT_TRUE(layoutMenu->property("visible").toBool()) << "the layout menu on right click";
    QMetaObject::invokeMethod(layoutMenu, "close");
    until([&] { return !layoutMenu->property("visible").toBool(); });

    // Press and hold offers the other fits, and the menu opens at its button (not at some stale mouse position)
    auto* zoomButton = find<QQuickItem>("zoomButton");
    ASSERT_NE(zoomButton, nullptr);
    const QPoint at = zoomButton->mapToScene(QPointF(zoomButton->width() / 2, zoomButton->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(1000);  // (press and hold)
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
    auto* fitMenu = find<QObject>("fitMenu");
    ASSERT_NE(fitMenu, nullptr);
    until([&] { return fitMenu->property("visible").toBool(); });
    EXPECT_TRUE(fitMenu->property("visible").toBool());
    // At its button: the pill sits at the bottom, so the menu flips above it (never at the window corner)
    EXPECT_LT(fitMenu->property("y").toDouble(), 0);
    EXPECT_GT(fitMenu->property("y").toDouble(), -window->height());
}

TEST_F(MainWindowTest, theToolBarCanBePutAway) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* tools = find<QQuickItem>("sideTools");
    auto* square = find<QQuickItem>("quickToolSquare");  // the small tool square
    auto* pen = find<QQuickItem>("contentsButton");  // a tool bar button
    ASSERT_NE(pen, nullptr);
    EXPECT_TRUE(pen->isVisible());

    // The little tab at the end of the bar puts it away
    auto* toggle = find<QQuickItem>("toolbarToggle");
    auto* show = find<QQuickItem>("toolbarShow");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(show, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_FALSE(show->isVisible());
    click(toggle);
    until([&] { return controller->toolbarHidden(); });
    EXPECT_TRUE(controller->toolbarHidden());
    EXPECT_TRUE(show->isVisible()) << "a slim strip brings it back";
    EXPECT_FALSE(toggle->isVisible());
    click(show);
    until([&] { return !controller->toolbarHidden(); });
    EXPECT_FALSE(controller->toolbarHidden());

    controller->setToolbarHidden(true);
    wait(80);
    EXPECT_FALSE(find<QQuickItem>("sideTools")->isVisible()) << "no bar at the side either";
    ASSERT_NE(square, nullptr);
    EXPECT_FALSE(square->isVisible()) << "the tool square is for full screen only";

    controller->setToolbarHidden(false);
    wait(80);
    EXPECT_TRUE(pen->isVisible());
    EXPECT_FALSE(square->isVisible());
    (void)tools;
}

// Docked at a side, the little tab points towards the bar it puts away, and the strip that brings the bar back is at
// that side, pointing into the pages (where the bar will come from).
TEST_F(MainWindowTest, theToolBarTabAndStripFollowTheDockSide) {
    auto* toggle = find<QQuickItem>("toolbarToggle");
    auto* show = find<QQuickItem>("toolbarShow");
    ASSERT_NE(toggle, nullptr);
    ASSERT_NE(show, nullptr);
    // Where the chevron on it points, from the icon and its rotation (clockwise, y down)
    auto pointsTo = [](QQuickItem* item) -> std::string {
        QQuickItem* arrow = nullptr;
        for (QQuickItem* c: item->childItems()) {
            if (c->property("source").isValid()) {
                arrow = c;
            }
        }
        if (!arrow) {
            return "no arrow";
        }
        const QString source = arrow->property("source").toUrl().toString();
        QPointF d = source.contains("chevron-up")      ? QPointF(0, -1)
                    : source.contains("chevron-down")  ? QPointF(0, 1)
                    : source.contains("chevron-right") ? QPointF(1, 0)
                    : source.contains("chevron-left")  ? QPointF(-1, 0)
                                                       : QPointF();
        const double a = arrow->rotation() * M_PI / 180.0;
        const QPointF r(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
        if (r.y() < -0.5) return "up";
        if (r.y() > 0.5) return "down";
        if (r.x() < -0.5) return "left";
        if (r.x() > 0.5) return "right";
        return "nowhere";
    };
    QQuickItem* area = show->parentItem();  // the window below the tab strip
    ASSERT_NE(area, nullptr);
    const double w = area->width();
    const double h = area->height();
    struct Case {
        const char* position;
        const char* hideArrow;  // towards the bar
        const char* showArrow;  // from the bar into the pages
    };
    for (const Case c: {Case{"top", "up", "down"}, Case{"left", "left", "right"}, Case{"right", "right", "left"}}) {
        SCOPED_TRACE(c.position);
        controller->setToolbarHidden(false);
        controller->setToolbarPosition(c.position);
        wait(60);
        ASSERT_TRUE(toggle->isVisible());
        EXPECT_EQ(pointsTo(toggle), c.hideArrow) << "the tab points towards the bar it puts away";

        controller->setToolbarHidden(true);
        wait(60);
        ASSERT_TRUE(show->isVisible());
        EXPECT_EQ(pointsTo(show), c.showArrow) << "the strip points to where the bar comes in";
        const QRectF strip(show->mapToItem(area, QPointF(0, 0)), show->size());
        if (std::string(c.position) == "top") {
            EXPECT_NEAR(strip.top(), 0, 1) << "at the top edge";
            EXPECT_GT(strip.width(), strip.height()) << "lying along the top edge";
        } else {
            EXPECT_GT(strip.height(), strip.width()) << "standing along the side";
            EXPECT_GT(strip.top(), h / 4) << "about the middle of the side, not at the top";
            EXPECT_LT(strip.bottom(), h * 3 / 4);
            if (std::string(c.position) == "left") {
                EXPECT_NEAR(strip.left(), 0, 1) << "at the left edge";
            } else {
                EXPECT_NEAR(strip.right(), w, 1) << "at the right edge";
            }
        }
        click(show);
        until([&] { return !controller->toolbarHidden(); });
        EXPECT_FALSE(controller->toolbarHidden()) << "a tap on the strip brings the bar back";
    }
    controller->setToolbarPosition("top");
}

TEST_F(MainWindowTest, penPillWithoutAToolBar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* pill = find<QQuickItem>("penPill");
    ASSERT_NE(pill, nullptr);
    EXPECT_FALSE(pill->isVisible()) << "the tool bar is there, no pill";

    controller->setToolbarHidden(true);
    controller->selectTool("pen");
    wait(80);
    EXPECT_TRUE(pill->isVisible());

    // The colors of the pill
    // (items a Repeater made have no QObject parent: through the item tree)
    std::vector<QQuickItem*> colors;
    std::function<void(QQuickItem*)> collect = [&](QQuickItem* item) {
        for (QQuickItem* child: item->childItems()) {
            if (child->objectName() == "penPillColor") {
                colors.push_back(child);
            }
            collect(child);
        }
    };
    collect(pill);
    QQuickItem* second = colors.size() > 1 ? colors[1] : nullptr;
    ASSERT_NE(second, nullptr) << "three colors to start with";
    click(second);
    EXPECT_EQ(controller->color(), QColor(0xff, 0x00, 0x00)) << "red";

    // The width goes through the five of the tool bar, one per tap, and starts over after the fifth
    const int size = controller->size();
    click(findItem("penPillWidth"));
    EXPECT_EQ(controller->size(), size >= 5 ? 1 : size + 1);
    for (int i = 0; i < 4; ++i) {
        click(findItem("penPillWidth"));
    }
    EXPECT_EQ(controller->size(), size) << "five taps: round once";
    click(findItem("penPillTool"));
    EXPECT_EQ(controller->tool(), "highlighter");
    click(findItem("penPillTool"));
    EXPECT_EQ(controller->tool(), "pen");

    controller->setToolbarHidden(false);
    wait(80);
    EXPECT_FALSE(pill->isVisible());
}

TEST_F(MainWindowTest, ctrlShiftShortcutsWork) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int tabs = controller->tabCount();
    key(Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
    wait(50);
    EXPECT_EQ(controller->tabCount(), tabs + 1) << "Ctrl+Shift+N makes a document";

    auto* overview = find<QObject>("tabOverview");
    ASSERT_NE(overview, nullptr);
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    until([&] { return overview->property("visible").toBool(); });
    EXPECT_TRUE(overview->property("visible").toBool()) << "Ctrl+Shift+E shows all documents";
    QMetaObject::invokeMethod(overview, "close");
    until([&] { return !overview->property("visible").toBool(); });

    key(Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    wait(50);
    EXPECT_TRUE(controller->homeVisible()) << "Ctrl+Shift+L shows the library";
}

TEST_F(MainWindowTest, shortcutSheetAndCustomKeys) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* shortcuts = controller->shortcutsModel();
    ASSERT_NE(shortcuts, nullptr);

    // F1 shows them all
    key(Qt::Key_F1);
    auto* sheet = find<QObject>("shortcutSheet");
    ASSERT_NE(sheet, nullptr);
    until([&] { return sheet->property("visible").toBool(); });
    EXPECT_TRUE(sheet->property("visible").toBool());
    QMetaObject::invokeMethod(sheet, "close");
    until([&] { return !sheet->property("visible").toBool(); });  // (it is modal while it fades out)

    // Another key for "add a page", and it works
    bool changedKeys = false;
    QStringList keys;
    QMetaObject::invokeMethod(shortcuts, "setKeys", Q_RETURN_ARG(bool, changedKeys), Q_ARG(QString, "addPage"),
                              Q_ARG(QString, "Ctrl+Alt+P"));
    ASSERT_TRUE(changedKeys);
    QMetaObject::invokeMethod(shortcuts, "keys", Q_RETURN_ARG(QStringList, keys), Q_ARG(QString, "addPage"));
    EXPECT_EQ(keys, QStringList{"Ctrl+Alt+P"});
    wait(50);
    const int before = controller->pageCount();
    key(Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(controller->pageCount(), before + 1) << "the new keys add a page";
    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), before + 1) << "the old ones do not";

    // A conflict is reported, and the defaults come back
    QString conflict;
    QMetaObject::invokeMethod(shortcuts, "conflict", Q_RETURN_ARG(QString, conflict), Q_ARG(QString, "addPage"),
                              Q_ARG(QString, "Ctrl+S"));
    EXPECT_FALSE(conflict.isEmpty()) << "Ctrl+S saves";
    QMetaObject::invokeMethod(shortcuts, "resetAll");
    wait(50);
    key(Qt::Key_N, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), before + 2) << "Ctrl+N again";
}

TEST_F(MainWindowTest, backgroundOfExistingPagesAndTheInsertDialog) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(50);
    auto backgroundOf = [&](int page) {
        auto* s = controller->tabManager().currentSession();
        return s->getDocument()->getPage(static_cast<size_t>(page))->getBackgroundType().format;
    };
    ASSERT_TRUE(controller->pagesHavePdfBackground({0})) << "the fixture annotates a PDF";

    // The dialog warns before a PDF page is replaced
    auto* dialog = find<QObject>("backgroundDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{0})));
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("overPdf").toBool());
    auto* warning = find<QQuickItem>("pdfWarning");
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->isVisible());
    QMetaObject::invokeMethod(dialog, "reject");
    wait(50);
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Pdf) << "cancelling changes nothing";

    // Graph paper instead, and back with one undo
    const int graph = controller->settingsModel()->property("pageBackgroundFormats").toStringList().indexOf("graph");
    ASSERT_GE(graph, 0);
    EXPECT_TRUE(controller->changePageBackground({0}, graph));
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Graph);
    controller->undoPages();
    EXPECT_EQ(backgroundOf(0), PageTypeFormat::Pdf);

    // The insert dialog still opens from the page menu (it is asked for through the controller)
    auto* insert = find<QObject>("insertPagesDialog");
    ASSERT_NE(insert, nullptr);
    QMetaObject::invokeMethod(insert, "openAt", Q_ARG(QVariant, QVariant::fromValue(1)));
    until([&] { return insert->property("visible").toBool(); });
    EXPECT_TRUE(insert->property("visible").toBool());
    QMetaObject::invokeMethod(insert, "reject");
}

// Space for notes beside slides (qt/docs/note-space.md): the dialog from the page menu and for all pages
TEST_F(MainWindowTest, spaceForNotesFromThePageMenuAndForAllPages) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(80);
    auto* s = controller->tabManager().currentSession();
    auto pageOf = [&](size_t i) { return s->getDocument()->getPage(i); };
    const size_t pages = s->getDocument()->getPageCount();
    ASSERT_GE(pages, 2u);
    const double w = pageOf(0)->getWidth(), h = pageOf(0)->getHeight();

    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 0);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });
    auto* item = find<QQuickItem>("pageMenuNoteSpace");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled());
    click(item);
    auto* dialog = find<QObject>("noteSpaceDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(dialog->property("scope").toInt(), 0) << "this page";

    // The preset: half the width on the right; the preview shows the slide in two thirds of the page
    click(find<QQuickItem>("noteSpacePresetRight"));
    auto* slide = find<QQuickItem>("noteSpaceSlide");
    ASSERT_NE(slide, nullptr);
    EXPECT_NEAR(slide->width() / slide->parentItem()->width(), 2.0 / 3, 0.02);
    EXPECT_NEAR(slide->x(), 0, 0.5);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(400);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("noteSpaceApply"));
    until([&] { return !dialog->property("visible").toBool(); });
    EXPECT_EQ(pageOf(0)->getNoteSpace(), (NoteSpace{0, 0, std::round(w / 2), 0}));
    EXPECT_EQ(pageOf(0)->getWidth(), w + std::round(w / 2));
    EXPECT_TRUE(pageOf(1)->getNoteSpace().empty()) << "only this page";
    controller->undoPages();
    EXPECT_EQ(pageOf(0)->getWidth(), w) << "one step to undo";

    // All pages (the More menu opens it so): below each slide, as high as it; in cm the amounts are shown as such
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{0})),
                              Q_ARG(QVariant, true));
    until([&] { return dialog->property("opened").toBool(); });
    EXPECT_NE(dialog->property("scope").toInt(), 0) << "all pages";
    EXPECT_EQ(dialog->property("targets").toList().size(), static_cast<qsizetype>(pages));
    click(find<QQuickItem>("noteSpacePresetBelow"));
    click(find<QQuickItem>("noteSpaceCm"));
    auto* bottom = find<QQuickItem>("noteSpaceBottom");
    ASSERT_NE(bottom, nullptr);
    EXPECT_NEAR(bottom->property("value").toInt(), h * 25.4 / 72, 1) << "millimetres";
    click(find<QQuickItem>("noteSpaceApply"));
    until([&] { return !dialog->property("visible").toBool(); });
    for (size_t i = 0; i < pages; ++i) {
        EXPECT_NEAR(pageOf(i)->getNoteSpace().bottom, pageOf(i)->getHeight() / 2, 2) << i;
    }
    controller->undoPages();
    for (size_t i = 0; i < pages; ++i) {
        EXPECT_TRUE(pageOf(i)->getNoteSpace().empty()) << "all in one step: " << i;
    }
}

// Changing a page's size (qt/page-size-change): "Page size…" in the page menu opens the dialog for the page; A7 makes
// it a card (what no longer fits is counted in the dialog and kept), one undo step; then the selection and all pages
TEST_F(MainWindowTest, pageSizeFromThePageMenu) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(80);
    auto* s = controller->tabManager().currentSession();
    auto pageOf = [&](size_t i) { return s->getDocument()->getPage(i); };
    auto sizeIs = [&](size_t i, QSizeF size) {
        return std::abs(pageOf(i)->getWidth() - size.width()) < 0.01 &&
               std::abs(pageOf(i)->getHeight() - size.height()) < 0.01;
    };
    const size_t pages = s->getDocument()->getPageCount();
    ASSERT_GE(pages, 3u);
    const QSizeF a4(pageOf(0)->getWidth(), pageOf(0)->getHeight());
    std::vector<QSizeF> sizes;  // (the last page is a small one)
    for (size_t i = 0; i < pages; ++i) {
        sizes.emplace_back(pageOf(i)->getWidth(), pageOf(i)->getHeight());
    }
    const QStringList formats = controller->settingsModel()->property("paperFormats").toStringList();
    const int a4Index = formats.indexOf("A4"), a7 = formats.indexOf("A7");
    ASSERT_GE(a7, 0);
    ASSERT_TRUE(sizeIs(0, xqt::SettingsModel::paperSize(a4Index)));
    {
        // A stroke at the right of the page: beyond an A7 card
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(2);
        stroke->addPoint(Point(400, 100));
        stroke->addPoint(Point(450, 120));
        std::unique_lock lock(*s->getDocument());
        pageOf(0)->getSelectedLayer()->addElement(std::move(stroke));
    }

    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 0);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });
    auto* item = find<QQuickItem>("pageMenuPageSize");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isEnabled());
    click(item);
    auto* dialog = find<QObject>("pageSizeDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(dialog->property("scope").toInt(), 0) << "this page";
    EXPECT_EQ(dialog->property("paper").toInt(), a4Index) << "the page's own format";
    EXPECT_FALSE(dialog->property("landscape").toBool());

    auto* paper = find<QQuickItem>("pageSizePaper");
    ASSERT_NE(paper, nullptr);
    EXPECT_EQ(paper->property("count").toInt(), formats.size()) << "an A4 page: no \"Other\"";
    paper->setProperty("currentIndex", a7);
    QMetaObject::invokeMethod(paper, "activated", Q_ARG(int, a7));
    EXPECT_EQ(dialog->property("paper").toInt(), a7);
    auto* outside = find<QQuickItem>("pageSizeOutside");
    ASSERT_NE(outside, nullptr);
    until([&] { return outside->isVisible(); });
    EXPECT_TRUE(outside->isVisible()) << "the stroke would be outside the card";
    EXPECT_TRUE(outside->property("text").toString().startsWith("1 element")) << outside->property("text").toString().toStdString();
    EXPECT_FALSE(find<QQuickItem>("pageSizePdfNote")->isVisible());
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(400);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("pageSizeApply"));
    until([&] { return !dialog->property("visible").toBool(); });
    EXPECT_TRUE(sizeIs(0, xqt::SettingsModel::paperSize(a7))) << "an A7 card";
    EXPECT_TRUE(sizeIs(1, a4)) << "only this page";
    controller->undoPages();
    EXPECT_TRUE(sizeIs(0, a4)) << "one step to undo";

    // The selected pages (from the page menu on a selection), landscape
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{1, 2})));
    until([&] { return dialog->property("opened").toBool(); });
    EXPECT_EQ(dialog->property("scope").toInt(), 1) << "the selection";
    click(find<QQuickItem>("pageSizeLandscape"));
    EXPECT_TRUE(dialog->property("landscape").toBool());
    click(find<QQuickItem>("pageSizeApply"));
    until([&] { return !dialog->property("visible").toBool(); });
    EXPECT_TRUE(sizeIs(0, a4));
    EXPECT_TRUE(sizeIs(1, a4.transposed()));
    EXPECT_TRUE(sizeIs(2, a4.transposed()));
    controller->undoPages();

    // All pages: A5
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{0})));
    until([&] { return dialog->property("opened").toBool(); });
    dialog->setProperty("scope", 2);
    EXPECT_EQ(dialog->property("targets").toList().size(), static_cast<qsizetype>(pages));
    const int a5 = formats.indexOf("A5");
    paper->setProperty("currentIndex", a5);
    QMetaObject::invokeMethod(paper, "activated", Q_ARG(int, a5));
    click(find<QQuickItem>("pageSizeApply"));
    until([&] { return !dialog->property("visible").toBool(); });
    for (size_t i = 0; i < pages; ++i) {
        EXPECT_TRUE(sizeIs(i, xqt::SettingsModel::paperSize(a5))) << i;
    }
    controller->undoPages();
    for (size_t i = 0; i < pages; ++i) {
        EXPECT_TRUE(sizeIs(i, sizes[i])) << "all in one step: " << i;
    }

    // A size that is none of the formats: offered as "Other", as it is
    ASSERT_EQ(controller->applyPageSize({0}, 300, 500), 1);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant::fromValue(QVariantList{0})));
    until([&] { return dialog->property("opened").toBool(); });
    EXPECT_EQ(dialog->property("paper").toInt(), -1);
    EXPECT_EQ(paper->property("count").toInt(), formats.size() + 1);
    EXPECT_EQ(paper->property("currentIndex").toInt(), formats.size());
    EXPECT_EQ(paper->property("currentText").toString(), QString::fromUtf8("Other: 106 × 176 mm"));
    EXPECT_EQ(dialog->property("info").toMap().value("pages").toInt(), 0) << "its own size: nothing to change";
    QMetaObject::invokeMethod(dialog, "reject");
}

TEST_F(MainWindowTest, rightClickOffersPasteWhereItWasClicked) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* pill = find<QObject>("contextPill");
    ASSERT_NE(pill, nullptr);
    EXPECT_FALSE(pill->property("visible").toBool());

    // Something to paste
    QGuiApplication::clipboard()->setText("pasted here");
    auto* canvas = find<QQuickItem>("canvas");
    const QPoint at = canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() * 0.3)).toPoint();
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, at);
    until([&] { return pill->property("visible").toBool(); });
    ASSERT_TRUE(pill->property("visible").toBool()) << "the pill appears where it was clicked";

    auto elements = [&] {  // on any page and layer: the click may be on the first or the second page
        auto* s = controller->tabManager().currentSession();
        size_t count = 0;
        for (size_t p = 0; p < s->getDocument()->getPageCount(); ++p) {
            for (const Layer* layer: s->getDocument()->getPage(p)->getLayersView()) {
                count += layer->getElementsView().size();
            }
        }
        return count;
    };
    const size_t before = elements();
    QQuickItem* pasteButton = findItem("contextPaste");
    ASSERT_NE(pasteButton, nullptr);
    QMetaObject::invokeMethod(pasteButton, "clicked");  // (a click in the overlay is unreliable off screen)
    until([&] { return elements() > before; });
    EXPECT_EQ(elements(), before + 1) << "the text went onto the page";
    until([&] { return !pill->property("visible").toBool(); });
    EXPECT_FALSE(pill->property("visible").toBool());
}

TEST_F(MainWindowTest, printingAsksWhatAndWhichPages) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* dialog = find<QObject>("printDialog");
    ASSERT_NE(dialog, nullptr);

    // Ctrl+P asks first (printer and copies come from the system afterwards)
    key(Qt::Key_P, Qt::ControlModifier);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(dialog->property("range").toString(), QString()) << "everything by default";
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // Pages chosen in the overview come as a range
    QMetaObject::invokeMethod(dialog, "openFor",
                              Q_ARG(QVariant, QVariant::fromValue(QVariantList{0, 1, 2, 4})));
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("1-3,5"));
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // The whole way of the "Print" button in the page overview: the pages selected there must arrive as page
    // numbers in the dialog (they came through as "QVariant()1,QVariant()1" before).
    click(find<QQuickItem>("pageGridButton"));
    until([&] { return find<QQuickItem>("pageGrid")->isVisible(); });
    auto* pagesModel = controller->property("pages").value<QObject*>();
    ASSERT_NE(pagesModel, nullptr);
    const QList<int> firstAndThird{0, 2};
    QMetaObject::invokeMethod(pagesModel, "selectPages", Q_ARG(QList<int>, firstAndThird));
    auto* printSelected = find<QQuickItem>("printSelectedButton");
    ASSERT_NE(printSelected, nullptr);
    until([&] { return printSelected->isVisible(); });
    click(printSelected);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("1,3")) << "the pages that were chosen";
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });

    // Two pages next to each other: one range
    const QList<int> lastTwo{3, 4};
    QMetaObject::invokeMethod(pagesModel, "selectPages", Q_ARG(QList<int>, lastTwo));
    click(printSelected);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_EQ(dialog->property("range").toString(), QString("4-5"));
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });
}

TEST_F(MainWindowTest, layersInTheSidebar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    click(findItem("sidebarLayersButton"));
    auto* list = findItem("layerList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() > 0; });
    EXPECT_EQ(list->property("count").toInt(), 2) << "one layer and the background";

    click(findItem("addLayerButton"));
    until([&] { return list->property("count").toInt() == 3; });
    EXPECT_EQ(list->property("count").toInt(), 3);

    // The eye of the top layer hides it
    QQuickItem* first = itemAt(list, 0);
    ASSERT_NE(first, nullptr);
    QQuickItem* eye = nullptr;
    for (auto* i: first->findChildren<QQuickItem*>()) {
        if (i->objectName() == "layerVisibleButton") {
            eye = i;
        }
    }
    ASSERT_NE(eye, nullptr);
    click(eye);
    wait(50);
    // The list makes its delegates again when the layers change, so `first` must not be used any more
    first = itemAt(list, 0);
    ASSERT_NE(first, nullptr);
    EXPECT_FALSE(first->property("layerVisible").toBool());
    click(findItem("showAllLayersButton"));
    wait(50);
    EXPECT_TRUE(itemAt(list, 0)->property("layerVisible").toBool());
}

// The menu of a page: the everyday actions are icons, so it stays small (it used to be a list of sixteen lines).
TEST_F(MainWindowTest, thePageMenuIsSmallAndWorksByIcons) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(80);
    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 2);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    ASSERT_TRUE(menu->property("visible").toBool());
    const double height = menu->property("height").toDouble();
    EXPECT_GT(height, 100) << "it is there";
    EXPECT_LT(height, 320) << "and less than half of a line for every action (sixteen of them once)";
    EXPECT_LT(menu->property("width").toDouble(), 300) << "and not wide either";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(700);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // The icons do the work of the lines
    auto* paste = find<QQuickItem>("pageMenuPaste");
    ASSERT_NE(paste, nullptr);
    EXPECT_FALSE(paste->isEnabled()) << "nothing copied yet";
    click(find<QQuickItem>("pageMenuCopy"));
    until([&] { return !menu->property("visible").toBool(); });
    EXPECT_EQ(controller->copiedPages(), 1) << "the page was copied and the menu closed";

    // Moving the third page up puts it before the second one, as one page change to undo
    const int pageCount = controller->pageCount();
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    auto* up = find<QQuickItem>("pageMenuUp");
    ASSERT_NE(up, nullptr);
    EXPECT_TRUE(up->isEnabled());
    click(up);
    wait(80);
    EXPECT_TRUE(controller->canUndoPages()) << "the page moved";
    EXPECT_EQ(controller->pageCount(), pageCount) << "and none was lost";

    // On the first page there is nowhere to move it up to
    menu->setProperty("page", 0);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    EXPECT_FALSE(find<QQuickItem>("pageMenuUp")->isEnabled());
    QMetaObject::invokeMethod(menu, "close");
}

// Scrolling through the document moves the highlight of the current page in the sidebar; that must not make its
// thumbnails load again (they blinked: the thicker border of the current page changed the size they were drawn at).
TEST_F(MainWindowTest, sidebarThumbnailsStayWhenTheCurrentPageChanges) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(100);
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    until([&] { return itemAt(list, 1) != nullptr; });
    QQuickItem* second = itemAt(list, 1);
    ASSERT_NE(second, nullptr);
    QQuickItem* image = nullptr;
    for (auto* c: second->findChildren<QQuickItem*>()) {
        if (c->objectName() == "sidebarThumbnail") {
            image = c;
        }
    }
    ASSERT_NE(image, nullptr);
    QQuickItem* frame = image->parentItem();
    ASSERT_NE(frame, nullptr);
    const qreal dpr = window->effectiveDevicePixelRatio();
    const auto drawnAtFrameWidth = [&] {
        return image->property("sourceSize").toSize().width() == qRound(frame->width() * dpr);
    };
    EXPECT_TRUE(drawnAtFrameWidth());
    controller->goToPage(1);  // the second page becomes the current one: its frame gets the thick border
    until([&] { return image->width() < frame->width() - 4; });
    ASSERT_LT(image->width(), frame->width() - 4) << "the image is narrower now (the thick border)";
    EXPECT_TRUE(drawnAtFrameWidth()) << "but it is still drawn at the width of the frame: not drawn again";
    controller->goToPage(0);
    until([&] { return image->width() > frame->width() - 4; });
    EXPECT_TRUE(drawnAtFrameWidth());
}

// Flying through the sidebar shows every page at once as its sketch (drawn in advance); the sharp thumbnails are
// asked for when it slows down.
TEST_F(MainWindowTest, sidebarPagesShowTheirSketchAndGetSharpWhenTheListSlowsDown) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    auto parts = [](QQuickItem* entry) {
        std::pair<QQuickItem*, QQuickItem*> p{nullptr, nullptr};  // sketch, sharp
        for (auto* c: entry->findChildren<QQuickItem*>()) {
            if (c->objectName() == "pageSketch") {
                p.first = c;
            } else if (c->objectName() == "pageSharp") {
                p.second = c;
            }
        }
        return p;
    };
    until([&] { return itemAt(list, 0) != nullptr; });
    auto [sketch, sharp] = parts(itemAt(list, 0));
    ASSERT_NE(sketch, nullptr);
    ASSERT_NE(sharp, nullptr);
    until([&] { return sketch->property("source").toUrl().toString().startsWith("image://sketch/"); });
    EXPECT_TRUE(sketch->property("source").toUrl().toString().startsWith("image://sketch/"));
    until([&] { return sharp->property("source").toUrl().toString().startsWith("image://thumbnail/"); });
    EXPECT_TRUE(sharp->property("source").toUrl().toString().startsWith("image://thumbnail/"));

    // Racing to the end: the pages that come into view have their sketch only
    auto* race = list->property("race").value<QObject*>();
    ASSERT_NE(race, nullptr);
    // The test says when the list races, not the speed of this machine: the watch neither looks at the moves (on
    // its way to the end the list moves more than once, and a small move a moment after the jump is no race) nor
    // calms down 150 ms after the last one (a test slowed down by load takes longer)
    race->property("moves").value<QObject*>()->setProperty("enabled", false);
    race->property("calm").value<QObject*>()->setProperty("interval", 60000);
    race->setProperty("racing", true);
    const int last = controller->pageCount() - 1;
    QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, last), Q_ARG(int, 2 /* ListView.End */));
    until([&] { return itemAt(list, last) != nullptr; });
    auto [lastSketch, lastSharp] = parts(itemAt(list, last));
    ASSERT_NE(lastSketch, nullptr);
    EXPECT_TRUE(lastSketch->property("source").toUrl().toString().startsWith("image://sketch/"));
    EXPECT_TRUE(lastSharp->property("source").toUrl().isEmpty()) << "no sharp one while racing";
    race->setProperty("racing", false);
    EXPECT_TRUE(lastSharp->property("source").toUrl().toString().startsWith("image://thumbnail/")) << "slowed down";
    sketches.setDelays(400, 1500);
}

// A page that is not rendered yet shows its preview (drawn in advance) on the canvas, not a white page.
TEST_F(MainWindowTest, theCanvasShowsThePreviewUntilThePageIsRendered) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    xqt::CanvasView* view = controller->tabManager().currentView();
    const size_t target = 6;
    until([&] { return !view->preview(target).isNull(); }, 10000);
    ASSERT_FALSE(view->preview(target).isNull()) << "page 7 has its preview";
    auto* render = controller->context().getRenderService();
    // Nothing is being rendered (page 7 in advance, landing after its buffer is gone, would show the page itself)
    render->waitForIdle();
    render->blockRerenderZoom(std::chrono::milliseconds(60000));  // (as if rendering took long)
    view->getPage(target)->deleteViewBuffer();
    controller->goToPage(static_cast<int>(target));
    int shown = 0;
    until([&] {
        QMetaObject::invokeMethod(canvas, "previewsShown", Q_RETURN_ARG(int, shown));
        return shown > 0;
    }, 10000);
    EXPECT_GE(shown, 1) << "the preview instead of a white page";
    render->blockRerenderZoom(std::chrono::milliseconds(0));
    until([&] { return view->getPage(target)->bufferInfo().valid; }, 10000);
    ASSERT_TRUE(view->getPage(target)->bufferInfo().valid);
    until([&] {
        QMetaObject::invokeMethod(canvas, "previewsShown", Q_RETURN_ARG(int, shown));
        return shown == 0;
    }, 10000);
    EXPECT_EQ(shown, 0) << "rendered: the page itself";
    sketches.setDelays(400, 1500);
}

// Scrolling fast must not compose and upload whole pages in one frame (that froze the canvas): only the tiles in
// view, a few per frame, with the page's preview under what is not composed yet.
TEST_F(MainWindowTest, fastScrollingComposesFewTilesPerFrameAndShowsPreviews) {
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    until([&] { return sketches.idle(); }, 10000);
    xqt::CanvasView* view = controller->tabManager().currentView();
    xqt::CanvasMemory::instance().planNow();
    until([&] { return view->getPage(view->pageCount() - 1)->bufferInfo().valid; }, 10000);
    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    // Zoomed in, a page is many tiles (as on a big screen)
    view->getViewController().setZoom(3.0, QPointF(100, 100));
    wait(400);  // (renders wait until the zoom is stable)
    xqt::CanvasMemory::instance().planNow();
    until(
            [&] {
                for (size_t p = 0; p < view->pageCount(); ++p) {
                    if (view->getPage(p)->bufferInfo().zoom != 3.0) {
                        return false;
                    }
                }
                return true;
            },
            20000);
    ASSERT_DOUBLE_EQ(view->getPage(view->pageCount() - 1)->bufferInfo().zoom, 3.0);
    wait(100);
    QMetaObject::invokeMethod(canvas, "forgetTileCount");

    int previews = 0, tiles = 0;
    for (int page = 1; page < controller->pageCount(); ++page) {  // (as if the scroll bar were dragged)
        controller->goToPage(page);
        wait(16);
    }
    QMetaObject::invokeMethod(canvas, "mostTilesInAFrame", Q_RETURN_ARG(int, tiles));
    QMetaObject::invokeMethod(canvas, "framesWithPreviews", Q_RETURN_ARG(int, previews));
    EXPECT_LE(tiles, 64) << "never a whole page (about 120 tiles) at once";
    EXPECT_GT(previews, 0) << "the pages scrolled past show their preview while their tiles are missing";
    sketches.setDelays(400, 1500);
}

TEST_F(MainWindowTest, pagesAreAppendedFromTheSidebar) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int before = controller->pageCount();
    auto* sidebarList = find<QQuickItem>("sidebarList");
    ASSERT_NE(sidebarList, nullptr);
    QMetaObject::invokeMethod(sidebarList, "positionViewAtEnd");
    until([&] { return findItem("appendButton") != nullptr; });

    click(findItem("appendMore"));
    click(findItem("appendMore"));  // three pages
    click(findItem("appendButton"));
    wait(80);
    EXPECT_EQ(controller->pageCount(), before + 3);
    controller->undoPages();  // one step for all of them
    wait(50);
    EXPECT_EQ(controller->pageCount(), before);
}

TEST_F(MainWindowTest, aPressOnTheCanvasClosesAnOpenMenu) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* layoutButton = find<QQuickItem>("layoutButton");
    ASSERT_NE(layoutButton, nullptr);
    const QPoint at =
            layoutButton->mapToScene(QPointF(layoutButton->width() / 2, layoutButton->height() / 2)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(1000);  // press and hold
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
    auto* menu = find<QObject>("layoutMenu");
    ASSERT_NE(menu, nullptr);
    until([&] { return menu->property("visible").toBool(); });
    ASSERT_TRUE(menu->property("visible").toBool());

    auto elements = [&] {
        auto* s = controller->tabManager().currentSession();
        return s->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    const size_t before = elements();
    click(find<QQuickItem>("canvas"));  // a menu without a close button: touching the page closes it
    until([&] { return !menu->property("visible").toBool(); });
    EXPECT_FALSE(menu->property("visible").toBool());
    EXPECT_EQ(elements(), before) << "that press must not draw";
}

// The setsquare and the compass sit in the shapes menu (they are not a way of drawing, they lie on the page).
TEST_F(MainWindowTest, theToolBarHasAStickyNoteButton) {
    auto* button = find<QQuickItem>("stickyNoteButton");
    ASSERT_NE(button, nullptr);
    until([&] { return button->isVisible(); });
    ASSERT_TRUE(button->isVisible());
    EXPECT_FALSE(controller->noteSelected());
    click(button);
    until([&] { return controller->noteSelected(); });
    EXPECT_TRUE(controller->noteSelected()) << "a note is placed and selected";
}

TEST_F(MainWindowTest, theShapesMenuPlacesAStickyNoteWithItsPill) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    click(find<QQuickItem>("shapeButton"));
    auto* item = find<QQuickItem>("stickyNoteItem");
    ASSERT_NE(item, nullptr);
    until([&] { return item->isVisible(); });
    scrollIntoView(item);
    click(item);
    until([&] { return controller->noteSelected(); });
    ASSERT_TRUE(controller->noteSelected()) << "the new note is selected";
    EXPECT_EQ(controller->tool(), QStringLiteral("selectRect")) << "to be moved and resized right away";

    // Its pill: colors, cover, delete
    auto* pill = find<QQuickItem>("notePill");
    ASSERT_NE(pill, nullptr);
    until([&] { return pill->isVisible(); });
    EXPECT_TRUE(pill->isVisible());
    click(findItem("noteColor2"));
    EXPECT_EQ(controller->noteColor(), controller->stickyNoteColors()[2].value<QColor>());
    auto* cover = find<QQuickItem>("noteCoverButton");
    click(cover);
    EXPECT_TRUE(controller->noteCovers());
    EXPECT_TRUE(cover->property("checked").toBool());
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // The page pill hides and shows the notes of the page
    auto* pageNotes = find<QQuickItem>("pageNotesButton");
    ASSERT_NE(pageNotes, nullptr);
    until([&] { return pageNotes->isVisible(); });
    EXPECT_TRUE(pageNotes->isVisible()) << "a page with notes";
    click(pageNotes);
    EXPECT_TRUE(controller->pageNotesHidden());
    EXPECT_FALSE(controller->noteSelected()) << "a hidden note is not selected";
    click(pageNotes);
    EXPECT_FALSE(controller->pageNotesHidden());

    // Deleted (and back with undo)
    controller->undo();  // (the cover)
    controller->undo();  // (the color)
    controller->undo();  // (the note)
    EXPECT_FALSE(controller->pageHasNotes());
    until([&] { return !pageNotes->isVisible(); });
    EXPECT_FALSE(pageNotes->isVisible());
    controller->redo();
    EXPECT_TRUE(controller->pageHasNotes());
}

// qt/sticky-containers: the note's pill writes the note's Markdown text ("Text": on the page, with the formatting bar)
// and puts an image on the note ("Image…": the window's file dialog, then insertImage)
TEST_F(MainWindowTest, theNotePillWritesTheNotesTextAndPutsAnImageOnIt) {
    xqt::CanvasView* view = controller->tabManager().currentView();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(controller->insertStickyNote());
    auto* pill = find<QQuickItem>("notePill");
    ASSERT_NE(pill, nullptr);
    until([&] { return pill->isVisible(); });
    auto* textButton = find<QQuickItem>("noteTextButton");
    auto* imageButton = find<QQuickItem>("noteImageButton");
    ASSERT_NE(textButton, nullptr);
    ASSERT_NE(imageButton, nullptr);
    EXPECT_TRUE(imageButton->isEnabled());
    Layer* note = view->notes().selectedLayer();
    ASSERT_NE(note, nullptr);

    click(textButton);
    until([&] { return view->getMarkdownEditor() != nullptr; });
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    EXPECT_TRUE(controller->markdownOnPage()) << "written on the page, with the formatting bar";
    EXPECT_FALSE(controller->noteSelected());
    type("Hello");
    EXPECT_EQ(view->getMarkdownEditor()->text(), "Hello");
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        // (a longer text for the picture: it goes on below the note, which says so at its bottom right)
        type(" **world**, this is a note with a text that is longer than the note is high, so it goes on below it.\r"
             "- one\rtwo\rthree\rfour\rfive\rsix\rseven\reight\rnine\rten\releven\rtwelve\rthirteen\rfourteen\rfifteen");
        controller->endMarkdownOnPage();
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
        return;
    }
    controller->endMarkdownOnPage();
    ASSERT_NE(xqt::sticky::textOf(*note), nullptr);
    EXPECT_EQ(xqt::sticky::textOf(*note)->getText(), "Hello");
    EXPECT_TRUE(xqt::sticky::textOf(*note)->isMarkdown());

    // The image chosen in the dialog goes onto the selected note
    QTemporaryDir tmp;
    QImage picture(120, 90, QImage::Format_RGB32);
    picture.fill(Qt::darkBlue);
    ASSERT_TRUE(picture.save(tmp.filePath("picture.png")));
    view->notes().select(*view->getPage(0), note);
    const size_t before = note->getElementsView().size();
    ASSERT_TRUE(controller->insertImage(QUrl::fromLocalFile(tmp.filePath("picture.png"))));
    controller->clearSelection();
    ASSERT_EQ(note->getElementsView().size(), before + 1);
    EXPECT_EQ(note->getElementsView().back()->getType(), ELEMENT_IMAGE);
}

// Sticky notes on the clipboard (qt/sticky-clipboard): the pill's Copy and Cut, and a note moved from the first page
// to the third with Ctrl+X, going there, Ctrl+V.
TEST_F(MainWindowTest, aStickyNoteIsMovedToAnotherPageByCutAndPaste) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* s = controller->tabManager().currentSession();
    ASSERT_GE(s->getDocument()->getPageCount(), 3u);
    auto notesOn = [&](size_t page) {
        std::vector<Layer*> notes;
        std::shared_lock lock(*s->getDocument());
        for (Layer* l: s->getDocument()->getPage(page)->getLayers()) {
            if (xqt::sticky::isNote(*l)) {
                notes.push_back(l);
            }
        }
        return notes;
    };
    auto clipboardHasNote = [] {
        const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
        return mime && mime->hasFormat(xqt::sticky::CLIPBOARD_MIME);
    };
    ASSERT_TRUE(controller->insertStickyNote());
    until([&] { return controller->noteSelected(); });
    auto* pill = find<QQuickItem>("notePill");
    ASSERT_NE(pill, nullptr);
    until([&] { return pill->isVisible(); });
    ASSERT_EQ(notesOn(0).size(), 1u);
    const auto look = *xqt::sticky::lookOf(*notesOn(0).front());

    // The pill's Copy: the note onto the clipboard, still selected
    QGuiApplication::clipboard()->clear();
    click(findItem("noteCopy"));
    EXPECT_TRUE(clipboardHasNote());
    EXPECT_TRUE(controller->noteSelected());
    EXPECT_EQ(notesOn(0).size(), 1u);

    // Ctrl+X: off its page; on the third page Ctrl+V puts it there, at the same place, selected
    QGuiApplication::clipboard()->clear();
    key(Qt::Key_X, Qt::ControlModifier);
    EXPECT_TRUE(clipboardHasNote());
    EXPECT_EQ(notesOn(0).size(), 0u);
    EXPECT_FALSE(controller->noteSelected());
    controller->goToPage(2);
    until([&] { return s->getCurrentPageNo() == 2; });
    ASSERT_EQ(s->getCurrentPageNo(), 2u);
    key(Qt::Key_V, Qt::ControlModifier);
    ASSERT_EQ(notesOn(2).size(), 1u);
    EXPECT_EQ(*xqt::sticky::lookOf(*notesOn(2).front()), look);
    EXPECT_TRUE(controller->noteSelected());
    until([&] { return pill->isVisible(); });
    EXPECT_TRUE(pill->isVisible()) << "its pill, at the pasted note";

    // The pill's Cut: gone again, one undo step brings it back
    click(findItem("noteCut"));
    EXPECT_EQ(notesOn(2).size(), 0u);
    EXPECT_FALSE(controller->noteSelected());
    controller->undo();
    EXPECT_EQ(notesOn(2).size(), 1u);
    controller->undo();  // (the paste)
    EXPECT_EQ(notesOn(2).size(), 0u);
    controller->undo();  // (the cut)
    EXPECT_EQ(notesOn(0).size(), 1u);

    // In the page sidebar (a page clicked there has the keys): Ctrl+V pastes the copied note onto that page, and
    // with the note selected Ctrl+C / Ctrl+X take the note, not the pages
    auto* list = find<QQuickItem>("sidebarList");
    ASSERT_NE(list, nullptr);
    const int pages = controller->pageCount();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(itemAt(list, 1)));
    until([&] { return s->getCurrentPageNo() == 1; });
    ASSERT_TRUE(clipboardHasNote());
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(notesOn(1).size(), 1u) << "the note pasted onto the page clicked";
    EXPECT_EQ(controller->pageCount(), pages) << "no pages pasted";
    ASSERT_TRUE(controller->noteSelected());
    key(Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(controller->copiedPages(), 0) << "the note copied, not the page";
    key(Qt::Key_X, Qt::ControlModifier);
    EXPECT_EQ(notesOn(1).size(), 0u) << "the note cut";
    EXPECT_EQ(controller->pageCount(), pages) << "not the page";
}

TEST_F(MainWindowTest, theShapesMenuPutsTheSetsquareOnThePage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    click(find<QQuickItem>("shapeButton"));
    auto* item = find<QQuickItem>("setsquareItem");
    ASSERT_NE(item, nullptr);
    until([&] { return item->isVisible(); });
    EXPECT_FALSE(item->property("checked").toBool());
    // Tapped as a user does, so that the menu closes (where it is wider, it lies over the pill)
    scrollIntoView(item);
    click(item);
    auto* menu = item->property("menu").value<QObject*>();
    ASSERT_NE(menu, nullptr);
    until([&] { return !menu->property("visible").toBool(); });
    EXPECT_FALSE(menu->property("visible").toBool());
    until([&] { return controller->geometryTool() == QStringLiteral("setsquare"); });
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("setsquare"));
    EXPECT_TRUE(item->property("checked").toBool()) << "the entry shows that it is out";

    // Its pill: the tool can be put aside and brought back, and turned in steps of 15 degrees
    auto* pill = find<QQuickItem>("geometryPill");
    ASSERT_NE(pill, nullptr);
    until([&] { return pill->isVisible(); });
    EXPECT_TRUE(pill->isVisible()) << "the pill comes with the tool";
    auto* steps = find<QQuickItem>("geometrySteps");
    ASSERT_NE(steps, nullptr);
    click(steps);
    EXPECT_TRUE(controller->geometryAngleSteps());
    EXPECT_TRUE(steps->property("checked").toBool());
    // The button shows the setting also when it changes elsewhere (another tab has its own setsquare)
    controller->setGeometryAngleSteps(false);
    EXPECT_FALSE(steps->property("checked").toBool()) << "it follows the setting after it was tapped";
    click(steps);
    EXPECT_TRUE(controller->geometryAngleSteps());
    EXPECT_TRUE(steps->property("checked").toBool());
    const double wide = pill->width();
    click(find<QQuickItem>("geometryToggle"));
    EXPECT_TRUE(controller->geometryMinimized()) << "put aside";
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("setsquare")) << "but still switched on";
    EXPECT_TRUE(pill->isVisible()) << "the pill stays";
    until([&] { return pill->width() < wide; });
    EXPECT_LT(pill->width(), wide) << "small, only the icon";
    EXPECT_FALSE(steps->isVisible());
    click(find<QQuickItem>("geometryToggle"));
    EXPECT_FALSE(controller->geometryMinimized()) << "back again";
    EXPECT_TRUE(controller->geometryAngleSteps()) << "with its steps";
    click(find<QQuickItem>("geometryHold"));
    EXPECT_FALSE(controller->geometryHeldToStroke()) << "a page without ink: nothing to hold on to";

    // Its marks, every centimetre or every half
    auto elements = [&] {
        return controller->tabManager().currentSession()->getDocument()->getPage(0)->getSelectedLayer()
                ->getElements().size();
    };
    const size_t before = elements();
    click(find<QQuickItem>("geometryMarks"));
    EXPECT_EQ(elements(), before + 17) << "a mark every centimetre along the edge";
    click(find<QQuickItem>("geometryMarkSpacing"));
    EXPECT_DOUBLE_EQ(controller->geometryMarkSpacing(), 0.5);
    click(find<QQuickItem>("geometryMarks"));
    EXPECT_EQ(elements(), before + 17 + 33) << "and every half centimetre";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        auto* v = qobject_cast<xqt::CanvasView*>(find<QQuickItem>("canvas")->property("view").value<QObject*>());
        v->getViewController().panBy(QPointF(0, -420));
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Snapping to the grid: off at first, switched from the same menu
    auto* snap = find<QObject>("snapGridItem");
    ASSERT_NE(snap, nullptr);
    auto* settingsModel = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(snap->property("checked").toBool());
    QMetaObject::invokeMethod(snap, "toggle");
    QMetaObject::invokeMethod(snap, "triggered");
    EXPECT_TRUE(settingsModel->get("snapGrid").toBool()) << "switched on from the shapes menu";
    QMetaObject::invokeMethod(snap, "toggle");
    QMetaObject::invokeMethod(snap, "triggered");
    EXPECT_FALSE(settingsModel->get("snapGrid").toBool());

    // The compass instead, then away again
    auto* compass = find<QObject>("compassItem");
    ASSERT_NE(compass, nullptr);
    QMetaObject::invokeMethod(compass, "triggered");
    EXPECT_EQ(controller->geometryTool(), QStringLiteral("compass"));
    QMetaObject::invokeMethod(compass, "triggered");
    EXPECT_TRUE(controller->geometryTool().isEmpty());
    until([&] { return !pill->isVisible(); });
    EXPECT_FALSE(pill->isVisible()) << "the Shapes entry takes tool and pill away";
}

// Selected PDF text used to freeze the canvas: its knobs lie over the whole canvas, and after copying nothing
// told the UI that nothing is selected any more, so the (still visible) overlay swallowed every press.
TEST_F(MainWindowTest, theCanvasKeepsItsInputWhilePdfTextIsSelected) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    ASSERT_NE(canvasItem, nullptr);
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto* handles = find<QQuickItem>("pdfTextHandles");
    ASSERT_NE(handles, nullptr);
    EXPECT_TRUE(handles->property("inputTransparent").toBool()) << "only its knobs take presses";

    // Where a word of the PDF is (the search knows it)
    auto* session = controller->tabManager().currentSession();
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
    session->search().clear();
    const QPointF onWord =
            view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();
    const QPoint onWordInWindow = canvasItem->mapToScene(onWord).toPoint();

    QSignalSpy changed(controller.get(), &AppController::pdfTextSelectionChanged);
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y())) << "the word under the finger";
    until([&] { return handles->isVisible(); });
    ASSERT_TRUE(controller->pdfTextIsSelected());
    EXPECT_TRUE(handles->isVisible()) << "the knobs are shown";
    EXPECT_GE(changed.count(), 1) << "the UI is told that something is selected";

    // A press far from the text unselects it - the overlay of the knobs must not swallow that press
    const auto elements = [&] {
        return session->getDocument()->getPage(0)->getSelectedLayer()->getElements().size();
    };
    const size_t before = elements();
    controller->selectTool("pen");
    const QPoint far = onWordInWindow + QPoint(0, 260);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, far);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, far);
    wait(50);
    EXPECT_FALSE(controller->pdfTextIsSelected()) << "a press beside the text unselects it";
    until([&] { return !handles->isVisible(); });
    EXPECT_FALSE(handles->isVisible());
    EXPECT_EQ(elements(), before) << "that press only unselects, it does not draw";

    // Copying ends the selection as well, and the canvas draws again afterwards (it used to be frozen)
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y()));
    until([&] { return handles->isVisible(); });
    EXPECT_TRUE(controller->copyPdfText());
    EXPECT_FALSE(controller->pdfTextIsSelected());
    until([&] { return !handles->isVisible(); });
    EXPECT_FALSE(handles->isVisible()) << "nothing is selected: the knobs are gone";
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, far);
    QTest::mouseMove(window, far + QPoint(40, 20));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, far + QPoint(40, 20));
    wait(50);
    EXPECT_EQ(elements(), before + 1) << "the pen works again after copying the text";
}

// The knobs and the actions belong to the text, so they travel with the page. Once the text is scrolled out of
// sight the pill waits at the top edge and takes the reader back to it.
TEST_F(MainWindowTest, theSelectedPdfTextTakesItsHandlesAndActionsAlong) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto* bar = find<QQuickItem>("pdfTextBar");
    auto* handles = find<QQuickItem>("pdfTextHandles");
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(handles, nullptr);

    // Select a word of the PDF (the search knows where one is)
    auto* session = controller->tabManager().currentSession();
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
    session->search().clear();
    const QPointF onWord = view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y()));
    until([&] { return bar->isVisible(); });
    ASSERT_TRUE(bar->isVisible());
    EXPECT_FALSE(bar->property("away").toBool()) << "the text is in view";
    const double barY = bar->y();
    const double textY = controller->pdfSelectionBox().y();

    // Scrolling moves the text under the pill, so the pill goes along (a little, the word stays in view)
    const double pan = std::max(10.0, std::min(40.0, textY - 20));
    view->getViewController().panBy(QPointF(0, -pan));
    until([&] { return std::abs(bar->y() - (barY - pan)) <= 3; }, 5000);
    EXPECT_NEAR(controller->pdfSelectionBox().y(), textY - pan, 2);
    EXPECT_NEAR(bar->y(), barY - pan, 3) << "the actions stay at the text";
    EXPECT_FALSE(bar->property("away").toBool()) << "still in view";

    // Far away: the pill waits at the top of the canvas, the knobs are out of the way
    controller->jumpToPage(controller->pageCount() - 1);
    until([&] { return bar->property("away").toBool(); }, 5000);
    EXPECT_TRUE(bar->property("away").toBool()) << "the text is out of sight";
    EXPECT_TRUE(bar->isVisible()) << "but the actions stay, the text is still selected";
    EXPECT_LT(bar->y(), canvasItem->mapToScene(QPointF(0, 0)).y() + 40) << "at the top edge";
    auto* back = find<QQuickItem>("pdfBackToSelection");
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->isVisible()) << "with the way back to the text";
    for (auto* knob: handles->findChildren<QQuickItem*>()) {
        EXPECT_FALSE(knob->isVisible() && knob->property("startEnd").isValid())
                << "no knobs while the text is away";
    }

    // The way back brings it into view again (the pill's row, which the button joined, laid out first: it is
    // clicked where it is shown)
    nextFrame();
    click(back);
    until([&] { return !bar->property("away").toBool(); }, 5000);
    EXPECT_FALSE(bar->property("away").toBool());
    const QRectF box = controller->pdfSelectionBox();
    EXPECT_GE(box.y(), 0);
    EXPECT_LE(box.y(), canvasItem->height());
    EXPECT_TRUE(controller->pdfTextIsSelected()) << "and it is still the same selection";
}

// A long press on the text of a PDF selects its word; the actions for that text then offer paste as well (at the
// place pressed), so paste is always at hand with a long press, with the finger and with the pen.
TEST_F(MainWindowTest, aLongPressOnPdfTextAlsoOffersPaste) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto* bar = find<QQuickItem>("pdfTextBar");
    ASSERT_NE(bar, nullptr);
    auto* session = controller->tabManager().currentSession();
    QSignalSpy searched(&session->search(), &xqt::DocumentSearch::finished);
    session->search().setQuery("Test", false);
    ASSERT_TRUE(searched.wait(3000));
    const auto placed = xqt::test::placedHits(session->search());
    ASSERT_FALSE(placed.empty());
    const QRectF hit = placed.front().rect;
    session->search().clear();
    const QPointF onWord = view->pageViewRect(0).topLeft() + hit.center() * view->getViewController().zoom();
    const QPoint onWordInWindow = canvasItem->mapToScene(onWord).toPoint();
    const auto elements = [&] {
        size_t n = 0;
        for (const Layer* l: session->getDocument()->getPage(0)->getLayersView()) {
            n += l->getElementsView().size();
        }
        return n;
    };
    const size_t before = elements();

    // A finger held on the word
    QGuiApplication::clipboard()->setText("pasted on the text");
    static QPointingDevice* finger = QTest::createTouchDevice();
    QTest::touchEvent(window, finger).press(1, onWordInWindow);
    wait(800);
    QTest::touchEvent(window, finger).release(1, onWordInWindow);
    wait(50);
    until([&] { return bar->isVisible(); });
    ASSERT_TRUE(controller->pdfTextIsSelected()) << "the word is selected";
    ASSERT_TRUE(bar->isVisible()) << "with its actions";
    QQuickItem* paste = findItem("pdfTextPaste");
    ASSERT_NE(paste, nullptr);
    EXPECT_TRUE(paste->isVisible()) << "paste is among them";
    QMetaObject::invokeMethod(paste, "clicked");  // (a click in the overlay is unreliable off screen)
    until([&] { return elements() > before; });
    EXPECT_EQ(elements(), before + 1) << "the text went onto the page";
    const auto* pasted = session->getDocument()->getPage(0)->getSelectedLayer()->getElementsView().back();
    const auto& box = pasted->getBoundingBox();
    EXPECT_TRUE(QRectF(box.x, box.y, box.width, box.height).adjusted(-40, -40, 40, 40).contains(hit.center()))
            << "where the finger was";
    until([&] { return !controller->pdfTextIsSelected(); });
    EXPECT_FALSE(controller->pdfTextIsSelected()) << "pasting ends the text selection";
    controller->clearSelection();
    wait(50);

    // Nothing to paste: not offered
    QGuiApplication::clipboard()->clear();
    ASSERT_FALSE(controller->canPaste());
    QTest::touchEvent(window, finger).press(1, onWordInWindow);
    wait(800);
    QTest::touchEvent(window, finger).release(1, onWordInWindow);
    until([&] { return bar->isVisible(); });
    ASSERT_TRUE(bar->isVisible());
    EXPECT_FALSE(paste->isVisible()) << "an empty clipboard: no paste";
    controller->clearPdfTextSelection();
    until([&] { return !bar->isVisible(); });

    // A text selected by dragging over it with the text tool (not a long press): no paste either, there is no place
    // pressed to paste at
    QGuiApplication::clipboard()->setText("pasted again");
    ASSERT_TRUE(controller->selectPdfTextAt(onWord.x(), onWord.y()));
    until([&] { return bar->isVisible(); });
    EXPECT_FALSE(paste->isVisible());
    controller->clearPdfTextSelection();
    until([&] { return !bar->isVisible(); });

    // The pen held still on the word, with the pen in hand: the same, and the dot it began does not stay
    controller->selectTool("pen");
    static QPointingDevice pen("ui test pen", 3002, QInputDevice::DeviceType::Stylus,
                               QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
    const auto tablet = [&](QEvent::Type type, Qt::MouseButton button, Qt::MouseButtons buttons, double pressure) {
        const QPointF at(onWordInWindow);
        QTabletEvent e(type, &pen, at, window->mapToGlobal(at), pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier,
                       button, buttons);
        QCoreApplication::sendEvent(window, &e);
    };
    const size_t withPasted = elements();
    tablet(QEvent::TabletPress, Qt::LeftButton, Qt::LeftButton, 0.4);
    wait(800);
    tablet(QEvent::TabletRelease, Qt::LeftButton, Qt::NoButton, 0.0);
    until([&] { return bar->isVisible(); });
    ASSERT_TRUE(controller->pdfTextIsSelected()) << "the pen held on the word selects it";
    EXPECT_TRUE(paste->isVisible()) << "and paste is offered";
    EXPECT_EQ(elements(), withPasted) << "no dot left by the pen";
    controller->clearPdfTextSelection();
    wait(50);
    xqt::PenHover::instance().reset();
}

// The eraser button: a tap takes the eraser, tapped again it offers how the eraser erases.
TEST_F(MainWindowTest, theEraserButtonOffersHowItErases) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    auto* button = find<QQuickItem>("eraserButton");
    ASSERT_NE(button, nullptr);
    auto* menu = find<QObject>("eraserMenu");
    ASSERT_NE(menu, nullptr);
    click(button);
    EXPECT_EQ(controller->tool(), "eraser");
    EXPECT_FALSE(menu->property("visible").toBool()) << "the first tap only takes the eraser";
    click(button);
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool()) << "the second tap offers its kinds";
    auto* whole = find<QObject>("eraserWholeStrokes");
    ASSERT_NE(whole, nullptr);
    QMetaObject::invokeMethod(whole, "triggered");
    EXPECT_EQ(settings->get("eraserMode").toString(), QStringLiteral("deleteStroke"));
    EXPECT_TRUE(whole->property("checked").toBool());
    QMetaObject::invokeMethod(menu, "close");
    until([&] { return !menu->property("visible").toBool(); });

    // The right mouse button opens it as well, with another tool in hand
    controller->selectTool("pen");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool()) << "right click";
    QMetaObject::invokeMethod(find<QObject>("eraserStandard"), "triggered");
    EXPECT_EQ(settings->get("eraserMode").toString(), QStringLiteral("default"));
    EXPECT_EQ(controller->tool(), "eraser") << "choosing a kind takes the eraser";
}

// The tools on single keys, as in Xournal++: P pen, E eraser, H highlighter, T text, S select, L lasso, A hand. Not
// while typing into a field, and not in the overviews (they search what is typed).
TEST_F(MainWindowTest, singleKeysTakeTheTools) {
    const std::vector<std::pair<Qt::Key, QString>> keys{{Qt::Key_E, "eraser"},      {Qt::Key_H, "highlighter"},
                                                        {Qt::Key_T, "text"},        {Qt::Key_S, "selectRect"},
                                                        {Qt::Key_L, "selectRegion"}, {Qt::Key_A, "hand"},
                                                        {Qt::Key_P, "pen"}};
    for (const auto& [k, tool]: keys) {
        key(k);
        EXPECT_EQ(controller->tool(), tool) << "key " << QKeySequence(k).toString().toStdString();
    }

    // Typing into the search bar: the letters are text there
    key(Qt::Key_F, Qt::ControlModifier);
    auto* field = find<QQuickItem>("searchField");
    ASSERT_NE(field, nullptr);
    until([&] { return field->hasActiveFocus(); });
    key(Qt::Key_E);
    EXPECT_EQ(controller->tool(), "pen") << "E typed into the search, not the eraser";
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("e"));
    key(Qt::Key_Escape);

    // In the page grid they do nothing to the tools
    key(Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(find<QQuickItem>("pageGrid")->isVisible());
    key(Qt::Key_H);
    EXPECT_EQ(controller->tool(), "pen");
}

// Ctrl+Shift+F searches all open documents, Ctrl+Alt+F the library - from anywhere.
// While a search runs, every hit found rebuilds the list of pages with hits. Their pictures must stay (QML keeps
// them by their URL), not blink away and be asked for again.
TEST_F(MainWindowTest, tabOverviewHitPicturesStayWhenMoreHitsCome) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };
    type("page");
    key(Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1); }));
    if (!overview->property("extendedView").toBool()) {
        click(find<QQuickItem>("overviewExtendedButton"));
    }
    QQuickItem* picture = nullptr;
    until([&] { return (picture = findItem("overviewHitPagePicture")) != nullptr && picture->isVisible(); }, 5000);
    ASSERT_NE(picture, nullptr);
    const QString source = picture->property("source").toUrl().toString();
    EXPECT_TRUE(source.startsWith("image://thumbnail/")) << source.toStdString();
    EXPECT_TRUE(picture->property("cache").toBool()) << "kept by QML: every hit builds the list again";
    EXPECT_EQ(source.mid(QString("image://thumbnail/").size()).count('/'), 2)
            << "the page and its revision only, nothing that changes while the overview is open: " << source.toStdString();
    until([&] { return picture->property("status").toInt() == 1 /* Image.Ready */; });
    ASSERT_EQ(picture->property("status").toInt(), 1);

    // As if another hit came: the list is built again
    key(Qt::Key_Return);
    wait(300);
    picture = findItem("overviewHitPagePicture");
    ASSERT_NE(picture, nullptr);
    EXPECT_EQ(picture->property("source").toUrl().toString(), source) << "the same page: the same URL";
    EXPECT_EQ(picture->property("status").toInt(), 1) << "there at once, not loaded again";
}

// XQT_BENCH_SCROLL=1: what one scroll change costs (the visible pages, the current page, the models, the sidebar
// that follows) - with the page sidebar shown and without it.
TEST_F(MainWindowTest, benchScrollCost) {
    if (!qEnvironmentVariableIsSet("XQT_BENCH_SCROLL")) {
        GTEST_SKIP() << "set XQT_BENCH_SCROLL=1";
    }
    auto& sketches = xqt::PageSketches::instance();
    sketches.setDelays(0, 0);
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    controller->insertPages(11, 0, -1, false, 30);  // 41 pages
    until([&] { return sketches.idle(); }, 30000);
    xqt::CanvasView* view = controller->tabManager().currentView();
    view->setVisibilityDelay(0);  // (measure every change)
    xqt::CanvasMemory::instance().planNow();
    wait(2000);
    for (const bool sidebar: {true, false}) {
        window->setProperty("sidebarShown", sidebar);
        wait(300);
        const quint64 before = view->visibilityUpdates();
        QElapsedTimer t;
        t.start();
        qint64 worst = 0;
        double y = 0;
        for (int i = 0; i < 40; ++i) {  // (as if the scroll bar were dragged)
            y += 700;
            QElapsedTimer step;
            step.start();
            view->getViewController().setScrollPosition(QPointF(0, y));
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            worst = std::max(worst, step.elapsed());
        }
        const quint64 updates = view->visibilityUpdates() - before;
        std::cout << (sidebar ? "with the sidebar" : "without the sidebar") << ": " << t.elapsed() << " ms for "
                  << updates << " scroll changes (" << (updates ? t.elapsed() / static_cast<qint64>(updates) : 0)
                  << " ms each, worst step " << worst << " ms)\n";
    }
    sketches.setDelays(400, 1500);
}

// The overview of open documents searches them like the library: extended, with the pages that have hits (tap one
// to open the document there), and by name only.
TEST_F(MainWindowTest, tabOverviewHasTheExtendedAndTheNameSearch) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto running = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchRunningRole).toBool(); };

    type("page");
    key(Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return !running(0) && !running(1); }));
    const QVariantList pages = tabs->index(1, 0).data(xqt::TabManager::HitPagesRole).toList();
    ASSERT_FALSE(pages.isEmpty()) << "old.xopp has \"Page 2\"";
    EXPECT_EQ(pages.first().toMap().value("page").toInt(), 1);
    EXPECT_FALSE(pages.first().toMap().value("rects").toList().isEmpty()) << "with the places of the hits";

    // Extended: the pages with hits under the document; one of them opens it right there
    click(find<QQuickItem>("overviewExtendedButton"));
    EXPECT_TRUE(overview->property("extendedView").toBool());
    QQuickItem* hitPage = nullptr;
    until([&] { return (hitPage = findItem("overviewHitPage")) != nullptr && hitPage->isVisible(); });
    ASSERT_NE(hitPage, nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(hitPage);
    ASSERT_TRUE(waitOpened(overview, false));
    EXPECT_EQ(controller->currentTab(), 1);
    EXPECT_EQ(controller->pageNumber(), 2) << "at the page with the hit";

    // Names only: "pages" is in the name of the first document only, its text is not searched
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    click(find<QQuickItem>("overviewNamesOnly"));
    EXPECT_TRUE(overview->property("namesOnly").toBool());
    auto* field = find<QQuickItem>("overviewSearchField");
    field->setProperty("text", QStringLiteral("pages"));
    QMetaObject::invokeMethod(overview, "runSearch", Q_ARG(QVariant, QStringLiteral("pages")));
    EXPECT_EQ(tabs->index(1, 0).data(xqt::TabManager::SearchHitsRole).toInt(), 0) << "no text search";
    const auto nameMatches = [&](int row) {
        QVariant matches;
        QMetaObject::invokeMethod(overview, "nameMatches", Q_RETURN_ARG(QVariant, matches),
                                  Q_ARG(QVariant, tabs->index(row, 0).data(xqt::TabManager::TitleRole)));
        return matches.toBool();
    };
    EXPECT_TRUE(nameMatches(0)) << "pages.xopp";
    EXPECT_FALSE(nameMatches(1)) << "old.xopp";
    EXPECT_FALSE(overview->property("extendedView").toBool()) << "no pages to show for names";
}

// The overview's search has the library's "Fuzzy" toggle (the same setting): fzf's syntax over the titles and the text.
TEST_F(MainWindowTest, tabOverviewHasTheFuzzySearch) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));  // page i: "p<i+1>"
    ASSERT_TRUE(controller->openPath(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp")));  // "Xournal", "Page 2"
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setFuzzySearch(false);  // (the tests share the config folder)
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* toggle = find<QQuickItem>("overviewSearchFuzzy");
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->property("checked").toBool());
    click(toggle);
    EXPECT_TRUE(library->fuzzySearch()) << "the library's setting";

    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto* field = find<QQuickItem>("overviewSearchField");
    auto search = [&](const char* text) {
        field->setProperty("text", QString::fromUtf8(text));
        QMetaObject::invokeMethod(overview, "runSearch", Q_ARG(QVariant, QString::fromUtf8(text)));
        EXPECT_TRUE(waitFor([&] {
            return !tabs->index(0, 0).data(xqt::TabManager::SearchRunningRole).toBool() &&
                   !tabs->index(1, 0).data(xqt::TabManager::SearchRunningRole).toBool();
        }));
    };
    auto matches = [&](int row) { return tabs->index(row, 0).data(xqt::TabManager::SearchMatchRole).toBool(); };
    search("p1 | xournal");
    EXPECT_TRUE(matches(0));
    EXPECT_TRUE(matches(1));
    EXPECT_EQ(tabs->index(0, 0).data(xqt::TabManager::SearchHitsRole).toInt(), 3) << "p1, p10, p11";
    search("p1 !xournal");
    EXPECT_TRUE(matches(0));
    EXPECT_FALSE(matches(1));
    // A term in the title: every page with the other term; its letters are highlighted
    search("p1 pgs");
    EXPECT_TRUE(matches(0));
    EXPECT_FALSE(matches(1));
    const QVariantList pages = tabs->index(0, 0).data(xqt::TabManager::HitPagesRole).toList();
    ASSERT_EQ(pages.size(), 3);
    const QString title = tabs->index(0, 0).data(xqt::TabManager::TitleRole).toString();
    QVariantMap name;
    QMetaObject::invokeMethod(controller.get(), "fuzzyName", Q_RETURN_ARG(QVariantMap, name),
                              Q_ARG(QString, QStringLiteral("p1 pgs")), Q_ARG(QString, title));
    EXPECT_FALSE(name.value("match").toBool()) << "p1 is not in the title " << title.toStdString();
    EXPECT_FALSE(name.value("marks").toList().isEmpty()) << "pgs is";
    QQuickItem* shown = nullptr;
    until([&] {
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (i->objectName() == "overviewTitle" && i->property("text").toString().contains("<font")) {
                shown = i;
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(window->contentItem());
        return shown != nullptr;
    });
    EXPECT_NE(shown, nullptr) << "the title with its matched letters";

    // Not valid: a hint, and the plain text
    auto* hint = find<QQuickItem>("overviewSyntaxHint");
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isVisible());
    search("(p1");
    until([&] { return hint->isVisible(); });
    EXPECT_TRUE(hint->isVisible());
    EXPECT_FALSE(matches(0));

    click(toggle);
    EXPECT_FALSE(library->fuzzySearch());
    search("p1 | xournal");
    EXPECT_FALSE(matches(0)) << "plain: that text is nowhere";
    EXPECT_FALSE(hint->isVisible());
}

TEST_F(MainWindowTest, searchShortcutsForAllDocumentsAndTheLibrary) {
    controller->newDocument();
    QObject* overview = find("tabOverview");
    auto* overviewSearch = find<QQuickItem>("overviewSearchField");
    ASSERT_NE(overviewSearch, nullptr);
    key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true)) << "the overview of all documents opens";
    until([&] { return overviewSearch->hasActiveFocus(); });
    EXPECT_TRUE(overviewSearch->hasActiveFocus()) << "with the cursor in its search";

    key(Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_TRUE(waitOpened(overview, false)) << "the library search closes the overview";
    auto* librarySearch = find<QQuickItem>("librarySearchField");
    ASSERT_NE(librarySearch, nullptr);
    until([&] { return librarySearch->hasActiveFocus(); });
    EXPECT_TRUE(controller->homeVisible()) << "the library is shown";
    EXPECT_TRUE(librarySearch->hasActiveFocus()) << "with the cursor in its search";

    // and back into a document the other shortcut works as well
    key(Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    EXPECT_FALSE(controller->homeVisible());
}

// One undo stack, as in upstream: Ctrl+N adds a page, Ctrl+Z on the page (or the undo button of the pill) takes it
// away again, and a notice says what was undone - the page may be out of view.
TEST_F(MainWindowTest, addingAPageIsUndoneLikeEverythingElse) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int pages = controller->pageCount();
    auto* snackbar = find<QQuickItem>("snackbar");
    auto* snackbarText = findItem("snackbarText");
    ASSERT_NE(snackbar, nullptr);
    ASSERT_NE(snackbarText, nullptr);

    key(Qt::Key_N, Qt::ControlModifier);
    ASSERT_EQ(controller->pageCount(), pages + 1) << "Ctrl+N adds a page";
    EXPECT_TRUE(controller->canUndo()) << "the same undo as for writing";

    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages) << "Ctrl+Z takes it away";
    until([&] { return snackbar->isVisible(); });
    EXPECT_TRUE(snackbar->isVisible());
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith(QStringLiteral("Undone:")))
            << snackbarText->property("text").toString().toStdString();

    key(Qt::Key_Y, Qt::ControlModifier);
    EXPECT_EQ(controller->pageCount(), pages + 1) << "and redo brings it back";
    until([&] { return snackbarText->property("text").toString().startsWith(QStringLiteral("Redone:")); });
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith(QStringLiteral("Redone:")));

    // The undo button of the pill as well
    click(find<QQuickItem>("undoButton"));
    EXPECT_EQ(controller->pageCount(), pages);
}

// The overview of open documents shows the stored preview of a saved document that is on its title page (nothing to
// draw), else the page it is on. The title page is chosen in the menu of a page.
TEST_F(MainWindowTest, theOverviewShowsTheStoredPreviewOnTheTitlePage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto picture = [&] { return tabs->index(controller->currentTab(), 0).data(xqt::TabManager::ThumbnailRole).toString(); };
    EXPECT_EQ(controller->titlePage(), 0);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on page 1, saved: the stored preview";

    controller->goToPage(2);
    wait(50);
    EXPECT_TRUE(picture().startsWith("image://thumbnail/")) << "on another page: that page";

    // Page 3 becomes the title page through the menu of the page
    auto* menu = find<QObject>("pageMenu");
    ASSERT_NE(menu, nullptr);
    menu->setProperty("page", 2);
    QMetaObject::invokeMethod(menu, "open");
    until([&] { return menu->property("opened").toBool(); });  // (after its opening animation: then it stands still)
    auto* item = find<QQuickItem>("titlePageItem");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->isVisible());
    click(item);
    EXPECT_EQ(controller->titlePage(), 2);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its title page now";

    controller->setTitlePage(0);  // (kept in the test's own cache, but tidy anyway)
}

// "Open where I left off" (off by default): a document opens at the page it was left at.
TEST_F(MainWindowTest, documentsOpenWhereTheyWereLeftOffIfWanted) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(settings->get("resumeAtLastPage").toBool()) << "off by default";
    const QString file = fixturePath(u8"load/pages.xopp");
    ASSERT_TRUE(controller->openPath(file));
    controller->goToPage(5);
    wait(50);
    controller->closeTab(controller->currentTab());

    ASSERT_TRUE(controller->openPath(file));
    EXPECT_EQ(controller->pageNumber(), 1) << "off: at the first page";
    controller->goToPage(5);
    controller->closeTab(controller->currentTab());

    // The switch on the library
    controller->setHomeVisible(true);
    auto* toggle = find<QQuickItem>("resumeSwitch");
    ASSERT_NE(toggle, nullptr);
    until([&] { return toggle->isVisible(); });
    click(toggle);
    EXPECT_TRUE(settings->get("resumeAtLastPage").toBool());
    ASSERT_TRUE(controller->openPath(file));
    EXPECT_EQ(controller->pageNumber(), 6) << "on: where it was left";
    settings->set("resumeAtLastPage", false);
}

// A PDF opened as it is (no .xopp beside it) gets all of it too: the stored preview in the overview, a title page,
// and it opens again where it was left - also when it was closed without saving.
TEST_F(MainWindowTest, aPdfWithoutXoppHasTitleAndLastPageToo) {
    // (a copy under an ordinary name: "<name>.xopp.bg.pdf" is the attached PDF of a .xopp, not a document)
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    ASSERT_TRUE(QFile::copy(fixturePath(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), pdf));
    ASSERT_TRUE(controller->openPath(pdf));
    wait(80);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    auto picture = [&] { return tabs->index(controller->currentTab(), 0).data(xqt::TabManager::ThumbnailRole).toString(); };
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its first page: the stored preview";
    EXPECT_FALSE(controller->tabManager().currentSession()->hasFilePath()) << "(no .xopp: a PDF as it is)";
    EXPECT_EQ(controller->titlePage(), 0) << "it has a title page";

    ASSERT_TRUE(controller->setTitlePage(1));
    controller->goToPage(1);
    wait(50);
    EXPECT_TRUE(picture().startsWith("image://preview/")) << "on its (new) title page";

    // Left on the second page; closed without saving; opened again with "Last page" on
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    settings->set("resumeAtLastPage", true);
    controller->closeTab(controller->currentTab());
    ASSERT_TRUE(controller->openPath(pdf));
    EXPECT_EQ(controller->pageNumber(), 2) << "where it was left";
    settings->set("resumeAtLastPage", false);
    controller->setTitlePage(0);
}

TEST_F(MainWindowTest, pageGridButtonIsInTheZoomPill) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    auto* button = find<QQuickItem>("pageGridButton");
    ASSERT_NE(button, nullptr);
    auto* canvas = find<QQuickItem>("canvas");
    const QPointF p = button->mapToScene(QPointF(0, 0));
    EXPECT_GT(p.y(), canvas->mapToScene(QPointF(0, canvas->height() / 2)).y()) << "at the bottom, over the canvas";
    click(button);
    EXPECT_TRUE(find<QQuickItem>("pageGrid")->isVisible());
}

TEST_F(HomeScreenTest, movesToAnotherLibraryAndWarnsAboutDownloads) {
    // A Downloads folder of our own (XDG user dirs in the tests' config folder)
    QTemporaryDir dl;
    const fs::path downloads = fs::path(dl.path().toStdString());
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    fs::create_directories(config.toStdString());
    {
        QFile dirs(config + "/user-dirs.dirs");
        ASSERT_TRUE(dirs.open(QIODevice::WriteOnly));
        dirs.write(("XDG_DOWNLOAD_DIR=\"" + downloads.string() + "\"\n").c_str());
    }
    wait(50);
    // Menu of "notes" → Move to… → the Downloads library → its top folder
    const int row = rowOf("notes.xopp");
    ASSERT_GE(row, 0);
    QQuickItem* menuButton = nullptr;
    for (auto* c: card(row)->findChildren<QQuickItem*>()) {
        if (c->objectName() == "cardMenuButton") {
            menuButton = c;
        }
    }
    click(menuButton);
    QObject* menu = find("homeItemMenu");
    ASSERT_TRUE(waitOpened(menu, true));
    QQuickItem* moveItem = nullptr;
    for (auto* c: menu->findChildren<QQuickItem*>()) {
        if (c->objectName() == "moveToItem") {
            moveItem = c;
        }
    }
    click(moveItem);
    QObject* dialog = find("transferDialog");
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* box = find<QQuickItem>("transferLibraryBox");
    ASSERT_NE(box, nullptr);
    const QVariantList libs = box->property("model").toList();
    int downloadsIndex = -1;
    for (int i = 0; i < libs.size(); ++i) {
        if (libs[i].toMap().value("downloads").toBool()) {
            downloadsIndex = i;
        }
    }
    ASSERT_GE(downloadsIndex, 0) << "the Downloads folder is offered";
    box->setProperty("currentIndex", downloadsIndex);
    QMetaObject::invokeMethod(box, "activated", Q_ARG(int, downloadsIndex));
    wait(50);
    auto* folders = find<QQuickItem>("transferFolders");
    ASSERT_GE(folders->property("count").toInt(), 1);
    click(itemAt(folders, 0));

    // Into Downloads: asked first
    QObject* warning = find("temporaryImportDialog");
    ASSERT_TRUE(waitOpened(warning, true));
    EXPECT_TRUE(fs::exists(root / "notes.xopp")) << "not before the answer";
    QMetaObject::invokeMethod(warning, "accept");
    wait(100);
    EXPECT_TRUE(fs::exists(downloads / "notes.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    QFile::remove(config + "/user-dirs.dirs");
}

// Markdown: written beside the page into a box (a text in the layer "Markdown"), opened again with the text tool.
TEST_F(MainWindowTest, theWritingButtonWritesMarkdownOnThePageItsSourceIsInItsMenu) {
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* button = find<QQuickItem>("textModeButton");
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(button, nullptr);
    xqt::CanvasView* view = controller->tabManager().currentView();
    ASSERT_NE(view, nullptr);

    EXPECT_EQ(find<QObject>("textModeItem"), nullptr) << "the text mode is deprecated: not offered any more";
    EXPECT_TRUE(button->property("markdownMode").toBool());
    // Markdown chosen in the button's menu: written on the page, formatted while typing (not beside it)
    QMetaObject::invokeMethod(find<QObject>("markdownItem"), "triggered");
    until([&] { return view->getMarkdownEditor() != nullptr; });
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    EXPECT_FALSE(panel->isVisible());
    EXPECT_TRUE(controller->markdownOnPage());
    EXPECT_TRUE(button->property("checked").toBool());
    type("# Title");
    EXPECT_EQ(view->getMarkdownEditor()->text(), "# Title");

    // The button again: done; once more (it remembers Markdown): on the page again, the cursor after the text
    click(button);
    until([&] { return view->getMarkdownEditor() == nullptr; });
    EXPECT_FALSE(controller->markdownOnPage());
    EXPECT_FALSE(button->property("checked").toBool());
    click(button);
    until([&] { return view->getMarkdownEditor() != nullptr; });
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    EXPECT_FALSE(panel->isVisible());
    type(" two");
    EXPECT_EQ(view->getMarkdownEditor()->text(), "# Title two") << "the cursor at the end of the page's text";

    // The source beside the page: the menu's other entry takes the text being written along
    QMetaObject::invokeMethod(find<QObject>("markdownSourceItem"), "triggered");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(view->getMarkdownEditor(), nullptr);
    EXPECT_EQ(find<QQuickItem>("markdownArea")->property("text").toString(), QStringLiteral("# Title two"));
}

TEST_F(MainWindowTest, markdownBoxIsWrittenAndOpenedAgainWithTheTextTool) {
    controller->setMarkdownInPanel(true);  // (the text tool opens it beside the page)
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    auto* markdownItem = find<QObject>("markdownSourceItem");  // in the menu of the writing button: beside the page
    ASSERT_NE(markdownItem, nullptr);
    QMetaObject::invokeMethod(markdownItem, "triggered");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_TRUE(controller->markdownActive());
    auto* area = find<QQuickItem>("markdownArea");
    ASSERT_TRUE(area->hasActiveFocus());

    type("# Notes");
    key(Qt::Key_Return);
    type("- one");
    key(Qt::Key_Return);  // the list goes on
    type("two");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // an empty item ends the list
    type("Some ");
    key(Qt::Key_B, Qt::ControlModifier);
    type("bold");
    wait(300);

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    const std::string source = "# Notes\n- one\n- two\n\nSome **bold**";
    EXPECT_EQ(box->getText(), source);
    EXPECT_TRUE(controller->modified());
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    EXPECT_FALSE(panel->isVisible());
    EXPECT_FALSE(controller->markdownActive());
    EXPECT_TRUE(find<QQuickItem>("textModeButton")->property("markdownMode").toBool()) << "the button's mode now";

    // The text tool on the box opens it again (not the text of the source in a text box)
    controller->selectTool("text");
    wait(200);  // (the zoom from before the panel comes back)
    QSignalSpy requested(controller.get(), &AppController::markdownRequested);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    const auto r = xqt::md::boxRect(*xqt::md::boxOf(*layer));
    view->getViewController().scrollToPageRect(0, QRectF(r.x, r.y, r.width, r.height));
    wait(100);
    const QPointF onBox = view->pageViewRect(0).topLeft() +
                          QPointF(r.x + 20, r.y + r.height / 2) * view->getViewController().zoom();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, canvasItem->mapToScene(onBox).toPoint());
    until([&] { return panel->isVisible(); });
    EXPECT_EQ(requested.count(), 1) << "tool " << controller->tool().toStdString() << " at " << onBox.x() << ","
                                    << onBox.y() << " box " << r.x << "," << r.y << " " << r.width << "x" << r.height;
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(area->property("text").toString().toStdString(), source);
    click(find<QQuickItem>("markdownCancel"));
    EXPECT_EQ(xqt::md::boxOf(*layer)->getText(), source);

    // One undo step for the whole text
    controller->undo();
    EXPECT_EQ(xqt::md::boxOf(*layer), nullptr);
}

// Formulas in the Markdown text ($…$, $$…$$) are drawn; one that cannot be drawn is its source in red, and resting
// the mouse on it says why.
TEST_F(MainWindowTest, markdownFormulasAndTheErrorOfOneOnHover) {
    controller->setMarkdownInPanel(true);
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(find<QObject>("markdownSourceItem"), "triggered");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    find<QQuickItem>("markdownArea")->setProperty(
            "text", QStringLiteral("Energy $E = mc^2$ and $\\frac{a}{b}$.\n\n$$\n\\sum_{k=1}^n k = \\frac{n(n+1)}{2}\n$$"
                                   "\n\nA broken one: $\\hline$ here.\n"));
    wait(300);
    click(find<QQuickItem>("markdownDone"));
    ASSERT_FALSE(panel->isVisible());

    auto* session = controller->tabManager().currentSession();
    Layer* layer = xqt::md::markdownLayer(session->getDocument()->getPage(0));
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    // Where the broken formula is drawn (page coordinates)
    std::optional<xqt::md::Rect> broken;
    int drawn = 0;
    for (const auto& it: xqt::md::cachedLayout(box->getText(), xqt::md::styleOf(*box)).items) {
        for (const auto& m: it.maths) {
            if (m.error.empty()) {
                ++drawn;
            } else {
                broken = xqt::md::textRects(it, m.start, m.start + m.length).at(0);
            }
        }
    }
    EXPECT_EQ(drawn, 3);
    ASSERT_TRUE(broken.has_value());
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    const auto& shift = box->getTransformation().shift;
    view->getViewController().scrollToPageRect(0, QRectF(shift.x, shift.y, 300, 200));
    wait(200);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    const QPointF onFormula =
            view->pageViewRect(0).topLeft() +
            QPointF(shift.x + broken->x + broken->width / 2, shift.y + broken->y + broken->height / 2) *
                    view->getViewController().zoom();
    auto* tip = find<QObject>("mathErrorTip");
    ASSERT_NE(tip, nullptr);
    EXPECT_FALSE(tip->property("visible").toBool());
    QTest::mouseMove(window, canvasItem->mapToScene(onFormula).toPoint());
    until([&] { return tip->property("visible").toBool(); });
    EXPECT_TRUE(tip->property("visible").toBool());
    const QString tipText = tip->property("text").toString();
    EXPECT_TRUE(tipText.contains(QStringLiteral("hline"))) << tipText.toStdString();
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(500);
        window->grabWindow().save(
                qEnvironmentVariable("XQT_TEST_SHOT").replace(QStringLiteral(".png"), QStringLiteral("-tip.png")));
    }
    // The mouse goes on: the tip goes
    QTest::mouseMove(window, canvasItem->mapToScene(onFormula + QPointF(0, 200)).toPoint());
    until([&] { return !tip->property("visible").toBool(); });
    EXPECT_FALSE(tip->property("visible").toBool());
}

// Markdown text boxes: the text tool with "Markdown" places them anywhere; edited on the page (the source is shown
// while editing), drawn formatted.
TEST_F(MainWindowTest, markdownTextBoxesAnywhereWithTheTextTool) {
    controller->setMarkdownInPanel(false);  // (written on the page)
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(200, 300, 200, 300));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(200, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr) << "written on the page";
    type("Some **bold**");
    key(Qt::Key_Escape);
    EXPECT_EQ(view->getMarkdownEditor(), nullptr);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxAt(*layer, 205, 300);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), "Some **bold**");
    EXPECT_EQ(box->getFontSize(), 10) << "the size of Markdown text";
    EXPECT_GT(box->getWrap(), 100) << "as wide as there is room";
    EXPECT_NE(page->getSelectedLayer(), layer) << "the pen still writes into its layer";

    // A tap on it edits it on the page again
    QSignalSpy requested(controller.get(), &AppController::markdownRequested);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(210, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    EXPECT_EQ(view->getMarkdownEditor()->text(), "Some **bold**");
    EXPECT_EQ(requested.count(), 0);
    key(Qt::Key_Escape);

    // Markdown off: ordinary texts again
    controller->setTextMarkdown(false);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(200, 500));
    wait(50);
    ASSERT_NE(view->getTextEditor(), nullptr) << "an ordinary text box";
    EXPECT_FALSE(view->getTextEditor()->isMarkdown());
    type("plain");
    key(Qt::Key_Escape);
    EXPECT_EQ(layer->getElements().size(), 1u);
    bool plain = false;
    for (const auto& e: page->getSelectedLayer()->getElements()) {
        plain = plain || (e->getType() == ELEMENT_TEXT && static_cast<const Text*>(e.get())->getText() == "plain");
    }
    EXPECT_TRUE(plain);
}

// Markdown text boxes are selected (rectangle, lasso, tap) and moved like other elements: in their layer
// "Markdown", which is the selected layer only while they are selected.
TEST_F(MainWindowTest, markdownTextBoxesAreSelectedAndMoved) {
    controller->setMarkdownInPanel(false);  // (written on the page)
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 150, 350, 300));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    const auto drag = [&](QPoint from, QPoint to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(window, from + (to - from) * i / 10);
            wait(10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(50);
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 200));
    wait(50);
    type("# Moved box");
    key(Qt::Key_Escape);

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    Text* box = xqt::md::boxAt(*layer, 155, 200);
    ASSERT_NE(box, nullptr);
    ASSERT_TRUE(box->isMarkdown());
    const auto before = box->getBoundingBox();
    const Layer* penLayer = page->getSelectedLayer();
    ASSERT_NE(penLayer, layer);

    // A rectangle around it selects it
    controller->selectTool("selectRect");
    drag(pagePoint(before.x - 10, before.y - 10), pagePoint(before.x + before.width + 10, before.y + before.height + 10));
    ASSERT_TRUE(controller->hasSelection());
    EXPECT_EQ(page->getSelectedLayer(), layer) << "while it is selected";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Dragged by (40, 60) points
    const QPoint inside = pagePoint(before.x + before.width / 2, before.y + before.height / 2);
    drag(inside, pagePoint(before.x + before.width / 2 + 40, before.y + before.height / 2 + 60));
    // A tap elsewhere ends the selection (with any tool: it only deselects)
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(420, 430));
    wait(50);
    EXPECT_FALSE(controller->hasSelection());
    EXPECT_EQ(page->getSelectedLayer(), penLayer) << "the pen writes into its layer again";
    ASSERT_EQ(layer->getElements().size(), 1u);
    const auto* moved = static_cast<const Text*>(layer->getElements().front().get());
    EXPECT_TRUE(moved->isMarkdown()) << "still a Markdown text, in its layer";
    EXPECT_NEAR(moved->getBoundingBox().x, before.x + 40, 3);
    EXPECT_NEAR(moved->getBoundingBox().y, before.y + 60, 3);
    EXPECT_NEAR(moved->getBoundingBox().width, before.width, 0.5) << "the drawn box moved as a whole";

    // A lasso around it, then Delete; undo brings it back
    controller->selectTool("selectRegion");
    const auto b = moved->getBoundingBox();
    const QPoint a = pagePoint(b.x - 15, b.y - 15);
    const QPoint c = pagePoint(b.x + b.width + 15, b.y + b.height + 15);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (const QPoint& p: {QPoint(c.x(), a.y()), c, QPoint(a.x(), c.y()), a}) {
        for (int i = 1; i <= 5; ++i) {
            QTest::mouseMove(window, p);
            wait(5);
        }
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a);
    wait(50);
    ASSERT_TRUE(controller->hasSelection());
    controller->deleteSelection();
    EXPECT_TRUE(layer->getElements().empty());
    EXPECT_EQ(page->getSelectedLayer(), penLayer);
    controller->undo();
    EXPECT_EQ(layer->getElements().size(), 1u);
}

// Markdown text boxes written beside the page: the page shows them formatted while typing.
TEST_F(MainWindowTest, markdownTextBoxesAreWrittenBesideThePage) {
    controller->setMarkdownInPanel(true);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 200));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_FALSE(controller->markdownIsPageText()) << "a text box, not the page's text";
    EXPECT_EQ(view->getTextEditor(), nullptr) << "not edited on the page";
    ASSERT_TRUE(area->hasActiveFocus());
    type("## Box");
    key(Qt::Key_Return);
    type("- one");
    wait(300);

    // Live on the page, where it was tapped
    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    const Text* box = xqt::md::boxAt(*layer, 155, 300);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), "## Box\n- one");
    EXPECT_TRUE(box->isMarkdown());
    EXPECT_NEAR(box->getTransformation().shift.x, 150, 1);  // (where it was tapped, to the pixel)
    EXPECT_EQ(box->getFontSize(), 10);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    EXPECT_FALSE(panel->isVisible());

    // A tap on it opens it again; the page's own text is another one
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(160, 300));
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(area->property("text").toString().toStdString(), "## Box\n- one");
    click(find<QQuickItem>("markdownCancel"));
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, QVariant(0)));
    EXPECT_TRUE(controller->markdownIsPageText());
    EXPECT_EQ(area->property("text").toString(), QString()) << "the page has no text of its own yet";
    click(find<QQuickItem>("markdownCancel"));

    controller->undo();
    EXPECT_EQ(xqt::md::boxAt(*layer, 155, 300), nullptr) << "one undo step for the box";
}

// Pasted into the Markdown beside the page: formulas as chat apps write them, \( \) and \[ \], become $ $ and $$ $$
// (the same as on the page), in one undo step of the text; other text is pasted by the TextArea as always.
TEST_F(MainWindowTest, markdownPanelPastesTexDelimitersAsDollars) {
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, QVariant(0)));
    ASSERT_TRUE(panel->isVisible());
    area->forceActiveFocus();
    ASSERT_TRUE(area->hasActiveFocus());
    type("Say REPLACE here");
    // In place of a selection ("REPLACE")
    QMetaObject::invokeMethod(area, "select", Q_ARG(int, 4), Q_ARG(int, 11));
    QGuiApplication::clipboard()->setText("\\(E = mc^2\\) and \\(n\\)th");
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(area->property("text").toString().toStdString(), "Say $E = mc^2$ and \\(n\\)th here");
    EXPECT_EQ(area->property("cursorPosition").toInt(), 26) << "after the pasted text";
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(area->property("text").toString().toStdString(), "Say REPLACE here") << "one undo step";
    // Nothing to convert: pasted as always
    area->setProperty("cursorPosition", 0);
    QGuiApplication::clipboard()->setText("plain ");
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(area->property("text").toString().toStdString(), "plain Say REPLACE here");
    click(find<QQuickItem>("markdownCancel"));
}

// The page's Markdown text flows onto new pages while it is written.
TEST_F(MainWindowTest, markdownFlowsOntoNewPages) {
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, QVariant(0)));
    ASSERT_TRUE(panel->isVisible());
    std::string text = "# A long text\n\n";
    for (int i = 0; i < 30; ++i) {
        text += "## Part " + std::to_string(i + 1) + "\n\n";
        for (int j = 0; j < 4; ++j) {
            text += "Some sentences of part " + std::to_string(i + 1) +
                    ", long enough to take a few lines on the page, so that the parts need several pages.\n\n";
        }
    }
    area->setProperty("text", QString::fromStdString(text));
    until([&] { return controller->pageCount() > 1; }, 3000);
    const int pages = controller->pageCount();
    EXPECT_GE(pages, 3);
    EXPECT_EQ(controller->markdownPage(), 0);
    EXPECT_EQ(controller->markdownLastPage(), pages - 1);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        controller->setZoomPercent(30);
        wait(2500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(find<QQuickItem>("markdownDone"));
    controller->undo();
    EXPECT_EQ(controller->pageCount(), 1) << "one undo step: text and pages";
}

// Markdown written on the page, as in Typora: formatted while typing, the block with the cursor shows its Markdown.
TEST_F(MainWindowTest, markdownIsWrittenOnThePage) {
    controller->setMarkdownInPanel(false);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 250));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    wait(50);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    EXPECT_FALSE(find<QQuickItem>("markdownPanel")->isVisible()) << "not beside the page";
    type("# Title");
    key(Qt::Key_Return);  // a new paragraph
    type("Some **bold** text");
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** text");

    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::md::markdownLayer(page);
    ASSERT_NE(layer, nullptr);
    Text* box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), editor->text()) << "the page has it while typing";
    EXPECT_TRUE(box->isInEditing()) << "drawn by the editor while it is written";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    // Keys: the line's start, words, undo in the text being written
    key(Qt::Key_Home);
    EXPECT_EQ(editor->cursorPosition(), 9u) << "the start of the line";
    key(Qt::Key_Right, Qt::ControlModifier);
    EXPECT_EQ(editor->cursorPosition(), 13u) << "after \"Some\"";
    key(Qt::Key_End);
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** ") << "the last word";
    key(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(editor->text(), "# Title\n\nSome **bold** text");

    // A tap on the heading puts the cursor there
    const auto r = xqt::md::boxRect(*box);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(r.x + 1, r.y + 4));
    wait(50);
    EXPECT_EQ(view->getMarkdownEditor(), editor) << "still writing";
    EXPECT_LE(editor->cursorPosition(), 7u) << "in the heading";

    // Bold with Ctrl+B around a selection
    key(Qt::Key_End);
    key(Qt::Key_Left, Qt::ShiftModifier | Qt::ControlModifier);
    key(Qt::Key_B, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), "# **Title**\n\nSome **bold** text");

    // Ctrl+Alt+M: the same text, its source beside the page
    key(Qt::Key_M, Qt::ControlModifier | Qt::AltModifier);
    auto* panel = find<QQuickItem>("markdownPanel");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_EQ(view->getMarkdownEditor(), nullptr) << "not on the page any more";
    EXPECT_EQ(find<QQuickItem>("markdownArea")->property("text").toString().toStdString(),
              "# **Title**\n\nSome **bold** text");
    click(find<QQuickItem>("markdownDone"));
    box = xqt::md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_FALSE(box->isInEditing());
    controller->undo();
    EXPECT_EQ(xqt::md::boxOf(*layer), nullptr) << "one undo step for all of it";
}

// The page's text written on the page flows onto the next pages; the cursor goes with it.
TEST_F(MainWindowTest, markdownWrittenOnThePageFlowsOntoPages) {
    controller->setMarkdownInPanel(false);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->startMarkdown(0, true, 100, 100);  // (the writing button's page text, on the page)
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    std::string text;
    for (int i = 0; i < 60; ++i) {
        text += "Paragraph " + std::to_string(i) + " with enough words to take a line or two of the page.\n\n";
    }
    QGuiApplication::clipboard()->setText(QString::fromStdString(text));
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), text);
    ASSERT_GE(controller->pageCount(), 2);
    EXPECT_EQ(view->indexOf(&editor->getPage()), std::optional<size_t>(controller->pageCount() - 1))
            << "the cursor (at the end) is on the last page";
    key(Qt::Key_Home, Qt::ControlModifier);
    EXPECT_EQ(view->indexOf(&editor->getPage()), std::optional<size_t>(0)) << "back on the first page";
    key(Qt::Key_Escape);
    controller->undo();
    EXPECT_EQ(controller->pageCount(), 1);
}

// A tap on a task's check box switches it: with the hand tool (or a finger), and while writing on the page.
TEST_F(MainWindowTest, markdownCheckBoxesAreTapped) {
    controller->setMarkdownInPanel(false);
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(10);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 250));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(150, 300));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    type("- [ ] milk");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // (the list ends)
    type("Shopping");
    key(Qt::Key_Escape);

    auto* session = controller->tabManager().currentSession();
    Layer* layer = xqt::md::markdownLayer(session->getDocument()->getPage(0));
    ASSERT_NE(layer, nullptr);
    const auto box = [&] { return xqt::md::boxOf(*layer); };
    ASSERT_NE(box(), nullptr);
    const std::string before = box()->getText();
    ASSERT_EQ(before, "- [ ] milk\n\nShopping") << "Enter on an empty item: the list ends, a paragraph follows";
    const auto checkBox = [&](size_t active) {
        const auto& l = xqt::md::cachedLayout(box()->getText(), xqt::md::styleOf(*box()), active);
        EXPECT_EQ(l.checkBoxes.size(), 1u) << box()->getText();
        if (l.checkBoxes.empty()) {
            return QPoint();
        }
        const auto& at = box()->getTransformation().shift;
        const auto& b = l.checkBoxes.front();
        return pagePoint(at.x + b.x + b.size / 2, at.y + b.y + b.size / 2);
    };

    // The hand tool: switched, one undo step
    controller->selectTool("hand");
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, checkBox(xqt::md::NO_SOURCE));
    wait(50);
    EXPECT_NE(box()->getText().find("- [x] milk"), std::string::npos) << box()->getText();
    controller->undo();
    EXPECT_EQ(box()->getText(), before);

    // While writing on the page (the cursor in the paragraph after the list)
    controller->selectTool("text");
    const auto r = xqt::md::boxRect(*box());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(r.x + r.width - 5, r.y + r.height - 3));
    wait(50);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    const size_t cursor = editor->cursorPosition();
    ASSERT_GT(cursor, before.find("Shopping")) << "in the paragraph";
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, checkBox(cursor));
    wait(50);
    EXPECT_EQ(view->getMarkdownEditor(), editor) << "still writing";
    EXPECT_NE(editor->text().find("- [x] milk"), std::string::npos) << editor->text();
    EXPECT_EQ(editor->cursorPosition(), cursor) << "the cursor stays";
    key(Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(editor->text(), before) << "undone in the text being written";
    key(Qt::Key_Escape);
}

// DEPRECATED text mode (qt/docs/text-mode.md): no longer offered in the UI, its panel opened directly here
TEST_F(MainWindowTest, textModeTypesThePageText) {
    auto* panel = find<QQuickItem>("textFlowPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "open");
    ASSERT_TRUE(panel->isVisible());
    EXPECT_TRUE(controller->textFlowActive());
    auto* area = find<QQuickItem>("textFlowArea");
    ASSERT_TRUE(area->hasActiveFocus());
    // Beside the pages, not over them
    auto* canvasItem = find<QQuickItem>("canvas");
    EXPECT_LE(canvasItem->mapToScene({canvasItem->width(), 0}).x(), panel->mapToScene({0, 0}).x() + 0.5);
    EXPECT_GT(canvasItem->width(), 100);

    type("# Lecture 5");
    key(Qt::Key_Return);  // after a heading: a paragraph
    type("Some text.");
    key(Qt::Key_Return);
    type("- first");
    key(Qt::Key_Return);  // the list goes on
    type("second");
    key(Qt::Key_Return);
    key(Qt::Key_Return);  // an empty item ends the list
    type("After the list.");
    wait(300);

    // The page: Xournal++ text boxes in the layer "Text"
    auto* session = controller->tabManager().currentSession();
    PageRef page = session->getDocument()->getPage(0);
    Layer* layer = xqt::TextFlow::textLayer(page);
    ASSERT_NE(layer, nullptr);
    std::vector<std::string> texts;
    for (const auto& e: layer->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            texts.push_back(static_cast<const Text*>(e)->getText());
        }
    }
    EXPECT_EQ(texts, (std::vector<std::string>{"Lecture 5", "Some text.", "•", "first", "•", "second", "After the list."}));
    const auto blocks = xqt::TextFlow::read(page, xqt::TextFlow::Style{});
    ASSERT_EQ(blocks.size(), 5u);
    EXPECT_EQ(blocks[0].kind, xqt::TextBlock::Kind::Heading1);
    EXPECT_EQ(blocks[2].kind, xqt::TextBlock::Kind::Bullet);
    EXPECT_EQ(blocks[4].kind, xqt::TextBlock::Kind::Paragraph);
    EXPECT_TRUE(controller->modified()) << "unsaved changes while typing";
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }

    click(find<QQuickItem>("textFlowDone"));
    EXPECT_FALSE(panel->isVisible());
    EXPECT_FALSE(controller->textFlowActive());
    // One step for the whole text; undo / redo are in the page pill
    auto* undoButton = find<QQuickItem>("undoButton");
    ASSERT_NE(undoButton, nullptr);
    EXPECT_TRUE(find<QQuickItem>("viewPill")->isAncestorOf(undoButton));
    click(undoButton);
    EXPECT_TRUE(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).empty());
    click(find<QQuickItem>("redoButton"));
    EXPECT_EQ(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).size(), 5u);

    // Cancel restores the page
    QMetaObject::invokeMethod(panel, "open");
    type("x");
    wait(300);
    click(find<QQuickItem>("textFlowCancel"));
    EXPECT_EQ(xqt::TextFlow::read(page, xqt::TextFlow::Style{}).size(), 5u);
}

// --- Hybrid PDF (qt/docs/hybrid-pdf.md) -----------------------------------------------------------------------------

namespace {
void makeLecturePdf(const QString& file, int pages) {
    cairo_surface_t* s = cairo_pdf_surface_create(file.toUtf8().constData(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < pages; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("lecturepage" + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// A stroke on a page of the current document, through the undo stack (the document is changed).
void drawStroke(xqt::DocumentSession& s, size_t pageNo) {
    auto page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(2);
    stroke->setColor(Color(0xffcc0000U));
    stroke->addPoint(Point(100, 100, 1.0));
    stroke->addPoint(Point(200, 180, 3.0));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    s.getDocument()->lock();
    layer->addElement(std::move(stroke));
    s.getDocument()->unlock();
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}

size_t strokesOn(Document& doc, size_t pageNo) {
    size_t n = 0;
    for (const Layer* l: doc.getPage(pageNo)->getLayers()) {
        for (const auto& e: l->getElementsView()) {
            n += e->getType() == ELEMENT_STROKE;
        }
    }
    return n;
}
}  // namespace

// Save as → PDF with notes: an annotated PDF gets "name.notes.pdf"; then Ctrl+S writes the hybrid PDF again, and
// More offers the .xopp for Xournal++ (also on every save, a setting). In the library the two are one document.
TEST_F(MainWindowTest, savedAsHybridPdfCtrlSKeepsItHybrid) {
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 3);
    ASSERT_TRUE(controller->openPath(pdf));
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    drawStroke(*s, 1);
    ASSERT_NE(find("shareItem"), nullptr);  // (in the More menu: Share… for Xournal++ replaced "Export as .xopp")
    EXPECT_FALSE(controller->isHybrid());
    EXPECT_FALSE(controller->savesWithoutDialog()) << "Ctrl+S asks where (as before)";

    const QUrl suggestion = controller->suggestedHybridFile();
    EXPECT_EQ(suggestion.toLocalFile(), dir.filePath("lecture.notes.pdf"));
    ASSERT_TRUE(controller->saveAsHybrid(suggestion));
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_FALSE(controller->modified());
    EXPECT_EQ(controller->title(), "lecture.notes.pdf");
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(dir.filePath("lecture.notes.pdf").toStdString())));
    const QByteArray original = [&] {
        QFile f(pdf);
        f.open(QIODevice::ReadOnly);
        return f.readAll();
    }();
    EXPECT_FALSE(original.contains("XournalQt")) << "the lecture itself is left alone";

    // Ctrl+S: the hybrid PDF again, with the new stroke
    drawStroke(*s, 2);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);  // (saved in the background)
    EXPECT_FALSE(controller->modified());
    auto reopened = xqt::DocumentSession::loadFile(fs::path(dir.filePath("lecture.notes.pdf").toStdString()));
    ASSERT_TRUE(reopened.document);
    EXPECT_TRUE(reopened.hybrid);
    EXPECT_EQ(strokesOn(*reopened.document, 2), 1u);

    // A .xopp for Xournal++ on every save
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(settings->get("hybridExportXopp").toBool()) << "off by default";
    settings->set("hybridExportXopp", true);
    drawStroke(*s, 0);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);
    settings->set("hybridExportXopp", false);
    const fs::path xopp = dir.filePath("lecture.notes.xopp").toStdString();
    ASSERT_TRUE(fs::exists(xopp));
    EXPECT_TRUE(fs::exists(dir.filePath(".lecture.notes.pages.pdf").toStdString())) << "its PDF, hidden";
    auto exported = xqt::DocumentSession::loadFile(xopp);
    ASSERT_TRUE(exported.document) << exported.error;
    EXPECT_EQ(strokesOn(*exported.document, 0), 1u);
    const auto item = xqt::DocumentFiles::itemOf(xopp);
    EXPECT_TRUE(item.hybrid);
    EXPECT_EQ(item.main(), fs::path(dir.filePath("lecture.notes.pdf").toStdString())) << "one card: the hybrid PDF";
    EXPECT_TRUE(controller->suggestedExportFile().toLocalFile().endsWith("lecture.notes_export.pdf"))
            << "Export as PDF never overwrites the hybrid PDF";
}

// "Save notes into the PDF itself" (off by default) explains itself once; then Ctrl+S on an annotated PDF writes the
// notes into it without asking, keeping "name.original.pdf".
TEST_F(MainWindowTest, notesGoIntoThePdfItselfIfWanted) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    EXPECT_FALSE(settings->get("hybridIntoPdf").toBool()) << "off by default";
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("documentsTab"));
    auto* toggle = findItem("hybridIntoPdfSwitch");
    ASSERT_NE(toggle, nullptr);
    until([&] { return toggle->isVisible(); });
    scrollIntoView(toggle);
    click(toggle);
    EXPECT_TRUE(settings->get("hybridIntoPdf").toBool());
    QObject* explanation = find("intoPdfExplanation");
    ASSERT_NE(explanation, nullptr);
    EXPECT_TRUE(waitOpened(explanation, true)) << "explained the first time";
    QMetaObject::invokeMethod(explanation, "close");
    ASSERT_TRUE(waitOpened(explanation, false));
    click(toggle);
    click(toggle);
    wait(100);
    EXPECT_FALSE(explanation->property("visible").toBool()) << "only once";
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));

    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 2);
    ASSERT_TRUE(controller->openPath(pdf));
    drawStroke(*controller->tabManager().currentSession(), 0);
    EXPECT_TRUE(controller->savesWithoutDialog());
    EXPECT_EQ(controller->suggestedHybridFile().toLocalFile(), pdf);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);  // (saved in the background)
    settings->set("hybridIntoPdf", false);
    EXPECT_FALSE(controller->modified());
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(pdf.toStdString())));
    EXPECT_TRUE(QFile::exists(dir.filePath("lecture.original.pdf")));
}

// Save as offers both formats in one dialog: "Xournal notes (.xopp)", the default for new documents, and "PDF with
// notes, editable (.pdf)", the default for a document that is a hybrid PDF already. The extension typed wins over the
// chosen type; the file name follows the type. There is no separate "Save as hybrid PDF…" any more, and "Export as
// plain PDF…" says that it flattens.
TEST_F(MainWindowTest, saveAsOffersXoppAndPdfWithNotes) {
    EXPECT_EQ(find("saveHybridItem"), nullptr) << "replaced by the type in Save as";
    auto* exportItem = find("exportPdfItem");
    ASSERT_NE(exportItem, nullptr);
    EXPECT_EQ(exportItem->property("text").toString(), QString::fromUtf8("Export as plain PDF…"));
    QObject* dialog = find("saveDialog");
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->property("nameFilters").toStringList().size(), 2);
    auto filterIndex = [&] {
        auto* filter = dialog->property("selectedNameFilter").value<QObject*>();
        return filter ? filter->property("index").toInt() : -1;
    };
    auto setUp = [&](const char* format) {
        QMetaObject::invokeMethod(window, "setUpSaveDialog", Q_ARG(QVariant, QVariant(QString(format))));
    };

    // A new document: .xopp
    setUp("");
    EXPECT_EQ(filterIndex(), 0);
    EXPECT_TRUE(dialog->property("selectedFile").toUrl().toLocalFile().endsWith(".xopp"));
    EXPECT_EQ(dialog->property("defaultSuffix").toString(), "xopp");
    // Choosing the PDF type: the name follows
    auto* filter = dialog->property("selectedNameFilter").value<QObject*>();
    ASSERT_NE(filter, nullptr);
    filter->setProperty("index", 1);
    EXPECT_EQ(dialog->property("defaultSuffix").toString(), "pdf");
    EXPECT_TRUE(dialog->property("selectedFile").toUrl().toLocalFile().endsWith(".pdf"));
    filter->setProperty("index", 0);
    EXPECT_TRUE(dialog->property("selectedFile").toUrl().toLocalFile().endsWith(".xopp"));
    // It opens (the window's own dialog off-screen), and closes without saving
    QMetaObject::invokeMethod(window, "openSaveDialog", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant("pdf")));
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool());
    EXPECT_EQ(filterIndex(), 1);
    QMetaObject::invokeMethod(dialog, "reject");
    until([&] { return !dialog->property("visible").toBool(); });
    EXPECT_FALSE(controller->anySaving());

    // The typed extension wins; without one, the chosen type
    QTemporaryDir dir;
    const auto url = [&](const char* name) { return QUrl::fromLocalFile(dir.filePath(name)); };
    EXPECT_TRUE(controller->savesAsPdf(url("a.pdf"), false));
    EXPECT_FALSE(controller->savesAsPdf(url("a.xopp"), true));
    EXPECT_TRUE(controller->savesAsPdf(url("a"), true));
    EXPECT_FALSE(controller->savesAsPdf(url("a"), false));
    EXPECT_EQ(controller->fileForFormat(url("a.xopp"), true), url("a.pdf"));
    EXPECT_EQ(controller->fileForFormat(url("a.pdf"), false), url("a.xopp"));
    makeLecturePdf(dir.filePath("lecture.pdf"), 1);
    EXPECT_EQ(controller->fileForFormat(url("lecture.xopp"), true), url("lecture.notes.pdf"))
            << "never over another PDF by default";

    // Saving with the PDF type chosen: a hybrid PDF; the next Save as offers the PDF first, with its own name
    drawStroke(*controller->tabManager().currentSession(), 0);
    QMetaObject::invokeMethod(window, "saveChosen", Q_ARG(QVariant, QVariant(url("notes"))),
                              Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant()));
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(dir.filePath("notes.pdf").toStdString())));
    setUp("");
    EXPECT_EQ(filterIndex(), 1);
    EXPECT_EQ(dialog->property("selectedFile").toUrl().toLocalFile().toStdString(),
              url("notes.pdf").toLocalFile().toStdString());
    setUp("xopp");
    EXPECT_EQ(filterIndex(), 0);
    EXPECT_EQ(dialog->property("selectedFile").toUrl().toLocalFile().toStdString(),
              url("notes.xopp").toLocalFile().toStdString());

    // And back to .xopp with the Xournal type
    QMetaObject::invokeMethod(window, "saveChosen", Q_ARG(QVariant, QVariant(url("notes.xopp"))),
                              Q_ARG(QVariant, QVariant(false)), Q_ARG(QVariant, QVariant()));
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_FALSE(controller->isHybrid());
    EXPECT_TRUE(QFile::exists(dir.filePath("notes.xopp")));
}

namespace {
/// Uses a fake for the system while it lives.
struct UseSystemApps {
    explicit UseSystemApps(xqt::SystemApps& apps) { xqt::SystemApps::setInstance(&apps); }
    ~UseSystemApps() { xqt::SystemApps::setInstance(nullptr); }
};
}  // namespace

// A document saved as "name.xopp" and then as a PDF with notes asks once what happens to the .xopp: to the trash (the
// default), kept up to date for Xournal++, or kept as it is. "Don't ask again" stores the choice, which Settings →
// Documents shows and changes. The .xopp open in another tab without changes: that tab is closed.
TEST_F(MainWindowTest, savingAXoppAsPdfAsksWhatHappensToIt) {
    FakeSystemApps fake;
    UseSystemApps use(fake);
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    settings->set("hybridOldXopp", "ask");  // (the tests share the config folder)
    QTemporaryDir dir;
    const auto url = [&](const char* name) { return QUrl::fromLocalFile(dir.filePath(name)); };
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    drawStroke(*s, 0);
    ASSERT_TRUE(controller->saveAs(url("notes.xopp")));
    EXPECT_EQ(controller->oldXoppToAsk(), "notes.xopp");
    // The same .xopp in another tab, unchanged
    {
        auto other = xqt::DocumentSession::loadFile(dir.filePath("notes.xopp").toStdString());
        ASSERT_TRUE(other.document);
        controller->tabManager().addTab(
                std::make_unique<xqt::DocumentSession>(controller->context(), std::move(other.document)));
        controller->tabManager().setCurrentIndex(controller->tabManager().indexOf(s));
    }
    ASSERT_EQ(controller->tabCount(), 2);

    QObject* dialog = find("oldXoppDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(window, "saveChosen", Q_ARG(QVariant, QVariant(url("notes.pdf"))),
                              Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant()));
    ASSERT_TRUE(waitOpened(dialog, true)) << "asked before anything is written";
    EXPECT_FALSE(QFile::exists(dir.filePath("notes.pdf")));
    EXPECT_TRUE(find("oldXoppTrash")->property("checked").toBool()) << "the trash is the default";
    click(find<QQuickItem>("oldXoppSave"));
    ASSERT_TRUE(waitOpened(dialog, false));
    until([&] { return !controller->anySaving() && !QFile::exists(dir.filePath("notes.xopp")); }, 20000);
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(dir.filePath("notes.pdf").toStdString())));
    EXPECT_FALSE(QFile::exists(dir.filePath("notes.xopp")));
    EXPECT_TRUE(fake.trashed.contains(dir.filePath("notes.xopp")));
    EXPECT_EQ(controller->tabCount(), 1) << "the other tab of it is closed";
    EXPECT_EQ(controller->tabManager().currentSession(), s);
    auto* snackbarText = findItem("snackbarText");
    ASSERT_NE(snackbarText, nullptr);
    EXPECT_TRUE(snackbarText->property("text").toString().contains("moved to the trash"));
    EXPECT_EQ(settings->get("hybridOldXopp").toString(), "ask") << "asked again next time";

    // Keep it as it is, and don't ask again
    controller->newDocument();
    drawStroke(*controller->tabManager().currentSession(), 0);
    ASSERT_TRUE(controller->saveAs(url("other.xopp")));
    QMetaObject::invokeMethod(window, "saveChosen", Q_ARG(QVariant, QVariant(url("other.pdf"))),
                              Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant()));
    ASSERT_TRUE(waitOpened(dialog, true));
    click(find<QQuickItem>("oldXoppKeep"));
    click(find<QQuickItem>("oldXoppDontAsk"));
    click(find<QQuickItem>("oldXoppSave"));
    ASSERT_TRUE(waitOpened(dialog, false));
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_TRUE(QFile::exists(dir.filePath("other.xopp"))) << "kept";
    EXPECT_EQ(settings->get("hybridOldXopp").toString(), "keep") << "stored";

    // Not asked now
    controller->newDocument();
    drawStroke(*controller->tabManager().currentSession(), 0);
    ASSERT_TRUE(controller->saveAs(url("third.xopp")));
    EXPECT_EQ(controller->oldXoppToAsk(), "");
    QMetaObject::invokeMethod(window, "saveChosen", Q_ARG(QVariant, QVariant(url("third.pdf"))),
                              Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant()));
    wait(100);
    EXPECT_FALSE(dialog->property("visible").toBool());
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_TRUE(QFile::exists(dir.filePath("third.xopp")));

    // Settings → Documents shows the choice and goes back to asking
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("documentsTab"));
    auto* row = findItem("hybridOldXoppRow");
    ASSERT_NE(row, nullptr);
    until([&] { return row->isVisible(); });
    QQuickItem* combo = nullptr;
    for (QQuickItem* c: row->childItems()) {
        if (c->inherits("QQuickComboBox")) {
            combo = c;
        }
    }
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->property("currentValue").toString(), "keep");
    settings->set("hybridOldXopp", "ask");
    wait(50);
    EXPECT_EQ(combo->property("currentValue").toString(), "ask");
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

// "Keep it updated for Xournal++": the .xopp the document was is written again from the PDF's notes, now and on
// every save (its PDF: the pages, hidden beside it); the annotated PDF itself is left alone. Removed by the user, it
// is not written again.
TEST_F(MainWindowTest, theOldXoppIsKeptUpdatedForXournalpp) {
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 3);
    ASSERT_TRUE(controller->openPath(pdf));
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    drawStroke(*s, 0);
    const QUrl xoppUrl = QUrl::fromLocalFile(dir.filePath("lecture.xopp"));
    ASSERT_TRUE(controller->saveAs(xoppUrl));
    const fs::path xopp = xoppUrl.toLocalFile().toStdString();
    const fs::path notes = dir.filePath("lecture.notes.pdf").toStdString();
    EXPECT_EQ(controller->suggestedHybridFile().toLocalFile().toStdString(), notes.string());

    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(QString::fromStdString(notes.string())), "update"));
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_TRUE(controller->isHybrid());
    EXPECT_EQ(xqt::HybridPdf::xoppExportOf(notes), xopp) << "recorded in the PDF";
    auto exported = xqt::DocumentSession::loadFile(xopp);
    ASSERT_TRUE(exported.document) << exported.error;
    EXPECT_EQ(strokesOn(*exported.document, 0), 1u);
    EXPECT_EQ(exported.document->getPdfFilepath(), fs::path(dir.filePath(".lecture.pages.pdf").toStdString()));
    {
        QFile f(pdf);
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        EXPECT_FALSE(f.readAll().contains("XournalQt")) << "the lecture itself is left alone";
    }

    // On every save
    drawStroke(*s, 2);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);
    exported = xqt::DocumentSession::loadFile(xopp);
    ASSERT_TRUE(exported.document);
    EXPECT_EQ(strokesOn(*exported.document, 2), 1u);
    EXPECT_EQ(xqt::HybridPdf::xoppExportOf(notes), xopp);

    // Removed: no more
    fs::remove(xopp);
    drawStroke(*s, 1);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);
    EXPECT_FALSE(fs::exists(xopp));
    EXPECT_EQ(xqt::HybridPdf::xoppExportOf(notes), fs::path());
}

// Share… → "PDF with notes": a PDF as it is, a hybrid PDF saved first when changed, handed to the system (the file
// manager); or onto the clipboard as a file URL, the PDF's bytes and its path. A .xopp is never turned into a PDF
// unasked: the window asks, or a PDF copy is written and the document stays as it is.
TEST_F(MainWindowTest, sharingThePdfWithNotes) {
    FakeSystemApps fake;
    UseSystemApps use(fake);
    EXPECT_EQ(controller->shareStep(), "saveAs") << "a new document: Save as first";
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 2);
    ASSERT_TRUE(controller->openPath(pdf));
    EXPECT_EQ(controller->shareStep(), "share") << "a PDF without notes: itself";
    ASSERT_TRUE(controller->sharePdf(false));
    ASSERT_EQ(fake.shared, QStringList{pdf});

    // Through the window: ⋮ → Share… → PDF with notes
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    const QString notes = dir.filePath("lecture.notes.pdf");
    drawStroke(*s, 0);
    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(notes)));
    drawStroke(*s, 1);
    EXPECT_EQ(controller->shareStep(), "save");
    QObject* dialog = find("shareDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QString())));
    ASSERT_TRUE(waitOpened(dialog, true));
    click(find<QQuickItem>("sharePdfChoice"));
    until([&] { return fake.shared.size() == 2; }, 20000);
    EXPECT_EQ(fake.shared.value(1), notes) << "saved first, then shown";
    EXPECT_FALSE(controller->modified());
    ASSERT_TRUE(waitOpened(dialog, false));

    // Saved incrementally since (Ctrl+S appends): written anew in one piece before it is shared, so that no earlier
    // revision with deleted ink goes along
    xqt::HybridPdf::compactAbove = 1000;  // (a small file: appended to, not compacted)
    drawStroke(*s, 0);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving(); }, 20000);
    xqt::HybridPdf::compactAbove = 0.25;
    const fs::path notesFile(notes.toStdString());
    EXPECT_TRUE(xqt::HybridPdf::hasEarlierRevisions(notesFile)) << "Ctrl+S appended";
    EXPECT_FALSE(controller->modified());
    EXPECT_EQ(controller->shareStep(), "save") << "earlier revisions: written anew first";
    ASSERT_TRUE(controller->sharePdf(false));
    until([&] { return fake.shared.size() == 3; }, 20000);
    EXPECT_EQ(fake.shared.value(2), notes);
    EXPECT_FALSE(xqt::HybridPdf::hasEarlierRevisions(notesFile));
    EXPECT_EQ(controller->shareStep(), "share");

    // The clipboard
    ASSERT_TRUE(controller->sharePdf(true));
    const QMimeData* data = QGuiApplication::clipboard()->mimeData();
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->hasUrls());
    EXPECT_EQ(data->urls().value(0).toLocalFile(), notes);
    EXPECT_TRUE(data->hasFormat("text/uri-list"));
    EXPECT_TRUE(data->data("application/pdf").startsWith("%PDF"));
    EXPECT_TRUE(data->text().contains(notes));
    auto* snackbarText = findItem("snackbarText");
    ASSERT_NE(snackbarText, nullptr);
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith("PDF copied"));

    // A .xopp: asked; a PDF copy leaves the document as it is
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(dir.filePath("lecture.xopp"))));
    EXPECT_EQ(controller->shareStep(), "ask");
    QMetaObject::invokeMethod(window, "sharePdfOf", Q_ARG(QVariant, QVariant(QString())), Q_ARG(QVariant, QVariant(false)));
    QObject* ask = find("shareXoppDialog");
    ASSERT_NE(ask, nullptr);
    ASSERT_TRUE(waitOpened(ask, true));
    QMetaObject::invokeMethod(ask, "close");
    ASSERT_TRUE(waitOpened(ask, false));
    ASSERT_TRUE(controller->sharePdfCopy(QUrl::fromLocalFile(dir.filePath("copy.pdf")), false));
    until([&] { return fake.shared.size() == 4; }, 20000);
    EXPECT_EQ(fake.shared.value(3), dir.filePath("copy.pdf"));
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(dir.filePath("copy.pdf").toStdString())));
    EXPECT_FALSE(controller->isHybrid());
    EXPECT_EQ(controller->title(), "lecture.xopp");
    // Copied: a PDF copy in the cache
    ASSERT_TRUE(controller->sharePdfCopy(QUrl(), true));
    until([&] { return !controller->anySaving(); }, 20000);
    data = QGuiApplication::clipboard()->mimeData();
    ASSERT_TRUE(data && data->hasUrls());
    const QString cached = data->urls().value(0).toLocalFile();
    EXPECT_TRUE(cached.endsWith("lecture.pdf")) << cached.toStdString();
    EXPECT_FALSE(cached.startsWith(dir.path())) << "not next to the document";
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(cached.toStdString())));
    EXPECT_EQ(controller->title(), "lecture.xopp");
}

// Share… → "For Xournal++": a one-time export into a folder the user chooses, never the document's own, as
// "name.xopp" + "name.xopp.bg.pdf", then shown; the note offers to copy both. Also from the tab and library card menus.
TEST_F(MainWindowTest, sharingForXournalpp) {
    FakeSystemApps fake;
    UseSystemApps use(fake);
    EXPECT_NE(find("shareCardItem"), nullptr);  // (the tab menu has "Share…" too: shareTabItem, in each tab)
    QTemporaryDir dir, out;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 3);
    ASSERT_TRUE(controller->openPath(pdf));
    drawStroke(*controller->tabManager().currentSession(), 1);
    const QString notes = dir.filePath("lecture.notes.pdf");
    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(notes)));

    EXPECT_FALSE(controller->shareForXournal(QUrl::fromLocalFile(dir.path()))) << "never next to the document";
    EXPECT_FALSE(QFile::exists(dir.filePath("lecture.xopp")));
    QObject* messageDialog = find("messageDialog");
    ASSERT_NE(messageDialog, nullptr);
    ASSERT_TRUE(waitOpened(messageDialog, true)) << "says why";
    EXPECT_TRUE(messageDialog->property("title").toString().contains("another folder"));
    QMetaObject::invokeMethod(messageDialog, "close");
    ASSERT_TRUE(waitOpened(messageDialog, false));

    ASSERT_TRUE(controller->shareForXournal(QUrl::fromLocalFile(out.path())));
    until([&] { return fake.shared.size() == 2; }, 20000);
    const QString xopp = out.filePath("lecture.xopp"), bg = out.filePath("lecture.xopp.bg.pdf");
    EXPECT_EQ(fake.shared, (QStringList{xopp, bg}));
    auto exported = xqt::DocumentSession::loadFile(xopp.toStdString());
    ASSERT_TRUE(exported.document) << exported.error;
    EXPECT_TRUE(exported.document->isAttachPdf());
    EXPECT_EQ(exported.document->getPdfFilepath(), fs::path(bg.toStdString()));
    EXPECT_EQ(exported.document->getPageCount(), 3u);
    EXPECT_EQ(strokesOn(*exported.document, 1), 1u);
    EXPECT_EQ(controller->title(), "lecture.notes.pdf") << "the document stays as it is";
    // The note's "Copy": both files onto the clipboard
    auto* action = findItem("snackbarAction");
    ASSERT_NE(action, nullptr);
    until([&] { return action->isVisible() && action->width() > 0; });
    nextFrame();  // (the note is laid out: the button is where the click goes, also under load)
    EXPECT_EQ(action->property("text").toString(), "Copy");
    click(action);  // (the message about the document's folder was closed above)
    const QMimeData* data = nullptr;
    until([&] {
        data = QGuiApplication::clipboard()->mimeData();
        return data && data->hasUrls();
    });
    ASSERT_TRUE(data && data->hasUrls());
    EXPECT_EQ(data->urls().size(), 2);
    EXPECT_EQ(data->urls().value(0).toLocalFile().toStdString(), xopp.toStdString());

    // Again: a free name
    ASSERT_TRUE(controller->shareForXournal(QUrl::fromLocalFile(out.path())));
    until([&] { return fake.shared.size() == 4; }, 20000);
    EXPECT_TRUE(QFile::exists(out.filePath("lecture (2).xopp")));

    // A library card's PDF, not open: loaded and exported in the background
    QTemporaryDir cardOut;
    ASSERT_TRUE(controller->shareForXournal(QUrl::fromLocalFile(cardOut.path()), notes));
    until([&] { return fake.shared.size() == 6; }, 20000);
    auto fromCard = xqt::DocumentSession::loadFile(cardOut.filePath("lecture.xopp").toStdString());
    ASSERT_TRUE(fromCard.document) << fromCard.error;
    EXPECT_EQ(strokesOn(*fromCard.document, 1), 1u);
}

// ⋮ → "Export for the archive…" (and Share → "For the archive"): the dialog says what it means, the PDF/A-3b file goes
// next to the document (or into a chosen folder), in the background, and the report says whether it is PDF/A. The
// document keeps its file and its unsaved changes.
TEST_F(MainWindowTest, exportForTheArchive) {
    FakeSystemApps fake;
    UseSystemApps use(fake);
    QTemporaryDir dir, out;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 2);
    ASSERT_TRUE(controller->openPath(pdf));
    drawStroke(*controller->tabManager().currentSession(), 1);
    ASSERT_TRUE(controller->modified());
    ASSERT_NE(find("exportArchiveItem"), nullptr);

    QObject* dialog = find("archiveDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, QVariant(QString())));
    ASSERT_TRUE(waitOpened(dialog, true));
    const QString explanation = findItem("archiveExplanation")->property("text").toString();
    EXPECT_TRUE(explanation.contains("PDF/A-3") && explanation.contains("decades") &&
                explanation.contains("merged into the pages") && explanation.contains("open it for editing"))
            << explanation.toStdString();
    auto* nextTo = findItem("archiveNextTo");
    EXPECT_TRUE(nextTo->property("checked").toBool());
    EXPECT_TRUE(nextTo->property("text").toString().contains("lecture.archive.pdf"));
    QObject* report = find("archiveReportDialog");
    ASSERT_NE(report, nullptr);
    click(findItem("archiveExportButton"));
    ASSERT_TRUE(waitOpened(report, true, 30000));
    const QString archive = dir.filePath("lecture.archive.pdf");
    EXPECT_EQ(report->property("path").toString(), archive);
    EXPECT_TRUE(report->property("pdfa").toBool());
    EXPECT_TRUE(findItem("archiveReportText")->property("text").toString().contains("PDF/A-3b"));
    EXPECT_TRUE(xqt::HybridPdf::isArchive(fs::path(archive.toStdString())));
    auto written = xqt::DocumentSession::loadFile(archive.toStdString());
    ASSERT_TRUE(written.document) << written.error;
    EXPECT_EQ(strokesOn(*written.document, 1), 1u) << "with the unsaved stroke";
    EXPECT_TRUE(controller->modified()) << "the document keeps its changes";
    EXPECT_EQ(controller->title(), "lecture.pdf");
    click(findItem("archiveShowButton"));
    until([&] { return fake.shared.size() == 1; });
    EXPECT_EQ(fake.shared.value(0), archive);
    ASSERT_TRUE(waitOpened(report, false));

    // Share → "For the archive" opens the same dialog; into a folder: the same name there
    QObject* share = find("shareDialog");
    QMetaObject::invokeMethod(share, "openFor", Q_ARG(QVariant, QVariant(QString())));
    ASSERT_TRUE(waitOpened(share, true));
    click(findItem("shareArchiveChoice"));
    ASSERT_TRUE(waitOpened(dialog, true));
    QMetaObject::invokeMethod(dialog, "close");
    ASSERT_TRUE(waitOpened(dialog, false));
    const QUrl inFolder = controller->archiveFileIn(QUrl::fromLocalFile(out.path()));
    EXPECT_EQ(inFolder.toLocalFile(), out.filePath("lecture.archive.pdf"));

    // A library card's document, not open: written in the background
    ASSERT_TRUE(controller->exportArchive(inFolder, archive));
    ASSERT_TRUE(waitOpened(report, true, 30000));
    EXPECT_EQ(report->property("path").toString(), out.filePath("lecture.archive.pdf"));
    EXPECT_TRUE(report->property("pdfa").toBool());
    QMetaObject::invokeMethod(report, "close");
    ASSERT_TRUE(waitOpened(report, false));
    EXPECT_EQ(controller->suggestedArchiveFile(archive).toLocalFile(), dir.filePath("lecture.archive (2).pdf"))
            << "never the archive itself";
}

// Settings → Search: the fuzzy search's toggle (the same setting as the search fields' button, both ways) and its typo
// tolerance, which the next query takes.
TEST_F(MainWindowTest, settingsSearchTabSetsTheFuzzySearch) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    auto* library = qobject_cast<xqt::LibraryModel*>(controller->libraryModel());
    library->setFuzzySearch(false);  // (the tests share the config folder)
    settings->set("fuzzyTypos", 1);
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("searchTab"));
    auto* toggle = findItem("fuzzySearchSwitch");
    ASSERT_NE(toggle, nullptr);
    until([&] { return toggle->isVisible(); });
    EXPECT_FALSE(toggle->property("checked").toBool());
    click(toggle);
    EXPECT_TRUE(library->fuzzySearch()) << "the search fields' setting";
    library->setFuzzySearch(false);  // (as the button in a search field does)
    until([&] { return !toggle->property("checked").toBool(); });
    EXPECT_FALSE(toggle->property("checked").toBool()) << "and back";

    auto* combo = findItem("fuzzyTyposCombo");
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->property("currentIndex").toInt(), 1) << "one typo by default";
    combo->forceActiveFocus();
    key(Qt::Key_Down);
    until([&] { return settings->get("fuzzyTypos").toInt() == 2; });
    EXPECT_EQ(settings->get("fuzzyTypos").toInt(), 2);
    EXPECT_EQ(xqt::FuzzyQuery::typoTolerance(), 2);
    const auto terms = xqt::FuzzyQuery::textTerms("trasnfromation", true);
    EXPECT_EQ(xqt::textmatch::count(u"the transformation", terms), 1) << "two typos in a long word";
    settings->set("fuzzyTypos", 0);
    until([&] { return combo->property("currentIndex").toInt() == 0; });
    EXPECT_EQ(combo->property("currentIndex").toInt(), 0);
    EXPECT_EQ(xqt::FuzzyQuery::typoTolerance(), 0);
    settings->set("fuzzyTypos", 1);
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
}

// A hybrid PDF whose ink another app moved: the window says so and offers to keep ours or import theirs.
TEST_F(MainWindowTest, inkChangedInAnotherAppIsAskedAbout) {
    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 2);
    const QString hybrid = dir.filePath("lecture.notes.pdf");
    {
        auto loaded = xqt::DocumentSession::loadFile(pdf.toStdString());
        ASSERT_TRUE(loaded.document);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(2);
        stroke->addPoint(Point(100, 100, 1.0));
        stroke->addPoint(Point(200, 180, 3.0));
        loaded.document->getPage(0)->getSelectedLayer()->addElement(std::move(stroke));
        ASSERT_TRUE(xqt::HybridPdf::write(*loaded.document, hybrid.toStdString()).ok);
    }
    {  // another app moves our ink
        QPDF q;
        q.processFile(hybrid.toUtf8().constData());
        auto annots = QPDFPageDocumentHelper(q).getAllPages().at(0).getAnnotations();
        ASSERT_EQ(annots.size(), 1u);
        annots[0].getObjectHandle().replaceKey("/Rect", QPDFObjectHandle::parse("[300 300 400 400]"));
        QPDFWriter w(q, (hybrid + ".tmp").toUtf8().constData());
        w.write();
        QFile::remove(hybrid);
        QFile::rename(hybrid + ".tmp", hybrid);
    }
    QObject* dialog = find("hybridEditedDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(controller->openPath(hybrid));
    ASSERT_TRUE(waitOpened(dialog, true));
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    EXPECT_EQ(strokesOn(*s->getDocument(), 0), 1u) << "the Xournal data until the choice";
    click(findItem("hybridImportButton"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(strokesOn(*s->getDocument(), 0), 0u) << "the other app's version, as a plain annotation";
    EXPECT_TRUE(controller->modified());
    controller->undo();
    EXPECT_EQ(strokesOn(*s->getDocument(), 0), 1u);
}

namespace {
/// Holds a save on its worker just before it writes the .xopp (PdfPageKeeper::stopSaveAt, step 1) until released.
struct HeldSave {
    HeldSave() {
        xqt::PdfPageKeeper::stopSaveAt = [this](int step) {
            if (step == 1) {
                ++entered;
                released.wait_for(std::chrono::seconds(10));
            }
            return false;
        };
    }
    ~HeldSave() {
        release();
        xqt::PdfPageKeeper::stopSaveAt = nullptr;
    }
    void release() {
        if (!done.exchange(true)) {
            promise.set_value();
        }
    }
    std::atomic<int> entered{0};
    std::atomic<bool> done{false};
    std::promise<void> promise;
    std::shared_future<void> released = promise.get_future().share();
};

size_t strokesIn(const QString& file) {
    auto loaded = xqt::DocumentSession::loadFile(file.toStdString());
    return loaded.document ? strokesOn(*loaded.document, 0) : 0;
}
}  // namespace

// Ctrl+S saves in the background: the window says "saving…" and keeps the modified dot until the file is written,
// and it stays usable meanwhile (a page added then is not in the file, and the document stays modified).
TEST_F(MainWindowTest, ctrlSSavesInTheBackground) {
    QTemporaryDir dir;
    const QString file = dir.filePath("notes.xopp");
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(file)));
    drawStroke(*controller->tabManager().currentSession(), 0);
    auto* tabs = qobject_cast<QAbstractItemModel*>(controller->tabsModel());
    HeldSave held;
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return held.entered == 1; }, 5000);
    ASSERT_EQ(held.entered, 1);
    EXPECT_TRUE(controller->saving());
    EXPECT_TRUE(controller->modified()) << "the dot stays until the file is written";
    EXPECT_TRUE(window->title().contains(QString::fromUtf8("saving…"))) << window->title().toStdString();
    EXPECT_TRUE(tabs->index(0, 0).data(xqt::TabManager::SavingRole).toBool());
    const int pages = controller->pageCount();
    key(Qt::Key_N, Qt::ControlModifier);  // the window goes on
    EXPECT_EQ(controller->pageCount(), pages + 1);
    held.release();
    until([&] { return !controller->saving(); }, 10000);
    EXPECT_FALSE(controller->saving());
    EXPECT_FALSE(window->title().contains(QString::fromUtf8("saving…")));
    EXPECT_TRUE(controller->modified()) << "the page added meanwhile is not saved";
    EXPECT_EQ(strokesIn(file), 1u);
    EXPECT_EQ(xqt::DocumentSession::loadFile(file.toStdString()).document->getPageCount(), static_cast<size_t>(pages));
}

// Closing a tab or the window while a document is saved: they wait for the save (the window stays usable), then
// close; the file is complete.
TEST_F(MainWindowTest, closingWaitsForARunningSave) {
    QTemporaryDir dir;
    const QString first = dir.filePath("first.xopp"), second = dir.filePath("second.xopp");
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(first)));
    controller->newDocument();
    ASSERT_TRUE(controller->saveAs(QUrl::fromLocalFile(second)));
    ASSERT_EQ(controller->tabCount(), 2);
    drawStroke(*controller->tabManager().currentSession(), 0);
    {
        HeldSave held;
        key(Qt::Key_S, Qt::ControlModifier);
        until([&] { return held.entered == 1; }, 5000);
        ASSERT_EQ(held.entered, 1);
        QMetaObject::invokeMethod(window, "requestCloseTab", Q_ARG(QVariant, 1));
        wait(200);
        EXPECT_EQ(controller->tabCount(), 2) << "the tab waits for its save";
        EXPECT_FALSE(find("unsavedDialog")->property("visible").toBool()) << "nothing to ask: it is being saved";
        held.release();
        until([&] { return controller->tabCount() == 1; }, 10000);
        EXPECT_EQ(controller->tabCount(), 1);
        EXPECT_EQ(strokesIn(second), 1u);
    }
    drawStroke(*controller->tabManager().currentSession(), 0);
    HeldSave held;
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return held.entered == 1; }, 5000);
    ASSERT_EQ(held.entered, 1);
    QMetaObject::invokeMethod(window, "closeWindow");
    wait(200);
    EXPECT_TRUE(window->isVisible()) << "the window waits for the save";
    EXPECT_FALSE(find("unsavedDialog")->property("visible").toBool());
    held.release();
    until([&] { return !window->isVisible(); }, 10000);
    EXPECT_FALSE(window->isVisible()) << "closed once it was written";
    EXPECT_EQ(strokesIn(first), 1u);
}

// --- qt/present: page number jump, 16:9 pages, horizontal scrolling, presentation --------------------------------

// Digits typed while the page is at hand: "Go to page: 12", Enter goes there (the last page at most), Escape cancels.
// Digits typed into a text on the page, the search field or a dialog stay there.
TEST_F(MainWindowTest, typingAPageNumberJumpsToThePage) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    const int pages = controller->pageCount();
    ASSERT_GE(pages, 10);
    auto* jump = find<QQuickItem>("pageJump");
    auto* digits = find<QQuickItem>("pageJumpDigits");
    ASSERT_NE(jump, nullptr);
    ASSERT_NE(digits, nullptr);
    EXPECT_FALSE(jump->isVisible());
    key(Qt::Key_1);
    ASSERT_TRUE(jump->isVisible()) << "the first digit opens it";
    key(Qt::Key_0);
    EXPECT_EQ(digits->property("text").toString(), "10");
    key(Qt::Key_Return);
    EXPECT_FALSE(jump->isVisible());
    EXPECT_EQ(controller->pageNumber(), 10);
    EXPECT_TRUE(controller->canGoBack()) << "a jump: Alt+Left goes back";

    key(Qt::Key_9);
    key(Qt::Key_9);
    key(Qt::Key_9);
    key(Qt::Key_Enter);
    EXPECT_EQ(controller->pageNumber(), pages) << "past the end: the last page";

    key(Qt::Key_2);
    key(Qt::Key_Escape);
    EXPECT_FALSE(jump->isVisible()) << "Escape cancels";
    EXPECT_EQ(controller->pageNumber(), pages);

    key(Qt::Key_2);
    key(Qt::Key_3);
    key(Qt::Key_Backspace);
    key(Qt::Key_Return);
    EXPECT_EQ(controller->pageNumber(), 2) << "Backspace takes the last digit back";

    key(Qt::Key_5, Qt::KeypadModifier);
    key(Qt::Key_Enter, Qt::KeypadModifier);
    EXPECT_EQ(controller->pageNumber(), 5) << "the number pad";

    // Another key: no page number after all
    key(Qt::Key_3);
    ASSERT_TRUE(jump->isVisible());
    key(Qt::Key_P);
    EXPECT_FALSE(jump->isVisible());
    EXPECT_EQ(controller->pageNumber(), 5);

    // The search field keeps its digits
    key(Qt::Key_F, Qt::ControlModifier);
    type("12");
    EXPECT_FALSE(jump->isVisible());
    EXPECT_EQ(find<QQuickItem>("searchField")->property("text").toString(), "12");
    key(Qt::Key_Escape);

    // So does a text on the page
    controller->setTextMarkdown(false);  // (an ordinary text box, whatever a test before chose)
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    const QRectF page = view->pageViewRect(controller->pageNumber() - 1);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      canvasItem->mapToScene(page.topLeft() + QPointF(60, 60)).toPoint());
    wait(50);
    ASSERT_NE(view->getTextEditor(), nullptr);
    type("42");
    EXPECT_FALSE(jump->isVisible());
    EXPECT_EQ(view->getTextEditor()->text(), "42");
    key(Qt::Key_Escape);
    controller->selectTool("pen");

    // And a dialog
    QObject* dialog = find("insertPagesDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitOpened(dialog, true));
    key(Qt::Key_7);
    EXPECT_FALSE(jump->isVisible()) << "not under a dialog";
    QMetaObject::invokeMethod(dialog, "close");
    ASSERT_TRUE(waitOpened(dialog, false));
}

// A 16:9 slide in the New document and the Insert pages dialogs: choosing it turns the page to landscape, and the
// pages are 960 x 540 points (PowerPoint's 13.33 x 7.5 in), a plain page size in the .xopp.
TEST_F(MainWindowTest, sixteenByNinePagesForPresenting) {
    const int slide = controller->settingsModel()->property("paperFormats").toStringList().indexOf("16:9 (presentation)");
    ASSERT_GE(slide, 0);
    auto sizeOf = [&](int page) {
        auto* s = controller->tabManager().currentSession();
        const PageRef p = s->getDocument()->getPage(static_cast<size_t>(page));
        return QSizeF(p->getWidth(), p->getHeight());
    };
    QObject* newDialog = find("newDocumentDialog");
    ASSERT_NE(newDialog, nullptr);
    QMetaObject::invokeMethod(newDialog, "open");
    ASSERT_TRUE(waitOpened(newDialog, true));
    auto* paperBox = findItem("paperBox");
    if (!paperBox) {
        paperBox = find<QQuickItem>("paperBox");
    }
    ASSERT_NE(paperBox, nullptr);
    newDialog->setProperty("landscape", false);
    paperBox->setProperty("currentIndex", slide);
    QMetaObject::invokeMethod(paperBox, "activated", Q_ARG(int, slide));
    EXPECT_TRUE(newDialog->property("landscape").toBool()) << "a slide is landscape";
    const int tabs = controller->tabCount();
    QMetaObject::invokeMethod(newDialog, "create");
    ASSERT_TRUE(waitOpened(newDialog, false));
    ASSERT_EQ(controller->tabCount(), tabs + 1);
    EXPECT_EQ(sizeOf(0), QSizeF(960, 540));

    // Inserted after an A4 page
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    settings->set("paperFormat", settings->paperFormats().indexOf("A4"));
    settings->set("landscape", false);
    controller->newDocument();
    ASSERT_NEAR(sizeOf(0).width(), 595.3, 0.1);
    QObject* insert = find("insertPagesDialog");
    ASSERT_NE(insert, nullptr);
    QMetaObject::invokeMethod(insert, "openAt", Q_ARG(QVariant, QVariant::fromValue(1)));
    ASSERT_TRUE(waitOpened(insert, true));
    auto* insertBox = findItem("insertPaperBox");
    if (!insertBox) {
        insertBox = find<QQuickItem>("insertPaperBox");
    }
    ASSERT_NE(insertBox, nullptr);
    EXPECT_FALSE(insert->property("landscape").toBool());
    insertBox->setProperty("currentIndex", slide + 1);
    QMetaObject::invokeMethod(insertBox, "activated", Q_ARG(int, slide + 1));
    EXPECT_TRUE(insert->property("landscape").toBool());
    QMetaObject::invokeMethod(insert, "insert");
    ASSERT_TRUE(waitOpened(insert, false));
    ASSERT_EQ(controller->pageCount(), 2);
    EXPECT_EQ(sizeOf(1), QSizeF(960, 540));
}

// Posters and flashcards (qt/page-sizes): A0 in the New document dialog, A7 cards inserted after it (landscape), and a
// size of new pages that is none of the formats (Xournal++'s custom size) stays when a document is made with it.
TEST_F(MainWindowTest, postersAndFlashcardsFromTheDialogs) {
    auto* settings = qobject_cast<xqt::SettingsModel*>(controller->settingsModel());
    ASSERT_NE(settings, nullptr);
    const QStringList formats = settings->paperFormats();
    const int a0 = formats.indexOf("A0"), a7 = formats.indexOf("A7");
    ASSERT_EQ(a0, 0);
    ASSERT_EQ(a7, 7);
    auto sizeOf = [&](int page) {
        auto* s = controller->tabManager().currentSession();
        const PageRef p = s->getDocument()->getPage(static_cast<size_t>(page));
        return QSizeF(p->getWidth(), p->getHeight());
    };
    auto near = [](QSizeF a, QSizeF b) { return std::abs(a.width() - b.width()) < 0.01 && std::abs(a.height() - b.height()) < 0.01; };
    QObject* newDialog = find("newDocumentDialog");
    ASSERT_NE(newDialog, nullptr);
    auto openNew = [&]() -> QQuickItem* {
        QMetaObject::invokeMethod(newDialog, "open");
        EXPECT_TRUE(waitOpened(newDialog, true));
        auto* box = findItem("paperBox");
        return box ? box : find<QQuickItem>("paperBox");
    };
    QQuickItem* paperBox = openNew();
    ASSERT_NE(paperBox, nullptr);
    EXPECT_EQ(paperBox->property("count").toInt(), formats.size()) << "all formats, nothing else";
    newDialog->setProperty("landscape", false);
    paperBox->setProperty("currentIndex", a0);
    QMetaObject::invokeMethod(paperBox, "activated", Q_ARG(int, a0));
    const int tabs = controller->tabCount();
    QMetaObject::invokeMethod(newDialog, "create");
    ASSERT_TRUE(waitOpened(newDialog, false));
    ASSERT_EQ(controller->tabCount(), tabs + 1);
    EXPECT_TRUE(near(sizeOf(0), xqt::SettingsModel::paperSize(a0))) << "A0: 841 x 1189 mm";

    // A7 flashcards after it, landscape
    QObject* insert = find("insertPagesDialog");
    ASSERT_NE(insert, nullptr);
    QMetaObject::invokeMethod(insert, "openAt", Q_ARG(QVariant, QVariant::fromValue(1)));
    ASSERT_TRUE(waitOpened(insert, true));
    auto* insertBox = findItem("insertPaperBox");
    if (!insertBox) {
        insertBox = find<QQuickItem>("insertPaperBox");
    }
    ASSERT_NE(insertBox, nullptr);
    insertBox->setProperty("currentIndex", a7 + 1);
    QMetaObject::invokeMethod(insertBox, "activated", Q_ARG(int, a7 + 1));
    insert->setProperty("landscape", true);
    QMetaObject::invokeMethod(insert, "insert");
    ASSERT_TRUE(waitOpened(insert, false));
    ASSERT_EQ(controller->pageCount(), 2);
    EXPECT_TRUE(near(sizeOf(1), xqt::SettingsModel::paperSize(a7).transposed())) << "A7 landscape: 105 x 74 mm";

    // Another size (set in Xournal++): offered as it is, and kept
    auto* s = controller->tabManager().currentSession();
    PageTemplateSettings tpl = s->getSettings()->getPageTemplateSettings();
    tpl.setPageWidth(300);
    tpl.setPageHeight(500);
    s->getSettings()->setPageTemplateSettings(tpl);
    paperBox = openNew();
    ASSERT_NE(paperBox, nullptr);
    EXPECT_EQ(paperBox->property("count").toInt(), formats.size() + 1);
    EXPECT_EQ(paperBox->property("currentIndex").toInt(), formats.size()) << "the other size, not A0";
    EXPECT_EQ(newDialog->property("otherPaper").toString(), QString("106 × 176 mm"));
    newDialog->setProperty("landscape", false);
    QMetaObject::invokeMethod(newDialog, "create");
    ASSERT_TRUE(waitOpened(newDialog, false));
    EXPECT_EQ(sizeOf(0), QSizeF(300, 500));
}

// Scrolling sideways from the layout menu: the pages in a row, ‹ › in the pill and the arrow keys go from page to
// page; kept in upstream's settings (viewFixedRows, viewRows) and ours (snapPages).
TEST_F(MainWindowTest, pagesSideBySideScrollSideways) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto& vc = view->getViewController();
    auto settle = [&] { until([&] { return !vc.isAnimating(); }, 2000); };
    auto* previous = find<QQuickItem>("previousPageButton");
    auto* next = find<QQuickItem>("nextPageButton");
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_FALSE(next->isVisible()) << "not while the pages go down";

    // From the layout menu (press and hold on the layout button)
    auto* layoutMenu = find<QObject>("layoutMenu");
    auto* layoutButton = find<QQuickItem>("layoutButton");
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier,
                      layoutButton->mapToScene(QPointF(layoutButton->width() / 2, layoutButton->height() / 2)).toPoint());
    ASSERT_TRUE(waitOpened(layoutMenu, true));
    QQuickItem* sideways = findItem("sidewaysItem");
    if (!sideways) {
        sideways = find<QQuickItem>("sidewaysItem");
    }
    ASSERT_NE(sideways, nullptr);
    QMetaObject::invokeMethod(sideways, "triggered");
    QMetaObject::invokeMethod(layoutMenu, "close");
    ASSERT_TRUE(waitOpened(layoutMenu, false));
    wait(50);
    EXPECT_TRUE(controller->horizontalScrolling());
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    EXPECT_TRUE(s->getSettings()->isViewFixedRows()) << "upstream's setting";
    ASSERT_TRUE(view->documentLayout().horizontal());
    EXPECT_EQ(view->documentLayout().rows(), 1u);
    EXPECT_NEAR(vc.zoom(), view->documentLayout().fitHeightZoom(vc.viewSize().height()), 1e-6) << "fit to the height";
    EXPECT_TRUE(next->isVisible()) << "the pill has ‹ ›";
    EXPECT_TRUE(previous->isVisible());
    ASSERT_EQ(controller->pageNumber(), 1);

    click(next);
    settle();
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 2);
    key(Qt::Key_Right);
    settle();
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 3);
    key(Qt::Key_PageDown);
    settle();
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 4);
    key(Qt::Key_PageUp);
    settle();
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 3);
    click(previous);
    settle();
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 2);
    key(Qt::Key_End);
    wait(30);
    EXPECT_EQ(controller->pageNumber(), controller->pageCount());
    key(Qt::Key_Home);
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 1);
    // The number jump too
    key(Qt::Key_5);
    key(Qt::Key_Return);
    wait(30);
    EXPECT_EQ(controller->pageNumber(), 5);
    EXPECT_NEAR(vc.scrollPosition().x(), vc.restRange(view->documentLayout().groupOf(4)).first, 1)
            << "the page where it rests";

    // Two rows; snapping off
    controller->setViewRows(2);
    wait(30);
    EXPECT_EQ(view->documentLayout().rows(), 2u);
    EXPECT_EQ(find<QQuickItem>("columnsLabel")->property("text").toString(), "2") << "the menu counts rows now";
    controller->setSnapPages(false);
    EXPECT_FALSE(vc.snapping());
    EXPECT_FALSE(controller->snapPages());

    // Back to pages going down
    controller->setViewRows(1);
    controller->setSnapPages(true);
    controller->setHorizontalScrolling(false);
    wait(30);
    EXPECT_FALSE(view->documentLayout().horizontal());
    EXPECT_FALSE(s->getSettings()->isViewFixedRows());
    EXPECT_FALSE(next->isVisible());
    key(Qt::Key_Right);
    EXPECT_EQ(controller->pageNumber(), 5) << "the arrow keys are not for pages going down";
}

// Presenting: F5 or the tool bar's button goes full screen with a page filling it; Space and the arrow keys go page
// by page like PowerPoint, a finger swipes one page on, the pen writes; Escape goes back to editing in full screen
// (the zoom from before), a second Escape leaves full screen.
TEST_F(MainWindowTest, presentingGoesPageByPageAndBackToEditing) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto& vc = view->getViewController();
    auto settle = [&] {
        until([&] { return !vc.isAnimating(); }, 2000);
        wait(30);
    };
    const double editZoom = vc.zoom();
    auto* pill = find<QQuickItem>("viewPill");
    auto* square = find<QQuickItem>("quickToolSquare");
    auto* indicator = find<QQuickItem>("presentPageIndicator");
    ASSERT_NE(indicator, nullptr);

    key(Qt::Key_F5);
    until([&] { return window->property("fullScreenMode").toBool(); });
    EXPECT_TRUE(window->property("fullScreenMode").toBool()) << "presenting is full screen";
    ASSERT_TRUE(controller->presenting());
    EXPECT_TRUE(view->isPresenting());
    wait(100);
    EXPECT_EQ(controller->pageNumber(), 1);
    EXPECT_FALSE(pill->isVisible()) << "a clean page";
    EXPECT_TRUE(square->isVisible()) << "the tools stay at hand";
    EXPECT_TRUE(indicator->isVisible()) << "the page number, for a moment";
    EXPECT_EQ(find<QQuickItem>("presentPageIndicatorText")->property("text").toString(),
              QString("1 / %1").arg(controller->pageCount()));
    const QRectF first = view->pageViewRect(0);
    const QSizeF size = vc.viewSize();
    EXPECT_TRUE(std::abs(first.height() - size.height()) < 1 || std::abs(first.width() - size.width()) < 1)
            << "the page fills the screen";
    EXPECT_NEAR(first.center().x(), size.width() / 2, 1);

    const std::vector<std::pair<Qt::Key, int>> steps{{Qt::Key_Space, 2},    {Qt::Key_Right, 3}, {Qt::Key_Down, 4},
                                                     {Qt::Key_PageDown, 5}, {Qt::Key_Left, 4},  {Qt::Key_Up, 3},
                                                     {Qt::Key_PageUp, 2},   {Qt::Key_Backspace, 1}};
    for (const auto& [k, page]: steps) {
        key(k);
        settle();
        EXPECT_EQ(controller->pageNumber(), page) << "after key " << k;
    }
    key(Qt::Key_End);
    settle();
    EXPECT_EQ(controller->pageNumber(), controller->pageCount());
    key(Qt::Key_Home);
    settle();
    EXPECT_EQ(controller->pageNumber(), 1);
    key(Qt::Key_4);
    key(Qt::Key_Return);
    settle();
    EXPECT_EQ(controller->pageNumber(), 4) << "a page number and Enter";

    // A finger swipes one page on
    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint from = canvasItem->mapToScene(QPointF(size.width() * 0.7, size.height() / 2)).toPoint();
    QTest::touchEvent(window, finger).press(0, from);
    for (int i = 1; i <= 6; ++i) {
        QTest::qWait(10);
        QTest::touchEvent(window, finger).move(0, from - QPoint(40 * i, 0));
    }
    QTest::touchEvent(window, finger).release(0, from - QPoint(240, 0));
    settle();
    EXPECT_EQ(controller->pageNumber(), 5) << "swiped on";

    // The pen writes on the page, it does not page
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    auto strokes = [&] { return s->getDocument()->getPage(4)->getSelectedLayer()->getElementsView().size(); };
    const size_t before = strokes();
    const QRectF page = view->pageViewRect(4);
    const QPoint a = canvasItem->mapToScene(page.center()).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (int i = 1; i <= 8; ++i) {
        QTest::mouseMove(window, a + QPoint(-20 * i, 10 * i));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(-160, 80));
    settle();
    EXPECT_EQ(strokes(), before + 1) << "written";
    EXPECT_EQ(controller->pageNumber(), 5) << "still on the page";
    EXPECT_EQ(view->pageViewRect(4), page);

    // Escape: editing in full screen again, with the zoom from before
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting());
    EXPECT_TRUE(window->property("fullScreenMode").toBool()) << "still full screen";
    EXPECT_FALSE(view->documentLayout().horizontal());
    EXPECT_NEAR(vc.zoom(), editZoom, 1e-6);
    EXPECT_EQ(controller->pageNumber(), 5);
    EXPECT_TRUE(pill->isVisible());

    // From the tools of the full screen: present, and back
    QMetaObject::invokeMethod(find("quickTools"), "open");
    ASSERT_TRUE(waitOpened(find("quickTools"), true));
    QQuickItem* toggle = findItem("presentToggleButton");
    if (!toggle) {
        toggle = find<QQuickItem>("presentToggleButton");
    }
    ASSERT_NE(toggle, nullptr);
    QMetaObject::invokeMethod(toggle, "clicked");
    EXPECT_TRUE(controller->presenting());
    EXPECT_EQ(controller->pageNumber(), 5) << "from the current page";
    EXPECT_TRUE(waitOpened(find("quickTools"), false)) << "the tools close";
    // Leaving full screen ends presenting too
    key(Qt::Key_F11);
    EXPECT_FALSE(window->property("fullScreenMode").toBool());
    EXPECT_FALSE(controller->presenting());

    // The tool bar's button: full screen and presenting at once
    window->setWidth(2000);  // (room for the whole tool bar)
    wait(100);
    until([&] { return !window->property("leavingFullScreen").toBool(); }, 2000);
    auto* present = findItem("presentButton");
    ASSERT_NE(present, nullptr);
    ASSERT_TRUE(present->isVisible());
    click(present);
    until([&] { return window->property("fullScreenMode").toBool(); });
    EXPECT_TRUE(controller->presenting());
    key(Qt::Key_Escape);
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting());
    EXPECT_FALSE(window->property("fullScreenMode").toBool()) << "the second Escape leaves full screen";
}

// qt/present-clean: Ctrl+F5 presents without controls: no pen pill, no tool square, no page number, only a faint
// mark in the lower left corner, a finger wide. A tap on it (finger or mouse) brings the controls back, another hides
// them; Ctrl+F5 does the same while presenting. The keys and the pen work as ever. Escape ends it; F5 presents with
// the controls. The mark is not there outside presenting.
TEST_F(MainWindowTest, presentingWithoutControls) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(50);
    controller->selectTool("pen");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    auto& vc = view->getViewController();
    auto settle = [&] {
        until([&] { return !vc.isAnimating(); }, 2000);
        wait(30);
    };
    auto* penPill = find<QQuickItem>("penPill");
    auto* square = find<QQuickItem>("quickToolSquare");
    auto* mark = find<QQuickItem>("presentCornerMark");
    auto* number = find<QQuickItem>("presentPageIndicator");
    ASSERT_NE(penPill, nullptr);
    ASSERT_NE(square, nullptr);
    ASSERT_NE(mark, nullptr);
    ASSERT_NE(number, nullptr);
    EXPECT_FALSE(mark->isVisible()) << "no mark while editing";
    auto clean = [&] { return window->property("presentClean").toBool(); };

    key(Qt::Key_F5, Qt::ControlModifier);
    until([&] { return window->property("fullScreenMode").toBool(); });
    ASSERT_TRUE(controller->presenting());
    wait(100);
    EXPECT_TRUE(clean());
    EXPECT_TRUE(view->isPresenting());
    EXPECT_FALSE(penPill->isVisible()) << "no pill";
    EXPECT_FALSE(square->isVisible()) << "no tool square";
    EXPECT_FALSE(number->isVisible()) << "not even the page number";
    EXPECT_FALSE(find<QQuickItem>("viewPill")->isVisible());
    EXPECT_FALSE(find<QQuickItem>("fullScreenTabs")->isVisible());
    ASSERT_TRUE(mark->isVisible()) << "the mark in the corner";

    // In the lower left corner, a finger wide; what shows of it is a few faint pixels
    const QRectF target = mark->mapRectToScene(QRectF(0, 0, mark->width(), mark->height()));
    EXPECT_GE(target.width(), 40);
    EXPECT_GE(target.height(), 40);
    EXPECT_NEAR(target.left(), 0, 1);
    EXPECT_NEAR(target.bottom(), window->contentItem()->height(), 1);
    auto* dot = findItem("presentCornerDot");
    ASSERT_NE(dot, nullptr);
    EXPECT_LE(dot->width(), 8);
    EXPECT_LE(dot->opacity(), 0.2) << "barely there";
    // The pointer over it: clearer
    const QPoint markCenter = target.center().toPoint();
    QTest::mouseMove(window, markCenter);
    until([&] { return dot->opacity() > 0.4; }, 2000);
    EXPECT_GT(dot->opacity(), 0.4);
    QTest::mouseMove(window, canvasItem->mapToScene(QPointF(canvasItem->width() / 2, 40)).toPoint());
    until([&] { return dot->opacity() < 0.2; }, 2000);
    EXPECT_LE(dot->opacity(), 0.2);

    // The keys page, the pen writes
    key(Qt::Key_Space);
    settle();
    EXPECT_EQ(controller->pageNumber(), 2);
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    auto strokes = [&] { return s->getDocument()->getPage(1)->getSelectedLayer()->getElementsView().size(); };
    const size_t before = strokes();
    const QPoint a = canvasItem->mapToScene(view->pageViewRect(1).center()).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (int i = 1; i <= 8; ++i) {
        QTest::mouseMove(window, a + QPoint(-20 * i, 10 * i));
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(-160, 80));
    settle();
    EXPECT_EQ(strokes(), before + 1) << "written";

    // A finger taps the mark: the controls are back, still presenting
    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    QTest::touchEvent(window, finger).press(0, markCenter);
    QTest::touchEvent(window, finger).release(0, markCenter);
    wait(50);
    EXPECT_FALSE(clean());
    EXPECT_TRUE(controller->presenting());
    EXPECT_TRUE(penPill->isVisible()) << "the pill is back";
    EXPECT_TRUE(square->isVisible()) << "the tool square is back";
    EXPECT_TRUE(mark->isVisible()) << "the mark stays, to hide them again";
    EXPECT_EQ(strokes(), before + 1) << "the tap did not write";
    key(Qt::Key_Space);
    settle();
    EXPECT_EQ(controller->pageNumber(), 3) << "the keys stay with the page";
    // Another tap (the mouse this time): hidden again
    click(mark);
    EXPECT_TRUE(clean());
    EXPECT_FALSE(penPill->isVisible());
    EXPECT_FALSE(square->isVisible());
    // Ctrl+F5 while presenting does the same
    key(Qt::Key_F5, Qt::ControlModifier);
    EXPECT_FALSE(clean());
    EXPECT_TRUE(square->isVisible());
    EXPECT_TRUE(controller->presenting());
    key(Qt::Key_F5, Qt::ControlModifier);
    EXPECT_TRUE(clean());
    EXPECT_FALSE(square->isVisible());

    // Escape ends presenting: full-screen editing with its tools, no mark
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting());
    EXPECT_FALSE(clean());
    EXPECT_TRUE(window->property("fullScreenMode").toBool());
    EXPECT_TRUE(square->isVisible());
    EXPECT_FALSE(mark->isVisible());

    // F5: with the controls (a clean presentation is not remembered), and the mark to hide them
    key(Qt::Key_F5);
    ASSERT_TRUE(controller->presenting());
    wait(50);
    EXPECT_FALSE(clean());
    EXPECT_TRUE(square->isVisible());
    EXPECT_TRUE(penPill->isVisible());
    EXPECT_TRUE(mark->isVisible());
    key(Qt::Key_Escape);
    key(Qt::Key_Escape);
    EXPECT_FALSE(controller->presenting());
    EXPECT_FALSE(window->property("fullScreenMode").toBool());
    EXPECT_FALSE(mark->isVisible());
}

// qt/present-clean: holding the presentation button (or a right click on it, or ⋮ → "Present without controls")
// presents without controls; a click presents with them. The shortcut is in Settings → Shortcuts.
TEST_F(MainWindowTest, presentButtonHeldPresentsWithoutControls) {
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    window->setWidth(2000);  // (room for the whole tool bar)
    wait(100);
    auto* present = findItem("presentButton");
    ASSERT_NE(present, nullptr);
    ASSERT_TRUE(present->isVisible());
    auto clean = [&] { return window->property("presentClean").toBool(); };
    auto back = [&] {
        key(Qt::Key_Escape);
        key(Qt::Key_Escape);
        ASSERT_FALSE(controller->presenting());
        until([&] { return !window->property("leavingFullScreen").toBool(); }, 2000);
        window->setWidth(2000);
        wait(100);
        ASSERT_TRUE(present->isVisible());
    };
    const QPoint at = present->mapToScene(QPointF(present->width() / 2, present->height() / 2)).toPoint();

    // Held
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 300);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
    until([&] { return window->property("fullScreenMode").toBool(); });
    ASSERT_TRUE(controller->presenting());
    EXPECT_TRUE(clean()) << "held: only the page";
    EXPECT_FALSE(find<QQuickItem>("quickToolSquare")->isVisible());
    EXPECT_TRUE(find<QQuickItem>("presentCornerMark")->isVisible());
    back();

    // Right click
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, at);
    until([&] { return controller->presenting(); });
    ASSERT_TRUE(controller->presenting());
    EXPECT_TRUE(clean()) << "right click: only the page";
    back();

    // A click: with the controls
    click(present);
    until([&] { return controller->presenting(); });
    ASSERT_TRUE(controller->presenting());
    EXPECT_FALSE(clean());
    EXPECT_TRUE(find<QQuickItem>("quickToolSquare")->isVisible());
    back();

    // The ⋮ menu has it too
    auto* item = find<QObject>("presentCleanItem");
    ASSERT_NE(item, nullptr);
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return controller->presenting(); });
    EXPECT_TRUE(controller->presenting());
    EXPECT_TRUE(clean());
    back();

    // Settings → Shortcuts lists it, on Ctrl+F5, and it can have other keys
    auto* shortcuts = qobject_cast<QAbstractItemModel*>(controller->shortcutsModel());
    ASSERT_NE(shortcuts, nullptr);
    const auto roles = shortcuts->roleNames();
    const int idRole = roles.key("actionId"), nameRole = roles.key("name"), keysRole = roles.key("keys");
    int row = -1;
    for (int r = 0; r < shortcuts->rowCount(); ++r) {
        if (shortcuts->data(shortcuts->index(r, 0), idRole).toString() == "presentClean") {
            row = r;
        }
    }
    ASSERT_GE(row, 0) << "listed";
    EXPECT_EQ(shortcuts->data(shortcuts->index(row, 0), nameRole).toString(), "Present without controls");
    EXPECT_EQ(shortcuts->data(shortcuts->index(row, 0), keysRole).toString(), "Ctrl+F5");
    bool changed = false;
    QMetaObject::invokeMethod(shortcuts, "setKeys", Q_RETURN_ARG(bool, changed), Q_ARG(QString, "presentClean"),
                              Q_ARG(QString, "Ctrl+Alt+F5"));
    ASSERT_TRUE(changed);
    wait(50);
    key(Qt::Key_F5, Qt::ControlModifier);
    EXPECT_FALSE(controller->presenting()) << "not the old keys";
    key(Qt::Key_F5, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return controller->presenting(); });
    EXPECT_TRUE(controller->presenting());
    EXPECT_TRUE(clean()) << "the new keys";
    QMetaObject::invokeMethod(shortcuts, "resetAll");
}

// Full screen (editing): a slim bar at the top shows the tabs as dots; a tap opens the overview of the documents, a
// swipe along it goes to the next or previous document (its title shows for a moment). Not with one tab, not while
// presenting; with many tabs "3 / 17" instead of dots.
TEST_F(MainWindowTest, fullScreenTabDotsSwitchDocuments) {
    auto* bar = find<QQuickItem>("fullScreenTabs");
    ASSERT_NE(bar, nullptr);
    key(Qt::Key_F11);
    ASSERT_TRUE(window->property("fullScreenMode").toBool());
    wait(50);
    EXPECT_FALSE(bar->isVisible()) << "one tab: nothing to switch";

    controller->newDocument();
    controller->newDocument();
    wait(50);
    ASSERT_EQ(controller->tabCount(), 3);
    ASSERT_EQ(controller->currentTab(), 2);
    ASSERT_TRUE(bar->isVisible());
    auto* dots = find<QQuickItem>("fullScreenTabDots");
    ASSERT_NE(dots, nullptr);
    EXPECT_TRUE(dots->isVisible());
    EXPECT_EQ(dots->property("count").toInt(), 3);
    EXPECT_EQ(dots->property("currentIndex").toInt(), 2);
    EXPECT_GE(bar->height(), 24) << "big enough for a finger";
    EXPECT_LT(bar->mapToScene(QPointF(0, bar->height())).y(), 40) << "at the top";
    auto* square = find<QQuickItem>("quickToolSquare");
    const QRectF barRect(bar->mapToScene(QPointF(0, 0)), bar->size());
    const QRectF squareRect(square->mapToScene(QPointF(0, 0)), square->size());
    EXPECT_FALSE(barRect.intersects(squareRect)) << "not over the tool square";

    // A swipe along the bar: the next / previous document, and its title for a moment
    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    auto swipe = [&](int dx) {
        const QPoint from = bar->mapToScene(QPointF(bar->width() / 2, bar->height() / 2)).toPoint();
        QTest::touchEvent(window, finger).press(0, from);
        for (int i = 1; i <= 8; ++i) {
            QTest::touchEvent(window, finger).move(0, from + QPoint(dx * i / 8, 0));
            wait(10);
        }
        QTest::touchEvent(window, finger).release(0, from + QPoint(dx, 0));
        wait(80);
    };
    swipe(90);  // to the right: back
    EXPECT_EQ(controller->currentTab(), 1);
    auto* toast = find<QQuickItem>("fullScreenTabToast");
    ASSERT_NE(toast, nullptr);
    EXPECT_TRUE(toast->isVisible()) << "the title shows";
    EXPECT_EQ(find<QQuickItem>("fullScreenTabToastText")->property("text").toString(), controller->title());
    swipe(-90);  // to the left: on
    EXPECT_EQ(controller->currentTab(), 2);
    EXPECT_TRUE(window->property("fullScreenMode").toBool()) << "still full screen";

    // The small arrows at the ends of the bar: previous / next (the last one: "next" does nothing)
    auto* previous = find<QQuickItem>("fullScreenTabPrevious");
    auto* next = find<QQuickItem>("fullScreenTabNext");
    ASSERT_NE(previous, nullptr);
    ASSERT_NE(next, nullptr);
    click(previous);
    EXPECT_EQ(controller->currentTab(), 1);
    click(previous);
    EXPECT_EQ(controller->currentTab(), 0);
    click(previous);
    EXPECT_EQ(controller->currentTab(), 0) << "the first: nothing before it";
    click(next);
    click(next);
    EXPECT_EQ(controller->currentTab(), 2);
    EXPECT_FALSE(find<QObject>("tabOverview")->property("visible").toBool()) << "an arrow does not open the overview";

    // A tap: the overview of the open documents
    QObject* overview = find("tabOverview");
    click(bar);
    ASSERT_TRUE(waitOpened(overview, true));
    QMetaObject::invokeMethod(overview, "close");
    ASSERT_TRUE(waitOpened(overview, false));

    // Not while presenting
    controller->setPresenting(true);
    wait(30);
    EXPECT_FALSE(bar->isVisible());
    controller->setPresenting(false);
    wait(30);
    EXPECT_TRUE(bar->isVisible());

    // Many tabs: a count instead of dots
    for (int i = 0; i < 14; ++i) {
        controller->newDocument();
    }
    wait(50);
    ASSERT_EQ(controller->tabCount(), 17);
    EXPECT_FALSE(dots->isVisible());
    auto* count = find<QQuickItem>("fullScreenTabCount");
    ASSERT_NE(count, nullptr);
    EXPECT_TRUE(count->isVisible());
    EXPECT_EQ(count->property("text").toString(), "17 / 17");
    key(Qt::Key_F11);
}

// Share… on a library card: a PDF is shared as it is (without opening it); a card of notes opens, then the Share
// choices are for that document.
TEST_F(HomeScreenFilterTest, shareFromALibraryCard) {
    QObject* home = find("homeView");
    ASSERT_NE(home, nullptr);
    QObject* dialog = find("shareDialog");
    ASSERT_NE(dialog, nullptr);
    const QString pdf = QString::fromStdString((root / "lecture.pdf").string());
    QMetaObject::invokeMethod(home, "shareRequested", Q_ARG(QString, pdf));
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(dialog->property("file").toString(), pdf);
    click(find<QQuickItem>("sharePdfChoice"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(fake.shared, QStringList{pdf});
    EXPECT_EQ(controller->tabCount(), 0) << "not opened";

    const QString notes = QString::fromStdString((root / "notes.xopp").string());
    QMetaObject::invokeMethod(home, "shareRequested", Q_ARG(QString, notes));
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(dialog->property("file").toString(), QString());
    EXPECT_EQ(controller->title(), "notes.xopp") << "opened, shared as the open document";
    EXPECT_EQ(controller->shareStep(), "ask");
    QMetaObject::invokeMethod(dialog, "close");
    ASSERT_TRUE(waitOpened(dialog, false));

    // A PDF with notes that holds earlier revisions (incremental updates): written anew in one piece, then shared
    const fs::path hybrid = root / "withnotes.pdf";
    {
        auto loaded = xqt::DocumentSession::loadFile(root / "lecture.pdf");
        ASSERT_TRUE(loaded.document);
        ASSERT_TRUE(xqt::HybridPdf::write(*loaded.document, hybrid).ok);
        xqt::IncrementalPdf::Tail tail;
        std::string error;
        ASSERT_TRUE(xqt::IncrementalPdf::readTail(hybrid, tail, error));
        QPDF q;
        q.processFile(hybrid.string().c_str());
        xqt::IncrementalPdf::Update u(q);
        QPDFObjectHandle info = q.getTrailer().getKey("/Info");
        u.touch(info);
        info.replaceKey("/Subject", QPDFObjectHandle::newString("a later revision"));
        ASSERT_TRUE(xqt::IncrementalPdf::append(hybrid, tail, u.serialize(tail)).ok);
    }
    ASSERT_TRUE(xqt::HybridPdf::hasEarlierRevisions(hybrid));
    const QString withNotes = QString::fromStdString(hybrid.string());
    fake.shared.clear();
    QMetaObject::invokeMethod(home, "shareRequested", Q_ARG(QString, withNotes));
    ASSERT_TRUE(waitOpened(dialog, true));
    click(find<QQuickItem>("sharePdfChoice"));
    until([&] { return fake.shared.size() == 1; }, 20000);
    EXPECT_EQ(fake.shared, QStringList{withNotes});
    EXPECT_FALSE(xqt::HybridPdf::hasEarlierRevisions(hybrid));
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(hybrid));
}

// --- the document mode (PDF files or Xournal++ files; session/DocumentMode.h) ---------------------------------------

// Every UI test runs with XQT_DOCUMENT_MODE=xopp (tests/ui/main.cpp): the first-start question never comes up in
// front of what a test clicks, and nothing is stored by it.
TEST_F(MainWindowTest, theFirstStartQuestionStaysAwayInTests) {
    Settings& settings = *controller->context().getSettings();
    EXPECT_EQ(xqt::DocumentMode::stored(settings), xqt::DocumentMode::Mode::Unset);
    EXPECT_FALSE(controller->askDocumentMode());
    EXPECT_EQ(controller->documentMode(), "xopp");
    EXPECT_FALSE(controller->pdfOnly());
    QObject* dialog = find("documentModeDialog");
    ASSERT_NE(dialog, nullptr);
    EXPECT_FALSE(dialog->property("visible").toBool());
}

namespace {
/// A first start: no document mode stored, XQT_DOCUMENT_MODE not set.
class FirstStartTest: public MainWindowTest {
protected:
    void SetUp() override {
        before = qgetenv("XQT_DOCUMENT_MODE");
        qunsetenv("XQT_DOCUMENT_MODE");
        MainWindowTest::SetUp();
    }
    void TearDown() override {
        forget();
        MainWindowTest::TearDown();
        qputenv("XQT_DOCUMENT_MODE", before);
    }
    void prepareController() override {
        forget();  // (also an install that had the app before the question: nothing stored)
        controller->newDocument();
    }
    void forget() { xqt::DocumentMode::store(*controller->context().getSettings(), xqt::DocumentMode::Mode::Unset); }
    xqt::DocumentMode::Mode stored() const { return xqt::DocumentMode::stored(*controller->context().getSettings()); }
    QByteArray before;
};
}  // namespace

// The first start asks how to keep documents: two cards (PDF files, Xournal++ files) with what each means, the
// recommendation chosen to start with and marked, and a line that it can be changed later. Only "Continue" closes it;
// then the choice is stored and not asked again.
TEST_F(FirstStartTest, asksOnceHowToKeepDocuments) {
    QObject* dialog = find("documentModeDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true)) << "asked at the first start";
    EXPECT_TRUE(controller->askDocumentMode());
    auto* pdf = findItem("documentModePdfCard");
    auto* xopp = findItem("documentModeXoppCard");
    ASSERT_NE(pdf, nullptr);
    ASSERT_NE(xopp, nullptr);
    EXPECT_EQ(pdf->property("text").toString(), "PDF files");
    EXPECT_TRUE(pdf->property("subtitle").toString().contains("Drawboard PDF"));
    EXPECT_EQ(pdf->property("badge").toString(), "Recommended for most people");
    EXPECT_EQ(xopp->property("text").toString(), "Xournal++ files");
    EXPECT_EQ(xopp->property("badge").toString(), "Recommended if you also use Xournal++");
    EXPECT_TRUE(pdf->property("chosen").toBool()) << "the recommendation, to start with";
    EXPECT_FALSE(xopp->property("chosen").toBool());
    EXPECT_LT(pdf->mapToScene(QPointF(0, 0)).y(), xopp->mapToScene(QPointF(0, 0)).y()) << "the recommendation first";

    key(Qt::Key_Escape);
    wait(100);
    EXPECT_TRUE(dialog->property("visible").toBool()) << "only Continue closes it";
    click(xopp);
    EXPECT_TRUE(xopp->property("chosen").toBool());
    EXPECT_FALSE(pdf->property("chosen").toBool());
    EXPECT_EQ(stored(), xqt::DocumentMode::Mode::Unset) << "stored on Continue";
    click(findItem("documentModeContinue"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(stored(), xqt::DocumentMode::Mode::Xopp);
    EXPECT_FALSE(controller->askDocumentMode()) << "asked once";
    EXPECT_FALSE(controller->pdfOnly());
    EXPECT_EQ(controller->documentMode(), "xopp");
}

TEST_F(FirstStartTest, continuingTakesTheRecommendation) {
    QObject* dialog = find("documentModeDialog");
    ASSERT_TRUE(waitOpened(dialog, true));
    click(findItem("documentModeContinue"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(stored(), xqt::DocumentMode::Mode::Pdf);
    EXPECT_TRUE(controller->pdfOnly());
    EXPECT_EQ(controller->saveFormat(), "pdf") << "a new document is saved as a PDF with notes";
}

// Settings → Documents shows the two cards; a tap changes the mode at once. In PDF files mode "Save notes into the PDF
// itself" is not offered (it is always so).
TEST_F(MainWindowTest, settingsChangeTheDocumentMode) {
    Settings& s = *controller->context().getSettings();
    QObject* sheet = find("settingsPage");
    key(Qt::Key_Comma, Qt::ControlModifier);
    ASSERT_TRUE(waitOpened(sheet, true));
    click(findItem("documentsTab"));
    auto* cards = findItem("documentModeCards");
    ASSERT_NE(cards, nullptr);
    until([&] { return cards->isVisible(); });
    auto* pdf = findItem("documentModePdfCard");
    auto* xopp = findItem("documentModeXoppCard");
    auto* intoPdf = findItem("hybridIntoPdfSwitch");
    ASSERT_NE(pdf, nullptr);
    ASSERT_NE(intoPdf, nullptr);
    EXPECT_TRUE(xopp->property("chosen").toBool()) << "the mode in effect (the tests': Xournal++ files)";
    EXPECT_TRUE(intoPdf->isVisible());
    // New text documents follow the mode while they are not chosen (qt/docs/md-pdf.md)
    auto* textRow = findItem("newTextDocumentsRow");
    ASSERT_NE(textRow, nullptr);
    QQuickItem* textCombo = textRow->childItems().last();
    EXPECT_EQ(textCombo->property("currentValue").toString(), "md");
    click(pdf);
    until([&] { return textCombo->property("currentValue").toString() == "pdf"; });
    EXPECT_EQ(textCombo->property("currentValue").toString(), "pdf");
    EXPECT_EQ(xqt::DocumentMode::stored(s), xqt::DocumentMode::Mode::Pdf);
    EXPECT_TRUE(controller->pdfOnly());
    EXPECT_TRUE(pdf->property("chosen").toBool());
    EXPECT_FALSE(xopp->property("chosen").toBool());
    until([&] { return !intoPdf->isVisible(); });
    EXPECT_FALSE(intoPdf->isVisible()) << "always so in PDF files mode";
    click(xopp);
    until([&] { return textCombo->property("currentValue").toString() == "md"; });
    EXPECT_EQ(textCombo->property("currentValue").toString(), "md");
    EXPECT_EQ(xqt::DocumentMode::stored(s), xqt::DocumentMode::Mode::Xopp);
    EXPECT_FALSE(controller->pdfOnly());
    EXPECT_TRUE(xopp->property("chosen").toBool());
    key(Qt::Key_Escape);
    ASSERT_TRUE(waitOpened(sheet, false));
    xqt::DocumentMode::store(s, xqt::DocumentMode::Mode::Unset);  // (the tests share the config folder)
}

// PDF files mode in the window: Save as starts on "PDF with notes" for a new document; Ctrl+S on an annotated PDF
// writes the notes into it without a dialog, and the first time a note says so.
TEST_F(MainWindowTest, pdfFilesModeSavesIntoThePdf) {
    Settings& s = *controller->context().getSettings();
    xqt::DocumentMode::store(s, xqt::DocumentMode::Mode::Pdf);
    QObject* dialog = find("saveDialog");
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(window, "setUpSaveDialog", Q_ARG(QVariant, QVariant(QString())));
    auto* filter = dialog->property("selectedNameFilter").value<QObject*>();
    ASSERT_NE(filter, nullptr);
    EXPECT_EQ(filter->property("index").toInt(), 1) << "PDF with notes";
    EXPECT_TRUE(dialog->property("selectedFile").toUrl().toLocalFile().endsWith(".pdf"));

    QTemporaryDir dir;
    const QString pdf = dir.filePath("lecture.pdf");
    makeLecturePdf(pdf, 2);
    ASSERT_TRUE(controller->openPath(pdf));
    drawStroke(*controller->tabManager().currentSession(), 0);
    key(Qt::Key_S, Qt::ControlModifier);
    until([&] { return !controller->anySaving() && !controller->modified(); }, 20000);
    EXPECT_FALSE(dialog->property("visible").toBool()) << "no dialog";
    EXPECT_FALSE(controller->modified());
    EXPECT_TRUE(xqt::HybridPdf::isHybrid(fs::path(pdf.toStdString())));
    EXPECT_EQ(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{"lecture.pdf"});
    auto* snackbarText = findItem("snackbarText");
    ASSERT_NE(snackbarText, nullptr);
    until([&] { return snackbarText->property("text").toString().startsWith("Your notes are saved in lecture.pdf"); });
    EXPECT_TRUE(snackbarText->property("text").toString().startsWith("Your notes are saved in lecture.pdf"))
            << snackbarText->property("text").toString().toStdString();
    xqt::DocumentMode::store(s, xqt::DocumentMode::Mode::Unset);  // (the tests share the config folder)
}

// --- Android: the libraries' home (qt/android-storage) -----------------------------------------------------------------

namespace {
/// Android without a phone: one window, "All files access" as the test says.
struct FakeAndroidApps: xqt::SystemApps {
    bool access = true;
    bool librariesInOwnWindows() override { return false; }
    bool hasAllFilesAccess() override { return access; }
    bool requestAllFilesAccess() override { return true; }
    bool openWithSystemApp(const QString&) override { return true; }
    bool showInFileManager(const QString&) override { return true; }
    bool startLibraryWindow(const QString&) override { return true; }
};

/// The libraries are in the app's own folder (the platform's folders are the test's).
class LibrariesHomeWindowTest: public MainWindowTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        const fs::path root(tmp.path().toStdString());
        phone.appDocuments = root / "storage/Android/data/org.xournalqt.app/files/Documents";
        phone.appDownloads = root / "storage/Android/data/org.xournalqt.app/files/Download";
        phone.sharedDocuments = root / "storage/Documents";
        phone.sharedDownloads = root / "storage/Download";
        phone.sharedStorage = root / "storage";
        inApp = phone.appDocuments / "Xournal_Libraries";
        shared = phone.sharedDocuments / "Xournal_Libraries";
        fs::create_directories(inApp / "Default/Lectures");
        std::ofstream(inApp / "Default/notes.xopp") << "not a real document";
        std::ofstream(inApp / "Default/Lectures/week 1.pdf") << "%PDF-1.4";
        xqt::Library::setPlatformFolders(&phone);
        xqt::SystemApps::setInstance(&fake);
        controller->chooseLibrariesHome();
        controller->setLibraryRoot(inApp / "Default");
    }
    void TearDown() override {
        MainWindowTest::TearDown();
        xqt::Library::setPlatformFolders(nullptr);
        xqt::SystemApps::setInstance(nullptr);
        std::error_code ec;
        fs::remove(Util::getConfigFile("settings.xml"), ec);  // (the home and the offer are settings)
    }
    QTemporaryDir tmp;
    xqt::PlatformFolders phone;
    fs::path inApp, shared;
    FakeAndroidApps fake;
};
}  // namespace

TEST_F(LibrariesHomeWindowTest, theOfferAtTheStartMovesTheLibraries) {
    QObject* dialog = find("librariesHomeDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true)) << "offered at the start";
    QQuickItem* note = findItem("librariesInAppNote");
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isVisible());
    click(findItem("librariesHomeContinue"));
    QObject* messageDialog = find("messageDialog");
    ASSERT_TRUE(waitOpened(messageDialog, true, 20000)) << "the result is said";
    EXPECT_TRUE(messageDialog->property("text").toString().contains("Documents/Xournal_Libraries"));
    EXPECT_TRUE(fs::exists(shared / "Default/Lectures/week 1.pdf"));
    EXPECT_FALSE(fs::exists(inApp)) << messageDialog->property("text").toString().toStdString();
    EXPECT_FALSE(note->isVisible()) << "no note any more";
    EXPECT_EQ(controller->libraryModel()->property("rootPath").toString(),
              QString::fromStdString(xqt::Library(shared / "Default").root().string()));
    QMetaObject::invokeMethod(messageDialog, "close");
}

TEST_F(LibrariesHomeWindowTest, notNowLeavesANoteThatAsksAgain) {
    QObject* dialog = find("librariesHomeDialog");
    ASSERT_TRUE(waitOpened(dialog, true));
    click(findItem("librariesHomeNotNow"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_FALSE(controller->offerLibrariesHome()) << "not at the next start";
    QQuickItem* note = findItem("librariesInAppNote");
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isVisible());
    EXPECT_TRUE(fs::exists(inApp / "Default/notes.xopp"));
    click(note);
    EXPECT_TRUE(waitOpened(dialog, true)) << "tapping the note asks again";
    QMetaObject::invokeMethod(dialog, "close");
}

namespace {
/// The libraries are in the phone's Documents already; the phone's storage has a Download folder with a paper.
class FolderChooserWindowTest: public LibrariesHomeWindowTest {
protected:
    void prepareController() override {
        ASSERT_TRUE(tmp.isValid());
        const fs::path root(tmp.path().toStdString());
        phone.appDocuments = root / "storage/Android/data/org.xournalqt.app/files/Documents";
        phone.sharedDocuments = root / "storage/Documents";
        phone.sharedDownloads = root / "storage/Download";
        phone.sharedStorage = root / "storage";
        shared = phone.sharedDocuments / "Xournal_Libraries";
        fs::create_directories(shared / "Default");
        fs::create_directories(phone.sharedDownloads / "Papers");
        std::ofstream(phone.sharedDownloads / "paper.pdf") << "%PDF-1.4";
        xqt::Library::setPlatformFolders(&phone);
        xqt::SystemApps::setInstance(&fake);
        controller->chooseLibrariesHome();  // (nothing to move: the phone's folder at once)
        controller->setLibraryRoot(xqt::Library::defaultRoot());
    }
};
}  // namespace

// Android's folder picker refuses the Download folder: with "All files access" the app lists the folders itself.
TEST_F(FolderChooserWindowTest, theDownloadFolderCanBeOpenedAsLibrary) {
    auto* home = find<QObject>("homeView");
    ASSERT_NE(home, nullptr);
    QMetaObject::invokeMethod(home, "pickLibraryFolder");
    QObject* chooser = find("folderChooser");
    ASSERT_NE(chooser, nullptr);
    ASSERT_TRUE(waitOpened(chooser, true)) << "the app's own list, not the system's picker";
    EXPECT_FALSE(findItem("folderChooserUse")->isEnabled()) << "not the whole storage";
    QStringList names;
    for (const QVariant& f: chooser->property("folders").toList()) {
        names << f.toMap().value("name").toString();
    }
    EXPECT_EQ(names, (QStringList{"Documents", "Download"}));
    // (the delegates are made by the list: found in the item tree)
    QQuickItem* download = nullptr;
    until([&] {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == "folderChooserEntry" && i->property("text").toString() == "Download") {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        download = walk(window->contentItem());
        return download != nullptr;
    });
    click(download);
    EXPECT_EQ(chooser->property("folder").toString(), QString::fromStdString(phone.sharedDownloads.string()));
    EXPECT_EQ(chooser->property("folders").toList().size(), 1) << "Papers";
    click(findItem("folderChooserUse"));
    ASSERT_TRUE(waitOpened(chooser, false));
    EXPECT_EQ(controller->libraryModel()->property("rootPath").toString(),
              QString::fromStdString(xqt::Library(phone.sharedDownloads).root().string()));
}

// The formatting bar (qt/docs/md-editor.md): shown while Markdown is written on the page; its buttons change the text
// at the cursor; the table editor edits cells in a grid and writes a pipe table.
TEST_F(MainWindowTest, theFormattingBarAndTheTableEditor) {
    controller->setMarkdownInPanel(false);
    auto* bar = find<QQuickItem>("markdownFormatBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible()) << "not while nothing is written";
    ASSERT_TRUE(controller->writeMarkdownOnPage());
    wait(50);
    EXPECT_TRUE(bar->isVisible()) << "while Markdown is written on the page";
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    type("Title");

    // The heading buttons (no menu): Heading 1; its button is the one highlighted at the cursor
    EXPECT_EQ(find<QObject>("mdHeadingMenu"), nullptr);
    EXPECT_TRUE(findItem("mdParagraph")->property("highlighted").toBool());
    click(findItem("mdHeading1"));
    EXPECT_EQ(editor->text(), "# Title");
    EXPECT_TRUE(findItem("mdHeading1")->property("highlighted").toBool()) << "the level at the cursor";
    EXPECT_FALSE(findItem("mdParagraph")->property("highlighted").toBool());
    EXPECT_TRUE(canvasItem->hasActiveFocus()) << "the text keeps the keys";

    // Bold around the selected word, the button checked
    key(Qt::Key_Return);
    type("word");
    key(Qt::Key_Left, Qt::ControlModifier | Qt::ShiftModifier);
    auto* bold = find<QQuickItem>("mdBold");
    EXPECT_FALSE(bold->property("checked").toBool());
    click(bold);
    EXPECT_EQ(editor->text(), "# Title\n\n**word**");
    EXPECT_TRUE(bold->property("checked").toBool()) << "bold at the cursor";

    // A new table: typed in its cells, a column removed, the pipe in a cell escaped
    click(find<QQuickItem>("mdTable"));
    auto* table = find<QObject>("tableEditor");
    ASSERT_NE(table, nullptr);
    ASSERT_TRUE(waitOpened(table, true));
    until([&] { auto* c = findItem("tableCell0_0"); return c && c->hasActiveFocus(); });
    type("Name");
    key(Qt::Key_Tab);
    type("Value");
    key(Qt::Key_Tab);
    EXPECT_TRUE(findItem("tableCell0_2")->hasActiveFocus()) << "Tab: the next cell";
    click(findItem("tableRemoveColumn"));
    EXPECT_EQ(table->property("columnCount").toInt(), 2);
    click(findItem("tableCell1_0"));
    type("a|b");
    key(Qt::Key_Tab);
    type("1");
    EXPECT_EQ(findItem("tablePlace")->property("text").toString(), "Row 1, Column 2");
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {  // (the bar and the table editor, to look at)
        wait(1500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    click(findItem("tableOk"));
    ASSERT_TRUE(waitOpened(table, false));
    const std::string written = "| Name | Value |\n"
                                "| ---- | ----- |\n"
                                "| a\\|b | 1     |\n"
                                "|      |       |";
    EXPECT_EQ(editor->text(), "# Title\n\n**word**\n\n" + written + "\n\n");
    editor->undo();
    EXPECT_EQ(editor->text(), "# Title\n\n**word**") << "one undo step";
    editor->redo();

    // Edited again: the table at the cursor is read into the grid; its first column centered
    editor->setCursorPosition(editor->text().find("a\\|b") + 1);
    wait(20);
    EXPECT_TRUE(find<QQuickItem>("mdTable")->property("checked").toBool()) << "in a table";
    click(find<QQuickItem>("mdTable"));
    ASSERT_TRUE(waitOpened(table, true));
    auto* cell = findItem("tableCell1_0");
    ASSERT_NE(cell, nullptr);
    EXPECT_EQ(cell->property("text").toString(), "a|b") << "the escaped pipe as a pipe";
    until([&] { return cell->hasActiveFocus(); });
    EXPECT_TRUE(cell->hasActiveFocus()) << "the cell of the cursor";
    click(findItem("tableAlignCenter"));
    click(findItem("tableOk"));
    ASSERT_TRUE(waitOpened(table, false));
    EXPECT_EQ(editor->text(), "# Title\n\n**word**\n\n| Name | Value |\n"
                              "| :--: | ----- |\n"
                              "| a\\|b | 1     |\n"
                              "|      |       |\n\n");

    // Cancel changes nothing
    click(find<QQuickItem>("mdTable"));
    ASSERT_TRUE(waitOpened(table, true));
    click(findItem("tableCancel"));
    ASSERT_TRUE(waitOpened(table, false));
    EXPECT_EQ(editor->text().find("| :--: |") != std::string::npos, true);

    // Beside the page: its own bar, on the source (one undo step of the source)
    key(Qt::Key_M, Qt::ControlModifier | Qt::AltModifier);
    auto* panel = find<QQuickItem>("markdownPanel");
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(panel->isVisible());
    EXPECT_FALSE(bar->isVisible()) << "the panel has its own";
    auto* area = find<QQuickItem>("markdownArea");
    const std::string before = area->property("text").toString().toStdString();
    click(findItem("panelMdBold"));
    EXPECT_EQ(area->property("text").toString().toStdString(), before + "****") << "empty marks at the cursor";
    EXPECT_EQ(area->property("cursorPosition").toInt(), static_cast<int>(before.size()) + 2) << "between them";
    QMetaObject::invokeMethod(area, "undo");
    EXPECT_EQ(area->property("text").toString().toStdString(), before);
    click(find<QQuickItem>("markdownCancel"));
}

// Beside the page (qt/docs/md-images.md): a picture pasted into the Markdown of saved notes is kept with the notes and
// linked; in notes not saved yet the text is pasted as before.
TEST_F(MainWindowTest, aPicturePastedBesideThePageIsKeptWithTheNotes) {
    QTemporaryDir dir;
    const fs::path xopp = fs::path(dir.path().toStdString()) / "board.xopp";
    ASSERT_TRUE(controller->tabManager().currentSession()->saveAs(xopp).ok);
    auto* panel = find<QQuickItem>("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "openBox", Q_ARG(QVariant, 0), Q_ARG(QVariant, 60.0), Q_ARG(QVariant, 300.0));
    until([&] { return panel->isVisible(); });
    auto* area = find<QQuickItem>("markdownArea");
    ASSERT_NE(area, nullptr);
    auto* document = area->property("textDocument").value<QQuickTextDocument*>();
    ASSERT_NE(document, nullptr);
    auto* mime = new QMimeData;
    QImage red(12, 12, QImage::Format_RGB32);
    red.fill(Qt::red);
    mime->setImageData(red);
    QGuiApplication::clipboard()->setMimeData(mime);
    ASSERT_TRUE(controller->pasteMarkdown(document, 0, 0));
    const QString text = area->property("text").toString();
    EXPECT_TRUE(text.startsWith("![](board.assets/image-")) << text.toStdString();
    const QString name = text.mid(QString("![](").size()).section(')', 0, 0);
    EXPECT_FALSE(xqt::md::images::resolve(name.toStdString()).empty());
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, false));
}

// A .md: the bar is there all the time (its text is written with the keyboard); a tool starts writing if needed.
TEST_F(HomeScreenMarkdownTest, aMarkdownFileHasTheFormattingBar) {
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(100);
    auto* bar = find<QQuickItem>("markdownFormatBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->isVisible());
    click(find<QQuickItem>("canvas"));
    type("Hello");
    click(find<QQuickItem>("mdTaskList"));
    const std::string text = controller->tabManager().currentSession()->currentText();
    const size_t hello = text.find("Hello");
    ASSERT_NE(hello, std::string::npos);
    const size_t line = text.rfind('\n', hello);
    EXPECT_EQ(text.substr(line + 1, 6), "- [ ] ") << text.substr(line + 1, 40);
    EXPECT_TRUE(find<QQuickItem>("mdTaskList")->property("checked").toBool());
}

// Pictures in a .md (qt/docs/md-images.md): the formatting bar's image opens a file picker, a picked file is copied
// into "kalman.assets/" and linked at the cursor; a pasted picture is saved there as image-….png; picture files
// dropped on the page are copied and linked. Each is one undo step of the text; undo leaves the files.
TEST_F(HomeScreenMarkdownTest, picturesArePickedPastedAndDroppedIntoAMarkdownFile) {
    const fs::path md = root / "kalman.md";
    ASSERT_TRUE(controller->openPath(QString::fromStdString(md.string())));
    wait(100);
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    click(canvasItem);
    xqt::MarkdownEditor* editor = view->getMarkdownEditor();
    ASSERT_NE(editor, nullptr);
    editor->setCursorPosition(0);
    type("X");
    const std::string start = editor->text();

    // A picture file somewhere else
    const fs::path outside = root / "elsewhere";
    fs::create_directories(outside);
    QImage red(40, 20, QImage::Format_RGB32);
    red.fill(Qt::red);
    ASSERT_TRUE(red.save(QString::fromStdString((outside / "my plot.png").string())));

    // The image of the insert menu: a file picker
    EXPECT_EQ(find<QObject>("mdInsertMenu"), nullptr) << "no tool hidden behind a menu";
    click(find<QQuickItem>("mdImage"));
    auto* dialog = find<QObject>("mdImageDialog");
    ASSERT_NE(dialog, nullptr);
    until([&] { return dialog->property("visible").toBool(); });
    EXPECT_TRUE(dialog->property("visible").toBool()) << "the picker is open";
    QMetaObject::invokeMethod(dialog, "close");
    // From Qt 6.8 on the picker (Qt Quick's own file dialog, off-screen) is a window of its own, and it keeps the
    // keyboard focus. A window manager gives the focus back to the main window when it closes; off-screen nothing
    // does, and the Ctrl+V below would go nowhere. As the window manager does:
    window->requestActivate();
    until([&] { return QGuiApplication::focusWindow() == window; });
    ASSERT_EQ(QGuiApplication::focusWindow(), window);
    // What it gives when a file is picked (the bar's action with the file's URL)
    auto* bar = find<QQuickItem>("markdownFormatBar");
    QMetaObject::invokeMethod(bar, "act", Q_ARG(QVariant, QString("image")),
                              Q_ARG(QVariant, QUrl::fromLocalFile(QString::fromStdString((outside / "my plot.png").string()))
                                                      .toString()));
    wait(50);
    const fs::path assets = root / "kalman.assets";
    EXPECT_TRUE(fs::exists(assets / "my plot.png")) << "copied next to the .md";
    const std::string link = "X![my plot](kalman.assets/my%20plot.png)";  // (a file's name is its alt text)
    EXPECT_EQ(editor->text().substr(0, link.size()), link);
    EXPECT_EQ(editor->text().substr(link.size()), start.substr(1));
    EXPECT_EQ(xqt::md::images::resolve("kalman.assets/my%20plot.png"), (assets / "my plot.png").string());
    EXPECT_TRUE(canvasItem->hasActiveFocus()) << "the text keeps the keys";

    // Ctrl+V with a picture on the clipboard: saved as image-YYYY-MM-DD-HHMMSS.png, linked at the cursor
    key(Qt::Key_Return);
    auto* mime = new QMimeData;
    QImage blue(30, 30, QImage::Format_RGB32);
    blue.fill(Qt::blue);
    mime->setImageData(blue);
    QGuiApplication::clipboard()->setMimeData(mime);
    const std::string beforePaste = editor->text();
    key(Qt::Key_V, Qt::ControlModifier);
    std::vector<fs::path> pasted;
    for (const auto& e: fs::directory_iterator(assets)) {
        if (e.path().filename().string().rfind("image-", 0) == 0) {
            pasted.push_back(e.path());
        }
    }
    ASSERT_EQ(pasted.size(), 1u);
    const std::string name = pasted[0].filename().string();
    EXPECT_EQ(name.size(), std::string("image-2026-09-26-101112.png").size()) << name;
    EXPECT_NE(editor->text().find("![](kalman.assets/" + name + ")"), std::string::npos) << editor->text().substr(0, 120);
    // One undo step; the file stays
    editor->undo();
    EXPECT_TRUE(editor->text() == beforePaste);
    EXPECT_TRUE(fs::exists(pasted[0]));
    editor->redo();

    // A copied text that also carries a picture (a spreadsheet's cells): the text
    auto* both = new QMimeData;
    both->setText("a\tb");
    both->setImageData(blue);
    QGuiApplication::clipboard()->setMimeData(both);
    key(Qt::Key_V, Qt::ControlModifier);
    EXPECT_NE(editor->text().find("a\tb"), std::string::npos);

    // Two picture files dropped on the page: both copied, linked at the cursor as one change
    QImage green(10, 10, QImage::Format_RGB32);
    green.fill(Qt::green);
    ASSERT_TRUE(green.save(QString::fromStdString((outside / "g.png").string())));
    ASSERT_TRUE(green.save(QString::fromStdString((outside / "h.jpg").string())));
    const std::string beforeDrop = editor->text();
    QMimeData drop;
    drop.setUrls({QUrl::fromLocalFile(QString::fromStdString((outside / "g.png").string())),
                  QUrl::fromLocalFile(QString::fromStdString((outside / "h.jpg").string()))});
    const QPoint at = canvasItem->mapToScene(QPointF(canvasItem->width() / 2, canvasItem->height() / 2)).toPoint();
    QDragEnterEvent enter(at, Qt::CopyAction, &drop, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &enter);
    QDragMoveEvent move(at, Qt::CopyAction, &drop, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &move);
    QDropEvent dropped(at, Qt::CopyAction, &drop, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &dropped);
    wait(50);
    EXPECT_TRUE(fs::exists(assets / "g.png"));
    EXPECT_TRUE(fs::exists(assets / "h.jpg"));
    EXPECT_NE(editor->text().find("![g](kalman.assets/g.png)\n\n![h](kalman.assets/h.jpg)"), std::string::npos);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {  // (the pictures on the page, to look at)
        editor->setCursorPosition(editor->text().find("The needle"));
        view->getViewController().scrollToPageRect(0, QRectF(0, 0, 600, 500));
        wait(1500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    editor->undo();
    EXPECT_TRUE(editor->text() == beforeDrop) << "one undo step";
}

// A .md open in a tab, renamed in the library (qt/docs/md-images.md): its pictures' folder goes along, the links in the
// file and in the tab are rewritten, the tab is not modified by it, and its pictures are still found.
TEST_F(HomeScreenMarkdownTest, aRenamedMarkdownFileTakesItsPicturesAlongAlsoWhenOpen) {
    std::ofstream(root / "photo.md") << "# Photo\n\n![](photo.assets/a.png)\n";
    fs::create_directories(root / "photo.assets");
    QImage img(20, 10, QImage::Format_RGB32);
    img.fill(Qt::red);
    ASSERT_TRUE(img.save(QString::fromStdString((root / "photo.assets" / "a.png").string())));
    controller->libraryModel()->setProperty("rootPath", QString::fromStdString(root.string()));
    QMetaObject::invokeMethod(controller->libraryModel(), "refresh");
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "photo.md").string())));
    wait(100);
    xqt::DocumentSession* s = controller->tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    EXPECT_FALSE(xqt::md::images::resolve("photo.assets/a.png").empty());
    int row = -1;
    QMetaObject::invokeMethod(controller->libraryModel(), "rowOf", Q_RETURN_ARG(int, row),
                              Q_ARG(QString, QString::fromStdString((root / "photo.md").string())));
    ASSERT_GE(row, 0);
    bool ok = false;
    QMetaObject::invokeMethod(controller->libraryModel(), "rename", Q_RETURN_ARG(bool, ok), Q_ARG(int, row),
                              Q_ARG(QString, QString("Holiday")));
    ASSERT_TRUE(ok);
    wait(100);
    EXPECT_TRUE(fs::exists(root / "Holiday.assets" / "a.png"));
    EXPECT_EQ(s->textFile()->path(), root / "Holiday.md") << "the tab follows";
    EXPECT_EQ(s->currentText(), "# Photo\n\n![](Holiday.assets/a.png)\n");
    EXPECT_FALSE(s->isModified()) << "the file has the same text";
    EXPECT_EQ(xqt::md::images::resolve("Holiday.assets/a.png"), (root / "Holiday.assets" / "a.png").string());
    std::string bytes;
    EXPECT_FALSE(s->textChangedOnDisk(bytes)) << "no question about a change on disk";
}

// ⋮ → Remove unused images… of a .md (qt/docs/md-images.md): lists the files of its folder the text does not link to
// (a link to a PDF there counts as used), and moves them to the trash only when asked.
TEST_F(HomeScreenMarkdownTest, removeUnusedImagesListsThemFirst) {
    FakeSystemApps apps;
    xqt::SystemApps::setInstance(&apps);
    std::ofstream(root / "pics.md") << "# Pictures\n\n![](pics.assets/used.png)\n\n[the paper](pics.assets/paper.pdf)\n";
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(Qt::blue);
    fs::create_directories(root / "pics.assets" / "old");
    for (const char* name: {"used.png", "unused.png", "old/older.png"}) {
        ASSERT_TRUE(img.save(QString::fromStdString((root / "pics.assets" / name).string())));
    }
    std::ofstream(root / "pics.assets" / "paper.pdf") << "%PDF-1.4";
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "pics.md").string())));
    wait(100);
    EXPECT_EQ(controller->unusedMarkdownImages(), (QStringList{"old/older.png", "unused.png"}));

    QObject* more = find("moreMenu");
    QMetaObject::invokeMethod(more, "open");
    ASSERT_TRUE(waitOpened(more, true));
    auto* item = find<QQuickItem>("unusedImagesItem");
    until([&] { return item->isVisible(); });
    click(item);
    auto* dialog = find<QObject>("unusedImagesDialog");
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(dialog->property("files").toStringList(), (QStringList{"old/older.png", "unused.png"}));
    EXPECT_TRUE(apps.trashed.isEmpty()) << "nothing before it is asked";
    click(findItem("unusedImagesTrash"));
    ASSERT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(apps.trashed.size(), 2);
    EXPECT_FALSE(fs::exists(root / "pics.assets" / "unused.png"));
    EXPECT_TRUE(fs::exists(root / "pics.assets" / "used.png"));
    EXPECT_TRUE(fs::exists(root / "pics.assets" / "paper.pdf"));
    // (a picture whose link was just deleted counts as unused: the text as it is now)
    EXPECT_TRUE(controller->unusedMarkdownImages().isEmpty());
    xqt::SystemApps::setInstance(nullptr);
}

// Emoji on the page: ":smi" typed in a text box shows the suggestions below the cursor, a tap takes one; the emoji
// button (shown while writing) opens the picker, whose search finds by name and puts the emoji at the cursor.
TEST_F(MainWindowTest, emojiSuggestionsAndPickerWhileWritingOnThePage) {
    const QString smiley = QString::fromUtf8("\xf0\x9f\x98\x83");
    const QString party = QString::fromUtf8("\xf0\x9f\x8e\x89");
    controller->setMarkdownInPanel(false);
    controller->setTextMarkdown(false);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 250, 350, 200));
    wait(100);
    auto* button = findItem("emojiButton");
    ASSERT_NE(button, nullptr);
    EXPECT_FALSE(button->isVisible()) << "only while writing";
    const QPointF at = view->pageViewRect(0).topLeft() + QPointF(150, 300) * view->getViewController().zoom();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, canvasItem->mapToScene(at).toPoint());
    wait(50);
    ASSERT_NE(view->getTextEditor(), nullptr);
    EXPECT_TRUE(button->isVisible());

    auto* list = findItem("emojiSuggestions");
    ASSERT_NE(list, nullptr);
    type("Hi :s");
    EXPECT_FALSE(list->isVisible());
    type("mi");
    until([&] { return list->isVisible(); });
    ASSERT_TRUE(list->isVisible());
    const QRectF cursor = canvasItem->property("emojiCompletionRect").toRectF();
    const QPointF cursorInScene = canvasItem->mapToScene(cursor.bottomLeft());
    const QPointF listInScene = list->mapToScene(QPointF(0, 0));
    EXPECT_GT(listInScene.y(), cursorInScene.y()) << "below the cursor";
    EXPECT_NEAR(listInScene.x(), cursorInScene.x(), 2);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        wait(1500);  // (the software renderer is slow)
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT"));
    }
    QQuickItem* second = nullptr;
    until([&] { return (second = findItem("emojiSuggestion1")) != nullptr; });
    click(second);
    EXPECT_EQ(view->getTextEditor()->text(), "Hi " + smiley);
    EXPECT_FALSE(list->isVisible());

    click(button);
    auto* picker = find<QObject>("emojiPicker");
    ASSERT_NE(picker, nullptr);
    ASSERT_TRUE(waitOpened(picker, true));
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT_PICKER")) {
        wait(1500);
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT_PICKER"));
    }
    find<QObject>("emojiSearch")->setProperty("text", "tada");
    wait(50);
    key(Qt::Key_Return);  // (the first found)
    EXPECT_TRUE(waitOpened(picker, false));
    ASSERT_NE(view->getTextEditor(), nullptr) << "still writing";
    EXPECT_EQ(view->getTextEditor()->text(), "Hi " + smiley + party);
    type("!");
    EXPECT_EQ(view->getTextEditor()->text(), "Hi " + smiley + party + "!") << "the keys went back to the text";
    key(Qt::Key_Escape);
    EXPECT_EQ(view->getTextEditor(), nullptr);
    EXPECT_FALSE(button->isVisible());
}

// The Markdown editor beside the page: the same suggestions (Down, Enter), the picker, and the arrows and Backspace
// over a whole flag.
TEST_F(MainWindowTest, emojiInTheMarkdownEditorBesideThePage) {
    const QString smiley = QString::fromUtf8("\xf0\x9f\x98\x83");
    const QString flag = QString::fromUtf8("\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa");
    const QString party = QString::fromUtf8("\xf0\x9f\x8e\x89");
    controller->setMarkdownInPanel(true);
    controller->setTextMarkdown(true);
    controller->selectTool("text");
    auto* panel = find<QQuickItem>("markdownPanel");
    auto* area = find<QQuickItem>("markdownArea");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    const QPointF at = view->pageViewRect(0).topLeft() + QPointF(150, 300) * view->getViewController().zoom();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, canvasItem->mapToScene(at).toPoint());
    until([&] { return panel->isVisible(); });
    ASSERT_TRUE(area->hasActiveFocus());

    auto* list = findItem("markdownEmojiSuggestions");
    ASSERT_NE(list, nullptr);
    type("Hi :smi");
    until([&] { return list->isVisible(); });
    ASSERT_TRUE(list->isVisible());
    key(Qt::Key_Down);
    key(Qt::Key_Return);
    EXPECT_EQ(area->property("text").toString(), "Hi " + smiley) << "no new line: Enter took the emoji";
    EXPECT_FALSE(list->isVisible());

    area->setProperty("text", "a" + flag + "b");
    area->setProperty("cursorPosition", 5);  // (after the flag: 2 UTF-16 units per regional indicator)
    key(Qt::Key_Left);
    EXPECT_EQ(area->property("cursorPosition").toInt(), 1) << "over the whole flag";
    key(Qt::Key_Right);
    EXPECT_EQ(area->property("cursorPosition").toInt(), 5);
    key(Qt::Key_Backspace);
    EXPECT_EQ(area->property("text").toString(), "ab") << "the whole flag";

    click(findItem("markdownEmoji"));
    auto* picker = find<QObject>("emojiPicker");
    QObject* panelPicker = nullptr;
    for (QObject* p: window->findChildren<QObject*>("emojiPicker")) {
        if (p->property("opened").toBool() || p->property("visible").toBool()) {
            panelPicker = p;
        }
    }
    ASSERT_NE(picker, nullptr);
    until([&] {
        for (QObject* p: window->findChildren<QObject*>("emojiPicker")) {
            if (p->property("opened").toBool()) {
                panelPicker = p;
                return true;
            }
        }
        return false;
    });
    ASSERT_NE(panelPicker, nullptr);
    for (QObject* s: window->findChildren<QObject*>("emojiSearch")) {
        s->setProperty("text", "tada");
    }
    wait(50);
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(panelPicker, false));
    EXPECT_EQ(area->property("text").toString(), "a" + party + "b");
    EXPECT_TRUE(area->hasActiveFocus());
}

// The PDF with notes (the export, print and sharing use it): emoji of a Markdown box (a shortcode among them) and of
// a text box are in it in colour, as poppler shows it to other viewers. (XQT_KEEP=<file>: keeps the PDF.)
TEST_F(MainWindowTest, emojiAreInTheExportedPdfInColour) {
    const QString flag = QString::fromUtf8("\xf0\x9f\x87\xa9\xf0\x9f\x87\xaa");
    const QString coder = QString::fromUtf8("\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb");
    controller->setMarkdownInPanel(false);
    controller->selectTool("text");
    auto* canvasItem = find<QQuickItem>("canvas");
    auto* view = qobject_cast<xqt::CanvasView*>(canvasItem->property("view").value<QObject*>());
    ASSERT_NE(view, nullptr);
    view->getViewController().scrollToPageRect(0, QRectF(100, 100, 400, 300));
    wait(100);
    const auto pagePoint = [&](double x, double y) {
        return canvasItem
                ->mapToScene(view->pageViewRect(0).topLeft() + QPointF(x, y) * view->getViewController().zoom())
                .toPoint();
    };
    // A Markdown box: "Hi :smile: 🇩🇪" (the shortcode shown as 😄)
    controller->setTextMarkdown(true);
    controller->setMarkdownFontSize(14);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(120, 150));
    wait(50);
    ASSERT_NE(view->getMarkdownEditor(), nullptr);
    type("Hi :smile: ");
    QGuiApplication::clipboard()->setText(flag);
    key(Qt::Key_V, Qt::ControlModifier);
    key(Qt::Key_Escape);
    // A text box: "Hey 👩‍💻"
    controller->setTextMarkdown(false);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, pagePoint(120, 300));
    wait(50);
    ASSERT_NE(view->getTextEditor(), nullptr);
    type("Hey ");
    QGuiApplication::clipboard()->setText(coder);
    key(Qt::Key_V, Qt::ControlModifier);
    key(Qt::Key_Escape);

    QTemporaryDir dir;
    const QString pdf = qEnvironmentVariableIsSet("XQT_KEEP") ? qEnvironmentVariable("XQT_KEEP") : dir.filePath("emoji.pdf");
    ASSERT_TRUE(controller->saveAsHybrid(QUrl::fromLocalFile(pdf)));
    until([&] { return !controller->anySaving(); }, 20000);

    // Poppler draws the page (150 dpi); the colour pixels: in the Markdown box's line and in the text box's line
    gchar* uri = g_filename_to_uri(pdf.toUtf8().constData(), nullptr, nullptr);
    PopplerDocument* doc = poppler_document_new_from_file(uri, nullptr, nullptr);
    g_free(uri);
    ASSERT_NE(doc, nullptr);
    PopplerPage* page = poppler_document_get_page(doc, 0);
    double w = 0, h = 0;
    poppler_page_get_size(page, &w, &h);
    const double scale = 150.0 / 72;
    QImage image(static_cast<int>(w * scale), static_cast<int>(h * scale), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    cairo_surface_t* surface = cairo_image_surface_create_for_data(image.bits(), CAIRO_FORMAT_ARGB32, image.width(),
                                                                   image.height(), static_cast<int>(image.bytesPerLine()));
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    poppler_page_render_for_printing(page, cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(page);
    g_object_unref(doc);
    // Warm colours (the yellow faces, the flag's red and gold, the skin and hair): not the page's blue ruling or its
    // magenta margin line
    const auto warmIn = [&](double top, double bottom) {  // (page points)
        int n = 0;
        for (int y = static_cast<int>(top * scale); y < static_cast<int>(bottom * scale); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor c = image.pixelColor(x, y);
                n += c.red() > 150 && c.blue() < 120 && c.red() - c.blue() > 80;
            }
        }
        return n;
    };
    EXPECT_GT(warmIn(135, 180), 300) << "😄 (:smile:) and 🇩🇪 of the Markdown box";
    EXPECT_GT(warmIn(285, 330), 50) << "👩‍💻 of the text box, at its place";
    EXPECT_EQ(warmIn(500, 545), 0) << "no emoji there";
}

// --- renaming a document from its tab, the ⋮ menu, the library's and the overview's cards (qt/rename) ---
namespace {
class RenameTest: public HomeScreenTest {
protected:
    static QQuickItem* inside(QQuickItem* root, const char* name) {
        if (!root) {
            return nullptr;
        }
        if (root->objectName() == name) {
            return root;
        }
        for (QQuickItem* c: root->childItems()) {
            if (QQuickItem* f = inside(c, name)) {
                return f;
            }
        }
        return nullptr;
    }
    QQuickItem* tab(int i) const { return itemAt(find<QQuickItem>("tabList"), i); }
    static bool editing(QQuickItem* inlineRename) {
        return inlineRename && inlineRename->property("active").toBool();
    }
    static QString fieldText(QQuickItem* inlineRename) {
        return inlineRename->property("field").value<QObject*>()->property("text").toString();
    }
    void doubleClick(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(item));
        wait(50);
    }
    void holdMouse(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        const QPoint at = centerOf(item);
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
        wait(800);  // (press and hold)
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at);
        wait(50);
    }
    void holdFinger(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        static QPointingDevice* finger = QTest::createTouchDevice();
        const QPoint at = centerOf(item);
        QTest::touchEvent(window, finger).press(1, at);
        wait(800);
        QTest::touchEvent(window, finger).release(1, at);
        wait(50);
    }
    void open(const char* name) {
        ASSERT_TRUE(controller->openPath(QString::fromStdString((root / name).string())));
        controller->setHomeVisible(false);
        wait(100);
    }
};
}  // namespace

// A double click on the shown tab's title edits its name in place: the name selected, the extension beside it.
// Escape cancels, a taken name says so and stays, Enter renames the file, and the tab follows.
TEST_F(RenameTest, aDoubleClickOnTheShownTabRenamesItsDocument) {
    open("notes.xopp");
    ASSERT_EQ(controller->tabCount(), 1);
    xqt::DocumentPlaces::setLastPage(root / "notes.xopp", 3);
    QQuickItem* field = inside(tab(0), "tabRename");
    ASSERT_NE(field, nullptr);
    EXPECT_FALSE(editing(field));

    doubleClick(inside(tab(0), "tabTitle"));
    ASSERT_TRUE(editing(field)) << "a double click on the shown tab's title";
    EXPECT_EQ(fieldText(field), "notes") << "the name without its extension";
    EXPECT_EQ(inside(field, "inlineRenameExtension")->property("text").toString(), ".xopp") << "which stays";
    type("x");
    key(Qt::Key_Escape);
    EXPECT_FALSE(editing(field)) << "Escape cancels";
    EXPECT_TRUE(fs::exists(root / "notes.xopp"));
    EXPECT_EQ(controller->title(), "notes.xopp");

    doubleClick(inside(tab(0), "tabTitle"));
    ASSERT_TRUE(editing(field));
    type("lecture");  // (the PDF's name)
    EXPECT_FALSE(field->property("problem").toString().isEmpty()) << "a taken name says so";
    key(Qt::Key_Return);
    EXPECT_TRUE(editing(field)) << "and is not taken";
    EXPECT_TRUE(fs::exists(root / "notes.xopp"));

    key(Qt::Key_A, Qt::ControlModifier);
    type("Week");
    EXPECT_TRUE(field->property("problem").toString().isEmpty());
    key(Qt::Key_Return);
    EXPECT_FALSE(editing(field));
    EXPECT_TRUE(fs::exists(root / "Week.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    EXPECT_EQ(controller->title(), "Week.xopp") << "the tab follows";
    EXPECT_EQ(inside(tab(0), "tabTitle")->property("text").toString(), "Week.xopp");
    EXPECT_EQ(xqt::DocumentPlaces::lastPage(root / "Week.xopp"), 3) << "its reading place goes along";
    EXPECT_GE(rowOf("Week.xopp"), 0) << "the library shows it under its new name";
}

// On a tab that is not shown, the first click of a double click shows it; the double click does not rename.
TEST_F(RenameTest, aDoubleClickOnAnotherTabOnlyShowsIt) {
    open("notes.xopp");
    open("lecture.pdf");
    ASSERT_EQ(controller->tabCount(), 2);
    ASSERT_EQ(controller->currentTab(), 1);
    doubleClick(inside(tab(0), "tabTitle"));
    EXPECT_EQ(controller->currentTab(), 0);
    EXPECT_FALSE(editing(inside(tab(0), "tabRename"))) << "not renamed on the way";
    // The tab's menu (right click; press and hold with a finger) has "Rename…" too
    QTest::mouseClick(window, Qt::RightButton, Qt::NoModifier, centerOf(tab(1)));
    wait(50);
    QObject* menu = tab(1)->findChild<QObject*>("tabMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitOpened(menu, true));
    click(findItem("renameTabItem"));
    ASSERT_TRUE(waitOpened(menu, false));
    until([&] { return editing(inside(tab(1), "tabRename")); });
    EXPECT_EQ(controller->currentTab(), 1) << "that tab is shown";
    ASSERT_TRUE(editing(inside(tab(1), "tabRename"))) << "and its name edited in place";
    EXPECT_EQ(fieldText(inside(tab(1), "tabRename")), "lecture");
    key(Qt::Key_Escape);
    EXPECT_TRUE(fs::exists(root / "lecture.pdf"));
}

// "Rename…" of the ⋮ menu: a dialog with the name and the extension as fixed text; a name that cannot be used says
// why and OK waits.
TEST_F(RenameTest, theMoreMenuRenamesInADialog) {
    open("lecture.pdf");
    QObject* menu = find("moreMenu");
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitOpened(menu, true));
    click(findItem("renameDocumentItem"));
    QObject* dialog = find("renameDocumentDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitOpened(dialog, true));
    auto* field = find<QQuickItem>("renameDocumentField");
    auto* problem = find<QQuickItem>("renameDocumentProblem");
    EXPECT_EQ(field->property("text").toString(), "lecture");
    EXPECT_EQ(find<QQuickItem>("renameDocumentExtension")->property("text").toString(), ".pdf");

    type("notes");  // a .xopp of that name is there
    EXPECT_TRUE(problem->isVisible());
    key(Qt::Key_Return);
    EXPECT_TRUE(dialog->property("visible").toBool()) << "a taken name is not accepted";
    key(Qt::Key_A, Qt::ControlModifier);
    type("a/b");
    EXPECT_TRUE(problem->isVisible()) << "no slash";
    key(Qt::Key_A, Qt::ControlModifier);
    type("Slides");
    EXPECT_FALSE(problem->isVisible());
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_TRUE(fs::exists(root / "Slides.pdf")) << "the extension is kept";
    EXPECT_FALSE(fs::exists(root / "lecture.pdf"));
    EXPECT_EQ(controller->title(), "Slides.pdf");

    // A new document that was never saved: the name it gets when it is saved
    controller->newDocument();
    wait(50);
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitOpened(menu, true));
    click(findItem("renameDocumentItem"));
    ASSERT_TRUE(waitOpened(dialog, true));
    EXPECT_EQ(find<QQuickItem>("renameDocumentExtension")->property("text").toString(), ".xopp");
    type("Ideas");
    key(Qt::Key_Return);
    EXPECT_TRUE(waitOpened(dialog, false));
    EXPECT_EQ(controller->title(), "Ideas");
    EXPECT_EQ(QFileInfo(controller->suggestedSaveFile().toLocalFile()).fileName(), "Ideas.xopp");
}

// A read-only file is not renamed (it says why).
TEST_F(RenameTest, aReadOnlyFileIsNotRenamed) {
    open("notes.xopp");
    fs::permissions(root / "notes.xopp", fs::perms::owner_read, fs::perm_options::replace);
    if (QFileInfo(QString::fromStdString((root / "notes.xopp").string())).isWritable()) {
        fs::permissions(root / "notes.xopp", fs::perms::owner_all, fs::perm_options::replace);
        GTEST_SKIP() << "files are writable anyway (root)";
    }
    EXPECT_FALSE(controller->tabRenameInfo(0).value("problem").toString().isEmpty());
    EXPECT_FALSE(controller->tabRenameProblem(0, "Diary").isEmpty());
    QSignalSpy messages(controller.get(), &AppController::message);
    EXPECT_FALSE(controller->renameTab(0, "Diary"));
    EXPECT_EQ(messages.count(), 1) << "it says why";
    EXPECT_TRUE(fs::exists(root / "notes.xopp"));
    fs::permissions(root / "notes.xopp", fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
}

// A .md with its pictures ("name.assets/"), renamed from its tab: both, and its links to the pictures follow (the
// library's rename).
TEST_F(RenameTest, aMarkdownFileAndItsPicturesAreRenamedFromTheTab) {
    fs::create_directories(root / "Trip.assets");
    QImage picture(8, 8, QImage::Format_RGB32);
    picture.fill(Qt::red);
    ASSERT_TRUE(picture.save(QString::fromStdString((root / "Trip.assets" / "a.png").string())));
    std::ofstream(root / "Trip.md") << "# Trip\n\n![a](Trip.assets/a.png)\n";
    open("Trip.md");
    ASSERT_EQ(controller->title(), "Trip.md");
    QQuickItem* field = inside(tab(0), "tabRename");
    doubleClick(inside(tab(0), "tabTitle"));
    ASSERT_TRUE(editing(field));
    EXPECT_EQ(fieldText(field), "Trip");
    type("Journey");
    key(Qt::Key_Return);
    EXPECT_TRUE(fs::exists(root / "Journey.md"));
    EXPECT_TRUE(fs::exists(root / "Journey.assets" / "a.png"));
    EXPECT_FALSE(fs::exists(root / "Trip.md"));
    EXPECT_FALSE(fs::exists(root / "Trip.assets"));
    std::ifstream in(root / "Journey.md");
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    EXPECT_NE(text.find("](Journey.assets/a.png)"), std::string::npos) << text;
    EXPECT_EQ(controller->title(), "Journey.md");
    EXPECT_FALSE(controller->property("modified").toBool()) << "the file and the text agree";
}

// The library: a press and hold (mouse or finger) or a double click on a card's title edits its name in place; a
// click on the title still opens it.
TEST_F(RenameTest, aCardTitleIsRenamedInPlace) {
    // Press and hold with the mouse
    QQuickItem* lecture = card(rowOf("lecture.pdf"));
    holdMouse(inside(lecture, "cardTitleArea"));
    QQuickItem* field = inside(lecture, "cardRename");
    ASSERT_TRUE(editing(field)) << "a press and hold on the title";
    EXPECT_EQ(fieldText(field), "lecture");
    EXPECT_EQ(controller->tabCount(), 0) << "not opened";
    type("Talk");
    key(Qt::Key_Return);
    EXPECT_TRUE(fs::exists(root / "Talk.pdf"));
    EXPECT_FALSE(fs::exists(root / "lecture.pdf"));

    // A double click with the mouse
    QQuickItem* notes = card(rowOf("notes.xopp"));
    doubleClick(inside(notes, "cardTitleArea"));
    field = inside(notes, "cardRename");
    ASSERT_TRUE(editing(field)) << "a double click on the title";
    type("Talk");
    EXPECT_FALSE(field->property("problem").toString().isEmpty()) << "taken";
    key(Qt::Key_Escape);
    EXPECT_FALSE(editing(field));
    wait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 100);
    EXPECT_EQ(controller->tabCount(), 0) << "a double click does not open it";

    // A press and hold with a finger: a folder too
    QQuickItem* physics = card(rowOf("Physics"));
    holdFinger(inside(physics, "cardTitleArea"));
    field = inside(physics, "cardRename");
    ASSERT_TRUE(editing(field)) << "a finger's press and hold on the title";
    type("Maths");
    key(Qt::Key_Return);
    EXPECT_TRUE(fs::is_directory(root / "Maths"));

    // A click on the title opens it (a moment later)
    click(inside(card(rowOf("notes.xopp")), "cardTitleArea"));
    until([&] { return controller->tabCount() == 1; });
    EXPECT_EQ(controller->tabCount(), 1);
    EXPECT_EQ(controller->title(), "notes.xopp");
}

// The overview of open documents: a double click (mouse) or a press and hold on a card's title edits the name in
// place; Escape cancels it and leaves the overview open.
TEST_F(RenameTest, anOverviewCardTitleIsRenamedInPlace) {
    open("notes.xopp");
    QObject* overview = find("tabOverview");
    key(Qt::Key_E, Qt::ControlModifier | Qt::ShiftModifier);
    ASSERT_TRUE(waitOpened(overview, true));
    auto* grid = find<QQuickItem>("tabGrid");
    QQuickItem* cell = itemAt(grid, 0);
    ASSERT_NE(cell, nullptr);
    QQuickItem* field = inside(cell, "overviewRename");
    doubleClick(inside(cell, "overviewTitleArea"));
    ASSERT_TRUE(editing(field)) << "a double click on the title";
    EXPECT_EQ(fieldText(field), "notes");
    key(Qt::Key_Escape);
    EXPECT_FALSE(editing(field));
    wait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 100);
    EXPECT_TRUE(overview->property("visible").toBool()) << "Escape cancels the name, not the overview";

    holdMouse(inside(cell, "overviewTitleArea"));
    ASSERT_TRUE(editing(field)) << "a press and hold on the title";
    type("Plan");
    key(Qt::Key_Return);
    EXPECT_TRUE(fs::exists(root / "Plan.xopp"));
    EXPECT_FALSE(fs::exists(root / "notes.xopp"));
    EXPECT_EQ(controller->title(), "Plan.xopp");
    EXPECT_EQ(inside(cell, "overviewTitle")->property("text").toString(), "Plan.xopp");
}

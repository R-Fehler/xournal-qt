/*
 * xournal-qt: the toolbox in the real window (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of
 * the canvas (right by default), undo and redo at its head, the app's tools (hand, select, snip, mark PDF text) lent to
 * it as items of the same arrangement; a tap picks a tool up, the rail scrolls when it is short (the same order on every
 * screen), the edge is chosen per window size. (The classic tool bar was removed in 0.8.0: the toolbox is the only one.)
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <functional>
#include <cmath>
#include <type_traits>
#include <memory>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QImage>
#include <QtMath>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>

#include "audio/AudioDevice.h"
#include "audio/AudioFiles.h"
#include "audio/FakeAudio.h"
#include "canvas/CanvasView.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "shell/ToolboxModel.h"

#include "AppController.h"

namespace {
class ToolboxTest: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        settings()->resetLayoutChoices();
        controller->toolboxModel()->reset();
        controller->setColorPalette("classic");
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
        resize(1920, 1080);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));  // (the pointer rests outside: nothing hovered, no tool tips)
        controller->newDocument();
        wait(200);
    }
    void TearDown() override {
        settings()->resetLayoutChoices();
        controller->shutdown();
        engine.reset();
        controller.reset();
    }

    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }
    xqt::ToolboxModel* tools() const { return controller->toolboxModel(); }
    void resize(int w, int h) {
        window->resize(w, h);
        until([&] { return window->width() == w && window->height() == h; });
        wait(300);  // (the size class, then the plans settle)
    }
    static void wait(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    static void until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
    /// By its objectName: among the window's objects, else in the tree of items (a Repeater's delegates)
    template <typename T = QQuickItem>
    T* find(const QString& name) const {
        if (T* t = window->findChild<T*>(name)) {
            return t;
        }
        if constexpr (std::is_base_of_v<QQuickItem, T>) {
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
            return qobject_cast<T*>(walk(window->contentItem()));
        }
        return nullptr;
    }
    /// Shown: visible all the way up, and of some size
    static bool shown(QQuickItem* item) { return item && item->isVisible() && item->width() > 0 && item->height() > 0; }
    static QRectF rectOf(QQuickItem* item) {
        return item ? item->mapRectToScene(QRectF(0, 0, item->width(), item->height())) : QRectF(-1000, -1000, 0, 0);
    }
    static bool inside(QQuickItem* item, QQuickItem* container) {
        for (QQuickItem* p = item; p; p = p->parentItem()) {
            if (p == container) {
                return true;
            }
        }
        return false;
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, rectOf(item).center().toPoint());
        wait(60);
    }
    QString nth(const QString& type, int n = 0) const {
        for (const QVariant& v: tools()->tools()) {
            if (v.toMap().value("type") == type && n-- == 0) {
                return v.toMap().value("id").toString();
            }
        }
        return {};
    }
    QQuickItem* entry(const QString& id) const { return find("toolEntry_" + id); }
    QVariant win(const char* property) const { return window->property(property); }
    static QObject* entryOf(QObject* menu, const char* name) {
        const int n = menu ? menu->property("count").toInt() : 0;
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            if (it && it->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
    /// Triggers an entry of a menu (it must be there and enabled), and waits for the menu to close
    void trigger(QObject* menu, const char* name) {
        until([&] { return menu->property("visible").toBool(); });
        QObject* item = entryOf(menu, name);
        ASSERT_NE(item, nullptr) << name;
        ASSERT_TRUE(item->property("enabled").toBool()) << name;
        QMetaObject::invokeMethod(item, "triggered");
        QMetaObject::invokeMethod(menu, "close");
        until([&] { return !menu->property("visible").toBool(); });
        wait(250);  // (its closing transition)
    }
    /// An item named `name` inside `root` (the first one, depth first)
    static QQuickItem* under(QQuickItem* root, const QString& name) {
        if (!root) {
            return nullptr;
        }
        for (QQuickItem* c: root->childItems()) {
            if (c->objectName() == name) {
                return c;
            }
            if (QQuickItem* f = under(c, name)) {
                return f;
            }
        }
        return nullptr;
    }
    /// The strokes of ink along the middle line of an item, in the window's picture (a solid line: 1; dashed: more)
    int inkRuns(QQuickItem* item) const {
        if (!item) {
            return -1;
        }
        const QImage picture = window->grabWindow();
        const qreal dpr = picture.devicePixelRatio();
        const QRectF r = rectOf(item);
        const int y = qRound(r.center().y() * dpr);
        int runs = 0;
        bool inInk = false;
        for (int x = qCeil(r.left() * dpr); x < qFloor(r.right() * dpr); ++x) {
            const bool ink = qGray(picture.pixel(x, y)) < 160;
            runs += ink && !inInk ? 1 : 0;
            inInk = ink;
        }
        return runs;
    }
    /// What the rail shows in its middle (qt/docs/toolbox.md, "A rail that scrolls"): its items along it ("toolEntry_e1",
    /// "railApp_hand", "railGroup_g1", "|"), how many cells lie wholly in sight, the room of the middle, the length of
    /// its view and of its contents, where it is scrolled to
    struct RailState {
        QStringList order;
        int cells = 0;
        int inSight = 0;
        double room = 0;
        double view = 0;
        double content = 0;
        double pos = 0;
        bool scrolls = false;
        /// The part of the cell at the view's end that shows (0: none cut)
        double cutShown = 0;
        QString text;
    };
    RailState rail() const {
        RailState s;
        auto* box = find("toolbox");
        auto* middle = find("toolboxMiddle");
        auto* grid = find("toolboxTools");
        if (!box || !middle || !grid) {
            return s;
        }
        const bool vertical = box->property("vertical").toBool();
        const QRectF sight = rectOf(middle);
        QList<QQuickItem*> kids = grid->childItems();
        std::sort(kids.begin(), kids.end(), [&](QQuickItem* a, QQuickItem* b) {
            return vertical ? rectOf(a).top() < rectOf(b).top() : rectOf(a).left() < rectOf(b).left();
        });
        for (QQuickItem* k: kids) {
            const QList<QQuickItem*> in = k->childItems();
            if (in.isEmpty() || !shown(k)) {
                continue;
            }
            const QString n = in.first()->objectName();
            s.order << (n.isEmpty() ? QString("|") : n);
            if (n.isEmpty()) {
                continue;
            }
            ++s.cells;
            const QRectF r = rectOf(k);
            const double a = vertical ? r.top() : r.left(), b = vertical ? r.bottom() : r.right();
            const double from = vertical ? sight.top() : sight.left();
            const double end = vertical ? sight.bottom() : sight.right();
            s.inSight += a >= from - 0.5 && b <= end + 0.5 ? 1 : 0;
            if (a < end - 0.5 && b > end + 0.5) {
                s.cutShown = end - a;
            }
        }
        s.room = box->property("middleRoom").toDouble();
        s.view = box->property("viewLength").toDouble();
        s.content = box->property("contentLength").toDouble();
        s.pos = box->property("scrollPos").toDouble();
        s.scrolls = box->property("scrolls").toBool();
        s.text = QString("%1×%2 %3%4: %5 cells, %6 in sight, room %7, view %8, content %9, at %10%11")
                         .arg(window->width())
                         .arg(window->height())
                         .arg(win("layoutClass").toString(), box->property("compact").toBool() ? " (dock)" : "")
                         .arg(s.cells)
                         .arg(s.inSight)
                         .arg(s.room)
                         .arg(s.view)
                         .arg(s.content)
                         .arg(s.pos)
                         .arg(s.scrolls ? ", scrolls" : "");
        return s;
    }
    /// The rail's items as the arrangement has them, by the names of their buttons
    QStringList arranged() const {
        QStringList out;
        for (const QVariant& v: tools()->entries()) {
            const QVariantMap m = v.toMap();
            out << (m.value("divider").toBool() ? QString("|")
                    : m.contains("app")         ? "railApp_" + m.value("app").toString()
                    : m.value("group").toBool() ? "railGroup_" + m.value("id").toString()
                                                : "toolEntry_" + m.value("id").toString());
        }
        return out;
    }
    /// Wholly in the view of the rail's middle, along the rail (the tool in hand is lifted a little across it)
    bool inSight(QQuickItem* item) const {
        if (!shown(item)) {
            return false;
        }
        const bool vertical = find("toolbox")->property("vertical").toBool();
        const QRectF sight = rectOf(find("toolboxMiddle"));
        // (the cell that holds it: not lifted, not scaled)
        QQuickItem* cell = item;
        while (cell->parentItem() && cell->parentItem()->objectName() != "toolboxTools") {
            cell = cell->parentItem();
        }
        const QRectF r = rectOf(cell);
        return vertical ? r.top() >= sight.top() - 0.5 && r.bottom() <= sight.bottom() + 0.5
                        : r.left() >= sight.left() - 0.5 && r.right() <= sight.right() + 0.5;
    }
    void scrollRail(double pos) {
        QMetaObject::invokeMethod(find("toolbox"), "scrollTo", Q_ARG(QVariant, pos));
        wait(50);
    }
    /// The rail shows the arrangement as it is (the same order on every screen, nothing folded): all of it in sight, or
    /// it scrolls, its view ending through the middle of a cell, a fade at the end that has more
    void expectRail(const char* where) {
        auto* box = find("toolbox");
        ASSERT_TRUE(shown(box)) << where;
        until([&] { return rail().cells > 0; });
        scrollRail(0);
        const RailState s = rail();
        const double cell = box->property("cell").toDouble();
        SCOPED_TRACE(std::string(where) + ": " + s.text.toStdString());
        EXPECT_EQ(s.order, arranged()) << "the same order as arranged";
        EXPECT_EQ(find("toolStack_" + tools()->active()), nullptr) << "nothing folded";
        EXPECT_GE(s.view, cell / 2);
        if (s.scrolls && s.room < cell * 1.5) {
            EXPECT_NEAR(s.view, s.room, 0.5) << "a room for one cell: all of it";
        } else if (!s.scrolls) {
            EXPECT_EQ(s.inSight, s.cells) << "every cell in sight";
            EXPECT_LE(s.content, s.room + 0.5);
            EXPECT_FALSE(shown(find("toolboxFadeEnd")));
        } else {
            // (scrolls, with room for more than a cell)
            EXPECT_GT(s.content, s.room);
            EXPECT_LE(s.view, s.room + 0.5);
            EXPECT_NEAR(s.cutShown, cell / 2, 1) << "half of the next cell shows";
            EXPECT_GE(s.inSight, int(s.view / cell) - 1) << "the cells in sight fill the view";
            EXPECT_TRUE(shown(find("toolboxFadeEnd"))) << "a fade where there is more";
            EXPECT_FALSE(shown(find("toolboxFadeStart")));
        }
    }
    void touchProfile(const char* mode) {
        QMetaObject::invokeMethod(controller->settingsModel(), "set", Q_ARG(QString, "touchProfile"),
                                  Q_ARG(QVariant, mode));
        wait(100);
    }
    /// A phone's safe area (Android edge to edge: the status bar, the navigation bar, a cut-out held sideways)
    void safeArea(double top, double right, double bottom, double left) {
        window->setProperty("safeTop", top);
        window->setProperty("safeRight", right);
        window->setProperty("safeBottom", bottom);
        window->setProperty("safeLeft", left);
        wait(300);
    }
    QObject* editor() const { return find<QObject>("toolEntryEditor"); }
    bool editorOpen() const { return editor() && editor()->property("visible").toBool(); }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

TEST_F(ToolboxTest, dockedAtTheRightWithUndoAndRedoTheToolsAndTheAppTools) {
    auto* box = find("toolbox");
    ASSERT_TRUE(shown(box));
    EXPECT_EQ(win("toolboxEdge").toString(), "right");
    const QRectF r = rectOf(box);
    EXPECT_NEAR(r.right(), window->width(), 1) << "at the window's right edge";
    EXPECT_LE(r.width(), 60);
    auto* canvas = find("canvas");
    EXPECT_LE(rectOf(canvas).right(), r.left() + 0.5) << "the page ends at the rail";

    // Undo and redo lead it; the view pill has none
    auto* undo = find("toolboxUndoButton");
    auto* redo = find("toolboxRedoButton");
    ASSERT_TRUE(shown(undo));
    ASSERT_TRUE(shown(redo));
    EXPECT_LT(rectOf(undo).top(), rectOf(entry(nth("pen"))).top());
    EXPECT_FALSE(find("undoButton")->isVisible());
    EXPECT_FALSE(shown(find("toolUndoButton"))) << "not in the command bar either";

    // The user's tools, then the app's tools (the window's own buttons, lent to the rail), then "+"; all in sight at
    // 1920 × 1080
    for (const QVariant& v: tools()->tools()) {
        auto* b = entry(v.toMap().value("id").toString());
        ASSERT_TRUE(shown(b)) << v.toMap().value("type").toString().toStdString();
        EXPECT_TRUE(inside(b, box));
    }
    expectRail("1920×1080");
    EXPECT_FALSE(rail().scrolls);
    const char* appTools[] = {"handButton", "selectButton", "snipButton", "pdfTextButton"};
    double last = rectOf(entry(nth("laser"))).bottom();
    for (const char* name: appTools) {
        auto* b = find(name);
        ASSERT_TRUE(shown(b)) << name;
        EXPECT_TRUE(inside(b, box)) << name;
        EXPECT_GE(rectOf(b).top(), last - 0.5) << name << ": after the user's tools, in their order";
        last = rectOf(b).bottom();
    }
    // Write on the page, the setsquare and the finger switch left the rail: the command bar has them (qt/top-bar will
    // place them in the top bar's own arrangement)
    for (const char* name: {"textModeButton", "geometryButton", "touchDrawingButton"}) {
        auto* b = find(name);
        ASSERT_TRUE(shown(b)) << name;
        EXPECT_FALSE(inside(b, box)) << name;
        EXPECT_TRUE(inside(b, find("toolArea"))) << name;
    }
    EXPECT_TRUE(shown(find("toolboxAddButton")));
    // The classic tools are gone (0.8.0): the toolbox's entries are the pens, erasers, shapes, text boxes and notes
    for (const char* name: {"penButton", "eraserButton", "shapeButton", "textButton", "stickyNoteButton",
                            "colorStrip", "widthStrip"}) {
        EXPECT_EQ(find(name), nullptr) << name;
    }
    EXPECT_TRUE(shown(find("searchButton"))) << "the commands stay at the top";

    // An app tool on the rail is its button: a tap takes it
    click(find("railApp_hand"));
    until([&] { return controller->tool() == "hand"; });
    EXPECT_EQ(controller->tool(), "hand");
    click(find("railApp_select"));
    until([&] { return controller->tool() == "selectRect" || controller->tool() == "selectRegion"; });
    const QString first = controller->tool();
    click(find("railApp_select"));
    until([&] { return controller->tool() != first; });
    EXPECT_EQ(controller->tool(), first == "selectRect" ? "selectRegion" : "selectRect")
            << "its own cycle: rectangle, lasso";

    // Undo from the rail
    controller->applyToolEntry(nth("pen"));
    controller->insertStickyNote();
    until([&] { return controller->canUndo(); });
    ASSERT_TRUE(undo->isEnabled());
    click(undo);
    until([&] { return controller->canRedo(); });
    EXPECT_TRUE(controller->canRedo());
}

TEST_F(ToolboxTest, aTapPicksUpAToolAndTheOneInHandIsLifted) {
    const QString red = nth("pen", 2);
    click(entry(red));
    until([&] { return controller->color() == QColor("#D6342C"); });
    EXPECT_EQ(controller->tool(), "pen");
    EXPECT_EQ(controller->color(), QColor("#D6342C"));
    EXPECT_EQ(tools()->active(), red);
    EXPECT_TRUE(entry(red)->property("inHand").toBool());
    EXPECT_FALSE(entry(nth("pen"))->property("inHand").toBool());

    const QString eraser = nth("eraser");
    click(entry(eraser));
    until([&] { return controller->tool() == "eraser"; });
    EXPECT_TRUE(entry(eraser)->property("inHand").toBool());
    EXPECT_FALSE(entry(red)->property("inHand").toBool());

    // An app tool: no entry in hand
    click(find("railApp_hand"));
    until([&] { return controller->tool() == "hand"; });
    EXPECT_FALSE(entry(eraser)->property("inHand").toBool());

    // A sticky note entry: a note on the page in its color
    click(entry(nth("sticky")));
    until([&] { return controller->noteSelected(); });
    EXPECT_TRUE(controller->noteSelected());

    // The keys: the entry of that type used last
    click(entry(red));
    click(entry(nth("highlighter")));
    QTest::keyClick(window, Qt::Key_P);
    until([&] { return controller->tool() == "pen"; });
    EXPECT_EQ(tools()->active(), red);
    EXPECT_EQ(controller->color(), QColor("#D6342C"));
}

TEST_F(ToolboxTest, theEdgeIsChosenPerWindowSize) {
    auto* box = find("toolbox");
    auto* canvas = find("canvas");
    QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, "left"));
    until([&] { return rectOf(box).left() < 1; });
    EXPECT_NEAR(rectOf(box).left(), 0, 1);
    EXPECT_GE(rectOf(canvas).left(), rectOf(box).right() - 0.5) << "the page begins after the rail (or the sidebar)";
    EXPECT_EQ(settings()->layoutChoice("desktopWide", "toolbox"), "left");

    QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, "top"));
    until([&] { return rectOf(box).width() > 1000; });
    EXPECT_GT(rectOf(box).width(), 1000) << "a row across the window";
    EXPECT_LE(rectOf(box).bottom(), rectOf(canvas).top() + 0.5);
    EXPECT_GT(rectOf(entry(nth("pen", 1))).left(), rectOf(entry(nth("pen"))).right() - 0.5) << "side by side";

    QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, "bottom"));
    until([&] { return rectOf(box).top() > 800; });
    EXPECT_LE(rectOf(box).bottom(), window->height() + 0.5);
    EXPECT_GT(rectOf(box).bottom(), window->height() - 4) << "at the window's bottom";
    EXPECT_LE(rectOf(canvas).bottom(), rectOf(box).top() + 0.5);

    // Another size class keeps its own (the automatic: the right)
    resize(1024, 700);
    EXPECT_EQ(win("toolboxEdge").toString(), "right");
    resize(1920, 1080);
    EXPECT_EQ(win("toolboxEdge").toString(), "bottom");
    QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, "right"));
    EXPECT_EQ(settings()->layoutChoice("desktopWide", "toolbox"), "") << "the automatic edge is not stored";
}

// The author on 0.7.0, a Galaxy Fold 7 unfolded: "the user defined tools on the rail are fully collapsed into a single
// button while the system tools are expanded to the rest … scrolling the tools would be good … the same ui on wide
// desktop or smaller screens since the tool placement and order can be the same" (qt/rail-scroll). A short rail
// scrolls: nothing folds, the order stays, half of the next cell shows, a fade at the end that has more
TEST_F(ToolboxTest, aShortRailScrollsAndKeepsItsOrder) {
    resize(1300, 600);
    auto* box = find("toolbox");
    ASSERT_TRUE(shown(box));
    expectRail("1300×600");
    ASSERT_TRUE(rail().scrolls);
    EXPECT_TRUE(rectOf(box).contains(rectOf(find("toolboxAddButton")))) << "\"+\" pinned at its end";
    EXPECT_TRUE(inSight(entry(nth("pen")))) << "the first tools in sight";
    // Scrolled to its end: the last app tool in sight, the fade at the start
    scrollRail(1e6);
    EXPECT_TRUE(inSight(find("railApp_pdfText")));
    EXPECT_TRUE(shown(find("toolboxFadeStart")));
    EXPECT_FALSE(shown(find("toolboxFadeEnd")));
    EXPECT_TRUE(shown(find("toolboxUndoButton"))) << "undo and redo do not scroll";

    // The tool in hand, taken by a key or from elsewhere: scrolled into view
    scrollRail(0);
    ASSERT_FALSE(inSight(find("railApp_hand")));
    controller->selectTool("hand");
    until([&] { return inSight(find("railApp_hand")); });
    EXPECT_TRUE(inSight(find("railApp_hand"))) << rail().text.toStdString();
    QTest::keyClick(window, Qt::Key_P);
    until([&] { return inSight(entry(nth("pen"))); });
    EXPECT_TRUE(inSight(entry(nth("pen"))));
    scrollRail(0);
    const QString laser = nth("laser");
    controller->applyToolEntry(laser);
    until([&] { return inSight(entry(laser)); });
    EXPECT_TRUE(inSight(entry(laser)));

    // The mouse wheel: over an app tool (no width) or a gap it scrolls; over a tool, its width
    scrollRail(1e6);
    const double end = rail().pos;
    const QPoint overApp = rectOf(find("railApp_hand")).center().toPoint();
    QWheelEvent upOverApp(overApp, window->mapToGlobal(overApp), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &upOverApp);
    until([&] { return rail().pos < end - 10; });
    EXPECT_LT(rail().pos, end - 10) << "scrolled by the wheel";
    const QString pen = nth("pen");
    scrollRail(0);
    const double before = tools()->entry(pen).value("width").toDouble();
    const QPoint overPen = rectOf(entry(pen)).center().toPoint();
    QWheelEvent up(overPen, window->mapToGlobal(overPen), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                   Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &up);
    until([&] { return tools()->entry(pen).value("width").toDouble() > before; });
    EXPECT_GT(tools()->entry(pen).value("width").toDouble(), before) << "its width";
    EXPECT_NEAR(rail().pos, 0, 0.5) << "and no scrolling";

    // A drag at once scrolls it, a tap takes
    scrollRail(0);
    const QPoint from = rectOf(entry(nth("highlighter"))).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int k = 1; k <= 10; ++k) {
        QTest::mouseMove(window, from - QPoint(0, 20 * k));
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from - QPoint(0, 200));
    until([&] { return rail().pos > 20; });
    EXPECT_GT(rail().pos, 20) << "a drag scrolls";
    EXPECT_NE(tools()->active(), nth("highlighter")) << "and takes nothing";
    EXPECT_EQ(rail().order, arranged()) << "and moves nothing";
}

// The same order at every size the author uses: the Fold 7 unfolded (900 × 1000), turned (1000 × 900), folded (412 ×
// 915, the dock) and folded held sideways (915 × 412), a desktop (1920 × 1080); with the touch profile and with and
// without a phone's insets. Nothing folds, and the tool in hand is scrolled into view
TEST_F(ToolboxTest, theSameOrderAtEverySizeAndTheToolInHandInView) {
    touchProfile("on");
    for (const bool insets: {false, true}) {
        safeArea(insets ? 40 : 0, 0, insets ? 24 : 0, 0);
        for (const auto& [w, h]: std::vector<std::pair<int, int>>{{900, 1000}, {1000, 900}, {412, 915}, {915, 412},
                                                                  {1920, 1080}}) {
            resize(w, h);
            const std::string at = std::to_string(w) + "x" + std::to_string(h) + (insets ? " with insets" : "");
            expectRail(at.c_str());
            // The last of the rail and the first, taken from elsewhere: in view
            controller->selectTool("hand");
            until([&] { return inSight(find("railApp_pdfText")) || inSight(find("railApp_hand")); });
            EXPECT_TRUE(inSight(find("railApp_hand"))) << at << ": " << rail().text.toStdString();
            QTest::keyClick(window, Qt::Key_P);
            until([&] { return inSight(entry(tools()->active())); });
            EXPECT_TRUE(inSight(entry(tools()->active()))) << at << ": " << rail().text.toStdString();
        }
    }
    // The dock of the folded phone scrolls sideways; unfolded, every one of the user's tools of the first start is in
    // sight at once (the author's report: they were folded into one button there)
    resize(412, 915);
    EXPECT_TRUE(rail().scrolls) << rail().text.toStdString();
    EXPECT_TRUE(find("toolbox")->property("compact").toBool());
    resize(900, 1000);
    scrollRail(0);
    for (const QVariant& v: tools()->tools()) {
        EXPECT_TRUE(inSight(entry(v.toMap().value("id").toString()))) << rail().text.toStdString();
    }
    safeArea(0, 0, 0, 0);
    touchProfile("auto");
}

// Where the rail was scrolled to is remembered per window class: the folded phone's dock and the unfolded rail each
// keep their own
TEST_F(ToolboxTest, theScrollPositionIsRememberedPerWindowClass) {
    touchProfile("on");
    controller->selectTool("text");  // (no tool of the rail in hand: that one would be scrolled into view)
    resize(412, 915);
    ASSERT_TRUE(rail().scrolls);
    scrollRail(100);
    const double folded = rail().pos;
    EXPECT_GT(folded, 50);
    wait(800);  // (written after a pause)
    resize(915, 412);
    ASSERT_TRUE(rail().scrolls) << rail().text.toStdString();
    scrollRail(0);
    wait(800);
    resize(412, 915);
    until([&] { return std::abs(rail().pos - folded) < 1; });
    EXPECT_NEAR(rail().pos, folded, 1) << "the dock as it was";
    resize(915, 412);
    until([&] { return rail().pos < 1; });
    EXPECT_NEAR(rail().pos, 0, 1) << "sideways: its own";
    touchProfile("auto");
}

TEST_F(ToolboxTest, aTapOnTheToolInHandOpensItsEditorAndAChangeIsTakenAtOnce) {
    const QString pen = nth("pen");
    controller->applyToolEntry(nth("highlighter"));
    click(entry(pen));
    until([&] { return tools()->active() == pen; });
    EXPECT_FALSE(editorOpen()) << "the first tap picks it up";
    click(entry(pen));
    until([&] { return editorOpen(); });
    ASSERT_TRUE(editorOpen());
    auto* panel = qobject_cast<QQuickItem*>(editor()->property("contentItem").value<QObject*>());
    ASSERT_NE(panel, nullptr);
    EXPECT_LT(rectOf(panel).right(), rectOf(find("toolbox")).left()) << "beside the rail, towards the page";
    EXPECT_FALSE(shown(find("toolEditorAdd"))) << "no Add: a change is written at once";

    click(find("editorSize_4"));
    until([&] { return tools()->entry(pen).value("width").toDouble() > 5; });
    EXPECT_DOUBLE_EQ(tools()->entry(pen).value("width").toDouble(), 5.67);
    EXPECT_DOUBLE_EQ(controller->customWidth(), 5.67) << "the tool in hand follows";
    EXPECT_EQ(find<QObject>("toolEditorWidthValue")->property("text").toString(), "2.0 mm");

    click(find("editorRole_warnings"));
    until([&] { return tools()->entry(pen).value("role") == "warnings"; });
    EXPECT_EQ(controller->color(), QColor("#D6342C"));

    click(find("editorLineStyle_dash"));
    until([&] { return controller->lineStyle() == "dash"; });
    EXPECT_EQ(tools()->entry(pen).value("lineStyle"), "dash");

    click(find("toolEditorFillSwitch"));
    until([&] { return controller->fillEnabled(); });
    EXPECT_TRUE(tools()->entry(pen).value("fill").toMap().value("on").toBool());

    // The hex code: a color of one's own (no role)
    auto* hex = find("toolEditorHex");
    ASSERT_TRUE(shown(hex));
    hex->forceActiveFocus();
    hex->setProperty("text", "#00aa00");
    QMetaObject::invokeMethod(hex, "accepted");
    until([&] { return controller->color() == QColor("#00aa00"); });
    EXPECT_EQ(tools()->entry(pen).value("role"), "");

    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !editorOpen(); });
    EXPECT_FALSE(editorOpen());

    // The wheel over the tool in hand: its width
    const double before = tools()->entry(pen).value("width").toDouble();
    const QPoint at = rectOf(entry(pen)).center().toPoint();
    QWheelEvent wheel(at, window->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &wheel);
    until([&] { return tools()->entry(pen).value("width").toDouble() > before; });
    EXPECT_NEAR(tools()->entry(pen).value("width").toDouble(), before * 1.25, 0.01);
    EXPECT_NEAR(controller->customWidth(), before * 1.25, 0.01);
}

TEST_F(ToolboxTest, theLineStylesShowTheirDashesInTheEditorAndOnTheRail) {
    // (the author, 2026-10-05: "the dashed and dotted line buttons just show a regular line")
    const QString pen = nth("pen");
    controller->applyToolEntry(pen);
    click(entry(pen));
    until([&] { return editorOpen(); });
    ASSERT_TRUE(editorOpen());
    wait(300);  // (opened, painted)
    auto sample = [&](const char* key) { return under(find(QString("editorLineStyle_") + key), "lineStyleSample"); };
    ASSERT_NE(sample("dash"), nullptr);
    EXPECT_EQ(inkRuns(sample("plain")), 1) << "solid: one line";
    EXPECT_GE(inkRuns(sample("dash")), 2) << "dashed: dashes";
    EXPECT_GE(inkRuns(sample("dashdot")), 3) << "dash-dot: a dash, a dot, a dash";
    EXPECT_GE(inkRuns(sample("dot")), 3) << "dotted: dots";
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !editorOpen(); });

    // The tool on the rail: a sample of its ink, dashed as it draws
    tools()->update(pen, {{"lineStyle", "dash"}});
    const QString dotted = tools()->duplicate(pen);
    tools()->update(dotted, {{"lineStyle", "dot"}});
    controller->applyToolEntry(nth("highlighter"));  // (neither is lifted in hand)
    until([&] { return shown(entry(dotted)); });
    wait(300);
    EXPECT_GE(inkRuns(under(entry(pen), "toolSample")), 2) << "a dashed pen on the rail";
    EXPECT_GE(inkRuns(under(entry(dotted), "toolSample")), 3) << "a dotted pen on the rail";
}

TEST_F(ToolboxTest, theEditorStaysBesideTheToolWhileSomethingIsChosenInIt) {
    // (the author, 2026-10-05: "when I select something on the toolbelt popup the popup moves to the upper left")
    const QString pen = nth("pen");
    controller->applyToolEntry(pen);
    click(entry(pen));
    until([&] { return editorOpen(); });
    ASSERT_TRUE(editorOpen());
    auto* panel = qobject_cast<QQuickItem*>(editor()->property("contentItem").value<QObject*>());
    ASSERT_NE(panel, nullptr);
    wait(250);  // (its opening transition)
    const QRectF before = rectOf(panel);
    const QRectF rail = rectOf(find("toolbox"));
    ASSERT_LT(before.right(), rail.left());
    ASSERT_GT(before.left(), rail.left() - 400) << "beside the rail";
    auto stays = [&](const char* what) {
        wait(150);
        const QRectF now = rectOf(panel);
        EXPECT_NEAR(now.left(), before.left(), 1) << what;
        EXPECT_NEAR(now.top(), before.top(), 1) << what;
    };
    click(find("editorRole_warnings"));
    until([&] { return tools()->entry(pen).value("role") == "warnings"; });
    stays("a color");
    click(find("editorSize_3"));
    until([&] { return tools()->entry(pen).value("width").toDouble() > 2; });
    stays("a width");
    click(find("editorLineStyle_dot"));
    until([&] { return tools()->entry(pen).value("lineStyle") == "dot"; });
    stays("a line style");
    // (a tool that is not the first: the rail's other tools are rebuilt around it too)
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !editorOpen(); });
    const QString marker = nth("highlighter", 1);
    controller->applyToolEntry(marker);
    click(entry(marker));
    until([&] { return editorOpen(); });
    wait(250);
    const QRectF second = rectOf(panel);
    EXPECT_NEAR(second.center().y(), rectOf(entry(marker)).center().y(), second.height() / 2 + 1);
    click(find("editorRole_keyTerms"));
    until([&] { return tools()->entry(marker).value("role") == "keyTerms"; });
    wait(150);
    EXPECT_NEAR(rectOf(panel).top(), second.top(), 1) << "the second highlighter's editor stays too";
    EXPECT_TRUE(editorOpen());
}

TEST_F(ToolboxTest, plusAddsAToolPrefilledFromTheLastOfItsKind) {
    const int count = tools()->tools().size();
    click(find("toolboxAddButton"));
    auto* types = find<QObject>("toolTypeMenu");
    trigger(types, "toolType_shape");
    until([&] { return editorOpen(); });
    ASSERT_TRUE(editorOpen());
    EXPECT_TRUE(shown(find("toolEditorAdd")));
    EXPECT_EQ(tools()->tools().size(), count) << "not yet";
    click(find("editorShape_arrow"));
    click(find("editorLineStyle_dot"));
    click(find("toolEditorAdd"));
    until([&] { return tools()->tools().size() == count + 1; });
    ASSERT_EQ(tools()->tools().size(), count + 1);
    const QVariantMap added = tools()->tools().last().toMap();
    EXPECT_EQ(added.value("type"), "shape");
    EXPECT_EQ(added.value("variant"), "arrow");
    EXPECT_EQ(added.value("lineStyle"), "dot");
    EXPECT_EQ(tools()->active(), added.value("id").toString()) << "taken at once";
    EXPECT_EQ(controller->drawingType(), "arrow");
    until([&] { return shown(entry(added.value("id").toString())); });
    EXPECT_TRUE(shown(entry(added.value("id").toString())));
    until([&] { return !editorOpen(); });
    EXPECT_FALSE(editorOpen());

    // Cancel adds nothing
    click(find("toolboxAddButton"));
    trigger(types, "toolType_pen");
    until([&] { return editorOpen(); });
    click(find("toolEditorCancel"));
    until([&] { return !editorOpen(); });
    EXPECT_EQ(tools()->tools().size(), count + 1);
}

TEST_F(ToolboxTest, aSnipIsACyclingToolOfItsOwnThatCanBeAdded) {
    // (the author, 2026-10-05: "can we have the snipping screenshots as a cycling tool in the toolbelt")
    const QString pen = nth("pen");
    controller->applyToolEntry(pen);
    const int count = tools()->tools().size();
    click(find("toolboxAddButton"));
    trigger(find<QObject>("toolTypeMenu"), "toolType_snip");
    until([&] { return editorOpen(); });
    ASSERT_TRUE(editorOpen());
    EXPECT_TRUE(shown(find("toolEditorSnipShapes")));
    EXPECT_FALSE(shown(find("toolEditorColors"))) << "a snip has no color";
    EXPECT_FALSE(shown(find("toolEditorWidth")));
    click(find("editorSnip_lasso"));
    click(find("toolEditorAdd"));
    until([&] { return tools()->tools().size() == count + 1; });
    const QString snip = tools()->tools().last().toMap().value("id").toString();
    EXPECT_EQ(tools()->entry(snip).value("variant"), "lasso");
    EXPECT_EQ(controller->snipShape(), "lasso") << "taken at once: the next lasso is copied";
    EXPECT_EQ(tools()->active(), pen) << "the pen comes back after the picture";
    until([&] { return !editorOpen(); });  // (its closing transition)
    until([&] { return shown(entry(snip)); });
    auto* button = entry(snip);
    ASSERT_TRUE(shown(button));
    EXPECT_TRUE(button->property("inHand").toBool());
    EXPECT_EQ(controller->tool(), "selectRegion");

    // A tap while it is armed: the other shape (the entry keeps it), as a cycling button
    click(button);
    until([&] { return controller->snipShape() == "rect"; });
    EXPECT_EQ(controller->snipShape(), "rect");
    EXPECT_EQ(tools()->entry(snip).value("variant"), "rect");
    EXPECT_FALSE(editorOpen()) << "a tap cycles, it does not open the editor";
    button = entry(snip);
    EXPECT_TRUE(button->property("inHand").toBool());

    // Escape: no snip, the pen again; the entry is put down
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return controller->snipShape().isEmpty(); });
    EXPECT_EQ(controller->tool(), "pen");
    until([&] { return !entry(snip)->property("inHand").toBool(); });
    EXPECT_FALSE(entry(snip)->property("inHand").toBool());
    EXPECT_TRUE(entry(pen)->property("inHand").toBool());
    // A tap again: the shape it has now
    click(entry(snip));
    until([&] { return controller->snipShape() == "rect"; });
    EXPECT_EQ(controller->snipShape(), "rect");
    controller->cancelSnip();
}

TEST_F(ToolboxTest, theMenuOfAToolMovesReplacesDuplicatesAndRemovesIt) {
    auto* menu = find<QObject>("toolEntryMenu");
    ASSERT_NE(menu, nullptr);
    const QString pen = nth("pen");
    auto openMenu = [&](const QString& id) {
        auto* b = entry(id);
        ASSERT_NE(b, nullptr);
        QMetaObject::invokeMethod(b, "pressAndHold");
        until([&] { return menu->property("visible").toBool(); });
        ASSERT_TRUE(menu->property("visible").toBool());
    };
    openMenu(pen);
    EXPECT_FALSE(entryOf(menu, "toolMoveEarlierItem")->property("enabled").toBool()) << "the first";
    trigger(menu, "toolMoveLaterItem");
    EXPECT_EQ(tools()->indexOf(pen), 1);

    openMenu(pen);
    trigger(menu, "toolDuplicateItem");
    EXPECT_EQ(tools()->indexOf(pen) + 1, tools()->indexOf(nth("pen", 2))) << "the copy right after it";
    EXPECT_EQ(tools()->tools().size(), 11);

    openMenu(pen);
    trigger(menu, "toolDividerItem");
    EXPECT_TRUE(tools()->hasDividerAfter(pen));

    // The only eraser cannot go; another tool can
    const QString eraser = nth("eraser");
    openMenu(eraser);
    EXPECT_FALSE(entryOf(menu, "toolRemoveItem")->property("enabled").toBool());
    QMetaObject::invokeMethod(menu, "close");
    until([&] { return !menu->property("visible").toBool(); });
    wait(250);
    const QString laser = nth("laser");
    openMenu(laser);
    trigger(menu, "toolRemoveItem");
    EXPECT_EQ(tools()->indexOf(laser), -1);
    until([&] { return entry(laser) == nullptr || !shown(entry(laser)); });

    // Replace with: the place stays, its editor opens
    const QString text = nth("text");
    const int at = tools()->indexOf(text);
    openMenu(text);
    trigger(menu, "toolReplaceItem");
    auto* types = find<QObject>("toolTypeMenu");
    trigger(types, "toolType_highlighter");
    until([&] { return editorOpen(); });
    EXPECT_EQ(tools()->entry(text).value("type"), "highlighter");
    EXPECT_EQ(tools()->indexOf(text), at);
    EXPECT_EQ(controller->tool(), "highlighter");
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !editorOpen(); });

    // Add a tool here: after it
    openMenu(pen);
    trigger(menu, "toolAddHereItem");
    trigger(types, "toolType_laser");
    until([&] { return editorOpen(); });
    click(find("toolEditorAdd"));
    until([&] { return !editorOpen(); });
    EXPECT_EQ(tools()->entries()[tools()->indexOf(pen) + 1].toMap().value("type"), "laser");
}

TEST_F(ToolboxTest, aToolHeldThenMovedIsCarriedToAnotherPlace) {
    const QString pen = nth("pen"), highlighter = nth("highlighter");
    const QPoint from = rectOf(entry(pen)).center().toPoint();
    const QPoint to = rectOf(entry(highlighter)).center().toPoint() + QPoint(0, 12);
    // A drag at once: not carried (the rail would scroll, it does not need to here)
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, from + (to - from) * k / 8);
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    wait(100);
    EXPECT_EQ(tools()->indexOf(pen), 0) << "not moved without the hold";

    // Held, then moved: lifted, carried, the place it would go marked; let go: there
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    wait(550);
    EXPECT_TRUE(entry(pen)->property("lifted").toBool()) << "lifted after 400 ms";
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, from + (to - from) * k / 8);
        wait(15);
    }
    EXPECT_TRUE(shown(find("toolDragGhost")));
    EXPECT_TRUE(shown(find("toolDropMark")));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    until([&] { return tools()->indexOf(pen) > 0; });
    EXPECT_EQ(tools()->indexOf(pen), tools()->indexOf(highlighter) + 1) << "after the first highlighter";
    EXPECT_FALSE(shown(find("toolDragGhost")));
    EXPECT_FALSE(find<QObject>("toolEntryMenu")->property("visible").toBool()) << "carried: no menu";

    // Let go away from the rail: it goes back
    const int before = tools()->indexOf(pen);
    const QPoint at = rectOf(entry(pen)).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(550);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, at + QPoint(-40 * k, -20 * k));
        wait(15);
    }
    EXPECT_FALSE(shown(find("toolDropMark")));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at + QPoint(-320, -160));
    wait(100);
    EXPECT_EQ(tools()->indexOf(pen), before);

    // Held and let go without moving: its menu
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rectOf(entry(pen)).center().toPoint());
    wait(550);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, rectOf(entry(pen)).center().toPoint());
    auto* menu = find<QObject>("toolEntryMenu");
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool());
    QMetaObject::invokeMethod(menu, "close");
}

TEST_F(ToolboxTest, theGripCarriesTheRailToAnotherEdge) {
    auto* grip = find("toolboxGrip");
    ASSERT_TRUE(shown(grip));
    const QPoint from = rectOf(grip).center().toPoint();
    const QPoint to(40, window->height() / 2);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int k = 1; k <= 12; ++k) {
        QTest::mouseMove(window, from + (to - from) * k / 12);
        wait(15);
    }
    EXPECT_TRUE(shown(find("toolboxEdgeHighlight")));
    EXPECT_EQ(win("toolboxEdgeTarget").toString(), "left");
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    until([&] { return win("toolboxEdge").toString() == "left"; });
    EXPECT_EQ(win("toolboxEdge").toString(), "left");
    EXPECT_FALSE(shown(find("toolboxEdgeHighlight")));
}

TEST_F(ToolboxTest, inFullScreenTheSameToolboxFloatsAndPresentingHidesIt) {
    window->setProperty("fullScreenMode", true);
    auto* box = find("toolbox");
    // (the window takes the screen's size: the rail follows)
    until([&] {
        const QRectF r = rectOf(box);
        return box->property("floating").toBool() && std::abs(r.right() - (window->width() - 8)) < 2
               && r.bottom() < window->height();
    });
    ASSERT_TRUE(shown(box));
    EXPECT_TRUE(box->property("floating").toBool());
    EXPECT_EQ(find("quickToolSquare"), nullptr) << "the classic tool square is gone";
    EXPECT_EQ(find("penPill"), nullptr);
    EXPECT_FALSE(shown(find("topTools")));
    const QRectF r = rectOf(box);
    EXPECT_NEAR(r.right(), window->width() - 8, 2) << "8 px off the right edge";
    EXPECT_GT(r.top(), 0);
    EXPECT_LT(r.bottom(), window->height());
    EXPECT_TRUE(shown(find("toolboxUndoButton")));
    EXPECT_TRUE(shown(entry(nth("pen"))));
    auto* more = find("toolboxMoreButton");
    ASSERT_TRUE(shown(more));
    auto* menu = find<QObject>("toolboxMoreMenu");
    // The tools that left the rail for the top bar, which full screen hides: in ⋯ (qt/rail-scroll)
    click(more);
    for (const char* name: {"toolboxMore_write", "toolboxMore_geometry", "toolboxMore_touchDrawing"}) {
        until([&] { return menu->property("visible").toBool(); });
        ASSERT_NE(entryOf(menu, name), nullptr) << name;
        EXPECT_TRUE(entryOf(menu, name)->property("offered").toBool()) << name;
    }
    trigger(menu, "toolboxMore_geometry");
    until([&] { return !controller->geometryTool().isEmpty(); });
    EXPECT_FALSE(controller->geometryTool().isEmpty()) << "the setsquare from ⋯";
    controller->toggleGeometryTool("");
    click(more);
    trigger(menu, "toolboxPresentItem");
    until([&] { return controller->presenting(); });
    EXPECT_TRUE(controller->presenting());
    EXPECT_TRUE(shown(box)) << "presenting keeps the tools (writing on the slides)";
    // The corner field hides them, and shows them again
    click(find("presentCornerMark"));
    until([&] { return !shown(box); });
    EXPECT_FALSE(shown(box));
    click(find("presentCornerMark"));
    until([&] { return shown(box); });
    EXPECT_TRUE(shown(box));
    controller->setPresenting(false);
    click(more);
    trigger(menu, "toolboxLeaveFullScreenItem");
    until([&] { return !win("fullScreenMode").toBool(); });
    EXPECT_FALSE(win("fullScreenMode").toBool());
    until([&] { return !box->property("floating").toBool(); });
    EXPECT_FALSE(box->property("floating").toBool()) << "docked again";
}

TEST_F(ToolboxTest, onAPhoneTheDockIsTheSameRailAndTheSheetHoldsThemAll) {
    resize(412, 915);
    auto* box = find("toolbox");
    until([&] { return box->property("compact").toBool(); });
    ASSERT_TRUE(shown(box));
    EXPECT_TRUE(inside(box, find("phoneDock")));
    EXPECT_FALSE(shown(find("dockToolButton"))) << "the dock's own cells give way";
    EXPECT_FALSE(shown(find("dockColorSlot")));
    EXPECT_TRUE(shown(find("toolboxUndoButton")));
    EXPECT_TRUE(shown(find("toolboxAllButton")));
    EXPECT_TRUE(shown(find("toolboxPageButton")));
    EXPECT_FALSE(shown(find("toolboxAddButton"))) << "in the sheet";
    const QRectF dock = rectOf(box);
    EXPECT_GT(dock.top(), 800) << "at the bottom";
    EXPECT_LE(dock.right(), 412.5);
    // The same items as the rail, scrolling sideways (the app tools too)
    expectRail("the dock");
    EXPECT_TRUE(rail().scrolls);
    EXPECT_FALSE(box->property("vertical").toBool());
    EXPECT_TRUE(inside(find("handButton"), box));
    // The tool in hand is scrolled into the dock
    const QString laser = nth("laser");
    controller->applyToolEntry(laser);
    until([&] { return inSight(entry(laser)); });
    EXPECT_TRUE(inSight(entry(laser))) << "the tool in hand in sight";
    EXPECT_TRUE(dock.contains(rectOf(entry(laser)).center()));
    // My tools: all of them, a tap takes one
    click(find("toolboxAllButton"));
    auto* sheet = find<QObject>("phoneToolSheet");
    until([&] { return sheet->property("visible").toBool(); });
    ASSERT_TRUE(sheet->property("visible").toBool());
    for (const QVariant& v: tools()->tools()) {
        EXPECT_NE(find("sheetEntry_" + v.toMap().value("id").toString()), nullptr);
    }
    EXPECT_NE(find("sheetAddTool"), nullptr);
    const QString yellow = nth("highlighter");
    auto* cell = find("sheetEntry_" + yellow);
    until([&] { return shown(cell); });
    click(cell);
    until([&] { return tools()->active() == yellow; });
    EXPECT_EQ(controller->tool(), "highlighter");
    until([&] { return !sheet->property("visible").toBool(); });

    // Held sideways: a rail at the right
    resize(915, 412);
    until([&] { return box->property("vertical").toBool(); });
    EXPECT_TRUE(box->property("vertical").toBool());
    EXPECT_NEAR(rectOf(box).right(), 915, 1);
}

TEST_F(ToolboxTest, theCommandBarShowsEntriesOfTheMoreMenuWhereThereIsRoom) {
    auto* moreMenu = find<QObject>("moreMenu");
    ASSERT_NE(moreMenu, nullptr);
    // (the entries of ⋮ in its submenus: found by their names)
    auto item = [&](const char* name) { return window->findChild<QObject*>(name); };
    // (qt/ui-rework: reading, the replay of the writing and the tags too; a milestone where versions are kept)
    const std::vector<std::pair<const char*, const char*>> promoted{
            {"shareButton", "shareItem"}, {"printButton", "printItem"}, {"readButton", "readItem"},
            {"replayButton", "replayItem"}, {"tagsButton", "documentTagsMenuItem"}};
    auto check = [&](int w, int h) {
        resize(w, h);
        for (const auto& [button, name]: promoted) {
            const bool inBar = shown(find(button));
            QObject* entry = item(name);
            ASSERT_NE(entry, nullptr) << name;
            EXPECT_NE(inBar, entry->property("offered").toBool())
                    << button << " at " << w << ": in the bar or in ⋮, never both, never neither";
        }
    };
    check(1920, 1080);
    for (const auto& [button, name]: promoted) {
        EXPECT_TRUE(shown(find(button))) << button << ": room for it at 1920";
    }
    EXPECT_FALSE(shown(find("moreToolsButton"))) << "nothing in \"more tools\"";
    EXPECT_FALSE(shown(find("milestoneButton"))) << "a new document keeps no versions";
    EXPECT_TRUE(item("saveWithMessageItem")->property("offered").toBool());
    // One place each: the rail's app tools are not in the bar; those that left the rail are
    for (const char* onRail: {"handButton", "selectButton", "snipButton", "pdfTextButton"}) {
        EXPECT_FALSE(inside(find(onRail), find("topTools"))) << onRail;
    }
    for (const char* inBar: {"textModeButton", "geometryButton", "touchDrawingButton"}) {
        EXPECT_TRUE(inside(find(inBar), find("topTools"))) << inBar;
    }
    // The ladder: the tags give way first, sharing last
    check(1366, 768);
    check(1024, 700);
    check(800, 600);
    resize(1920, 1080);
    const int full = qRound(find("topTools")->width());
    for (int w = full; w >= 600; w -= 40) {
        resize(w, 900);
        if (!shown(find("tagsButton"))) {
            break;
        }
        EXPECT_TRUE(shown(find("shareButton"))) << "at " << w << ": the tags go before sharing";
    }
    // Reading from the bar: full screen, read only
    resize(1920, 1080);
    click(find("readButton"));
    until([&] { return win("readOnlyOn").toBool(); });
    EXPECT_TRUE(win("fullScreenMode").toBool());
    window->setProperty("fullScreenMode", false);
    until([&] { return !win("readOnlyOn").toBool(); });
}

TEST_F(ToolboxTest, aTextDocumentHasUndoRedoAndItsCommandsInTheFormatBar) {
    QTemporaryDir dir;
    const QString md = dir.path() + "/notes.md";
    {
        QFile f(md);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("# Notes\n\nSome text.\n");
    }
    resize(1366, 768);
    ASSERT_TRUE(controller->openPath(md));
    until([&] { return shown(find("formatUndoButton")); });
    EXPECT_FALSE(shown(find("toolbox"))) << "no ink tools in a text document";
    EXPECT_TRUE(shown(find("formatUndoButton")));
    EXPECT_TRUE(shown(find("formatRedoButton")));
    EXPECT_FALSE(find("undoButton")->isVisible()) << "not in the view pill too";
    auto* commands = find("formatCommands");
    ASSERT_NE(commands, nullptr);
    until([&] { return inside(find("searchButton"), commands); });
    for (const char* name: {"searchButton", "fullScreenButton", "saveButton"}) {
        EXPECT_TRUE(shown(find(name)) && inside(find(name), commands)) << name << " stays at 1366";
    }
    EXPECT_FALSE(find<QObject>("formatBarFlick")->property("interactive").toBool()) << "no scrolling at 1366";
    resize(1920, 1080);
    until([&] { return inside(find("settingsButton"), commands); });
    for (const char* name: {"searchButton", "fullScreenButton", "saveButton", "settingsButton"}) {
        EXPECT_TRUE(shown(find(name)) && inside(find(name), commands)) << name << " at 1920";
    }
    resize(720, 1000);
    until([&] { return !inside(find("searchButton"), commands); });
    EXPECT_FALSE(shown(find("searchButton")) && inside(find("searchButton"), commands)) << "in \"more tools\" at 720";
}

/// Reading (qt/docs/toolbox.md, "Reading"; qt/ui-rework): read only in full screen and presenting, the edges turn the
/// pages, no ink, the tools back when it ends
class ReadingTest: public ToolboxTest {
protected:
    void SetUp() override {
        ToolboxTest::SetUp();
        controller->addPageAfterCurrent();
        controller->addPageAfterCurrent();
        controller->firstPage();
        until([&] { return page() == 1; });
        controller->applyToolEntry(nth("pen"));
    }
    int page() const { return controller->property("pageNumber").toInt(); }
    size_t ink() const {
        size_t n = 0;
        auto doc = controller->tabManager().currentSession()->getDocument();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            n += doc->getPage(i)->getSelectedLayer()->getElements().size();
        }
        return n;
    }
    /// A stroke with the mouse (the pen in hand) across the middle of the page
    void stroke() {
        const QPoint a = rectOf(find("canvas")).center().toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
        for (int k = 1; k <= 6; ++k) {
            QTest::mouseMove(window, a + QPoint(10 * k, 8 * k));
            wait(10);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(60, 48));
        wait(100);
    }
    void tapField(const char* name) {
        auto* field = find(name);
        ASSERT_TRUE(shown(field)) << name;
        const QRectF r = rectOf(field);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(qRound(r.center().x()), qRound(r.top() + r.height() * 0.4)));
        wait(80);
    }
    bool readOnly() const { return window->property("readOnlyOn").toBool(); }
    /// ⋮ → View → Read, then the checks every size shares: full screen, read only, the tools away, the edges turn the
    /// pages, no ink, Esc gives everything back
    /// Full screen has taken the screen's size (the window follows it a little later)
    void fullScreenSettled() {
        until([&] { return window->size() == window->screen()->size(); });
        wait(300);
    }
    void readAndLeave() {
        auto* canvas = find("canvas");
        QMetaObject::invokeMethod(find<QObject>("readItem"), "triggered");
        until([&] { return readOnly(); });
        ASSERT_TRUE(readOnly());
        fullScreenSettled();
        EXPECT_TRUE(win("fullScreenMode").toBool()) << "full screen";
        EXPECT_TRUE(win("reading").toBool());
        EXPECT_TRUE(canvas->property("readingOnly").toBool());
        EXPECT_FALSE(shown(find("toolbox"))) << "no tools";
        EXPECT_EQ(find("readingPill"), nullptr) << "the reading pill is gone";
        EXPECT_TRUE(shown(find("readOnlyMark"))) << "the lock says so";
        // The fields: a fifth of the width at each edge, the page's whole height
        auto* next = find("readingNextField");
        ASSERT_TRUE(shown(next));
        EXPECT_NEAR(rectOf(next).right(), rectOf(canvas).right(), 1);
        EXPECT_NEAR(rectOf(next).height(), rectOf(canvas).height(), 1);
        EXPECT_GE(rectOf(next).width(), std::max(48.0, rectOf(canvas).width() * 0.19));
        EXPECT_LE(rectOf(next).width(), std::max(48.0, rectOf(canvas).width() * 0.26));
        // The pen writes nothing
        const size_t before = ink();
        stroke();
        EXPECT_EQ(ink(), before) << "no ink by accident";
        // The edges turn the pages, with a hint where it was tapped
        controller->firstPage();
        until([&] { return page() == 1; });
        tapField("readingNextField");
        until([&] { return page() == 2; });
        EXPECT_EQ(page(), 2) << "the right edge: the next page";
        EXPECT_GT(find("readingNextHint")->opacity(), 0) << "a short hint";
        tapField("readingNextField");
        until([&] { return page() == 3; });
        EXPECT_EQ(page(), 3);
        tapField("readingPreviousField");
        until([&] { return page() == 2; });
        EXPECT_EQ(page(), 2) << "the left edge: the previous page";
        EXPECT_EQ(ink(), before) << "taps write nothing either";
        // A finger: a tap at the edge turns the page too; a swipe there scrolls and turns none
        static QPointingDevice* finger = QTest::createTouchDevice();
        const QRectF field = rectOf(find("readingNextField"));
        const QPoint from(qRound(field.center().x()), qRound(field.top() + field.height() * 0.7));
        const int at = page();
        QTest::touchEvent(window, finger).press(1, from);
        QTest::touchEvent(window, finger).release(1, from);
        until([&] { return page() == at + 1; });
        EXPECT_EQ(page(), at + 1) << "a finger's tap at the right edge";
        wait(700);  // (the hint has faded)
        // (towards the page before: the last page may end the scrolling)
        const QPoint top(from.x(), qRound(field.top() + field.height() * 0.2));
        const double y0 = canvas->property("contentY").toDouble();
        QTest::touchEvent(window, finger).press(1, top);
        for (int k = 1; k <= 8; ++k) {
            wait(16);
            QTest::touchEvent(window, finger).move(1, top + QPoint(0, 25 * k));
        }
        wait(16);
        const double y1 = canvas->property("contentY").toDouble();
        QTest::touchEvent(window, finger).release(1, top + QPoint(0, 200));
        wait(100);
        EXPECT_LT(y1, y0) << "the page scrolled under the swipe";
        EXPECT_LT(find("readingNextHint")->opacity(), 0.01) << "a swipe is no tap";
        EXPECT_EQ(ink(), before);
        // Esc: out of full screen, the tools again
        QTest::keyClick(window, Qt::Key_Escape);
        until([&] { return !win("fullScreenMode").toBool(); });
        EXPECT_FALSE(win("fullScreenMode").toBool());
        EXPECT_FALSE(readOnly());
        EXPECT_FALSE(canvas->property("readingOnly").toBool());
        EXPECT_FALSE(shown(find("readingNextField")));
        stroke();
        EXPECT_GT(ink(), before) << "the pen writes again";
    }
};

TEST_F(ReadingTest, onADesktopReadIsFullScreenReadOnlyAndTheEdgesTurnThePages) {
    readAndLeave();
    until([&] { return shown(find("toolbox")); });
    EXPECT_TRUE(shown(find("toolbox"))) << "the toolbox docked again";
    EXPECT_FALSE(find("toolbox")->property("floating").toBool());
}

/// The same on a phone's screen (ReadingPhone.ui@phone: an off-screen screen of 412 × 915, so full screen stays a phone)
class ReadingPhoneTest: public ReadingTest {
protected:
    void SetUp() override {
        if (QGuiApplication::primaryScreen()->size() != QSize(412, 915)) {
            GTEST_SKIP() << "needs a phone's screen (ReadingPhone.ui@phone)";
        }
        ReadingTest::SetUp();
    }
    void TearDown() override {
        if (controller) {
            ReadingTest::TearDown();
        }
    }
};

TEST_F(ReadingPhoneTest, onAPhoneReadIsFullScreenReadOnlyAndTheEdgesTurnThePages) {
    resize(412, 915);
    until([&] { return find("toolbox")->property("compact").toBool(); });
    ASSERT_EQ(win("phoneLayout").toBool(), true);
    readAndLeave();
    EXPECT_TRUE(win("phoneLayout").toBool()) << "still a phone in full screen";
    until([&] { return shown(find("toolbox")); });
    EXPECT_TRUE(find("toolbox")->property("compact").toBool()) << "the phone's dock again";
}

TEST_F(ReadingTest, readOnlyIsAToggleOfFullScreenAndPresenting) {
    window->setProperty("fullScreenMode", true);
    fullScreenSettled();
    auto* box = find("toolbox");
    until([&] { return shown(box) && box->property("floating").toBool(); });
    // The floating toolbox's ⋯: read only
    click(find("toolboxMoreButton"));
    trigger(find<QObject>("toolboxMoreMenu"), "toolboxReadOnlyItem");
    until([&] { return readOnly(); });
    ASSERT_TRUE(readOnly());
    EXPECT_FALSE(shown(box));
    EXPECT_TRUE(win("fullScreenMode").toBool());
    // The lock: write again, still full screen
    click(find("readOnlyButton"));
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly());
    until([&] { return shown(box); });
    EXPECT_TRUE(shown(box));
    EXPECT_TRUE(win("fullScreenMode").toBool());
    // The key, while presenting: page by page
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    until([&] { return controller->presenting(); });
    wait(300);  // (presenting has settled: the console placed, the window active)
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return readOnly(); });
    ASSERT_TRUE(readOnly());
    EXPECT_TRUE(find("canvas")->property("readingOnly").toBool());
    EXPECT_FALSE(shown(box));
    const size_t before = ink();
    stroke();
    EXPECT_EQ(ink(), before);
    tapField("readingNextField");
    until([&] { return page() == 2; });
    EXPECT_EQ(page(), 2) << "the next slide";
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly());
    until([&] { return shown(box); });
    EXPECT_TRUE(shown(box)) << "presenting with the tools again";
    controller->setPresenting(false);
    window->setProperty("fullScreenMode", false);
    until([&] { return !box->property("floating").toBool(); });
    // Not in a window: the key there enters full screen with it
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return readOnly(); });
    EXPECT_TRUE(win("fullScreenMode").toBool());
    window->setProperty("fullScreenMode", false);
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly()) << "full screen ends: read only with it";
}

namespace {
/// Recording (qt/docs/audio.md) with the fake microphone, or in a build without any audio backend
class ToolboxAudioTest: public ToolboxTest {
protected:
    virtual bool withAudio() const { return true; }
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        if (withAudio()) {
            xqt::audio::useFakeDevices(true);
        } else {
            xqt::audio::useNoDevices(true);
        }
        xqt::audio::fake::reset();
        xqt::audio::setAppFolder(std::filesystem::path(tmp.filePath("audio").toStdString()));
        ToolboxTest::SetUp();
    }
    void TearDown() override {
        ToolboxTest::TearDown();
        xqt::audio::setAppFolder({});
        xqt::audio::fake::reset();
        xqt::audio::useFakeDevices(false);
    }
    QObject* audio() const { return controller->property("audio").value<QObject*>(); }
    QTemporaryDir tmp;
};
class ToolboxNoAudioTest: public ToolboxAudioTest {
protected:
    bool withAudio() const override { return false; }
};
}  // namespace

// The record button left the rail (qt/rail-scroll; qt/top-bar puts it in the top bar's first layout): it is in the
// command bar, one place, not on the rail too; the recording pill stays in sight, clear of the toolbox, docked and
// floating (stopped there with its own stop), and a phone's sheet has it under Insert
TEST_F(ToolboxAudioTest, recordingIsInTheCommandBarAndItsPillStaysInSight) {
    auto* box = find("toolbox");
    auto* record = find("recordButton");
    ASSERT_NE(record, nullptr);
    until([&] { return shown(record); });
    ASSERT_TRUE(shown(record));
    EXPECT_FALSE(inside(record, box)) << "not on the rail";
    EXPECT_TRUE(inside(record, find("toolArea"))) << "in the command bar";

    click(record);
    until([&] { return audio()->property("recording").toBool(); });
    ASSERT_TRUE(audio()->property("recording").toBool());
    EXPECT_TRUE(record->property("checked").toBool());
    auto* pill = find("recordingPill");
    until([&] { return shown(pill); });
    EXPECT_TRUE(shown(pill));
    EXPECT_FALSE(rectOf(pill).intersects(rectOf(box))) << "clear of the rail";

    // Full screen, the toolbox at the top: it floats in the middle of the top edge, the pill below it
    window->setProperty("fullScreenMode", true);
    // (the window takes the screen's size, and with it another size class: the edge is chosen for that one)
    until([&] { return box->property("floating").toBool() && window->width() != 1920; });
    ASSERT_TRUE(box->property("floating").toBool());
    wait(300);
    QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, "top"));
    until([&] { return win("toolboxEdge").toString() == "top" && !box->property("vertical").toBool(); });
    ASSERT_EQ(win("toolboxEdge").toString(), "top");
    wait(200);
    EXPECT_TRUE(shown(pill));
    EXPECT_FALSE(rectOf(pill).intersects(rectOf(box))) << "below the floating toolbox";
    EXPECT_GE(rectOf(pill).top(), rectOf(box).bottom());
    click(find("recordingStop"));  // (there: the pill's own stop)
    until([&] { return !audio()->property("recording").toBool(); });
    EXPECT_FALSE(audio()->property("recording").toBool());
    window->setProperty("fullScreenMode", false);
    until([&] { return !box->property("floating").toBool(); });

    // A phone: in "My tools", under Insert
    resize(412, 915);
    until([&] { return box->property("compact").toBool(); });
    click(find("toolboxAllButton"));
    auto* sheet = find<QObject>("phoneToolSheet");
    until([&] { return sheet->property("visible").toBool(); });
    ASSERT_TRUE(sheet->property("visible").toBool());
    auto* cell = find("toolCell_record");
    ASSERT_NE(cell, nullptr);
    EXPECT_TRUE(inside(cell, find("phoneToolSection_insert")));
}

// Without an audio backend nothing offers recording: not the rail, not the command bar, not the phone's sheet
TEST_F(ToolboxNoAudioTest, withoutAudioNothingOffersRecording) {
    ASSERT_FALSE(audio()->property("available").toBool());
    auto* record = find("recordButton");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(shown(record));
    EXPECT_FALSE(inside(record, find("toolbox")));
    EXPECT_FALSE(inside(record, find("toolArea")) && shown(record)) << "not in the command bar";
    EXPECT_TRUE(shown(find("handButton")) && inside(find("handButton"), find("toolbox")));
    EXPECT_TRUE(shown(find("touchDrawingButton")) && inside(find("touchDrawingButton"), find("toolArea")));
    resize(412, 915);
    until([&] { return find("toolbox")->property("compact").toBool(); });
    click(find("toolboxAllButton"));
    auto* sheet = find<QObject>("phoneToolSheet");
    until([&] { return sheet->property("visible").toBool(); });
    ASSERT_TRUE(sheet->property("visible").toBool());
    EXPECT_NE(find("toolCell_image"), nullptr);
    EXPECT_EQ(find("toolCell_record"), nullptr);
}

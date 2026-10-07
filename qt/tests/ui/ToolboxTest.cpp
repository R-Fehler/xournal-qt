/*
 * xournal-qt: the toolbox in the real window (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of
 * the canvas (right by default), undo and redo at its head, the app's tools (hand, select, snip, mark PDF text) lent to
 * it as items of the same arrangement; a tap picks a tool up, the rail scrolls when it is short (the same order on every
 * screen), the edge is chosen per window size.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <functional>
#include <cmath>
#include <type_traits>
#include <map>
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
#include "shell/DocumentCovers.h"
#include "shell/RecentFiles.h"
#include "shell/SettingsModel.h"
#include "shell/TabManager.h"
#include "shell/Thumbnails.h"
#include "shell/ToolboxModel.h"

#include "AppController.h"
#include "UiFixture.h"

namespace {
class ToolboxTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        makeController();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        settings()->resetLayoutChoices();
        controller->toolboxModel()->reset();
        controller->setColorPalette("classic");
        ASSERT_NO_FATAL_FAILURE(loadWindow({.size = QSize(1920, 1080)}));
        until([&] { return window->width() == 1920 && window->height() == 1080; });
        wait(300);  // (the size class, then the plans settle)
        controller->newDocument();
        wait(200);
    }
    void TearDown() override {
        settings()->resetLayoutChoices();
        closeApp();
    }

    xqt::SettingsModel* settings() const { return qobject_cast<xqt::SettingsModel*>(controller->settingsModel()); }
    xqt::ToolboxModel* tools() const { return controller->toolboxModel(); }
    void resize(int w, int h) {
        window->resize(w, h);
        until([&] { return window->width() == w && window->height() == h; });
        wait(300);  // (the size class, then the plans settle)
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
    /// ... of the rail, or of the top bar (`top`)
    RailState rail(bool top = false) const {
        RailState s;
        auto* box = find(top ? "topBar" : "toolbox");
        auto* middle = find(top ? "topBarMiddle" : "toolboxMiddle");
        auto* grid = find(top ? "topBarTools" : "toolboxTools");
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
            if (in.isEmpty() || !shown(k) || k->objectName().endsWith("AddInline")) {
                continue;  // (the phone's "+" at the end of the dock's items)
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
                         .arg(win("adaptive").value<QObject*>()->property("layoutClass").toString(), box->property("compact").toBool() ? " (dock)" : "")
                         .arg(s.cells)
                         .arg(s.inSight)
                         .arg(s.room)
                         .arg(s.view)
                         .arg(s.content)
                         .arg(s.pos)
                         .arg(s.scrolls ? ", scrolls" : "");
        return s;
    }
    /// The window's button of an app item (toolArea.slots)
    QObject* slot(const QString& name) const {
        const QVariantMap slots = find<QObject>("toolArea")->property("slots").toMap();
        return slots.value(name).value<QObject*>();
    }
    /// The rail's items (or the top bar's) as the arrangement has them, by the names of their buttons; the app items
    /// not offered here are left out, and the dividers that would then lead, end or follow another
    QStringList arranged(bool top = false) const {
        QStringList out;
        const QString app = top ? "topApp_" : "railApp_", group = top ? "topGroup_" : "railGroup_";
        for (const QVariant& v: top ? tools()->topItems() : tools()->entries()) {
            const QVariantMap m = v.toMap();
            if (m.contains("app")) {
                QObject* b = slot(m.value("app").toString());
                if (!b || !b->property("offered").value<bool>() && b->property("offered").isValid()) {
                    continue;
                }
            }
            const QString n = m.value("divider").toBool() ? QString("|")
                              : m.contains("app")         ? app + m.value("app").toString()
                              : m.value("group").toBool() ? group + m.value("id").toString()
                                                          : "toolEntry_" + m.value("id").toString();
            if (n == "|" && (out.isEmpty() || out.last() == "|")) {
                continue;
            }
            out << n;
        }
        while (!out.isEmpty() && out.last() == "|") {
            out.removeLast();
        }
        return out;
    }
    /// Wholly in the view of the rail's middle (or the top bar's), along it (the tool in hand is lifted a little across
    /// it)
    bool inSight(QQuickItem* item) const {
        if (!shown(item)) {
            return false;
        }
        const bool top = inside(item, find("topBar"));
        const bool vertical = find(top ? "topBar" : "toolbox")->property("vertical").toBool();
        const QRectF sight = rectOf(find(top ? "topBarMiddle" : "toolboxMiddle"));
        // (the cell that holds it: not lifted, not scaled)
        QQuickItem* cell = item;
        const QString grid = top ? "topBarTools" : "toolboxTools";
        while (cell->parentItem() && cell->parentItem()->objectName() != grid) {
            cell = cell->parentItem();
        }
        const QRectF r = rectOf(cell);
        return vertical ? r.top() >= sight.top() - 0.5 && r.bottom() <= sight.bottom() + 0.5
                        : r.left() >= sight.left() - 0.5 && r.right() <= sight.right() + 0.5;
    }
    void scrollRail(double pos, bool top = false) {
        QMetaObject::invokeMethod(find(top ? "topBar" : "toolbox"), "scrollTo", Q_ARG(QVariant, pos));
        wait(50);
    }
    /// Carries what is under `from` (held until it lifts) along `path` to `to`, waiting `dwellMs` at the path's end
    void carry(const QPoint& from, const std::vector<QPoint>& path, int dwellMs, const QPoint& to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        wait(550);
        QPoint at = from;
        for (const QPoint& p: path) {
            for (int k = 1; k <= 6; ++k) {
                QTest::mouseMove(window, at + (p - at) * k / 6);
                wait(15);
            }
            at = p;
        }
        wait(dwellMs);
        for (int k = 1; k <= 4; ++k) {
            QTest::mouseMove(window, at + (to - at) * k / 4);
            wait(15);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(150);
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
    // Write on the page, the setsquare and the finger switch left the rail: the top bar has them (its first layout)
    for (const char* name: {"textModeButton", "geometryButton", "touchDrawingButton"}) {
        auto* b = find(name);
        ASSERT_TRUE(shown(b)) << name;
        EXPECT_FALSE(inside(b, box)) << name;
        EXPECT_TRUE(inside(b, find("topBar"))) << name;
    }
    EXPECT_TRUE(shown(find("toolboxAddButton")));
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
    until([&] {  // (once the dock is laid out for its size)
        scrollRail(100);
        return rail().pos > 50;
    });
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

// The tool in hand is in the rail's view once a window is made a phone's (the rail moved to the dock at the bottom),
// whatever place the dock remembers: the first pen at the start of the rail, the laser far along it. Its lift turns
// towards the page at once: it never slides along the rail (the first cell went 5 px out of the dock's view at its
// start, where no scrolling reaches, for as long as the turn was animated: on a slow phone at the start, long enough
// for PhoneChromeTest to find it there under load, 2026-10-06)
TEST_F(ToolboxTest, theToolInHandIsInTheDocksViewAfterTheWindowBecomesAPhones) {
    touchProfile("on");
    auto* box = find("toolbox");
    const auto dockSettled = [&] {
        return box->property("compact").toBool() && !box->property("vertical").toBool() && rail().cells > 0;
    };
    /// The button itself (lifted, not only its cell) wholly in the view of the rail's middle
    const auto inView = [&](QQuickItem* button) {
        return shown(button) && inSight(button) &&
               rectOf(find("toolboxMiddle")).adjusted(-1, -1, 1, 1).contains(rectOf(button));
    };
    struct Case {
        QString inHand;
        double remembered;  // (where the dock was left: its start, its end)
    };
    for (const Case& c: {Case{nth("pen"), 1e6}, Case{nth("laser"), 0}, Case{nth("pen"), 0}}) {
        // The dock left at a place, with no tool of the rail in hand (that one would be scrolled into view)
        controller->selectTool("text");
        resize(412, 915);
        until(dockSettled);
        until([&] {
            scrollRail(c.remembered);
            return std::abs(rail().pos - (c.remembered > 0 ? rail().content - rail().view : 0)) < 1;
        });
        wait(800);  // (written after a pause)
        // On the desktop, the tool taken; then the window is a phone's again
        resize(1920, 1080);
        controller->applyToolEntry(c.inHand);
        until([&] { return entry(c.inHand) && entry(c.inHand)->property("inHand").toBool(); });
        wait(200);  // (lifted towards the page, at the left)
        auto* button = entry(c.inHand);
        ASSERT_NE(button, nullptr);
        const std::string at = c.inHand.toStdString() + ", the dock left at " + std::to_string(c.remembered);
        window->resize(412, 915);
        // Every few ms while the dock lays itself out: lifted across the rail only, never along it
        double along = 0;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 600) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
            if (dockSettled()) {
                const QRectF cell = rectOf(button->parentItem());
                along = std::max(along, std::abs(rectOf(button).left() - cell.left()));
            }
        }
        EXPECT_LT(along, 0.5) << at << ": the lift slid along the dock by " << along;
        until([&] { return dockSettled() && inView(button); });
        EXPECT_TRUE(inView(button)) << at << ": " << rail().text.toStdString();
    }
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

    // Let go away from both bars: it leaves them (qt/top-bar: one home per item, the rail, the top bar or none);
    // "Removed · Undo" brings it back
    const int before = tools()->indexOf(pen);
    const QPoint at = rectOf(entry(pen)).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at);
    wait(550);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, at + QPoint(-40 * k, 20 * k));
        wait(15);
    }
    EXPECT_FALSE(shown(find("toolDropMark")));
    EXPECT_FALSE(shown(find("topDropMark")));
    EXPECT_TRUE(shown(find("toolDragAway"))) << "marked: it leaves the bars";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at + QPoint(-320, 160));
    until([&] { return tools()->indexOf(pen) < 0; });
    EXPECT_EQ(tools()->indexOf(pen), -1) << "removed";
    EXPECT_TRUE(find("snackbarText")->property("text").toString().startsWith("Removed"));
    QMetaObject::invokeMethod(find("snackbarAction"), "clicked");
    until([&] { return tools()->indexOf(pen) == before; });
    EXPECT_EQ(tools()->indexOf(pen), before) << "undone";
    until([&] { return shown(entry(pen)); });
    wait(150);  // (the rail laid out anew)

    // Held and let go without moving: its menu
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rectOf(entry(pen)).center().toPoint());
    wait(550);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, rectOf(entry(pen)).center().toPoint());
    auto* menu = find<QObject>("toolEntryMenu");
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_TRUE(menu->property("visible").toBool());
    QMetaObject::invokeMethod(menu, "close");
}

// Groups the user makes (the author, 2026-10-06: "Maybe we can let the user group tools into cycle groups themselves
// if they like? By dragging a tool and holding long over another tool?"): a tool carried onto another and held there
// until it shows a ring; let go: a group, which the snackbar can undo. Moving on before the ring reorders as before.
TEST_F(ToolboxTest, aToolHeldOverAnotherUntilTheRingMakesAGroup) {
    const QString pen1 = nth("pen"), pen3 = nth("pen", 2), hl = nth("highlighter");
    auto carry = [&](const QPoint& from, const std::vector<QPoint>& path, int dwellMs, const QPoint& to) {
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        wait(550);
        QPoint at = from;
        for (const QPoint& p: path) {
            for (int k = 1; k <= 6; ++k) {
                QTest::mouseMove(window, at + (p - at) * k / 6);
                wait(15);
            }
            at = p;
        }
        wait(dwellMs);
        for (int k = 1; k <= 4; ++k) {
            QTest::mouseMove(window, at + (to - at) * k / 4);
            wait(15);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(150);
    };
    // Over the first pen for a moment, then on to the place before the third pen: a reorder, no group
    const QRectF third = rectOf(entry(pen3));
    const QPoint beforeThird(int(third.center().x()), int(third.top()) + 3);
    carry(rectOf(entry(hl)).center().toPoint(), {rectOf(entry(pen1)).center().toPoint()}, 150, beforeThird);
    EXPECT_EQ(tools()->groupOf(hl), "") << "moved on before the ring";
    EXPECT_EQ(tools()->indexOf(hl) + 1, tools()->indexOf(pen3)) << "reordered as before";

    // Held over the first pen until the ring shows: let go, a group in the pen's place
    const QStringList before = arranged();
    const QPoint overPen = rectOf(entry(pen1)).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, rectOf(entry(hl)).center().toPoint());
    wait(550);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, rectOf(entry(hl)).center().toPoint() + (overPen - rectOf(entry(hl)).center().toPoint()) * k / 8);
        wait(15);
    }
    until([&] { return entry(pen1)->property("ringed").toBool(); }, 2000);
    EXPECT_TRUE(entry(pen1)->property("ringed").toBool()) << "the ring after about 0.6 s";
    EXPECT_FALSE(shown(find("toolDropMark"))) << "no place marked: a group";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, overPen);
    until([&] { return !tools()->groupOf(hl).isEmpty(); });
    const QString g = tools()->groupOf(hl);
    ASSERT_FALSE(g.isEmpty());
    EXPECT_EQ(tools()->groupOf(pen1), g);
    EXPECT_EQ(tools()->indexOf(g), 0) << "in the pen's place";
    auto* face = find("railGroup_" + g);
    until([&] { return shown(face = find("railGroup_" + g)); });
    ASSERT_TRUE(shown(face));
    EXPECT_EQ(face->property("stackCount").toInt(), 2) << "dots for its two tools";
    EXPECT_FALSE(shown(entry(pen1))) << "the pen is in the group";
    // "Grouped · Undo"
    auto* snackbar = find("snackbar");
    EXPECT_TRUE(shown(snackbar));
    EXPECT_EQ(find("snackbarText")->property("text").toString(), "Grouped");
    auto* undo = find("snackbarAction");
    ASSERT_TRUE(shown(undo));
    QMetaObject::invokeMethod(undo, "clicked");
    until([&] { return tools()->groupOf(hl).isEmpty(); });
    EXPECT_EQ(tools()->groupOf(hl), "") << "undone";
    until([&] { return rail().order == before; });
    EXPECT_EQ(rail().order, before) << "as it was";
}

// A group shows the tool used last; a tap takes it; a tap while one of its tools is in hand takes the next with two or
// three tools, and opens its list with more than three (the author's rule). The list: pick one, carry one out (it
// leaves the group); its menu: Ungroup. App tools group too, and keep their own cycle outside a group.
TEST_F(ToolboxTest, aGroupCyclesWithThreeAndListsWithFour) {
    const QString pen1 = nth("pen"), pen2 = nth("pen", 1), pen3 = nth("pen", 2), hl = nth("highlighter");
    controller->applyToolEntry(nth("eraser"));
    QString g = tools()->group(pen2, pen1);
    tools()->group(pen3, pen1);
    ASSERT_EQ(tools()->members(g).size(), 3);
    wait(200);  // (the rail made anew)
    auto* face = find("railGroup_" + g);
    until([&] { return shown(face = find("railGroup_" + g)); });
    ASSERT_TRUE(shown(face));
    // A tap: the one shown (the one carried in last); again: the next, round the group
    click(face);
    until([&] { return tools()->active() == pen3; });
    EXPECT_EQ(tools()->active(), pen3);
    EXPECT_TRUE(face->property("inHand").toBool());
    click(face);
    until([&] { return tools()->active() == pen1; });
    EXPECT_EQ(tools()->active(), pen1) << "the next, from the start again";
    EXPECT_EQ(tools()->shownOf(g), pen1) << "the group shows it";
    click(face);
    until([&] { return tools()->active() == pen2; });
    EXPECT_EQ(controller->color(), QColor("#D96B00"));
    EXPECT_FALSE(find<QObject>("toolGroupFlyout")->property("visible").toBool()) << "three: no list";

    // A fourth: a tap while one of them is in hand opens the list
    tools()->group(hl, g);
    wait(200);
    until([&] { return find("railGroup_" + g) && find("railGroup_" + g)->property("stackCount").toInt() == 4; });
    face = find("railGroup_" + g);
    EXPECT_EQ(tools()->shownOf(g), hl);
    ASSERT_EQ(tools()->active(), pen2) << "one of its tools in hand";
    click(face);
    auto* flyout = find<QObject>("toolGroupFlyout");
    until([&] { return flyout->property("visible").toBool(); });
    ASSERT_TRUE(flyout->property("visible").toBool()) << "four: its list";
    EXPECT_EQ(tools()->active(), pen2) << "nothing taken";
    QQuickItem* inList = nullptr;
    until([&] { return shown(inList = entry(pen3)); });
    ASSERT_TRUE(shown(inList));
    click(inList);
    until([&] { return tools()->active() == pen3; });
    EXPECT_EQ(controller->color(), QColor("#D6342C")) << "picked from the list";
    until([&] { return !flyout->property("visible").toBool(); });

    // Carried out of the list onto the rail: it leaves the group
    click(face);
    until([&] { return flyout->property("visible").toBool(); });
    until([&] { return shown(inList = entry(pen2)); });
    const QPoint from = rectOf(inList).center().toPoint();
    const QRectF eraser = rectOf(find("toolEntry_" + nth("eraser")));
    const QPoint to(int(eraser.center().x()), int(eraser.top()) + 3);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    wait(550);
    for (int k = 1; k <= 10; ++k) {
        QTest::mouseMove(window, from + (to - from) * k / 10);
        wait(15);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
    until([&] { return tools()->groupOf(pen2).isEmpty(); });
    EXPECT_EQ(tools()->groupOf(pen2), "") << "carried out of the group";
    EXPECT_EQ(tools()->members(g).size(), 3);
    EXPECT_EQ(tools()->indexOf(pen2) + 1, tools()->indexOf(nth("eraser")));

    // Its menu: Ungroup
    wait(200);
    auto* menu = find<QObject>("toolEntryMenu");
    QMetaObject::invokeMethod(find("railGroup_" + g), "pressAndHold");
    until([&] { return menu->property("visible").toBool(); });
    EXPECT_FALSE(entryOf(menu, "toolEditItem")->property("offered").toBool()) << "a group has no editor";
    trigger(menu, "toolUngroupItem");
    EXPECT_EQ(tools()->kindOf(g), "");
    until([&] { return shown(entry(pen1)); });
    EXPECT_TRUE(shown(entry(pen1)));
    EXPECT_TRUE(shown(entry(hl)));

    // App tools: select and snip in one group; a tap cycles from one to the other
    const QString sg = tools()->group(tools()->idOfApp("snip"), tools()->idOfApp("select"));
    wait(200);
    auto* appFace = find("railGroup_" + sg);
    until([&] { return shown(appFace = find("railGroup_" + sg)); });
    ASSERT_TRUE(shown(appFace));
    EXPECT_FALSE(shown(find("handButton")) && inside(find("selectButton"), find("toolArea"))) << "still lent to the rail";
    click(appFace);
    until([&] { return !controller->snipShape().isEmpty(); });
    EXPECT_FALSE(controller->snipShape().isEmpty()) << "the snip, shown last";
    click(appFace);
    until([&] { return controller->tool() == "selectRect" || controller->tool() == "selectRegion"; });
    EXPECT_TRUE(controller->snipShape().isEmpty()) << "then select";
    EXPECT_EQ(tools()->shownOf(sg), tools()->idOfApp("select"));
    // Outside a group select keeps its own cycle (rectangle, lasso): see dockedAtTheRight…
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
    // ⋯ → "Hide the tools": presenting in Zen; the dot's pill shows them again
    click(more);
    trigger(menu, "toolboxPresentCleanItem");
    until([&] { return !shown(box); });
    EXPECT_FALSE(shown(box));
    click(find("zenDot"));
    until([&] { return shown(find("zenPill")); });
    click(find("zenShowControls"));
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

TEST_F(ToolboxTest, onAPhoneTheDockIsTheSameRailWithPlusAtItsEnd) {
    resize(412, 915);
    auto* box = find("toolbox");
    until([&] { return box->property("compact").toBool(); });
    ASSERT_TRUE(shown(box));
    EXPECT_TRUE(inside(box, find("phoneDock")));
    EXPECT_FALSE(shown(find("dockToolButton"))) << "the dock's own cells give way";
    EXPECT_FALSE(shown(find("dockColorSlot")));
    EXPECT_TRUE(shown(find("toolboxUndoButton")));
    EXPECT_TRUE(shown(find("toolboxPageButton")));
    EXPECT_FALSE(shown(find("toolboxAddButton"))) << "not pinned: its room goes to the tools";
    const QRectF dock = rectOf(box);
    EXPECT_GT(dock.top(), 800) << "at the bottom";
    EXPECT_LE(dock.right(), 412.5);
    // The same items as the rail, scrolling sideways (the app tools too), "+" after them
    expectRail("the dock");
    EXPECT_TRUE(rail().scrolls);
    EXPECT_FALSE(box->property("vertical").toBool());
    EXPECT_TRUE(inside(find("handButton"), box));
    auto* plus = find("toolboxAddInline");
    ASSERT_TRUE(shown(plus));
    EXPECT_GE(rectOf(plus).left(), rectOf(find("railApp_pdfText")).right() - 0.5) << "after the items";
    // The tool in hand is scrolled into the dock
    const QString laser = nth("laser");
    controller->applyToolEntry(laser);
    until([&] { return inSight(entry(laser)); });
    EXPECT_TRUE(inSight(entry(laser))) << "the tool in hand in sight";
    EXPECT_TRUE(dock.contains(rectOf(entry(laser)).center()));
    // "+": the catalog, a sheet
    scrollRail(1e6);
    click(plus);
    auto* sheet = find<QObject>("menuSheet");
    until([&] { return sheet->property("visible").toBool(); });
    ASSERT_TRUE(sheet->property("visible").toBool()) << "the catalog is a sheet on a phone";
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !sheet->property("visible").toBool(); });

    // Held sideways: a rail at the right; the page number in the app bar (the rail's room goes to the tools)
    resize(915, 412);
    until([&] { return box->property("vertical").toBool(); });
    EXPECT_TRUE(box->property("vertical").toBool());
    EXPECT_NEAR(rectOf(box).right(), 915, 1);
    EXPECT_FALSE(shown(find("toolboxPageButton")));
    EXPECT_TRUE(shown(find("phonePageButton")));
    EXPECT_TRUE(inside(find("phonePageButton"), find("phoneAppBar")));
}

// --- the top bar (qt/top-bar; qt/docs/toolbox.md, "The top bar") ------------------------------------------------------

// The top bar shows the other list of the arrangement in the user's order, with its dividers and groups (the items not
// offered here skipped, not removed); it scrolls as the rail does: half of the next cell, a fade, the wheel; "+" and ⋮
// are pinned at its end
TEST_F(ToolboxTest, theTopBarShowsTheStoredArrangementAndScrolls) {
    auto* bar = find("topBar");
    ASSERT_TRUE(shown(bar));
    EXPECT_TRUE(inside(bar, find("topTools")));
    until([&] { return rail(true).cells > 0; });
    RailState s = rail(true);
    EXPECT_EQ(s.order, arranged(true)) << "the stored order";
    EXPECT_FALSE(s.scrolls) << s.text.toStdString();
    // The first layout: open, save, (milestone), share, print | image, stickers, add page, write | …
    const QStringList first = arranged(true).mid(0, 6);
    EXPECT_EQ(first, (QStringList{"topApp_open", "topApp_save", "topApp_share", "topApp_print", "|", "topApp_image"}))
            << "a new document keeps no versions: the milestone is skipped";
    EXPECT_FALSE(tools()->idOfApp("milestone").isEmpty()) << "skipped, not removed";
    for (const char* name: {"openButton", "zenButton", "settingsButton", "textModeButton"}) {
        EXPECT_TRUE(inside(find(name), bar)) << name;
    }
    // "+" and ⋮ pinned at its end, ⋮ last
    auto* plus = find("topBarAddButton");
    auto* more = find("moreButton");
    ASSERT_TRUE(shown(plus));
    ASSERT_TRUE(shown(more));
    EXPECT_TRUE(inside(more, bar));
    EXPECT_GT(rectOf(more).left(), rectOf(plus).left());
    EXPECT_NEAR(rectOf(more).right(), rectOf(bar).right(), 8);

    // The order is the store's: an item moved there moves on the bar
    tools()->moveTo(tools()->idOfApp("settings"), "top", 0);
    until([&] { return rail(true).order.value(0) == "topApp_settings"; });
    EXPECT_EQ(rail(true).order, arranged(true));
    tools()->resetLayout();

    // Narrower: it scrolls, through the middle of a cell, a fade at the end that has more; ⋮ stays in sight
    resize(1000, 800);
    until([&] { return rail(true).scrolls; });
    s = rail(true);
    SCOPED_TRACE(s.text.toStdString());
    const double cell = bar->property("cell").toDouble();
    EXPECT_TRUE(s.scrolls);
    EXPECT_NEAR(s.cutShown, cell / 2, 1) << "half of the next cell shows";
    EXPECT_TRUE(shown(find("topBarFadeEnd")));
    EXPECT_FALSE(shown(find("topBarFadeStart")));
    EXPECT_TRUE(rectOf(bar).contains(rectOf(more))) << "⋮ pinned";
    // The wheel scrolls it; a drag at once scrolls it and runs nothing
    const QPoint over = rectOf(find("topApp_save")).center().toPoint();
    QWheelEvent down(over, window->mapToGlobal(over), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                     Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &down);
    until([&] { return rail(true).pos > 10; });
    EXPECT_GT(rail(true).pos, 10) << "the wheel scrolls it";
    EXPECT_TRUE(shown(find("topBarFadeStart")));
    scrollRail(0, true);
    const QPoint from = rectOf(find("topApp_print")).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int k = 1; k <= 10; ++k) {
        QTest::mouseMove(window, from - QPoint(20 * k, 0));
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from - QPoint(200, 0));
    until([&] { return rail(true).pos > 20; });
    EXPECT_GT(rail(true).pos, 20) << "a drag scrolls";
    EXPECT_FALSE(find<QObject>("printDialog")->property("visible").toBool()) << "and runs nothing";
    // A tap runs it
    scrollRail(1e6, true);
    click(find("topApp_zen"));
    until([&] { return win("zen").toBool(); });
    EXPECT_TRUE(win("zen").toBool()) << "Zen from the top bar";
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !win("zen").toBool(); });
}

// One home per item, carried between the bars: held until it lifts, carried onto the other bar (it shows where it
// goes), within the top bar, onto an item there (the ring: a group), away from both (into the catalog)
TEST_F(ToolboxTest, itemsAreCarriedBetweenTheRailAndTheTopBar) {
    auto* bar = find("topBar");
    until([&] { return rail(true).cells > 0; });
    // A pen from the rail onto the top bar, before "search"
    const QString pen = nth("pen", 1);
    const QRectF search = rectOf(find("topApp_search"));
    const QPoint beforeSearch(int(search.left()) + 4, int(search.center().y()));
    const QPoint start = rectOf(entry(pen)).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    wait(550);
    for (int k = 1; k <= 12; ++k) {
        QTest::mouseMove(window, start + (beforeSearch - start) * k / 12);
        wait(15);
    }
    EXPECT_TRUE(shown(find("topDropMark"))) << "the top bar shows where it goes";
    EXPECT_FALSE(shown(find("toolDropMark"))) << "the rail does not";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, beforeSearch);
    until([&] { return tools()->barOf(pen) == "top"; });
    ASSERT_EQ(tools()->barOf(pen), "top");
    EXPECT_EQ(tools()->indexOf(pen) + 1, tools()->indexOf(tools()->idOfApp("search"))) << "before search";
    until([&] { return inside(entry(pen), bar); });
    EXPECT_TRUE(inside(entry(pen), bar)) << "drawn on the top bar";
    EXPECT_FALSE(inside(entry(pen), find("toolbox")));
    // A tool on the top bar is a tool: a tap takes it
    click(entry(pen));
    until([&] { return tools()->active() == pen; });
    EXPECT_EQ(controller->tool(), "pen");

    // "search" from the top bar onto the rail, after the first pen
    const QString searchId = tools()->idOfApp("search");
    const QRectF firstPen = rectOf(entry(nth("pen")));
    const QPoint afterFirst(int(firstPen.center().x()), int(firstPen.bottom()) - 3);
    carry(rectOf(find("topApp_search")).center().toPoint(), {afterFirst + QPoint(-20, 0)}, 0, afterFirst);
    until([&] { return tools()->barOf(searchId) == "rail"; });
    ASSERT_EQ(tools()->barOf(searchId), "rail");
    EXPECT_EQ(tools()->indexOf(searchId), tools()->indexOf(nth("pen")) + 1);
    until([&] { return shown(find("railApp_search")); });
    EXPECT_TRUE(inside(find("searchButton"), find("toolbox"))) << "its button is lent to the rail now";
    click(find("railApp_search"));
    until([&] { return find("searchBar")->isVisible(); });
    EXPECT_TRUE(find("searchBar")->isVisible()) << "and does what it did";
    QTest::keyClick(window, Qt::Key_Escape);

    // Within the top bar: "print" before "open"
    const QString print = tools()->idOfApp("print"), open = tools()->idOfApp("open");
    const QRectF openRect = rectOf(find("topApp_open"));
    carry(rectOf(find("topApp_print")).center().toPoint(), {}, 0,
          QPoint(int(openRect.left()) + 4, int(openRect.center().y())));
    until([&] { return tools()->indexOf(print) < tools()->indexOf(open); });
    EXPECT_EQ(tools()->indexOf(print) + 1, tools()->indexOf(open));
    EXPECT_EQ(rail(true).order, arranged(true));

    // Onto an item until the ring shows: a group ("Grouped · Undo")
    const QString share = tools()->idOfApp("share"), save = tools()->idOfApp("save");
    const QPoint overSave = rectOf(find("topApp_save")).center().toPoint();
    const QPoint fromShare = rectOf(find("topApp_share")).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, fromShare);
    wait(550);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, fromShare + (overSave - fromShare) * k / 8);
        wait(15);
    }
    auto ringed = [&] {
        QQuickItem* r = under(find("topApp_save"), "toolRing");
        return r && r->isVisible();
    };
    until(ringed, 2000);
    EXPECT_TRUE(ringed()) << "the ring after about 0.6 s";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, overSave);
    until([&] { return !tools()->groupOf(share).isEmpty(); });
    const QString g = tools()->groupOf(share);
    ASSERT_FALSE(g.isEmpty());
    EXPECT_EQ(tools()->groupOf(save), g);
    EXPECT_EQ(tools()->barOf(g), "top");
    EXPECT_EQ(find("snackbarText")->property("text").toString(), "Grouped");
    until([&] { return shown(find("topGroup_" + g)); });
    EXPECT_TRUE(shown(find("topGroup_" + g)));

    // Away from both bars: the app item goes into the catalog ("… is in + now · Undo")
    const QString tags = tools()->idOfApp("tags");
    const QPoint t = rectOf(find("topApp_tags")).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, t);
    wait(550);
    for (int k = 1; k <= 8; ++k) {
        QTest::mouseMove(window, t + QPoint(0, 50 * k));
        wait(15);
    }
    EXPECT_TRUE(shown(find("topDragAway"))) << "marked: it leaves the bars";
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, t + QPoint(0, 400));
    until([&] { return tools()->idOfApp("tags").isEmpty(); });
    EXPECT_TRUE(tools()->unplaced().contains("tags")) << "in the catalog";
    EXPECT_FALSE(inside(find("tagsButton"), bar));
    EXPECT_TRUE(find<QObject>("documentTagsMenuItem")->property("offered").toBool()) << "⋮ still has it";
    QMetaObject::invokeMethod(find("snackbarAction"), "clicked");
    until([&] { return !tools()->idOfApp("tags").isEmpty(); });
    EXPECT_EQ(tools()->idOfApp("tags"), tags) << "undone";
    // The menu of an item: to the other bar
    auto* menu = find<QObject>("toolEntryMenu");
    until([&] { return shown(find("topApp_tags")); });
    QMetaObject::invokeMethod(menu, "openFor", Q_ARG(QVariant, tools()->entry(tags)),
                              Q_ARG(QVariant, QVariant::fromValue<QObject*>(find("topApp_tags"))), Q_ARG(QVariant, QVariant()));
    trigger(menu, "toolOtherBarItem");
    EXPECT_EQ(tools()->barOf(tags), "rail");
    EXPECT_EQ(tools()->indexOf(tags), tools()->entries().size() - 1) << "at the rail's end";
}

// A group of commands opens its list on a tap: nothing runs by accident; a group of tools on the top bar cycles as on
// the rail
TEST_F(ToolboxTest, aGroupOfCommandsOpensItsListOnATap) {
    const QString open = tools()->idOfApp("open"), save = tools()->idOfApp("save");
    const QString g = tools()->group(save, open);
    ASSERT_FALSE(g.isEmpty());
    auto* face = find("topGroup_" + g);
    until([&] { return shown(face = find("topGroup_" + g)); });
    ASSERT_TRUE(shown(face));
    wait(150);  // (the bar laid out anew)
    click(face);
    auto* list = find<QObject>("topGroupFlyout");
    until([&] { return list->property("visible").toBool(); });
    EXPECT_TRUE(list->property("visible").toBool()) << "its list";
    EXPECT_FALSE(find<QObject>("openDialog")->property("visible").toBool()) << "nothing ran";
    QMetaObject::invokeMethod(list, "close");
    until([&] { return !list->property("visible").toBool(); });
    // Zen in a group: a tap lists, a tap in the list runs it
    const QString zen = tools()->idOfApp("zen"), full = tools()->idOfApp("fullScreen");
    const QString g2 = tools()->group(zen, full);
    until([&] { return shown(find("topGroup_" + g2)); });
    wait(150);
    scrollRail(1e6, true);
    click(find("topGroup_" + g2));
    until([&] { return list->property("visible").toBool(); });
    EXPECT_FALSE(win("zen").toBool()) << "nothing ran";
    auto* zenCell = find("topApp_zen");
    until([&] { return shown(zenCell = find("topApp_zen")); });
    click(zenCell);
    until([&] { return win("zen").toBool(); });
    EXPECT_TRUE(win("zen").toBool()) << "picked in the list";
    wait(300);  // (the list closes: it would take Esc)
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !win("zen").toBool(); });

    // Tools on the top bar: hand and select (moved there), grouped: a tap takes one, again the other
    const QString hand = tools()->idOfApp("hand"), select = tools()->idOfApp("select");
    tools()->moveTo(hand, "top", 0);
    const QString g3 = tools()->group(select, hand);
    scrollRail(0, true);
    auto* tools3 = find("topGroup_" + g3);
    until([&] { return shown(tools3 = find("topGroup_" + g3)); });
    wait(150);
    click(tools3);
    until([&] { return controller->tool() == "selectRect" || controller->tool() == "selectRegion"; });
    EXPECT_TRUE(controller->tool().startsWith("select")) << "the one shown (carried in last)";
    click(tools3);
    until([&] { return controller->tool() == "hand"; });
    EXPECT_EQ(controller->tool(), "hand") << "two tools: the next";
    EXPECT_FALSE(list->property("visible").toBool());
}

// "+" (the catalog): a new tool of a kind, or an app item on neither bar, put at the end of the bar it was opened from
TEST_F(ToolboxTest, theCatalogAddsToEitherBar) {
    auto* catalog = find<QObject>("toolTypeMenu");
    // Off the bars: tags (from the top bar) and hand (from the rail)
    ASSERT_TRUE(tools()->remove(tools()->idOfApp("tags")));
    ASSERT_TRUE(tools()->remove(tools()->idOfApp("hand")));
    EXPECT_TRUE(tools()->unplaced().contains("tags"));
    // From the top bar's "+": the kinds, and what is on neither bar, by section; it goes to the top bar's end
    click(find("topBarAddButton"));
    until([&] { return catalog->property("visible").toBool(); });
    EXPECT_EQ(catalog->property("title").toString(), "Add to the top bar");
    for (const char* name: {"toolType_pen", "toolType_snip", "catalogSection_tools", "catalog_hand",
                            "catalogSection_document", "catalog_tags"}) {
        ASSERT_NE(entryOf(catalog, name), nullptr) << name;
        EXPECT_TRUE(entryOf(catalog, name)->property("offered").toBool()) << name;
    }
    EXPECT_EQ(entryOf(catalog, "catalog_open"), nullptr) << "on a bar: not offered";
    trigger(catalog, "catalog_tags");
    const QString tags = tools()->idOfApp("tags");
    ASSERT_FALSE(tags.isEmpty());
    EXPECT_EQ(tools()->barOf(tags), "top");
    EXPECT_EQ(tools()->indexOf(tags), tools()->topItems().size() - 1) << "at its end";
    until([&] { return inside(find("tagsButton"), find("topBar")); });
    EXPECT_TRUE(inside(find("tagsButton"), find("topBar")));
    // From the rail's "+": to the rail's end
    click(find("toolboxAddButton"));
    until([&] { return catalog->property("visible").toBool(); });
    EXPECT_EQ(catalog->property("title").toString(), "Add to the rail");
    trigger(catalog, "catalog_hand");
    const QString hand = tools()->idOfApp("hand");
    EXPECT_EQ(tools()->barOf(hand), "rail");
    EXPECT_EQ(tools()->indexOf(hand), tools()->entries().size() - 1);
    until([&] { return inside(find("handButton"), find("toolbox")); });
    EXPECT_TRUE(inside(find("handButton"), find("toolbox")));
    // A new tool from the top bar's "+": the editor, then at the top bar's end
    const int count = tools()->tools().size();
    click(find("topBarAddButton"));
    trigger(catalog, "toolType_highlighter");
    until([&] { return editorOpen(); });
    click(find("toolEditorAdd"));
    until([&] { return tools()->tools().size() == count + 1; });
    const QString added = tools()->active();
    EXPECT_EQ(tools()->entry(added).value("type"), "highlighter");
    EXPECT_EQ(tools()->barOf(added), "top");
    EXPECT_EQ(tools()->indexOf(added), tools()->topItems().size() - 1);
    until([&] { return inside(entry(added), find("topBar")); });
    EXPECT_TRUE(inside(entry(added), find("topBar")));
}

// Settings → "Back to the first layout": both bars as at a first start, asked first; the tools stay
TEST_F(ToolboxTest, backToTheFirstLayout) {
    const QString pen = nth("pen", 2);
    tools()->moveTo(pen, "top", 0);
    tools()->remove(tools()->idOfApp("tags"));
    tools()->moveTo(tools()->idOfApp("search"), "rail", 0);
    tools()->group(tools()->idOfApp("save"), tools()->idOfApp("open"));
    const int count = tools()->tools().size();
    auto* settingsPage = find<QObject>("settingsPage");
    QMetaObject::invokeMethod(settingsPage, "open");
    until([&] { return settingsPage->property("opened").toBool(); });
    auto* button = find("resetBarsButton");
    ASSERT_NE(button, nullptr);
    QMetaObject::invokeMethod(button, "clicked");
    auto* dialog = find<QObject>("resetBarsDialog");
    until([&] { return dialog->property("opened").toBool(); });
    ASSERT_TRUE(dialog->property("opened").toBool()) << "asked first";
    EXPECT_TRUE(tools()->unplaced().contains("tags")) << "nothing changed yet";
    QMetaObject::invokeMethod(dialog, "accept");
    until([&] { return !tools()->unplaced().contains("tags"); });
    QStringList top;
    for (const QVariant& v: tools()->topItems()) {
        const QVariantMap m = v.toMap();
        top << (m.value("divider").toBool() ? QString("|") : m.value("app").toString());
    }
    EXPECT_EQ(top, xqt::ToolboxModel::defaultTopLayout());
    EXPECT_EQ(tools()->tools().size(), count) << "the tools stay";
    EXPECT_EQ(tools()->barOf(pen), "rail") << "on the rail";
    QMetaObject::invokeMethod(settingsPage, "close");
}

// Zen is on the top bar at every size, phones too (the author, 2026-10-06: "I think zen is helpful put it into the top
// bar")
TEST_F(ToolboxTest, zenIsOnTheTopBarAtEverySize) {
    for (const auto& [w, h]: std::vector<std::pair<int, int>>{{1920, 1080}, {1366, 768}, {900, 1000}, {1000, 900},
                                                              {412, 915}, {915, 412}}) {
        resize(w, h);
        const std::string at = std::to_string(w) + "x" + std::to_string(h);
        auto* bar = find("topBar");
        ASSERT_TRUE(shown(bar)) << at;
        auto* zen = find("zenButton");
        ASSERT_TRUE(inside(zen, bar)) << at;
        EXPECT_EQ(rail(true).order, arranged(true)) << at << ": the same order";
        if (win("phoneLayout").toBool()) {
            EXPECT_TRUE(inside(bar, find("phoneAppBar"))) << at << ": in the app bar";
        }
        QMetaObject::invokeMethod(bar, "reveal", Q_ARG(QVariant, QVariant::fromValue<QObject*>(find("topApp_zen"))));
        until([&] { return inSight(find("topApp_zen")); });
        ASSERT_TRUE(inSight(find("topApp_zen"))) << at << ": " << rail(true).text.toStdString();
        click(find("topApp_zen"));
        until([&] { return win("zen").toBool(); });
        EXPECT_TRUE(win("zen").toBool()) << at;
        EXPECT_FALSE(shown(bar)) << at << ": hidden in Zen";
        QTest::keyClick(window, Qt::Key_Escape);
        until([&] { return !win("zen").toBool(); });
    }
}

// Android's back key (and gesture) leaves Zen first; Read ends with it; presenting without controls gets its controls
TEST_F(ToolboxTest, backLeavesZen) {
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    until([&] { return win("zen").toBool(); });
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !win("zen").toBool(); });
    EXPECT_FALSE(win("zen").toBool()) << "Back leaves Zen";
    EXPECT_TRUE(shown(find("topBar")));
    // Zen with its pill open: Back leaves Zen (not only the pill)
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    until([&] { return shown(find("zenDot")); });
    click(find("zenDot"));
    until([&] { return shown(find("zenPill")); });
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !win("zen").toBool(); });
    EXPECT_FALSE(win("zen").toBool());
    EXPECT_FALSE(shown(find("zenPill")));
    // A sheet open over the page in Zen takes Back first (it is in front); the next Back leaves Zen
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    until([&] { return win("zen").toBool(); });
    auto* settingsPage = find<QObject>("settingsPage");
    QMetaObject::invokeMethod(settingsPage, "open");
    until([&] { return settingsPage->property("opened").toBool(); });
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !settingsPage->property("visible").toBool(); });
    EXPECT_FALSE(settingsPage->property("visible").toBool());
    EXPECT_TRUE(win("zen").toBool()) << "the settings closed first";
    wait(200);
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !win("zen").toBool(); });
    EXPECT_FALSE(win("zen").toBool());
    // Read: Back ends it (read only, and the full screen it entered)
    QMetaObject::invokeMethod(window, "startReading");
    until([&] { return win("readOnlyOn").toBool() && win("zen").toBool(); });
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !win("readOnlyOn").toBool(); });
    EXPECT_FALSE(win("zen").toBool());
    EXPECT_FALSE(win("fullScreenMode").toBool());
    // Presenting without controls: Back shows them (presenting goes on)
    window->showNormal();
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, true));
    until([&] { return controller->presenting() && win("zen").toBool(); });
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !win("zen").toBool(); });
    EXPECT_TRUE(controller->presenting());
    until([&] { return shown(find("toolbox")); });
    EXPECT_TRUE(shown(find("toolbox"))) << "the controls";
    controller->setPresenting(false);
    window->setProperty("fullScreenMode", false);
}

// Full screen hides the top bar: the floating rail's ⋯ lists what it holds (in its order, a group's members one by
// one) and New
TEST_F(ToolboxTest, inFullScreenTheMoreMenuListsTheTopBarAndNew) {
    const QString pen = nth("pen", 1);
    tools()->moveTo(pen, "top", 0);
    tools()->group(tools()->idOfApp("save"), tools()->idOfApp("open"));
    window->setProperty("fullScreenMode", true);
    auto* box = find("toolbox");
    until([&] { return box->property("floating").toBool(); });
    EXPECT_FALSE(shown(find("topBar")));
    // (the window has grown to the screen and the rail shows its ⋯ where it stays: a click before missed it)
    until([&] { return window->size() == window->screen()->size() && shown(find("toolboxMoreButton")); });
    nextFrame();
    click(find("toolboxMoreButton"));
    auto* menu = find<QObject>("toolboxMoreMenu");
    until([&] { return menu->property("visible").toBool(); });
    for (const QString& name: {"toolboxMore_" + pen, QString("toolboxMore_open"), QString("toolboxMore_save"),
                               QString("toolboxMore_share"), QString("toolboxMore_geometry"), QString("toolboxMore_write"),
                               QString("toolboxMore_touchDrawing"), QString("toolboxNewItem")}) {
        QObject* e = entryOf(menu, name.toUtf8().constData());
        ASSERT_NE(e, nullptr) << name.toStdString();
        EXPECT_TRUE(e->property("offered").toBool()) << name.toStdString();
    }
    EXPECT_EQ(entryOf(menu, "toolboxMore_fullScreen"), nullptr) << "not twice";
    EXPECT_EQ(entryOf(menu, "toolboxMore_zen"), nullptr) << "⋯ has its own";
    QObject* tabsModel = controller->property("tabs").value<QObject*>();
    const int tabs = tabsModel->property("count").toInt();
    trigger(menu, "toolboxNewItem");
    until([&] { return tabsModel->property("count").toInt() == tabs + 1; });
    EXPECT_EQ(tabsModel->property("count").toInt(), tabs + 1) << "a new document";
    click(find("toolboxMoreButton"));
    trigger(menu, ("toolboxMore_" + pen).toUtf8().constData());
    until([&] { return tools()->active() == pen; });
    EXPECT_EQ(tools()->active(), pen) << "a tool of the top bar, taken from ⋯";
    window->setProperty("fullScreenMode", false);
}

// Nothing is unreachable: every app item offered here is on a bar or in ⋮ (complete since qt/top-bar), at the sizes
// the author uses; ⋮ itself is in sight
TEST_F(ToolboxTest, nothingIsUnreachable) {
    // (⋮'s entry for each app item that is not a "moreCmd_")
    const std::map<QString, QString> inMore{
            {"share", "shareItem"}, {"print", "printItem"}, {"milestone", "saveWithMessageItem"},
            {"tags", "documentTagsMenuItem"}, {"zen", "zenItem"}, {"read", "readItem"}, {"replay", "replayItem"},
            {"bookmark", "bookmarkPageItem"}, {"favourite", "favouriteDocumentItem"}};
    // Some off the bars: ⋮ has them
    tools()->remove(tools()->idOfApp("share"));
    tools()->remove(tools()->idOfApp("select"));
    for (const auto& [w, h]: std::vector<std::pair<int, int>>{{1920, 1080}, {1366, 768}, {900, 1000}, {412, 915}}) {
        resize(w, h);
        const std::string at = std::to_string(w) + "x" + std::to_string(h);
        auto* more = find("moreButton");
        ASSERT_TRUE(shown(more)) << at;
        EXPECT_TRUE(QRectF(0, 0, window->width(), window->height()).contains(rectOf(more))) << at << ": ⋮ in sight";
        for (const QString& name: xqt::ToolboxModel::appItemNames()) {
            QObject* b = slot(name);
            ASSERT_NE(b, nullptr) << name.toStdString();
            if (b->property("offered").isValid() && !b->property("offered").toBool()) {
                continue;  // (not offered here: a new document keeps no versions, the tab strip has New, …)
            }
            const QString entryName = inMore.count(name) ? inMore.at(name) : "moreCmd_" + name;
            QObject* e = find<QObject>(entryName);
            ASSERT_NE(e, nullptr) << at << ": " << entryName.toStdString();
            EXPECT_TRUE(e->property("offered").toBool()) << at << ": " << name.toStdString() << " in ⋮";
            const QString bar = tools()->barOf(tools()->idOfApp(name));
            if (!bar.isEmpty()) {
                EXPECT_TRUE(inside(qobject_cast<QQuickItem*>(b), find(bar == "top" ? "topBar" : "toolbox")))
                        << at << ": " << name.toStdString() << " on its bar";
            }
        }
    }
    // ⋮'s entry does what the button does
    resize(1920, 1080);
    QObject* moreMenu = find<QObject>("moreMenu");
    QMetaObject::invokeMethod(find("moreButton"), "clicked");
    until([&] { return moreMenu->property("visible").toBool(); });
    QMetaObject::invokeMethod(find<QObject>("moreCmd_select"), "triggered");
    QMetaObject::invokeMethod(moreMenu, "close");
    until([&] { return controller->tool().startsWith("select"); });
    EXPECT_TRUE(controller->tool().startsWith("select")) << "select from ⋮ → Tools";
}

// A text document: undo and redo at the start of its format bar, the formatting first, then the commands (the top
// bar, at the end of the row); nothing folds, the row scrolls as the bars do; ⋮ pinned at its end
TEST_F(ToolboxTest, aTextDocumentsFormatBarScrollsWithItsCommands) {
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
    EXPECT_FALSE(shown(find("topTools"))) << "one row";
    auto* commands = find("formatCommands");
    ASSERT_NE(commands, nullptr);
    auto* bar = find("topBar");
    until([&] { return inside(bar, commands); });
    ASSERT_TRUE(inside(bar, commands)) << "the top bar at the end of the row";
    for (const char* name: {"searchButton", "fullScreenButton", "saveButton", "settingsButton"}) {
        EXPECT_TRUE(inside(find(name), bar)) << name;
    }
    wait(300);  // (laid out)
    auto* flick = find("formatBarFlick");
    auto* fb = find("markdownFormatBar");
    EXPECT_FALSE(fb->property("insertsInMenu").toBool()) << "nothing folds";
    EXPECT_TRUE(shown(find("mdImage")));
    EXPECT_LT(rectOf(find("mdBold")).right(), rectOf(find("searchButton")).left()) << "the formatting first";
    EXPECT_TRUE(flick->property("interactive").toBool()) << "it scrolls at 1366";
    EXPECT_FALSE(inside(find("moreButton"), flick)) << "⋮ pinned";
    EXPECT_TRUE(shown(find("moreButton")));
    // Its view ends through the middle of a button, the fade at the end that has more
    const QRectF view = rectOf(flick);
    bool cut = false;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
        for (QQuickItem* c: i->childItems()) {
            if (c->isVisible() && c->inherits("QQuickAbstractButton") && c->width() >= 30) {
                const QRectF r = rectOf(c);
                cut = cut || (r.left() < view.right() - 5 && r.right() > view.right() + 5);
            }
            walk(c);
        }
    };
    walk(flick);
    EXPECT_TRUE(cut) << "a button cut at the view's end";
    EXPECT_TRUE(shown(find("formatBarFadeRight")));
    // The wheel scrolls it
    const QPoint over = rectOf(find("mdBold")).center().toPoint();
    QWheelEvent down(over, window->mapToGlobal(over), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                     Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &down);
    until([&] { return flick->property("contentX").toDouble() > 10; });
    EXPECT_GT(flick->property("contentX").toDouble(), 10) << "the wheel scrolls it";
    // Wide: everything in sight
    resize(2400, 1000);
    until([&] { return !flick->property("interactive").toBool(); });
    EXPECT_FALSE(flick->property("interactive").toBool()) << "no scrolling at 2400";
    EXPECT_TRUE(rectOf(flick).contains(rectOf(find("settingsButton"))));
}

/// Zen and read only (qt/docs/zen.md; qt/zen): three switches of their own - full screen, Zen (only the page and the
/// dot), read only (the edges turn the pages, no ink) - and Read, Zen and read only together
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
    void stroke(QPoint offset = {}) {
        const QPoint a = rectOf(find("canvas")).center().toPoint() + offset;
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
    bool zen() const { return window->property("zen").toBool(); }
    QObject* note() const { return find<QObject>("readOnlyNote"); }
    /// Full screen has taken the screen's size (the window follows it a little later)
    void fullScreenSettled() {
        until([&] { return window->size() == window->screen()->size(); });
        wait(300);
    }
    /// The dot's pill, open
    QQuickItem* openPill() {
        auto* pill = find("zenPill");
        if (!shown(pill)) {
            click(find("zenDot"));
            until([&] { return shown(pill); });
        }
        return pill;
    }
    /// ⋮ → View → Read, then the checks every size shares: full screen, Zen, read only, the edges turn the pages, no
    /// ink (said once), a finger's tap in the middle opens the dot's pill, Esc gives everything back
    void readAndLeave() {
        auto* canvas = find("canvas");
        QMetaObject::invokeMethod(find<QObject>("readItem"), "triggered");
        until([&] { return readOnly(); });
        ASSERT_TRUE(readOnly());
        fullScreenSettled();
        EXPECT_TRUE(win("fullScreenMode").toBool()) << "full screen";
        EXPECT_TRUE(zen()) << "Zen";
        EXPECT_TRUE(win("readOnlyOn").toBool());
        EXPECT_TRUE(canvas->property("readingOnly").toBool());
        EXPECT_FALSE(shown(find("toolbox"))) << "no tools";
        EXPECT_FALSE(shown(find("viewPill"))) << "no view pill";
        EXPECT_TRUE(shown(find("zenDot"))) << "the dot";
        // The fields: a fifth of the width at each edge, the page's whole height
        auto* next = find("readingNextField");
        ASSERT_TRUE(shown(next));
        EXPECT_NEAR(rectOf(next).right(), rectOf(canvas).right(), 1);
        EXPECT_NEAR(rectOf(next).height(), rectOf(canvas).height(), 1);
        EXPECT_GE(rectOf(next).width(), std::max(48.0, rectOf(canvas).width() * 0.19));
        EXPECT_LE(rectOf(next).width(), std::max(48.0, rectOf(canvas).width() * 0.26));
        // The pen writes nothing; the first stroke says so, at the pen, once
        const size_t before = ink();
        EXPECT_FALSE(shown(qobject_cast<QQuickItem*>(note())));
        stroke();
        EXPECT_EQ(ink(), before) << "no ink by accident";
        until([&] { return shown(qobject_cast<QQuickItem*>(note())); });
        EXPECT_TRUE(shown(qobject_cast<QQuickItem*>(note()))) << "the note";
        EXPECT_EQ(find("readOnlyNoteText")->property("text").toString(), "Read only — tap the dot to write");
        EXPECT_LT(QLineF(rectOf(qobject_cast<QQuickItem*>(note())).center(),
                         rectOf(canvas).center() + QPointF(60, 48)).length(), 200) << "at the pen";
        stroke(QPoint(-100, 0));
        EXPECT_EQ(note()->property("toldCount").toInt(), 1) << "once";
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
        // A finger's tap in the middle: the dot's pill (a tap on the page closes it)
        wait(400);  // (no double tap with the swipe)
        const QPoint middle = rectOf(canvas).center().toPoint();
        QTest::touchEvent(window, finger).press(1, middle);
        QTest::touchEvent(window, finger).release(1, middle);
        until([&] { return shown(find("zenPill")); });
        EXPECT_TRUE(shown(find("zenPill"))) << "a tap in the middle: the pill";
        QTest::touchEvent(window, finger).press(1, middle + QPoint(0, 60));
        QTest::touchEvent(window, finger).release(1, middle + QPoint(0, 60));
        until([&] { return !shown(find("zenPill")); });
        EXPECT_FALSE(shown(find("zenPill"))) << "a tap on the page closes it";
        // Esc: out of Read - Zen, read only and its full screen
        QTest::keyClick(window, Qt::Key_Escape);
        until([&] { return !win("fullScreenMode").toBool(); });
        EXPECT_FALSE(win("fullScreenMode").toBool());
        EXPECT_FALSE(readOnly());
        EXPECT_FALSE(zen());
        EXPECT_FALSE(canvas->property("readingOnly").toBool());
        EXPECT_FALSE(shown(find("readingNextField")));
        stroke();
        EXPECT_GT(ink(), before) << "the pen writes again";
    }
};

TEST_F(ReadingTest, onADesktopReadIsZenReadOnlyInFullScreen) {
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

TEST_F(ReadingPhoneTest, onAPhoneReadIsZenReadOnlyInFullScreen) {
    resize(412, 915);
    until([&] { return find("toolbox")->property("compact").toBool(); });
    ASSERT_EQ(win("phoneLayout").toBool(), true);
    readAndLeave();
    EXPECT_TRUE(win("phoneLayout").toBool()) << "still a phone in full screen";
    until([&] { return shown(find("toolbox")); });
    EXPECT_TRUE(find("toolbox")->property("compact").toBool()) << "the phone's dock again";
}

// Read only is a switch of its own: in a window with every tool shown (⋮ → View → Read only), in full screen (the
// floating toolbox's ⋯), while presenting (Read: Zen and read only); no lock, the first stroke says so once
TEST_F(ReadingTest, readOnlyIsASwitchOfItsOwn) {
    auto* box = find("toolbox");
    // In the window: the tools stay, the pen writes nothing, a note says how to write again
    QObject* item = find<QObject>("readOnlyItem");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->property("checkable").toBool());
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return readOnly(); });
    ASSERT_TRUE(readOnly());
    EXPECT_FALSE(win("fullScreenMode").toBool()) << "not full screen";
    EXPECT_FALSE(zen()) << "not Zen";
    EXPECT_TRUE(shown(box)) << "the tools stay";
    EXPECT_TRUE(item->property("checked").toBool());
    EXPECT_TRUE(shown(find("readingNextField"))) << "the edges turn the pages";
    const size_t before = ink();
    stroke();
    EXPECT_EQ(ink(), before);
    until([&] { return shown(qobject_cast<QQuickItem*>(note())); });
    EXPECT_EQ(find("readOnlyNoteText")->property("text").toString(), "Read only — ⋮ → View → Read only to write");
    stroke(QPoint(-100, 0));
    EXPECT_EQ(note()->property("toldCount").toInt(), 1) << "once";
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return !readOnly(); });
    stroke();
    EXPECT_GT(ink(), before) << "writes again";
    // Turned on again: a new read-only time, the note again
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return readOnly(); });
    stroke(QPoint(-100, -60));
    EXPECT_EQ(note()->property("toldCount").toInt(), 2) << "said again after read only was off";
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return !readOnly(); });

    // Full screen: the floating toolbox's ⋯
    window->setProperty("fullScreenMode", true);
    fullScreenSettled();
    until([&] { return shown(box) && box->property("floating").toBool(); });
    click(find("toolboxMoreButton"));
    trigger(find<QObject>("toolboxMoreMenu"), "toolboxReadOnlyItem");
    until([&] { return readOnly(); });
    ASSERT_TRUE(readOnly());
    EXPECT_TRUE(shown(box)) << "the floating toolbox stays (Zen hides it)";
    EXPECT_TRUE(win("fullScreenMode").toBool());
    click(find("toolboxMoreButton"));
    trigger(find<QObject>("toolboxMoreMenu"), "toolboxReadOnlyItem");
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly());
    // Presenting: Read (the keys) is presenting in Zen with read only; page by page
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    until([&] { return controller->presenting(); });
    wait(300);  // (presenting has settled: the console placed, the window active)
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return readOnly(); });
    ASSERT_TRUE(readOnly());
    EXPECT_TRUE(win("presentClean").toBool()) << "presenting without controls";
    EXPECT_TRUE(find("canvas")->property("readingOnly").toBool());
    EXPECT_FALSE(shown(box));
    const size_t before2 = ink();
    stroke();
    EXPECT_EQ(ink(), before2);
    tapField("readingNextField");
    until([&] { return page() == 2; });
    EXPECT_EQ(page(), 2) << "the next slide";
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly());
    until([&] { return shown(box); });
    EXPECT_TRUE(shown(box)) << "presenting with the tools again";
    EXPECT_TRUE(controller->presenting());
    controller->setPresenting(false);
    window->setProperty("fullScreenMode", false);
    until([&] { return !box->property("floating").toBool(); });
}

// Zen (qt/docs/zen.md): only the page and a faint dot in the lower left corner; the pen writes, P/H/E/T take tools
TEST_F(ReadingTest, zenHidesEverythingButThePageAndTheDot) {
    resize(1280, 800);
    auto* canvas = find("canvas");
    QObject* item = find<QObject>("zenItem");
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->property("checkable").toBool());
    QMetaObject::invokeMethod(item, "triggered");
    until([&] { return zen(); });
    ASSERT_TRUE(zen());
    EXPECT_FALSE(win("fullScreenMode").toBool()) << "Zen is no full screen";
    EXPECT_FALSE(readOnly()) << "nor read only";
    for (const char* gone: {"toolbox", "topTools", "toolArea", "tabStrip", "sidebarArrow", "viewPill", "sidebar", "phoneDock",
                            "phoneAppBar"}) {
        EXPECT_FALSE(shown(find(gone))) << gone << " is hidden";
    }
    auto* dot = find("zenDot");
    ASSERT_TRUE(shown(dot));
    // In the lower left corner of the page, a finger wide; what shows of it is a 10 px dot, faint after 2 s
    const QRectF target = rectOf(dot);
    EXPECT_GE(target.width(), 48);
    EXPECT_GE(target.height(), 48);
    EXPECT_NEAR(target.left(), rectOf(canvas).left(), 1);
    EXPECT_NEAR(target.bottom(), rectOf(canvas).bottom(), 1);
    auto* mark = find("zenDotMark");
    ASSERT_NE(mark, nullptr);
    EXPECT_LE(mark->width(), 12);
    EXPECT_GE(mark->width(), 8);
    until([&] { return mark->opacity() <= 0.21; }, 4000);
    EXPECT_LE(mark->opacity(), 0.21) << "about 20 % after 2 s";
    // The mouse near it: clearer
    QTest::mouseMove(window, (target.topRight() + QPointF(40, -20)).toPoint());
    until([&] { return mark->opacity() > 0.5; }, 2000);
    EXPECT_GT(mark->opacity(), 0.5) << "brighter while the pointer is near";
    QTest::mouseMove(window, rectOf(canvas).center().toPoint());
    // Safe areas: the dot keeps clear of them
    safeArea(0, 0, 30, 20);
    EXPECT_NEAR(rectOf(dot).left(), rectOf(canvas).left() + 20, 1);
    EXPECT_NEAR(rectOf(dot).bottom(), rectOf(canvas).bottom() - 30, 1);
    safeArea(0, 0, 0, 0);
    // The pen writes, the keys take the tools
    const size_t before = ink();
    stroke();
    EXPECT_EQ(ink(), before + 1) << "Zen writes";
    QTest::keyClick(window, Qt::Key_H);
    until([&] { return controller->tool() == "highlighter"; });
    EXPECT_EQ(controller->tool(), "highlighter");
    stroke(QPoint(-120, -80));
    EXPECT_EQ(ink(), before + 2);
    QTest::keyClick(window, Qt::Key_P);
    until([&] { return controller->tool() == "pen"; });
    EXPECT_EQ(controller->tool(), "pen");
    // Its keys (Ctrl+Alt+Z) leave it, and enter it again; Esc leaves it
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return !zen(); });
    EXPECT_FALSE(zen());
    EXPECT_TRUE(shown(find("toolbox")));
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return zen(); });
    EXPECT_TRUE(zen());
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !zen(); });
    EXPECT_FALSE(zen()) << "Esc";
    EXPECT_TRUE(shown(find("toolbox")));
    EXPECT_TRUE(shown(find("viewPill")));
    // Settings → Shortcuts: Zen on Ctrl+Alt+Z, Read on Ctrl+Alt+R
    auto* shortcuts = qobject_cast<QAbstractItemModel*>(controller->shortcutsModel());
    ASSERT_NE(shortcuts, nullptr);
    const auto roles = shortcuts->roleNames();
    const int idRole = roles.key("actionId"), keysRole = roles.key("keys");
    QMap<QString, QString> byId;
    for (int r = 0; r < shortcuts->rowCount(); ++r) {
        byId[shortcuts->data(shortcuts->index(r, 0), idRole).toString()] =
                shortcuts->data(shortcuts->index(r, 0), keysRole).toString();
    }
    EXPECT_EQ(byId.value("zen"), "Ctrl+Alt+Z");
    EXPECT_EQ(byId.value("read"), "Ctrl+Alt+R");
}

// The dot's pill (qt/docs/zen.md): beside the dot over the page (the page does not move); Show controls, Read only, the
// page number (all pages), fit the width / the whole page; a tap on the page closes it and writes nothing
TEST_F(ReadingTest, theZenDotsPillAndItsEntries) {
    resize(1280, 800);
    auto* canvas = find("canvas");
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    until([&] { return zen(); });
    wait(300);  // (the page takes the room of the bars)
    const QRectF canvasBefore = rectOf(canvas);
    const double y0 = canvas->property("contentY").toDouble();
    auto* pill = openPill();
    ASSERT_TRUE(shown(pill));
    const QRectF dot = rectOf(find("zenDot"));
    EXPECT_GE(rectOf(pill).left(), dot.right() - 8) << "beside the dot";
    EXPECT_NEAR(rectOf(pill).bottom(), dot.bottom(), 8);
    EXPECT_EQ(rectOf(canvas), canvasBefore) << "the page does not move";
    EXPECT_EQ(canvas->property("contentY").toDouble(), y0);
    for (const char* name: {"zenShowControls", "zenReadOnly", "zenPage", "zenFitWidth", "zenFitPage"}) {
        EXPECT_TRUE(shown(find(name))) << name;
        EXPECT_TRUE(rectOf(pill).adjusted(-1, -1, 1, 1).contains(rectOf(find(name)))) << name << " in the pill";
    }
    EXPECT_EQ(find("zenPage")->property("text").toString(), "1 / 3");
    // A tap on the page closes it, and writes nothing
    const size_t before = ink();
    click(canvas);
    until([&] { return !shown(pill); });
    EXPECT_FALSE(shown(pill));
    EXPECT_EQ(ink(), before) << "the tap that closed it did not write";
    // Read only: a switch
    openPill();
    click(find("zenReadOnly"));
    until([&] { return readOnly(); });
    EXPECT_TRUE(readOnly());
    EXPECT_TRUE(zen());
    EXPECT_TRUE(find("zenReadOnly")->property("checked").toBool());
    click(find("zenReadOnly"));
    until([&] { return !readOnly(); });
    EXPECT_FALSE(readOnly());
    window->setProperty("readOnly", true);
    EXPECT_TRUE(find("zenReadOnly")->property("checked").toBool()) << "follows read only turned on elsewhere";
    window->setProperty("readOnly", false);
    // The fits
    openPill();
    click(find("zenFitPage"));
    until([&] { return !shown(pill); });
    wait(300);
    const int whole = controller->property("zoomPercent").toInt();
    openPill();
    click(find("zenFitWidth"));
    wait(300);
    const int width = controller->property("zoomPercent").toInt();
    EXPECT_GT(width, whole) << "the width of a portrait page in a landscape window: larger than the whole page";
    // The page number: all pages, to go to one
    openPill();
    click(find("zenPage"));
    auto* grid = find("pageGrid");
    until([&] { return shown(grid); });
    EXPECT_TRUE(shown(grid));
    EXPECT_FALSE(shown(pill));
    QMetaObject::invokeMethod(grid, "close");
    until([&] { return !shown(grid); });
    // Show controls: Zen ends
    openPill();
    click(find("zenShowControls"));
    until([&] { return !zen(); });
    EXPECT_FALSE(zen());
    EXPECT_FALSE(shown(pill));
    EXPECT_FALSE(shown(find("zenDot")));
    EXPECT_TRUE(shown(find("toolbox")));
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
TEST_F(ToolboxAudioTest, recordingIsOnTheTopBarAndItsPillStaysInSight) {
    auto* box = find("toolbox");
    auto* record = find("recordButton");
    ASSERT_NE(record, nullptr);
    until([&] { return shown(record); });
    ASSERT_TRUE(shown(record));
    EXPECT_FALSE(inside(record, box)) << "not on the rail";
    EXPECT_TRUE(inside(record, find("topBar"))) << "on the top bar (its first layout)";

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

    // A phone: on the top bar in the app bar, as everywhere; and ⋮ → Tools
    resize(412, 915);
    until([&] { return box->property("compact").toBool(); });
    EXPECT_TRUE(inside(record, find("phoneAppBar")));
    EXPECT_TRUE(find<QObject>("moreCmd_record")->property("offered").toBool());
}

// Without an audio backend nothing offers recording: not the rail, not the top bar, not ⋮, not the catalog
TEST_F(ToolboxNoAudioTest, withoutAudioNothingOffersRecording) {
    ASSERT_FALSE(audio()->property("available").toBool());
    auto* record = find("recordButton");
    ASSERT_NE(record, nullptr);
    EXPECT_FALSE(shown(record));
    EXPECT_FALSE(inside(record, find("toolbox")));
    EXPECT_FALSE(inside(record, find("topBar")) && shown(record)) << "not on the top bar";
    EXPECT_FALSE(find("topApp_record")) << "its place is skipped";
    EXPECT_TRUE(shown(find("handButton")) && inside(find("handButton"), find("toolbox")));
    EXPECT_TRUE(shown(find("touchDrawingButton")) && inside(find("touchDrawingButton"), find("topBar")));
    EXPECT_FALSE(find<QObject>("moreCmd_record")->property("offered").toBool()) << "nor ⋮";
    // ... nor the catalog, though it is on neither bar
    tools()->remove(tools()->idOfApp("record"));
    click(find("topBarAddButton"));
    auto* catalog = find<QObject>("toolTypeMenu");
    until([&] { return catalog->property("visible").toBool(); });
    EXPECT_EQ(entryOf(catalog, "catalog_record"), nullptr);
    QMetaObject::invokeMethod(catalog, "close");
}

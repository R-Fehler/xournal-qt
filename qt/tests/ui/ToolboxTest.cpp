/*
 * xournal-qt: the toolbox in the real window (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of
 * the canvas (right by default), undo and redo at its head, the fixed tools lent to it; a tap picks a tool up, sections
 * fold into stacks when the rail is short, the edge is chosen per window size. (The classic tool bar was removed in
 * 0.8.0: the toolbox is the only one.)
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
    /// What the rail shows in its middle (qt/docs/toolbox.md, "Short rails"): the cells (the user's tools, stacks, the
    /// fixed tools and their stack) shown, how many of them lie in sight, the room of the middle along the rail and
    /// the length its contents take
    struct RailState {
        int cells = 0;
        int inSight = 0;
        double room = 0;
        double content = 0;
        bool scrolls = false;
        bool anyFolded = false;
        bool allFolded = false;
        double nextUnfold = 1e9;
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
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            for (QQuickItem* c: i->childItems()) {
                const QString n = c->objectName();
                const bool fixed = c->parentItem() && c->parentItem()->objectName() == "toolboxFixed";
                if ((n.startsWith("toolEntry_") || n.startsWith("toolStack_") || n == "toolboxFixedStack" || fixed) &&
                    shown(c)) {
                    ++s.cells;
                    const QRectF r = rectOf(c).adjusted(1, 1, -1, -1);
                    s.inSight += sight.contains(r) ? 1 : 0;
                }
                walk(c);
            }
        };
        walk(grid);
        s.room = vertical ? middle->height() : middle->width();
        s.content = vertical ? middle->property("contentHeight").toDouble() : middle->property("contentWidth").toDouble();
        s.scrolls = middle->property("interactive").toBool();
        const QVariantMap plan = box->property("plan").toMap();
        const int fixed = box->property("fixedButtons").toList().size();
        const int fixedShown = box->property("fixedShown").toInt();
        bool all = fixedShown == 0 || fixed <= 1;
        bool any = fixedShown < fixed;
        // (the room the next unfolding needs: a fixed tool more on its own takes a cell; a section, all but one)
        s.nextUnfold = fixedShown < fixed ? box->property("cell").toDouble() : 1e9;
        const QVariantList folded = plan.value("folded").toList();
        const QVariantList sections = box->property("sections").toList();
        for (int i = 0; i < folded.size() && i < sections.size(); ++i) {
            const bool f = folded[i].toBool();
            any = any || f;
            all = all && (f || sections[i].toList().size() <= 1);
            if (f) {
                s.nextUnfold = std::min(s.nextUnfold, (sections[i].toList().size() - 1) * box->property("cell").toDouble());
            }
        }
        s.anyFolded = any;
        s.allFolded = all;
        s.text = QString("%1×%2 %3%4: %5 cells, %6 in sight, room %7, content %8%9%10")
                         .arg(window->width())
                         .arg(window->height())
                         .arg(win("layoutClass").toString(), box->property("compact").toBool() ? " (dock)" : "")
                         .arg(s.cells)
                         .arg(s.inSight)
                         .arg(s.room)
                         .arg(s.content)
                         .arg(s.anyFolded ? ", folded" : "", s.scrolls ? ", scrolls" : "");
        return s;
    }
    /// The rail uses the room it has: every cell it shows is in sight unless everything is folded and it scrolls;
    /// while something is folded, the room left over is less than the smallest unfolding needs (plus the 16 px a
    /// growing rail keeps against flicker: `grew`)
    void expectRailFills(const char* where, bool grew = false) {
        auto* box = find("toolbox");
        ASSERT_TRUE(shown(box)) << where;
        until([&] { return rail().cells > 0; });
        const RailState s = rail();
        const double cell = box->property("cell").toDouble();
        SCOPED_TRACE(std::string(where) + ": " + s.text.toStdString());
        EXPECT_GE(s.room, cell) << "a middle at least a cell long";
        if (box->property("compact").toBool()) {
            // The dock: as many of the user's tools as fit, then "My tools"
            const int all = tools()->tools().size();
            EXPECT_GE(s.cells, std::min(all, 1));
            EXPECT_EQ(s.inSight, s.cells) << "no tool cut off";
            if (s.cells < all) {
                EXPECT_LT(s.room - s.cells * cell, cell + 8.5) << "room for one more tool left empty";
            }
            EXPECT_TRUE(shown(find("toolboxAllButton")));
            return;
        }
        if (s.scrolls) {
            EXPECT_TRUE(s.allFolded) << "scrolls only when everything is folded";
            EXPECT_GE(s.inSight, int(s.room / cell) - 1) << "the cells in sight fill the middle";
        } else {
            EXPECT_EQ(s.inSight, s.cells) << "every cell in sight";
            EXPECT_LE(s.content, s.room + 0.5);
            if (s.anyFolded) {
                EXPECT_LT(s.room - s.content, s.nextUnfold + (grew ? 16 : 0)) << "folded with room to spare";
            }
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

TEST_F(ToolboxTest, dockedAtTheRightWithUndoAndRedoAndTheFixedTools) {
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

    // The user's tools, then the fixed ones (the window's own buttons), then "+"
    for (const QVariant& v: tools()->tools()) {
        auto* b = entry(v.toMap().value("id").toString());
        ASSERT_TRUE(shown(b)) << v.toMap().value("type").toString().toStdString();
        EXPECT_TRUE(inside(b, box));
    }
    // (at 1920×1080 the rail is a little short for all of them: the last ones are in one stack after the others)
    const int fixedShown = box->property("fixedShown").toInt();
    EXPECT_GE(fixedShown, 5) << "hand, select, snip, write, setsquare on their own";
    const char* fixedNames[] = {"handButton", "selectButton", "snipButton", "textModeButton", "geometryButton",
                                "pdfTextButton", "touchDrawingButton"};
    for (int i = 0; i < 7; ++i) {
        auto* b = find(fixedNames[i]);
        if (i < fixedShown) {
            ASSERT_TRUE(shown(b)) << fixedNames[i];
            EXPECT_TRUE(inside(b, box)) << fixedNames[i];
        } else {
            EXPECT_FALSE(shown(b)) << fixedNames[i] << ": in the stack";
        }
    }
    EXPECT_EQ(shown(find("toolboxFixedStack")), fixedShown < 7);
    EXPECT_TRUE(shown(find("toolboxAddButton")));
    // The classic tools are gone (0.8.0): the toolbox's entries are the pens, erasers, shapes, text boxes and notes
    for (const char* name: {"penButton", "eraserButton", "shapeButton", "textButton", "stickyNoteButton",
                            "colorStrip", "widthStrip"}) {
        EXPECT_EQ(find(name), nullptr) << name;
    }
    EXPECT_TRUE(shown(find("searchButton"))) << "the commands stay at the top";

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

    // A fixed tool: no entry in hand
    click(find("handButton"));
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

TEST_F(ToolboxTest, aShortRailFoldsSectionsIntoStacksAndKeepsTheOneInHand) {
    resize(1300, 600);
    auto* box = find("toolbox");
    ASSERT_TRUE(shown(box));
    // Everything inside the rail; at least the fixed tools folded
    EXPECT_TRUE(shown(find("toolboxFixedStack")));
    EXPECT_FALSE(shown(find("handButton"))) << "in the folded fixed tools' list";
    const QRectF r = rectOf(box);
    EXPECT_TRUE(r.contains(rectOf(find("toolboxAddButton"))));
    // The pens are a stack showing the pen used last; a tap takes it, a tap again opens the list
    const QString pen1 = nth("pen"), pen3 = nth("pen", 2);
    controller->applyToolEntry(pen3);
    wait(100);
    QQuickItem* stack = nullptr;
    until([&] { return (stack = find("toolStack_" + pen3)) != nullptr; });
    ASSERT_NE(stack, nullptr) << "the pens folded, the one in hand shown";
    EXPECT_TRUE(r.contains(rectOf(stack).adjusted(6, 6, -6, -6)));
    EXPECT_TRUE(stack->property("inHand").toBool());
    click(stack);
    auto* flyout = find<QObject>("toolStackFlyout");
    until([&] { return flyout->property("visible").toBool(); });
    ASSERT_TRUE(flyout->property("visible").toBool());
    auto* first = find("stackEntry_" + pen1);
    ASSERT_TRUE(shown(first));
    click(first);
    until([&] { return tools()->active() == pen1; });
    EXPECT_EQ(controller->color(), QColor("#2B2B2B"));
    until([&] { return !flyout->property("visible").toBool(); });
    EXPECT_TRUE(shown(find("toolStack_" + pen1))) << "the stack shows the pen in hand now";

    // Taller again: everything unfolds (the last fixed tools only where the rail is too short for them)
    resize(1920, 1200);
    EXPECT_TRUE(shown(entry(pen1)));
    EXPECT_TRUE(shown(find("handButton")));
    EXPECT_FALSE(shown(find("toolboxFixedStack")));
    resize(1920, 1080);
    EXPECT_TRUE(shown(entry(pen1)));
    EXPECT_TRUE(shown(find("handButton")));
}

// The author on 0.6.0, a Galaxy Fold 7 unfolded: "the rail is just showing one item although there is plenty space on
// the rail" (qt/rail-fill). Its sizes with the touch profile and an Android phone's safe area: unfolded (900 × 1000,
// tablet portrait), turned (1000 × 900), folded (412 × 915, the dock) and folded held sideways (915 × 412)
TEST_F(ToolboxTest, theRailFillsItsRoomAtTheFoldsSizes) {
    touchProfile("on");
    for (const bool insets: {false, true}) {
        safeArea(insets ? 40 : 0, 0, insets ? 24 : 0, 0);
        // (the rail grows at 1920 × 1080: it may keep 16 px against flicker)
        for (const auto& [w, h]: std::vector<std::pair<int, int>>{{900, 1000}, {1000, 900}, {412, 915}, {915, 412},
                                                                  {1920, 1080}, {1300, 600}}) {
            resize(w, h);
            const std::string at = std::to_string(w) + "x" + std::to_string(h) + (insets ? " with insets" : "");
            expectRailFills(at.c_str(), w == 1920);
        }
    }
    // Unfolded with room for every tool of the first start but the last fixed ones: no stack of one's own tools
    resize(900, 1000);
    EXPECT_FALSE(rail().allFolded) << rail().text.toStdString();
    EXPECT_GE(rail().cells, 12) << rail().text.toStdString();
    touchProfile("auto");
}

// What the device goes through: started folded (the dock), unfolded, turned, folded again; and the safe area changing
// at one size (the navigation bar, a cut-out): the rail plans anew for every length
TEST_F(ToolboxTest, theRailPlansAnewWhenThePhoneIsFoldedUnfoldedAndTurned) {
    touchProfile("on");
    safeArea(40, 0, 24, 0);
    resize(412, 915);
    expectRailFills("folded");
    resize(900, 1000);
    expectRailFills("unfolded");
    const RailState unfolded = rail();
    resize(1000, 900);
    expectRailFills("turned");
    resize(900, 1000);
    expectRailFills("unfolded again", true);
    EXPECT_EQ(rail().cells, unfolded.cells) << "the same as before";
    resize(412, 915);
    expectRailFills("folded again");
    resize(915, 412);
    expectRailFills("folded, sideways");
    resize(900, 1000);
    expectRailFills("unfolded from sideways");
    EXPECT_EQ(rail().cells, unfolded.cells);
    // The navigation bar grows under the rail's end, and goes again: the same size, another length
    safeArea(40, 0, 200, 0);
    expectRailFills("a taller navigation bar");
    safeArea(40, 0, 24, 0);
    expectRailFills("the navigation bar as before", true);
    EXPECT_EQ(rail().cells, unfolded.cells);
    // Android reports the new size and the new insets one after the other (unfolding: the window grows while the
    // insets of the moment before still hold): the rail must not stay folded for the insets of before
    resize(412, 915);
    safeArea(40, 0, 420, 0);
    resize(900, 1000);
    safeArea(40, 0, 24, 0);
    expectRailFills("unfolded, the insets after the size", true);
    EXPECT_EQ(rail().cells, unfolded.cells) << "as many as unfolded with the insets at once";
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
    click(more);
    auto* menu = find<QObject>("toolboxMoreMenu");
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

TEST_F(ToolboxTest, onAPhoneTheDockHoldsTheFirstToolsAndTheSheetHoldsThemAll) {
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
    EXPECT_FALSE(shown(find("handButton")));
    const QRectF dock = rectOf(box);
    EXPECT_GT(dock.top(), 800) << "at the bottom";
    EXPECT_LE(dock.right(), 412.5);
    // The first tools, the one in hand among them
    const QString laser = nth("laser");
    controller->applyToolEntry(laser);
    until([&] { return shown(entry(laser)); });
    EXPECT_TRUE(shown(entry(laser))) << "the tool in hand is always in the dock";
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
            {"zenButton", "zenItem"}, {"replayButton", "replayItem"}, {"tagsButton", "documentTagsMenuItem"}};
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
    // One place each: the rail's fixed tools are not in the bar
    for (const char* fixed: {"handButton", "selectButton", "snipButton", "textModeButton"}) {
        EXPECT_FALSE(inside(find(fixed), find("topTools"))) << fixed;
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
    // Reading from the bar: Zen and read only, full screen; the keys again end it
    resize(1920, 1080);
    click(find("readButton"));
    until([&] { return win("readOnlyOn").toBool(); });
    EXPECT_TRUE(win("fullScreenMode").toBool());
    EXPECT_TRUE(win("zen").toBool());
    QTest::keyClick(window, Qt::Key_R, Qt::ControlModifier | Qt::AltModifier);
    until([&] { return !win("readOnlyOn").toBool(); });
    EXPECT_FALSE(win("zen").toBool());
    EXPECT_FALSE(win("fullScreenMode").toBool());
    // Zen from the bar (qt/zen; the top bar places it in its first layout): the window stays as it is
    window->showNormal();
    resize(1920, 1080);
    until([&] { return shown(find("zenButton")); });
    click(find("zenButton"));
    until([&] { return win("zen").toBool(); });
    EXPECT_TRUE(win("zen").toBool());
    EXPECT_FALSE(win("fullScreenMode").toBool());
    EXPECT_FALSE(win("readOnlyOn").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !win("zen").toBool(); });
    EXPECT_FALSE(win("zen").toBool());
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
        EXPECT_TRUE(win("reading").toBool());
        EXPECT_TRUE(canvas->property("readingOnly").toBool());
        EXPECT_FALSE(shown(find("toolbox"))) << "no tools";
        EXPECT_FALSE(shown(find("viewPill"))) << "no view pill";
        EXPECT_EQ(find("readingPill"), nullptr) << "the reading pill is gone";
        EXPECT_EQ(find("readOnlyMark"), nullptr) << "no lock (0.8.0)";
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
    EXPECT_EQ(find("readOnlyMark"), nullptr) << "no lock";
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
    EXPECT_EQ(byId.value("readOnly"), "Ctrl+Alt+R");
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

// The record button is a fixed tool of the rail (one place: not in the command bar too), so recording is there docked,
// floating in full screen and on a phone's sheet; the recording pill stays in sight, clear of the toolbox
TEST_F(ToolboxAudioTest, recordingIsAFixedToolOfTheRailAndItsPillStaysInSight) {
    resize(1920, 1200);  // (room for all the fixed tools: at 1080 the last ones are in a stack)
    auto* box = find("toolbox");
    auto* record = find("recordButton");
    ASSERT_NE(record, nullptr);
    until([&] { return shown(record) && inside(record, box); });
    ASSERT_TRUE(shown(record));
    EXPECT_TRUE(inside(record, find("toolboxFixed"))) << "among the fixed tools";
    EXPECT_FALSE(inside(record, find("toolArea"))) << "not in the command bar too";
    EXPECT_FALSE(shown(find("moreToolsButton"))) << "nothing in \"more tools\" at 1920";

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
    if (box->property("plan").toMap().value("fixedFolded").toBool()) {
        // (a short toolbox: the fixed tools in one stack, which shows the recording, a tap lists them)
        auto* stack = find("toolboxFixedStack");
        ASSERT_TRUE(shown(stack));
        EXPECT_EQ(stack->property("iconName"), record->property("iconName")) << "the stack shows the recording";
        click(stack);
        until([&] { return shown(record); });
    }
    ASSERT_TRUE(shown(record)) << "in the floating toolbox";
    click(record);  // (there: it stops)
    until([&] { return !audio()->property("recording").toBool(); });
    EXPECT_FALSE(audio()->property("recording").toBool());
    QMetaObject::invokeMethod(find<QObject>("toolboxFixedFlyout"), "close");
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
    for (const char* name: {"handButton", "touchDrawingButton"}) {
        EXPECT_TRUE(shown(find(name)) && inside(find(name), find("toolboxFixed"))) << name;
    }
    resize(412, 915);
    until([&] { return find("toolbox")->property("compact").toBool(); });
    click(find("toolboxAllButton"));
    auto* sheet = find<QObject>("phoneToolSheet");
    until([&] { return sheet->property("visible").toBool(); });
    ASSERT_TRUE(sheet->property("visible").toBool());
    EXPECT_NE(find("toolCell_image"), nullptr);
    EXPECT_EQ(find("toolCell_record"), nullptr);
}

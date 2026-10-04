/*
 * xournal-qt: the toolbox in the real window (qt/docs/toolbox.md): the user's own tools in a rail docked to a side of
 * the canvas (right by default), undo and redo at its head, the fixed tools lent to it; a tap picks a tool up, sections
 * fold into stacks when the rail is short, the edge is chosen per window size. The classic tool bar comes back with
 * toolbarMode=classic.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>
#include <cmath>
#include <type_traits>
#include <memory>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QWheelEvent>
#include <gtest/gtest.h>

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
        settings()->set("toolbarMode", "toolbox");
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
        settings()->set("toolbarMode", "classic");  // (the other UI tests keep the classic bar)
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
    for (const char* name: {"handButton", "selectButton", "textModeButton", "geometryButton", "pdfTextButton",
                            "touchDrawingButton"}) {
        auto* b = find(name);
        ASSERT_TRUE(shown(b)) << name;
        EXPECT_TRUE(inside(b, box)) << name;
    }
    EXPECT_TRUE(shown(find("toolboxAddButton")));
    // The classic tools are not in the command bar
    for (const char* name: {"penButton", "eraserButton", "shapeButton", "textButton", "stickyNoteButton"}) {
        EXPECT_FALSE(shown(find(name))) << name;
    }
    EXPECT_FALSE(shown(find("colorStrip")));
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

    // Taller again: everything unfolds
    resize(1920, 1080);
    EXPECT_TRUE(shown(entry(pen1)));
    EXPECT_TRUE(shown(find("handButton")));
    EXPECT_FALSE(shown(find("toolboxFixedStack")));
}

TEST_F(ToolboxTest, theClassicBarComesBackWithItsSetting) {
    settings()->set("toolbarMode", "classic");
    until([&] { return !shown(find("toolbox")) && shown(find("penButton"))
                       && find("handButton")->width() == find("handButton")->implicitWidth(); });
    EXPECT_FALSE(shown(find("toolbox")));
    EXPECT_TRUE(shown(find("penButton")));
    EXPECT_TRUE(shown(find("handButton")));
    EXPECT_FALSE(inside(find("handButton"), find("toolbox")));
    EXPECT_TRUE(shown(find("toolUndoButton")));
    EXPECT_EQ(find("handButton")->width(), find("handButton")->implicitWidth()) << "its own size again";
    settings()->set("toolbarMode", "toolbox");
    until([&] { return shown(find("toolbox")) && inside(find("handButton"), find("toolbox")); });
    EXPECT_TRUE(inside(find("handButton"), find("toolbox")));
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
    EXPECT_FALSE(shown(find("quickToolSquare"))) << "the classic tool square is not there";
    EXPECT_FALSE(shown(find("penPill")));
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
    auto check = [&](int w, int h) {
        resize(w, h);
        for (const auto& [button, item]: {std::pair{"shareButton", "shareItem"}, std::pair{"printButton", "printItem"}}) {
            const bool inBar = shown(find(button));
            QObject* entry = entryOf(moreMenu, item);
            ASSERT_NE(entry, nullptr);
            EXPECT_NE(inBar, entry->property("offered").toBool())
                    << button << " at " << w << ": in the bar or in ⋮, never both, never neither";
        }
    };
    check(1920, 1080);
    EXPECT_TRUE(shown(find("shareButton"))) << "room for them at 1920";
    EXPECT_TRUE(shown(find("printButton")));
    EXPECT_FALSE(shown(find("moreToolsButton"))) << "nothing in \"more tools\"";
    check(1024, 700);
    check(800, 600);
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

TEST_F(ToolboxTest, readingIsReadOnlyWithItsPillAndEscLeavesIt) {
    controller->applyToolEntry(nth("pen"));
    auto* canvas = find("canvas");
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "reader"));
    until([&] { return win("reading").toBool(); });
    ASSERT_TRUE(win("reading").toBool());
    EXPECT_TRUE(canvas->property("readingOnly").toBool());
    EXPECT_TRUE(canvas->property("snapVertically").toBool());
    EXPECT_FALSE(shown(find("toolbox")));
    EXPECT_FALSE(shown(find("topTools")));
    auto* pill = find("readingPill");
    ASSERT_TRUE(shown(pill));
    EXPECT_TRUE(shown(find("readingSnapButton")));
    // The pen writes nothing (it scrolls)
    const size_t before = [&] {
        auto* page = controller->tabManager().currentSession()->getDocument()->getPage(0).get();
        return page->getSelectedLayer()->getElements().size();
    }();
    const QPoint a = rectOf(canvas).center().toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, a);
    for (int k = 1; k <= 6; ++k) {
        QTest::mouseMove(window, a + QPoint(10 * k, 8 * k));
        wait(10);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, a + QPoint(60, 48));
    wait(100);
    auto* page = controller->tabManager().currentSession()->getDocument()->getPage(0).get();
    EXPECT_EQ(page->getSelectedLayer()->getElements().size(), before) << "no ink by accident";
    // It fades after 2 s
    until([&] { return pill->opacity() < 0.01; }, 4000);
    EXPECT_LT(pill->opacity(), 0.01);
    // Sideways from the pill
    QMetaObject::invokeMethod(pill, "wake");
    until([&] { return pill->opacity() > 0.99; });
    click(find("readingSidewaysButton"));
    until([&] { return controller->horizontalScrolling(); });
    EXPECT_TRUE(controller->horizontalScrolling());
    controller->setHorizontalScrolling(false);
    // Esc: the tools again
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !win("reading").toBool(); });
    EXPECT_FALSE(win("reading").toBool());
    EXPECT_FALSE(canvas->property("readingOnly").toBool());
    EXPECT_TRUE(shown(find("toolbox")));
}

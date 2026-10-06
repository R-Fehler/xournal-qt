/*
 * xournal-qt: the layout for the window's size (qt/docs/adaptive-layout.md) in the real window: the size class at the
 * audit's sizes, the page sidebar beside the page or as a drawer, the choices kept per size class and their reset,
 * the hysteresis and a held pointer, the chrome apart from the window state, and no control of the home screen or the
 * document outside the window.
 *
 * The menus (qt/adaptive-menus): at each size ⋮ (and its submenus), the page menu, the library menu and a card's menu
 * fit in the window, as wide as their entries and clear of their buttons; in the phone classes they are a bottom sheet
 * with drill-in.
 *
 * The tool bar (qt/adaptive-toolbar): ⋮ pinned inside the window and never in a scrolling area; the tools that are
 * never hidden shown (on phones they may be in "more tools"); two rows on a portrait tablet, all tools at 960 px; the
 * place chosen per size class; the colors' and widths' forms by the room; the sidebar's arrow; the view pill inside the
 * window, with the contents button, clear of the reference's pill.
 *
 * The phone chrome (qt/phone-chrome; PhoneChromeTest): in the phone classes the app bar (the library, the title with
 * the tab dots, the tab count, ⋮) instead of the tab strip, the tool dock instead of the tool bar and the pills, the
 * sheets of all tools, the colors and the widths, Zen in a tiny window, presenting's Zen dot, one window
 * on Android and iOS, the library's top without breadcrumbs, the Fold 7 folded and unfolded.
 *
 * What later blocks fix is listed as known (knownOutside, expectLater): the test passes now and is made stricter by
 * the block that fixes it. The walk over all 18 sizes of the audit runs with XQT_UI_ADAPTIVE=1.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QStyleHints>
#include <QImage>
#include <QJSValue>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QTest>
#include <gtest/gtest.h>

#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/ReferenceMode.h"
#include "shell/Thumbnails.h"
#include "shell/TabManager.h"
#include "shell/ToolboxModel.h"
#include "session/DocumentSession.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "model/Layer.h"

#include "AdaptiveLayout.h"
#include "AppController.h"
#include "LayoutWalk.h"
#include "config-test.h"

namespace fs = std::filesystem;
using xqt::SizeClass;
using xqt::uitest::WindowSize;

// --- the classes, without a window ---------------------------------------------------------------------------------

TEST(SizeClasses, theSizesOfTheAudit) {
    const std::map<std::string, std::string> expected{
            {"desktop-fhd", "desktopWide"},          {"laptop", "desktopWide"},
            {"laptop-16x10", "desktopWide"},         {"small-desktop", "desktopNarrow"},
            {"tiny-desktop", "desktopNarrow"},       {"narrow-tall", "desktopNarrow"},
            {"phone-portrait", "phonePortrait"},     {"phone-landscape", "phoneShort"},
            {"fold7-inner", "tabletPortrait"},       {"short-wide", "phoneShort"},
            {"surface-200-portrait", "tabletPortrait"}, {"surface-200-landscape", "desktopWide"},
            {"surface-150-portrait", "tabletPortrait"}, {"surface-150-landscape", "desktopWide"},
            {"2in1-150-portrait", "tabletPortrait"}, {"2in1-125-portrait", "tabletPortrait"},
            {"2in1-landscape", "desktopWide"},       {"2in1-125-landscape", "desktopWide"},
    };
    for (const WindowSize& s: xqt::uitest::auditSizes) {
        EXPECT_EQ(xqt::AdaptiveLayout::classify(s.w, s.h).toStdString(), expected.at(s.label))
                << s.label << " " << s.w << "x" << s.h;
    }
}

TEST(SizeClasses, theEdgesInTheirOrder) {
    auto cls = [](double w, double h) { return xqt::AdaptiveLayout::classify(w, h).toStdString(); };
    EXPECT_EQ(cls(359, 800), "tiny") << "tiny first";
    EXPECT_EQ(cls(800, 359), "tiny");
    EXPECT_EQ(cls(599, 1000), "phonePortrait") << "phones before tablets";
    EXPECT_EQ(cls(500, 500), "phonePortrait") << "narrow before short";
    EXPECT_EQ(cls(1280, 559), "phoneShort");
    EXPECT_EQ(cls(600, 1000), "tabletPortrait");
    EXPECT_EQ(cls(1280, 1872), "tabletPortrait") << "a Surface Pro in portrait at 150 %";
    EXPECT_EQ(cls(1281, 1900), "desktopWide") << "a monitor turned upright";
    EXPECT_EQ(cls(1000, 1000), "desktopNarrow") << "square: no portrait";
    EXPECT_EQ(cls(840, 600), "desktopNarrow");
    EXPECT_EQ(cls(700, 800), "desktopNarrow") << "what no class takes";
    EXPECT_EQ(cls(1280, 560), "desktopWide");
}

TEST(SizeClasses, aHysteresisOf32Pixels) {
    using xqt::adaptive::classAfter;
    EXPECT_EQ(classAfter(SizeClass::DesktopWide, 1260, 800), SizeClass::DesktopWide) << "20 px past the edge: stays";
    EXPECT_EQ(classAfter(SizeClass::DesktopWide, 1240, 800), SizeClass::DesktopNarrow) << "40 px: changes";
    EXPECT_EQ(classAfter(SizeClass::DesktopNarrow, 1300, 800), SizeClass::DesktopNarrow) << "and back the same";
    EXPECT_EQ(classAfter(SizeClass::DesktopNarrow, 1320, 800), SizeClass::DesktopWide);
    EXPECT_EQ(classAfter(SizeClass::PhonePortrait, 620, 900), SizeClass::PhonePortrait);
    EXPECT_EQ(classAfter(SizeClass::PhonePortrait, 640, 900), SizeClass::TabletPortrait);
    EXPECT_EQ(classAfter(SizeClass::PhoneShort, 915, 580), SizeClass::PhoneShort);
    EXPECT_EQ(classAfter(SizeClass::PhoneShort, 915, 600), SizeClass::DesktopNarrow);
    EXPECT_EQ(classAfter(SizeClass::TabletPortrait, 412, 915), SizeClass::PhonePortrait) << "a big step: at once";
}

// --- the real window ------------------------------------------------------------------------------------------------

namespace {

/// An item outside the window that a later block brings in: at these sizes ("" all of them), on this screen
struct Known {
    const char* screen;  ///< "home", "doc"
    const char* item;    ///< its label in the walk (LayoutWalk.h), or the start of it
    const char* block;   ///< the block that fixes it (qt/docs/ui-adaptive-audit.md, "Implementation blocks")
};
const Known knownOutside[] = {
        // (the view pill stays inside the window since qt/adaptive-toolbar, the library header since qt/adaptive-home)
};
/// A folder deep down in the library: its breadcrumbs are too long for a phone
const char* const deepFolder = "Physics/Semester 3 (winter)/Quantum mechanics/Exercise sheets";

class AdaptiveLayoutTest: public ::testing::Test {
protected:
    void SetUp() override {
        if (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()).rfind("allSizes", 0) == 0 &&
            qEnvironmentVariableIsEmpty("XQT_UI_ADAPTIVE")) {
            GTEST_SKIP() << "set XQT_UI_ADAPTIVE=1 (all 18 sizes)";
        }
        if (std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) == "toolBarPictures" &&
            qEnvironmentVariableIsEmpty("XQT_TOOLBAR_SHOTS")) {
            GTEST_SKIP() << "set XQT_TOOLBAR_SHOTS=<folder> (pictures of the tool bar)";
        }
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/pages.xopp")), root / "pages.xopp");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/pages.xopp.bg_1.png")), root / "pages.xopp.bg_1.png");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/strokes.xopp")), root / "notes.xopp");
        fs::copy_file(fs::path(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf")), root / "lecture.pdf");
        fs::create_directories(root / deepFolder);

        controller = std::make_unique<AppController>();
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        settings = controller->settingsModel();
        QMetaObject::invokeMethod(settings, "resetLayoutChoices");  // (the tests of a run share the config)
        QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "adaptiveLayout"), Q_ARG(QVariant, true));
        QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "auto"));
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
        QTest::mouseMove(window, QPoint(-20, -20));
        adaptive = window->property("adaptive").value<QObject*>();
        ASSERT_NE(adaptive, nullptr);
        wait(100);
    }
    void TearDown() override {
        if (settings) {
            QMetaObject::invokeMethod(settings, "resetLayoutChoices");
        }
        if (controller) {
            controller->shutdown();
        }
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
    void until(const std::function<bool()>& done, int ms = 3000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
    }
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
    QPoint centerOf(QQuickItem* item) const {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    }
    void click(QQuickItem* item) {
        ASSERT_NE(item, nullptr);
        // (moved there first: a tool tip of what the pointer rested on, e.g. the home tab, may lie over the item
        // until it has faded out)
        QTest::mouseMove(window, centerOf(item));
        until([&] {
            const auto popups = engine->findChildren<QObject*>();
            return std::none_of(popups.begin(), popups.end(), [](QObject* o) {
                return o->inherits("QQuickToolTip") && o->property("visible").toBool();
            });
        });
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(item));
        wait(60);
    }
    void resize(int w, int h) {
        window->resize(w, h);
        until([&] { return window->width() == w && window->height() == h; });
        wait(150);  // (the bindings and the layout follow)
    }
    QString sizeClass() const { return adaptive->property("sizeClass").toString(); }
    /// The compact chrome (full screen's: the tab dots, the floating toolbox, the view pill) in the window as it is:
    /// full screen, then the window back at its size. (The chrome chosen for a size class, Settings → Display →
    /// "Controls at this size", is gone since 0.8.0; the tests still look at the compact chrome at every size.)
    void compactChrome(bool on) {
        const QSize size = window->size();
        window->setProperty("fullScreenMode", on);
        if (on) {
            window->setProperty("windowFullScreen", false);
        }
        resize(size.width(), size.height());
    }
    bool flag(const char* name) const { return window->property(name).toBool(); }
    QString choice(const char* sizeClass, const char* what) const {
        QString v;
        QMetaObject::invokeMethod(settings, "layoutChoice", Q_RETURN_ARG(QString, v), Q_ARG(QString, sizeClass),
                                  Q_ARG(QString, what));
        return v;
    }
    void openDocument() {
        ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "pages.xopp").string())));
        until([&] { return !controller->homeVisible(); });
        wait(300);
    }
    /// What should hold and does not yet: a later block makes it true (then this says so, and the test is to be
    /// made stricter there)
    static void expectLater(bool holds, const std::string& what, const char* block) {
        if (holds) {
            std::cerr << "[ NOW HOLDS ] " << what << " (was left to " << block << ": make it an EXPECT)\n";
        } else {
            std::cerr << "[ KNOWN     ] " << what << " - TODO " << block << "\n";
        }
    }
    /// No control of this screen outside the window, except those a later block fixes (knownOutside)
    void expectInside(const char* screen) {
        const auto f = xqt::uitest::walkLayout(window);
        const QString size = QString("%1x%2").arg(window->width()).arg(window->height());
        if (!qEnvironmentVariableIsEmpty("XQT_UI_ADAPTIVE")) {  // (the full walk: what else it found, to look at)
            std::cerr << "[ WALK      ] " << screen << " " << size.toStdString() << " "
                      << sizeClass().toStdString() << ": outside [" << f.outside.join(' ').toStdString()
                      << "] hidden by scrolling [" << f.hidden.join(' ').toStdString() << "] small ["
                      << f.small.join(' ').toStdString() << "]\n";
        }
        for (const QString& name: f.outsideNames()) {
            const Known* known = nullptr;
            for (const Known& k: knownOutside) {
                if (screen == std::string(k.screen) && name.startsWith(QLatin1String(k.item))) {
                    known = &k;
                }
            }
            if (known) {
                expectLater(false, QString("%1 %2: %3 outside the window").arg(screen, size, name).toStdString(),
                            known->block);
            } else {
                ADD_FAILURE() << screen << " " << size.toStdString() << ": " << name.toStdString()
                              << " lies outside the window (" << f.outside.join(' ').toStdString() << ")";
            }
        }
    }
    /// The tool bar's ⋮: shown without scrolling the bar
    bool moreButtonShown() {
        QQuickItem* more = findItem("moreButton");
        if (!more || !more->isVisible()) {
            return false;
        }
        QQuickItem* flick = more->parentItem();
        while (flick && !flick->inherits("QQuickFlickable")) {
            flick = flick->parentItem();
        }
        const QRectF r = more->mapRectToScene(QRectF(0, 0, more->width(), more->height()));
        const QRectF view = flick ? flick->mapRectToScene(QRectF(0, 0, flick->width(), flick->height())) : r;
        return view.contains(r) && QRectF(0, 0, window->width(), window->height()).contains(r);
    }
    // --- the tool bar (qt/adaptive-toolbar) ---
    QQuickItem* named(const char* name) const { return window->findChild<QQuickItem*>(name); }
    /// The plan the tool bar is laid out by (ToolBarPlan.js)
    QVariantMap toolPlan() const {
        const QVariant v = named("toolArea")->property("plan");
        return v.canConvert<QJSValue>() ? v.value<QJSValue>().toVariant().toMap() : v.toMap();
    }
    QStringList overflowNames() const {
        const QVariant v = named("toolArea")->property("overflowNames");
        return v.canConvert<QJSValue>() ? v.value<QJSValue>().toVariant().toStringList() : v.toStringList();
    }
    /// A button of the tool bar in "more tools"
    static bool inOverflow(QQuickItem* button) {
        return button && button->parentItem() && button->parentItem()->objectName() == "overflowContent";
    }
    static bool inScrollingArea(QQuickItem* item) {
        for (QQuickItem* p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
            if (p->inherits("QQuickFlickable")) {
                return true;
            }
        }
        return false;
    }
    // --- the toolbox (qt/docs/toolbox.md) ---
    /// The id of the toolbox's `n`th entry of a type ("pen", "highlighter", "eraser", "text", "sticky", …)
    QString entryOf(const QString& type, int n = 0) const {
        for (const QVariant& v: controller->toolboxModel()->tools()) {
            if (v.toMap().value("type") == type && n-- == 0) {
                return v.toMap().value("id").toString();
            }
        }
        return {};
    }
    QQuickItem* toolEntry(const QString& id) const { return findItem(("toolEntry_" + id).toUtf8().constData()); }
    /// A tool of the rail: on it, in sight or scrolled out of it (it scrolls, nothing folds; qt/docs/toolbox.md, "A rail
    /// that scrolls")
    bool onTheRail(QQuickItem* button) const {
        auto* box = named("toolbox");
        return button && box && button->isVisible() && box->isAncestorOf(button);
    }
    // --- the phone chrome (qt/phone-chrome) ---
    bool phoneChrome() const { return window->property("phoneChrome").toBool(); }
    /// The phone chrome instead of the command bar: the app bar at the top (the library, the title, the tab count, ⋮;
    /// no tab strip), the dock at the bottom or the side holding the toolbox (undo, redo, the first tools that fit with
    /// the one in hand among them, "My tools", the page number; above the bottom safe area), no view pill; every tool in
    /// the sheet "My tools"
    void checkPhoneChrome(const std::string& at) {
        auto* bar = named("phoneAppBar");
        ASSERT_NE(bar, nullptr);
        EXPECT_TRUE(bar->isVisible()) << at << ": the app bar";
        EXPECT_FALSE(named("tabStrip")->isVisible()) << at << ": no tab strip";
        EXPECT_FALSE(named("topTools")->isVisible()) << at << ": no command bar";
        EXPECT_FALSE(named("viewPill")->isVisible()) << at << ": the dock has the view pill's buttons";
        const QRectF barRect = sceneRect(bar);
        EXPECT_NEAR(barRect.top(), 0, 1) << at << ": at the top";
        EXPECT_LE(barRect.height(), 48 + window->property("safeTop").toDouble() + 1) << at << ": slim";
        for (const char* name: {"phoneHomeButton", "phoneTitle", "phoneTabCount", "moreButton"}) {
            auto* item = findItem(name);
            ASSERT_NE(item, nullptr) << name;
            EXPECT_TRUE(shownInWindow(item)) << at << ": " << name;
            EXPECT_TRUE(barRect.adjusted(-1, -1, 1, 1).contains(sceneRect(item))) << at << ": " << name << " in the app bar "
                    << sceneRect(item).x() << "," << sceneRect(item).y() << " " << sceneRect(item).width() << "x"
                    << sceneRect(item).height() << " bar " << barRect.width() << "x" << barRect.height();
        }
        EXPECT_FALSE(inScrollingArea(named("moreButton"))) << at;
        auto* dock = named("phoneDock");
        ASSERT_NE(dock, nullptr);
        EXPECT_TRUE(dock->isVisible()) << at << ": the dock";
        auto* box = named("toolbox");
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(dock->isAncestorOf(box)) << at << ": the toolbox in the dock";
        const double safeBottom = window->property("safeBottom").toDouble();
        const QString inHand = controller->toolboxModel()->active();
        for (const QString& name: {QString("toolboxUndoButton"), QString("toolboxRedoButton"), QString("toolboxAllButton"),
                                   QString("toolboxPageButton"), "toolEntry_" + inHand}) {
            auto* item = findItem(name.toUtf8().constData());
            ASSERT_NE(item, nullptr) << at << ": " << name.toStdString();
            EXPECT_TRUE(shownInWindow(item)) << at << ": " << name.toStdString();
            EXPECT_TRUE(sceneRect(dock).adjusted(-1, -1, 1, 1).contains(sceneRect(item)))
                    << at << ": " << name.toStdString() << " in the dock";
            EXPECT_LE(sceneRect(item).bottom(), window->height() - safeBottom + 0.5)
                    << at << ": " << name.toStdString() << " above the bottom safe area";
        }
        EXPECT_FALSE(named("moreToolsButton")->isVisible()) << at << ": no \"more tools\": the dock's \"My tools\"";
        // Every tool in the sheet "My tools": the user's, then the other tools
        auto* toolSheet = window->findChild<QObject*>("phoneToolSheet");
        ASSERT_NE(toolSheet, nullptr);
        click(findItem("toolboxAllButton"));
        ASSERT_TRUE(opened(toolSheet, true)) << at << ": My tools";
        settled(toolSheet);
        const QRectF r = popupRect(toolSheet);
        EXPECT_TRUE(insideWindow(r)) << at << ": the sheet of all tools";
        EXPECT_NEAR(r.bottom(), window->height(), 1.5) << at << ": at the bottom";
        QStringList cells;
        for (const char* type: {"pen", "highlighter", "eraser", "text", "sticky"}) {
            cells << "sheetEntry_" + entryOf(type);
        }
        cells << "toolCell_hand" << "toolCell_touchDrawing" << "toolCell_select_selectRect" << "toolCell_select_selectRegion"
              << "toolCell_write";
        for (const QString& cell: cells) {
            auto* c = findItem(cell.toUtf8().constData());
            ASSERT_NE(c, nullptr) << at << ": " << cell.toStdString();
            EXPECT_TRUE(c->isVisible()) << at << ": " << cell.toStdString();
            EXPECT_TRUE(r.adjusted(-1, -1, 1, 1).contains(sceneRect(c)) || inScrollingArea(c)) << at << ": " << cell.toStdString();
            EXPECT_FALSE(c->property("name").toString().isEmpty()) << at << ": " << cell.toStdString() << ": its name";
        }
        QTest::keyClick(window, Qt::Key_Escape);
        EXPECT_TRUE(opened(toolSheet, false)) << at;
    }
    /// ⋮ inside the window, not in a scrolling area; the toolbox docked with undo and redo at its head and the tools
    /// that are never hidden on it (on their own, or in a stack of a short rail); the commands in the bar or in "more
    /// tools". The phone chrome: checkPhoneChrome. (The classic tool bar's colors and widths went in 0.8.0.)
    void checkToolBar(const std::string& at) {
        if (phoneChrome()) {
            checkPhoneChrome(at);
            return;
        }
        auto* more = named("moreButton");
        ASSERT_NE(more, nullptr);
        EXPECT_TRUE(shownInWindow(more)) << at << ": ⋮ shown";
        EXPECT_FALSE(inScrollingArea(more)) << at << ": ⋮ outside anything that scrolls";
        auto* box = named("toolbox");
        ASSERT_NE(box, nullptr);
        EXPECT_TRUE(shownInWindow(box)) << at << ": the toolbox";
        EXPECT_FALSE(box->property("floating").toBool()) << at << ": docked";
        for (const char* name: {"handButton", "selectButton", "snipButton", "pdfTextButton"}) {
            EXPECT_TRUE(onTheRail(named(name))) << at << ": " << name << " on the rail";
        }
        // (they left the rail, qt/rail-scroll: the command bar has them, or its "more tools")
        for (const char* name: {"touchDrawingButton", "textModeButton"}) {
            EXPECT_FALSE(onTheRail(named(name))) << at << ": " << name;
            EXPECT_TRUE(named("toolArea")->isAncestorOf(named(name)) || inOverflow(named(name)))
                    << at << ": " << name << " in the command bar";
        }
        for (const char* type: {"pen", "eraser", "text", "sticky"}) {
            EXPECT_TRUE(onTheRail(toolEntry(entryOf(type)))) << at << ": the " << type << " on the rail";
        }
        // Undo and redo lead the toolbox, never in "more tools"; the command bar and the view pill do not repeat them
        // (qt/undo-redo)
        {
            auto* undo = named("toolboxUndoButton");
            auto* redo = named("toolboxRedoButton");
            ASSERT_NE(undo, nullptr);
            ASSERT_NE(redo, nullptr);
            EXPECT_TRUE(shownInWindow(undo)) << at << ": undo at the toolbox's head";
            EXPECT_TRUE(shownInWindow(redo)) << at << ": redo at the toolbox's head";
            EXPECT_TRUE(box->isAncestorOf(undo)) << at;
            EXPECT_FALSE(overflowNames().contains("undo") || overflowNames().contains("redo")) << at;
            EXPECT_FALSE(named("toolUndoButton")->isVisible()) << at << ": not in the command bar too";
            EXPECT_FALSE(named("undoButton")->isVisible()) << at << ": not in the view pill too";
            EXPECT_FALSE(named("redoButton")->isVisible()) << at << ": not in the view pill too";
            EXPECT_TRUE(undo->property("tip").toString().contains("Ctrl+Z")) << at << ": the keys in its tip";
        }
        auto* moreTools = named("moreToolsButton");
        EXPECT_EQ(moreTools->isVisible(), !overflowNames().isEmpty()) << at << ": \"more tools\" when something is in it";
        if (moreTools->isVisible()) {
            EXPECT_TRUE(shownInWindow(moreTools)) << at;
            EXPECT_FALSE(inScrollingArea(moreTools)) << at;
        }
        // The view pill: inside the window
        auto* pill = named("viewPill");
        EXPECT_TRUE(insideWindow(sceneRect(pill))) << at << ": the view pill inside the window";
    }
    /// `layout`: the class, the sidebar, nothing outside the window; `menus`: the menus fit (checkMoreMenu, …)
    void checkSizes(const std::vector<WindowSize>& sizes, bool layout, bool menus);

    // --- menus (qt/adaptive-menus) ---
    bool phoneClass() const {
        const QString c = adaptive->property("layoutClass").toString();
        return c == "phonePortrait" || c == "phoneShort" || c == "tiny";
    }
    QRectF sceneRect(QQuickItem* item) const {
        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
    }
    /// A popup's rectangle in the window (its item: the parent of its content item)
    QRectF popupRect(QObject* popup) const {
        auto* content = popup->property("contentItem").value<QQuickItem*>();
        QQuickItem* item = content ? content->parentItem() : nullptr;
        return item ? sceneRect(item) : QRectF();
    }
    bool insideWindow(const QRectF& r) const {
        return r.isValid() && QRectF(0, 0, window->width(), window->height()).contains(r.adjusted(1, 1, -1, -1));
    }
    bool opened(QObject* popup, bool open) {
        until([&] { return popup->property("opened").toBool() == open && popup->property("visible").toBool() == open; });
        return popup->property("opened").toBool() == open;
    }
    QObject* sheet() const { return window->findChild<QObject*>("menuSheet"); }
    /// The keys go to the menu sheet (its Esc and back: a level up)
    bool sheetHasTheKeys() const {
        auto* content = sheet()->property("contentItem").value<QQuickItem*>();
        QQuickItem* focus = window->activeFocusItem();
        return content && focus && (focus == content || content->isAncestorOf(focus));
    }
    /// Until a popup's enter transition is over (a menu grows in, a sheet slides in): it is where it stays
    void settled(QObject* popup) {
        until([&] {
            return popup->property("scale").toDouble() == 1.0 && popup->property("opacity").toDouble() == 1.0 &&
                   (!popup->property("slide").isValid() || popup->property("slide").toDouble() == 0.0);
        });
    }
    std::vector<QQuickItem*> menuEntries(QObject* menu) const {
        std::vector<QQuickItem*> entries;
        const int n = menu->property("count").toInt();
        for (int i = 0; i < n; ++i) {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, i));
            const QVariant offered = it ? it->property("offered") : QVariant();
            if (it && !it->inherits("QQuickMenuSeparator") && (!offered.isValid() || offered.toBool())) {
                entries.push_back(it);
            }
        }
        return entries;
    }
    /// The entry of `menu` that opens the submenu `name`
    QQuickItem* submenuEntry(QObject* menu, const char* name) const {
        for (QQuickItem* it: menuEntries(menu)) {
            auto* sub = it->property("subMenu").value<QObject*>();
            if (sub && sub->objectName() == name) {
                return it;
            }
        }
        return nullptr;
    }
    /// The rows of the menu sheet, shown now
    std::vector<QQuickItem*> sheetRows() const {
        std::vector<QQuickItem*> rows;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (i->objectName() == "menuSheetRow" && i->isVisible()) {
                rows.push_back(i);
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(sheet()->property("contentItem").value<QQuickItem*>());
        return rows;
    }
    /// The sheet's row for the menu entry `name` (or the entry of the submenu `name`)
    QQuickItem* sheetRow(const char* name) const {
        for (QQuickItem* row: sheetRows()) {
            auto* entry = row->property("menuEntry").value<QObject*>();
            auto* sub = entry ? entry->property("subMenu").value<QObject*>() : nullptr;
            if (entry && (entry->objectName() == name || (sub && sub->objectName() == name))) {
                return row;
            }
        }
        return nullptr;
    }
    /// A button of the window, shown (not scrolled away, inside the window)
    bool shownInWindow(QQuickItem* item) const {
        if (!item || !item->isVisible()) {
            return false;
        }
        const QRectF r = sceneRect(item);
        for (QQuickItem* p = item->parentItem(); p; p = p->parentItem()) {
            if (p->clip() && !sceneRect(p).contains(r.adjusted(1, 1, -1, -1))) {
                return false;
            }
        }
        return insideWindow(r);
    }
    /// The menu `menu` has been opened (from `button`, if it has one): in the desktop classes inside the window, no
    /// taller than it, as wide as its widest entry, and not over its button; in the phone classes the sheet shows it,
    /// at the bottom of the window, with rows as tall as a finger needs and nothing cut off. Closed again afterwards.
    void checkOpenMenu(const std::string& at, QObject* menu, QQuickItem* button = nullptr, bool noScrolling = false) {
        const std::string name = menu->objectName().toStdString();
        if (phoneClass()) {
            QObject* s = sheet();
            ASSERT_NE(s, nullptr);
            ASSERT_TRUE(opened(s, true)) << at << ": " << name << " opens as a sheet";
            EXPECT_EQ(s->property("menu").value<QObject*>(), menu) << at << ": " << name;
            EXPECT_FALSE(menu->property("visible").toBool()) << at << ": " << name << ": the menu itself stays closed";
            settled(s);
            const QRectF r = popupRect(s);
            EXPECT_TRUE(insideWindow(r)) << at << ": " << name << " sheet at " << r.x() << "," << r.y() << " "
                                         << r.width() << "x" << r.height();
            EXPECT_NEAR(r.bottom(), window->height(), 1.5) << at << ": " << name << ": at the bottom";
            EXPECT_LE(r.height(), 0.85 * window->height() + 1) << at << ": " << name;
            const auto rows = sheetRows();
            EXPECT_FALSE(rows.empty()) << at << ": " << name;
            for (QQuickItem* row: rows) {
                const QString text = row->property("text").toString();
                EXPECT_GE(row->height(), 48) << at << ": " << name << ": " << text.toStdString();
                auto* content = row->property("contentItem").value<QQuickItem*>();
                ASSERT_NE(content, nullptr);
                const double needed = content->implicitWidth() + row->property("leftPadding").toDouble() +
                                      row->property("rightPadding").toDouble();
                EXPECT_LE(needed, row->width() + 0.5) << at << ": " << name << ": \"" << text.toStdString()
                                                      << "\" cut off";
            }
            QTest::keyClick(window, Qt::Key_Escape);
            EXPECT_TRUE(opened(s, false)) << at << ": " << name << ": Esc closes the sheet";
            return;
        }
        ASSERT_TRUE(opened(menu, true)) << at << ": " << name;
        EXPECT_FALSE(sheet()->property("visible").toBool()) << at << ": " << name << ": a menu, no sheet";
        settled(menu);
        checkMenuGeometry(at, menu, button, noScrolling);
        QMetaObject::invokeMethod(menu, "close");
        EXPECT_TRUE(opened(menu, false)) << at << ": " << name;
    }
    void checkMenuGeometry(const std::string& at, QObject* menu, QQuickItem* button, bool noScrolling) {
        const std::string name = menu->objectName().toStdString();
        const QRectF r = popupRect(menu);
        EXPECT_TRUE(insideWindow(r)) << at << ": " << name << " at " << r.x() << "," << r.y() << " " << r.width()
                                     << "x" << r.height();
        EXPECT_LE(r.height(), window->height()) << at << ": " << name;
        const double room = menu->property("availableWidth").toDouble();
        for (QQuickItem* it: menuEntries(menu)) {
            EXPECT_LE(it->implicitWidth(), room + 0.5)
                    << at << ": " << name << ": \"" << it->property("text").toString().toStdString() << "\" cut off";
        }
        if (button && shownInWindow(button)) {
            EXPECT_FALSE(r.adjusted(1, 1, -1, -1).intersects(sceneRect(button)))
                    << at << ": " << name << " lies over the button it came from";
        }
        if (noScrolling) {
            auto* list = menu->property("contentItem").value<QQuickItem*>();
            ASSERT_NE(list, nullptr);
            EXPECT_LE(list->property("contentHeight").toDouble(), list->height() + 0.5)
                    << at << ": " << name << " fits without scrolling";
        }
    }
    /// ⋮: at most 10 entries at the top (Help joined in qt/onboarding); each of its submenus fits too (desktop, if
    /// `submenus`)
    void checkMoreMenu(const std::string& at, bool submenus) {
        auto* button = findItem("moreButton");
        auto* more = window->findChild<QObject*>("moreMenu");
        ASSERT_NE(button, nullptr);
        ASSERT_NE(more, nullptr);
        EXPECT_LE(menuEntries(more).size(), 10u) << at << ": ⋮ has at most 10 entries at the top";
        QMetaObject::invokeMethod(button, "clicked");
        checkOpenMenu(at, more, button, sizeClass() == "desktopWide");
        if (phoneClass() || !submenus) {
            return;
        }
        QMetaObject::invokeMethod(button, "clicked");
        ASSERT_TRUE(opened(more, true));
        for (const char* subName: {"moreDocumentMenu", "moreExportMenu", "morePageMenu", "moreViewMenu"}) {
            QQuickItem* entry = submenuEntry(more, subName);
            ASSERT_NE(entry, nullptr) << at << ": " << subName;
            auto* sub = entry->property("subMenu").value<QObject*>();
            QTest::mouseMove(window, centerOf(entry));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(entry));
            ASSERT_TRUE(opened(sub, true)) << at << ": " << subName;
            settled(sub);
            checkMenuGeometry(at, sub, nullptr, false);
            EXPECT_TRUE(more->property("visible").toBool()) << at << ": ⋮ stays open beside " << subName;
        }
        QMetaObject::invokeMethod(more, "dismiss");
        EXPECT_TRUE(opened(more, false));
        QTest::mouseMove(window, QPoint(-20, -20));
    }
    /// The page menu of the sidebar (a popup of its own): inside the window, or a sheet at the bottom on phones
    void checkPageMenu(const std::string& at) {
        QObject* pageMenu = findItem("sidebar")->findChild<QObject*>("pageMenu");
        ASSERT_NE(pageMenu, nullptr);
        QMetaObject::invokeMethod(pageMenu, "openFor", Q_ARG(QVariant, 0),
                                  Q_ARG(QVariant, QVariant::fromValue(static_cast<QObject*>(window->contentItem()))),
                                  Q_ARG(QVariant, window->width() - 20), Q_ARG(QVariant, window->height() - 20));
        ASSERT_TRUE(opened(pageMenu, true)) << at;
        wait(50);
        const QRectF r = popupRect(pageMenu);
        EXPECT_TRUE(insideWindow(r)) << at << ": the page menu at " << r.x() << "," << r.y() << " " << r.width()
                                     << "x" << r.height();
        EXPECT_EQ(pageMenu->property("asSheet").toBool(), phoneClass()) << at;
        if (phoneClass()) {
            EXPECT_NEAR(r.bottom(), window->height(), 1.5) << at << ": the page menu's sheet at the bottom";
        }
        QTest::keyClick(window, Qt::Key_Escape);
        EXPECT_TRUE(opened(pageMenu, false)) << at;
    }
    void checkHomeMenus(const std::string& at) {
        auto* libraryButton = findItem("libraryMenuButton");
        auto* libraryMenu = window->findChild<QObject*>("libraryMenu");
        ASSERT_NE(libraryButton, nullptr);
        ASSERT_NE(libraryMenu, nullptr);
        QMetaObject::invokeMethod(libraryButton, "clicked");
        checkOpenMenu(at, libraryMenu, libraryButton);
        // A card's ⋮ (in a short window the grid is scrolled to show one)
        QQuickItem* cardButton = nullptr;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (!cardButton && i->objectName() == "cardMenuButton" && i->isVisible()) {
                cardButton = i;
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(window->contentItem());
        if (cardButton && !shownInWindow(cardButton)) {
            QQuickItem* flick = cardButton->parentItem();
            while (flick && !flick->inherits("QQuickFlickable")) {
                flick = flick->parentItem();
            }
            auto* content = flick ? flick->property("contentItem").value<QQuickItem*>() : nullptr;
            if (content) {
                const QRectF r = cardButton->mapRectToItem(content, QRectF(0, 0, cardButton->width(), cardButton->height()));
                flick->setProperty("contentY", r.bottom() + 8 - flick->height());
                wait(100);
            }
        }
        ASSERT_NE(cardButton, nullptr) << at << ": a card with its ⋮";
        QMetaObject::invokeMethod(cardButton, "clicked");
        checkOpenMenu(at, window->findChild<QObject*>("homeItemMenu"), cardButton);
        // "+" and View (qt/adaptive-home), where the header groups its actions; on a phone "+" floats at the bottom
        QQuickItem* home = findItem("homeView");
        if (!home->property("expanded").toBool()) {
            QQuickItem* add = findItem(phoneClass() ? "newDocumentFab" : "newDocumentButton");
            ASSERT_NE(add, nullptr);
            EXPECT_TRUE(shownInWindow(add)) << at << ": " << add->objectName().toStdString();
            QMetaObject::invokeMethod(add, "clicked");
            checkOpenMenu(at, window->findChild<QObject*>("newMenu"), add);
            QQuickItem* view = findItem("homeViewButton");
            ASSERT_NE(view, nullptr);
            EXPECT_TRUE(shownInWindow(view)) << at << ": View";
            QMetaObject::invokeMethod(view, "clicked");
            checkOpenMenu(at, window->findChild<QObject*>("homeViewMenu"), view);
        } else {
            for (const char* name: {"newDocumentButton", "importButton", "showButton", "favouritesChip", "zoomInButton",
                                    "homeSettingsButton"}) {
                EXPECT_TRUE(shownInWindow(findItem(name))) << at << ": " << name << " (the expanded header)";
            }
        }
    }
    /// The home screen (qt/adaptive-home): each of its pages, a deep folder with its breadcrumbs (and nothing in it),
    /// a selection with its actions, and the tab overview lie inside the window, and no row of the home screen is
    /// wider than the window; on a phone upright the header is one row, the switch and the search rows of their own
    void checkHomeScreens(const std::string& at);
    /// Nothing of the home screen's column (its rows and bars) wider than the window
    void expectNoRowWider(const std::string& at) {
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (!i->isVisible() || i->inherits("QQuickItemView")) {
                return;  // (the grids scroll their cards)
            }
            if (i->inherits("QQuickLayout") || i->inherits("QQuickFlickable")) {
                const QRectF r = sceneRect(i);
                EXPECT_TRUE(r.left() >= -1 && r.right() <= window->width() + 1)
                        << at << ": " << xqt::uitest::labelOf(i).toStdString() << " from " << r.left() << " to "
                        << r.right() << " (wider than the window)";
            }
            if (i->inherits("QQuickFlickable")) {
                return;  // (what scrolls inside may be wider)
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(findItem("homeView"));
    }

    // --- dialogs (qt/adaptive-dialogs) ---
    /// The items under this one (its visual children, theirs, …)
    static QList<QQuickItem*> itemsUnder(QQuickItem* item) {
        QList<QQuickItem*> all;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            for (QQuickItem* c: i->childItems()) {
                all << c;
                walk(c);
            }
        };
        walk(item);
        return all;
    }
    /// A dialog of the window: its objectName, how it is opened, and its kind (AdaptiveDialog.kind)
    struct DialogCase {
        const char* name;
        const char* method;
        QVariantList args;
        const char* kind;
    };
    static std::vector<DialogCase> documentDialogs(bool all);
    /// ... of the library (the home screen shown), and of the settings (opened over them)
    std::vector<DialogCase> homeDialogs() const;
    static std::vector<DialogCase> settingsDialogs();
    /// Opens each, checks it and closes it again
    void checkDialogCases(const std::vector<DialogCase>& cases, const WindowSize& s);
    QObject* openDialog(const DialogCase& c);
    void closeDialog(QObject* dialog);
    /// The dialog lies inside the window, its confirm button too, and every control of it can be reached: those of the
    /// body once it is scrolled, none wider than the window
    void checkDialog(QObject* dialog, const std::string& at);
    void checkDialogs(const std::vector<WindowSize>& sizes, bool all);

    QTemporaryDir tmp;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
    QObject* adaptive = nullptr;
    QObject* settings = nullptr;
};

// At each size: the class, the sidebar (beside the page only where there is room), and nothing of the home screen and
// the document outside the window; the menus: ⋮ (its submenus at 1280x800 and in the full walk), the page menu, the
// library menu and a card's menu fit in the window, or are a sheet in the phone classes
void AdaptiveLayoutTest::checkSizes(const std::vector<WindowSize>& sizes, bool layout, bool menus) {
    openDocument();
    const bool fullWalk = !qEnvironmentVariableIsEmpty("XQT_UI_ADAPTIVE");
    for (const WindowSize& s: sizes) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        if (layout) {
            EXPECT_EQ(sizeClass(), xqt::AdaptiveLayout::classify(s.w, s.h)) << at;
            const bool room = sizeClass().startsWith("desktop") && s.w >= xqt::adaptive::SIDEBAR_ROOM_PX;
            EXPECT_EQ(flag("sidebarShown"), room) << at << ": the page sidebar beside the page only where there is room";
            EXPECT_EQ(findItem("sidebar")->isVisible(), room) << at;
            expectInside("doc");
            EXPECT_TRUE(moreButtonShown()) << at << ": the tool bar's ⋮ shown without scrolling";
            checkToolBar(at);
            if (s.w == 960 && s.h == 1392) {
                EXPECT_TRUE(overflowNames().isEmpty()) << at << ": all commands shown (" << overflowNames().join(',').toStdString() << ")";
            }
        }
        if (menus) {
            checkMoreMenu(at, fullWalk || (s.w == 1280 && s.h == 800));
            checkPageMenu(at);
        }
        controller->setHomeVisible(true);
        wait(250);
        if (layout) {
            expectInside("home");
            checkHomeScreens(at);
        }
        if (menus) {
            checkHomeMenus(at);
        }
        controller->setHomeVisible(false);
        wait(150);
    }
}

void AdaptiveLayoutTest::checkHomeScreens(const std::string& at) {
    QQuickItem* home = findItem("homeView");
    QObject* library = controller->libraryModel();
    const bool phone = phoneClass();
    for (int page: {1, 2, 0}) {
        home->setProperty("page", page);
        wait(120);
        expectInside("home");
        expectNoRowWider(at + " page " + std::to_string(page));
    }
    if (phone) {
        EXPECT_TRUE(shownInWindow(findItem("newDocumentFab"))) << at << ": \"+\" floats at the bottom";
        EXPECT_FALSE(findItem("newDocumentButton")->isVisible()) << at << ": (not in the header too)";
    }
    // Wherever the header groups its actions: the switch is the library's name, its ▾ and three icons (Recent,
    // Favourites, Bookmarks), all in the header's row with View and Settings
    const char* const headerRow[] = {"libraryPageButton", "libraryMenuButton", "recentPageButton", "favouritesChip",
                                     "bookmarksPageButton", "homeViewButton", "homeSettingsButton"};
    if (!home->property("expanded").toBool()) {
        auto* header = findItem("homeHeader");
        EXPECT_FALSE(header->property("interactive").toBool()) << at << ": the header fits without scrolling";
        EXPECT_LE(header->height(), 56) << at << ": the header is one row";
        const double row = sceneRect(findItem("homeViewButton")).center().y();
        for (const char* name: headerRow) {
            auto* b = findItem(name);
            EXPECT_TRUE(shownInWindow(b)) << at << ": " << name;
            EXPECT_NEAR(sceneRect(b).center().y(), row, 4) << at << ": " << name << " in the header's row";
        }
        for (const char* name: {"recentPageButton", "favouritesChip", "bookmarksPageButton"}) {
            EXPECT_TRUE(findItem(name)->property("iconOnly").toBool()) << at << ": " << name << " as its icon";
        }
        EXPECT_GE(findItem("libraryPageButton")->width(), 80) << at << ": room for the library's name";
    }
    if (sizeClass() == "phonePortrait") {
        EXPECT_GT(sceneRect(findItem("librarySearchField")).top(), sceneRect(findItem("homeHeader")).bottom() - 1)
                << at << ": the search in a row of its own below";
    }
    if (sizeClass() == "phoneShort") {
        // Held sideways: one header row with the search (and the breadcrumbs, below) in it
        const double row = sceneRect(findItem("homeViewButton")).center().y();
        EXPECT_NEAR(sceneRect(findItem("librarySearchField")).center().y(), row, 4) << at << ": the search in the header";
    }
    // At the library's top no breadcrumbs: they would only repeat its name (qt/phone-chrome)
    EXPECT_FALSE(findItem("crumbArea")->isVisible()) << at << ": no breadcrumbs at the library's top";
    EXPECT_FALSE(findItem("crumbRow")->isVisible()) << at << ": no row for them";

    // A folder deep down (and empty): the breadcrumbs elide from the middle, the last one shows
    library->setProperty("folder", deepFolder);
    wait(250);
    expectInside("homeDeep");
    expectNoRowWider(at + " deep folder");
    QQuickItem* crumbArea = findItem("crumbArea");
    ASSERT_NE(crumbArea, nullptr);
    const QRectF area = sceneRect(crumbArea);
    EXPECT_TRUE(insideWindow(area)) << at << ": the breadcrumbs";
    QQuickItem* last = nullptr;
    int crumbsShown = 0;
    for (QQuickItem* i: itemsUnder(crumbArea)) {
        if (i->objectName() == "crumbLabel" && i->isVisible()) {
            ++crumbsShown;
            last = i;
        }
    }
    ASSERT_NE(last, nullptr) << at;
    EXPECT_TRUE(crumbArea->isVisible()) << at << ": the breadcrumbs in a folder";
    if (sizeClass() == "phoneShort") {
        EXPECT_NEAR(area.center().y(), sceneRect(findItem("homeViewButton")).center().y(), 4)
                << at << ": held sideways the breadcrumbs are in the header";
    }
    EXPECT_EQ(last->property("text").toString(), "Exercise sheets") << at << ": the folder shown";
    EXPECT_TRUE(area.adjusted(-1, -1, 1, 1).contains(sceneRect(last))) << at << ": the last breadcrumb is not cut off";
    EXPECT_GE(last->width(), 40) << at;
    QQuickItem* ellipsis = nullptr;
    for (QQuickItem* i: itemsUnder(crumbArea)) {
        if (i->objectName() == "crumbEllipsis" && i->isVisible()) {
            ellipsis = i;
        }
    }
    if (phone) {
        EXPECT_NE(ellipsis, nullptr) << at << ": the folders between are left out (…)";
        EXPECT_LT(crumbsShown, 5) << at;
    }
    if (ellipsis) {
        QMetaObject::invokeMethod(ellipsis, "clicked");
        QObject* crumbMenu = window->findChild<QObject*>("crumbMenu");
        checkOpenMenu(at, crumbMenu, ellipsis);
    }
    library->setProperty("folder", "");
    wait(150);

    // Everything selected: the selection bar, on a phone the actions at the bottom
    QMetaObject::invokeMethod(library, "selectAll");
    wait(150);
    expectInside("homeSelection");
    expectNoRowWider(at + " selection");
    EXPECT_TRUE(shownInWindow(findItem("homeSelectionBar"))) << at;
    auto* actions = findItem("selectionActionBar");
    if (phone) {
        EXPECT_TRUE(actions->isVisible()) << at << ": the actions in a bar at the bottom";
        EXPECT_NEAR(sceneRect(actions).bottom(), window->height(), 1.5) << at;
        for (const char* name: {"selectionOpenAction", "selectionCopyAction", "selectionMoveAction",
                                "selectionTrashAction", "selectionMoreButton"}) {
            EXPECT_TRUE(shownInWindow(findItem(name))) << at << ": " << name;
        }
        EXPECT_FALSE(findItem("newDocumentFab")->isVisible()) << at << ": no \"+\" while selecting";
    }
    if (!actions->isVisible()) {
        for (const char* name: {"openSelectedButton", "copySelectedButton", "moveSelectedButton", "trashSelectedButton"}) {
            EXPECT_TRUE(shownInWindow(findItem(name))) << at << ": " << name;
        }
    }
    QMetaObject::invokeMethod(library, "clearSelection");
    wait(100);

    // The tab overview (with the documents open)
    QObject* overview = window->findChild<QObject*>("tabOverview");
    QQmlExpression(qmlContext(overview), overview, "enter = null; exit = null").evaluate();
    QMetaObject::invokeMethod(overview, "open");
    ASSERT_TRUE(opened(overview, true)) << at;
    wait(150);
    expectInside("overview");
    auto* grid = findItem("tabGrid");
    ASSERT_NE(grid, nullptr);
    EXPECT_TRUE(insideWindow(sceneRect(grid))) << at << ": the overview's grid";
    EXPECT_TRUE(shownInWindow(findItem("overviewSearchField"))) << at;
    if (phone) {
        EXPECT_GE(grid->property("columns").toInt(), 2) << at << ": two documents side by side at least";
        EXPECT_GE(grid->property("cellWidth").toDouble(), 170) << at;
    }
    if (window->width() < 600) {
        EXPECT_GT(sceneRect(findItem("overviewSearchField")).top(), sceneRect(findItem("closeAllButton")).bottom() - 1)
                << at << ": the search in a row of its own below the buttons";
    }
    QMetaObject::invokeMethod(overview, "close");
    EXPECT_TRUE(opened(overview, false)) << at;
}

// --- dialogs (qt/adaptive-dialogs) ---------------------------------------------------------------------------------

std::vector<AdaptiveLayoutTest::DialogCase> AdaptiveLayoutTest::documentDialogs(bool all) {
    const QVariant firstPage = QVariantList{0};
    const QString longUrl("https://www.example.org/search/a/rather/long/address?q=adaptive+dialogs&hl=en&num=20");
    std::vector<DialogCase> list{
            {"insertPagesDialog", "openAt", {1}, "form"},  // (731 px high on a desktop: F13.1)
            {"pageSizeDialog", "openFor", {firstPage}, "form"},
            {"newDocumentDialog", "open", {}, "form"},
            {"renameDocumentDialog", "openFor", {0}, "form"},
            {"shareDialog", "openFor", {QString()}, "question"},
            {"unsavedDialog", "open", {}, "question"},
            {"webConfirm", "ask", {longUrl, QString("Search the web")}, "question"},
            {"messageDialog", "open", {}, "card"},
            {"introDialog", "show", {}, "form"},  // (the first start: qt/docs/onboarding.md)
    };
    if (all) {
        const std::vector<DialogCase> more{
                {"backgroundDialog", "openFor", {firstPage}, "form"},
                {"noteSpaceDialog", "openFor", {firstPage, false}, "form"},
                {"printDialog", "openFor", {QVariant(QVariantList{0, 1})}, "form"},
                {"chapterDialog", "openFor", {0}, "form"},
                {"bookmarkDialog", "openFor", {0}, "form"},
                {"documentModeDialog", "open", {}, "form"},
                {"archiveDialog", "openFor", {QString()}, "form"},
                {"backlinksDialog", "show", {}, "form"},
                {"findPaperSheet", "openFor", {QString("A. Vaswani et al. Attention is all you need. NIPS 2017.")}, "form"},
                {"arxivSheet", "openSearch", {QString("Attention is all you need")}, "form"},
                {"shortcutSheet", "open", {}, "form"},
                {"searchFuzzyHelp", "open", {}, "form"},
                {"unusedImagesDialog", "show", {}, "form"},
                {"oldXoppDialog", "ask", {QString("notes.xopp"), QUrl("file:///tmp/notes.pdf"), QVariant()}, "question"},
                {"recoveryDialog", "open", {}, "question"},
                {"closeAllDialog", "open", {}, "question"},
                {"shareXoppDialog", "open", {}, "question"},
                {"externalSaveDialog", "open", {}, "question"},
                {"textChangedDialog", "open", {}, "question"},
                {"hybridEditedDialog", "open", {}, "question"},
                {"linkFoundDialog", "open", {}, "question"},
                {"linkMissingDialog", "open", {}, "question"},
                {"editAnywayDialog", "open", {}, "question"},
                {"markdownReplaceDialog", "open", {}, "question"},
                {"webImageConfirm", "ask", {longUrl, QString("www.example.org"), QString("ask")}, "question"},
                {"archiveReportDialog", "open", {}, "card"},
        };
        list.insert(list.end(), more.begin(), more.end());
    }
    return list;
}

std::vector<AdaptiveLayoutTest::DialogCase> AdaptiveLayoutTest::homeDialogs() const {
    return {
            {"renameDialog", "open", {}, "form"},
            {"folderNameDialog", "open", {}, "form"},
            {"textFileDialog", "open", {}, "form"},
            {"transferDialog", "open", {}, "form"},
            {"libraryArchiveDialog", "open", {}, "form"},
            {"conflictDialog", "open", {}, "form"},
            {"folderChooser", "openAt", {QString::fromStdString(root.string())}, "form"},
            {"trashDialog", "open", {}, "question"},
            {"storageAccessDialog", "ask", {QString()}, "question"},
            {"librariesHomeDialog", "open", {}, "question"},
            {"temporaryImportDialog", "open", {}, "question"},
    };
}

std::vector<AdaptiveLayoutTest::DialogCase> AdaptiveLayoutTest::settingsDialogs() {
    return {
            {"removeCachesDialog", "open", {}, "question"},
            {"intoPdfExplanation", "open", {}, "card"},
            {"shortcutCapture", "open", {}, "card"},
    };
}

QObject* AdaptiveLayoutTest::openDialog(const DialogCase& c) {
    QObject* d = window->findChild<QObject*>(c.name);
    if (!d) {
        ADD_FAILURE() << c.name << " not found";
        return nullptr;
    }
    // (no animation: the test waits for it to be open and closed)
    QQmlExpression(qmlContext(d), d, "enter = null; exit = null").evaluate();
    const QVariantList& a = c.args;
    bool ok = false;
    switch (a.size()) {
        case 0: ok = QMetaObject::invokeMethod(d, c.method); break;
        case 1: ok = QMetaObject::invokeMethod(d, c.method, Q_ARG(QVariant, a[0])); break;
        case 2: ok = QMetaObject::invokeMethod(d, c.method, Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1])); break;
        default:
            ok = QMetaObject::invokeMethod(d, c.method, Q_ARG(QVariant, a[0]), Q_ARG(QVariant, a[1]),
                                           Q_ARG(QVariant, a[2]));
    }
    EXPECT_TRUE(ok) << c.name << "." << c.method;
    until([&] { return d->property("opened").toBool(); });
    EXPECT_TRUE(d->property("opened").toBool()) << c.name;
    wait(50);  // (laid out: the layouts are polished before a frame is drawn)
    return d;
}

void AdaptiveLayoutTest::closeDialog(QObject* dialog) {
    QMetaObject::invokeMethod(dialog, "close");
    until([&] { return !dialog->property("visible").toBool(); });
}

void AdaptiveLayoutTest::checkDialog(QObject* dialog, const std::string& at) {
    const std::string name = dialog->objectName().toStdString() + " " + at;
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(content, nullptr) << name;
    QQuickItem* popupItem = content->parentItem();
    ASSERT_NE(popupItem, nullptr) << name;
    auto sceneRect = [](QQuickItem* i) { return i->mapRectToScene(QRectF(0, 0, i->width(), i->height())); };
    const QRectF win = QRectF(0, 0, window->width(), window->height()).adjusted(-1, -1, 1, 1);
    const QRectF box = sceneRect(popupItem);
    EXPECT_TRUE(win.contains(box)) << name << ": the dialog lies outside the window (" << box.x() << "," << box.y()
                                   << " " << box.width() << "x" << box.height() << ")";
    if (auto* confirm = dialog->property("confirmItem").value<QQuickItem*>()) {
        EXPECT_TRUE(confirm->isVisible()) << name << ": its confirm button";
        EXPECT_TRUE(win.contains(sceneRect(confirm))) << name << ": its confirm button lies outside the window";
    }
    // The body scrolled to its end
    auto* body = dialog->property("bodyFlickable").value<QQuickItem*>();
    ASSERT_NE(body, nullptr) << name;
    const double end = std::max(0.0, body->property("contentHeight").toDouble() - body->height());
    body->setProperty("contentY", end);
    wait(20);
    QQuickItem* last = nullptr;  // the control lowest in the body
    double lastBottom = -1;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
        if (!i->isVisible() || i->opacity() <= 0.01) {
            return;
        }
        const bool control = i->inherits("QQuickAbstractButton") || i->inherits("QQuickTextInput") ||
                             i->inherits("QQuickTextEdit") || i->inherits("QQuickComboBox") ||
                             i->inherits("QQuickSpinBox") || i->inherits("QQuickSlider");
        if (control && i->isEnabled() && i->width() > 1 && i->height() > 1) {
            const std::string what = name + ": " + xqt::uitest::labelOf(i).toStdString();
            QQuickItem* f = i->parentItem();  // the scrolling area it is in (the body, or a list in it)
            while (f && f != popupItem && !f->inherits("QQuickFlickable")) {
                f = f->parentItem();
            }
            if (f && f != body && !body->isAncestorOf(f)) {
                f = nullptr;  // (the footer's row of buttons)
            }
            if (f) {
                auto* fc = f->property("contentItem").value<QQuickItem*>();
                const QRectF r = i->mapRectToItem(fc, QRectF(0, 0, i->width(), i->height()));
                const double w = std::max(f->width(), f->property("contentWidth").toDouble());
                const double h = std::max(f->height(), f->property("contentHeight").toDouble());
                EXPECT_GE(r.left(), -1) << what << ": cut at the left";
                EXPECT_LE(r.right(), w + 1) << what << ": wider than the dialog";
                EXPECT_LE(r.bottom(), h + 1) << what << ": below the end of what scrolls";
                EXPECT_TRUE(box.adjusted(-1, -1, 1, 1).contains(sceneRect(f))) << what << ": its scrolling area";
                if (f == body && r.bottom() > lastBottom) {
                    lastBottom = r.bottom();
                    last = i;
                }
            } else {
                EXPECT_TRUE(win.contains(sceneRect(i))) << what << ": outside the window";
            }
        }
        for (QQuickItem* c: i->childItems()) {
            walk(c);
        }
    };
    walk(popupItem);
    if (last) {
        const QRectF view = sceneRect(body).adjusted(-1, -1, 1, 1);
        EXPECT_TRUE(view.contains(sceneRect(last)))
                << name << ": " << xqt::uitest::labelOf(last).toStdString() << " not shown with the body at its end";
    }
    body->setProperty("contentY", 0);
}

void AdaptiveLayoutTest::checkDialogCases(const std::vector<DialogCase>& cases, const WindowSize& s) {
    const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
    const QString cls = sizeClass();
    for (const DialogCase& c: cases) {
        QObject* d = openDialog(c);
        if (!d) {
            continue;
        }
        EXPECT_EQ(d->property("kind").toString(), c.kind) << c.name;
        // Where it goes: a form takes the whole screen on a phone, a question comes up from the bottom in portrait
        const QString kind = c.kind;
        const bool portrait = cls == "phonePortrait";
        const bool shortPhone = cls == "phoneShort" || cls == "tiny";
        const QString expected = kind == "card"                   ? "centered"
                                 : portrait && kind == "question" ? "bottom"
                                 : portrait || shortPhone         ? "fullScreen"
                                                                  : "centered";
        EXPECT_EQ(d->property("placement").toString(), expected) << c.name << " " << at;
        checkDialog(d, at);
        if (const QString dir = qEnvironmentVariable("XQT_UI_DIALOG_SHOTS"); !dir.isEmpty()) {
            // (to look at: XQT_UI_DIALOG_SHOTS=<folder>; the off-screen pictures lack the dialogs' backgrounds)
            window->grabWindow().save(QString("%1/%2-%3.png").arg(dir, c.name, QString::fromStdString(at)));
        }
        if (expected == "fullScreen") {
            auto* item = d->property("contentItem").value<QQuickItem*>()->parentItem();
            EXPECT_NEAR(item->width(), s.w, 1) << c.name << " " << at << ": the whole width";
        }
        closeDialog(d);
    }
}

void AdaptiveLayoutTest::checkDialogs(const std::vector<WindowSize>& sizes, bool all) {
    openDocument();
    const auto cases = documentDialogs(all);
    for (const WindowSize& s: sizes) {
        resize(s.w, s.h);
        checkDialogCases(cases, s);
        if (!all) {
            continue;
        }
        auto* settingsPage = window->findChild<QObject*>("settingsPage");
        QMetaObject::invokeMethod(settingsPage, "open");
        until([&] { return settingsPage->property("opened").toBool(); });
        checkDialogCases(settingsDialogs(), s);
        QMetaObject::invokeMethod(settingsPage, "close");
        until([&] { return !settingsPage->property("visible").toBool(); });
        controller->setHomeVisible(true);
        wait(150);
        checkDialogCases(homeDialogs(), s);
        controller->setHomeVisible(false);
        wait(100);
    }
}

}  // namespace

const std::vector<WindowSize> fiveSizes{{1920, 1080, "desktop-fhd"},
                                        {1280, 800, "laptop-16x10"},
                                        {960, 1392, "surface-200-portrait"},
                                        {412, 915, "phone-portrait"},
                                        {915, 412, "phone-landscape"}};

TEST_F(AdaptiveLayoutTest, classesSidebarAndControlsAtFiveSizes) {
    checkSizes(fiveSizes, true, false);
}

// The menus (qt/adaptive-menus): no menu taller or wider than the window, none of its entries cut off, none over the
// button it came from; ⋮ has at most 12 entries at the top and fits without scrolling in a wide desktop window; in the
// phone classes the menus are a bottom sheet
TEST_F(AdaptiveLayoutTest, menusFitAtFiveSizes) {
    checkSizes(fiveSizes, false, true);
}

// On a phone the menus are a bottom sheet: a submenu drills in (the sheet shows its entries, with a back arrow and its
// title), Esc goes back a level, and a row does what its entry does. A row of controls (the layout menu's columns)
// comes along into the sheet and goes back into the menu. (The tab menu: phones have no tab strip since
// qt/phone-chrome; their recent tabs are a sheet, PhoneChromeTest.)
TEST_F(AdaptiveLayoutTest, menusAreSheetsOnPhones) {
    openDocument();
    resize(412, 915);
    ASSERT_EQ(sizeClass(), "phonePortrait");
    QObject* s = sheet();
    ASSERT_NE(s, nullptr);
    auto* more = window->findChild<QObject*>("moreMenu");
    auto menuShown = [&] {
        auto* m = s->property("menu").value<QObject*>();
        return m ? m->objectName().toStdString() : std::string();
    };

    QMetaObject::invokeMethod(findItem("moreButton"), "clicked");
    ASSERT_TRUE(opened(s, true));
    settled(s);
    auto* back = findItem("menuSheetBack");
    ASSERT_NE(back, nullptr);
    EXPECT_FALSE(back->isVisible()) << "at the top: no way back";
    ASSERT_NE(sheetRow("moreDocumentMenu"), nullptr);
    EXPECT_EQ(sheetRow("renameDocumentItem"), nullptr) << "inside Document";
    click(sheetRow("moreDocumentMenu"));
    EXPECT_EQ(menuShown(), "moreDocumentMenu") << "drilled in";
    EXPECT_NE(sheetRow("renameDocumentItem"), nullptr);
    EXPECT_EQ(sheetRow("shareItem"), nullptr);
    EXPECT_TRUE(back->isVisible());
    EXPECT_EQ(findItem("menuSheetTitle")->property("text").toString(), "Document");
    click(back);
    EXPECT_EQ(s->property("menu").value<QObject*>(), more) << "back at the top";
    EXPECT_NE(sheetRow("shareItem"), nullptr);

    // Esc goes up a level (the phone classes have no toolbox position: their dock; nor "All open documents": the
    // app bar's tab count)
    click(sheetRow("moreViewMenu"));
    EXPECT_EQ(sheetRow("toolboxPositionMenu"), nullptr) << "the dock instead";
    EXPECT_EQ(sheetRow("allDocumentsItem"), nullptr) << "the tab count instead";
    ASSERT_NE(sheetRow("pageLayoutItem"), nullptr);
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_EQ(s->property("menu").value<QObject*>(), more) << "Esc: a level up";
    EXPECT_TRUE(s->property("opened").toBool());

    // A row triggers its entry, and the sheet closes
    click(sheetRow("moreDocumentMenu"));
    click(sheetRow("linkedFromItem"));
    EXPECT_TRUE(opened(s, false));
    auto* backlinks = window->findChild<QObject*>("backlinksDialog");
    ASSERT_NE(backlinks, nullptr);
    until([&] { return backlinks->property("visible").toBool(); });
    EXPECT_TRUE(backlinks->property("visible").toBool()) << "Document → Linked from…";
    QMetaObject::invokeMethod(backlinks, "close");
    until([&] { return !backlinks->property("visible").toBool(); });

    // The layout menu's columns come along, and go back into the menu (in phone portrait the pill has no room for the
    // layout button: ⋮ → View → Page layout)
    auto* layoutButton = named("layoutButton");
    ASSERT_NE(layoutButton, nullptr);
    EXPECT_FALSE(layoutButton->isVisible()) << "not in the view pill of a phone";
    QMetaObject::invokeMethod(findItem("moreButton"), "clicked");
    ASSERT_TRUE(opened(s, true));
    click(sheetRow("moreViewMenu"));
    ASSERT_NE(sheetRow("pageLayoutItem"), nullptr);
    click(sheetRow("pageLayoutItem"));
    ASSERT_TRUE(opened(s, true));
    EXPECT_EQ(menuShown(), "layoutMenu");
    settled(s);
    auto* moreColumns = findItem("moreColumnsButton");
    ASSERT_NE(moreColumns, nullptr);
    EXPECT_TRUE(moreColumns->isVisible()) << "the columns row, in the sheet";
    EXPECT_TRUE(insideWindow(sceneRect(moreColumns)));
    const int columns = controller->property("viewColumns").toInt();
    click(moreColumns);
    EXPECT_EQ(controller->property("viewColumns").toInt(), columns + 1);
    EXPECT_TRUE(s->property("opened").toBool()) << "a row of controls leaves the sheet open";
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(s, false));

    // Back on the desktop: the layout menu is a menu again, with its columns
    resize(1280, 800);
    QMetaObject::invokeMethod(layoutButton, "pressAndHold");
    auto* layoutMenu = window->findChild<QObject*>("layoutMenu");
    ASSERT_TRUE(opened(layoutMenu, true));
    EXPECT_FALSE(s->property("visible").toBool());
    settled(layoutMenu);
    EXPECT_TRUE(moreColumns->isVisible());
    EXPECT_TRUE(popupRect(layoutMenu).contains(sceneRect(moreColumns))) << "the columns row given back to the menu";
    checkMenuGeometry("1280x800", layoutMenu, layoutButton, true);
    QMetaObject::invokeMethod(layoutMenu, "close");
    EXPECT_TRUE(opened(layoutMenu, false));
}

// The library's "+" and View (qt/adaptive-home) on a phone (sheets; "+" floats at the bottom) and in a desktop window
// too narrow for every button (menus in the header): each entry does what it says, and a switch of Show or the size
// of the cards leaves the menu open
TEST_F(AdaptiveLayoutTest, theHomeScreensPlusAndViewMenusWork) {
    QObject* library = controller->libraryModel();
    QQuickItem* home = findItem("homeView");
    ASSERT_NE(home, nullptr);
    for (const WindowSize& s: {WindowSize{412, 915, "phone-portrait"}, WindowSize{1024, 700, "small-desktop"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        const bool phone = phoneClass();
        home->setProperty("page", 0);
        wait(100);
        ASSERT_FALSE(home->property("expanded").toBool()) << at << ": the header groups its actions";
        QObject* newMenu = window->findChild<QObject*>("newMenu");
        QObject* viewMenu = window->findChild<QObject*>("homeViewMenu");
        auto openMenu = [&](QObject* menu, const char* button) {
            // (after an entry is chosen the menu fades out for a moment: a click on its button meanwhile does not
            // open it again, in Qt 6.8)
            until([&] { return !(phone ? sheet() : menu)->property("visible").toBool(); });
            click(findItem(button));
            ASSERT_TRUE(opened(phone ? sheet() : menu, true)) << at << ": " << button;
            settled(phone ? sheet() : menu);
        };
        // An entry of the menu shown (on a phone: its row in the sheet; a submenu drills in)
        auto choose = [&](const char* entry) {
            QQuickItem* target = phone ? sheetRow(entry) : nullptr;
            if (!phone) {
                for (QObject* m: {newMenu, viewMenu, window->findChild<QObject*>("viewSortMenu"),
                                  window->findChild<QObject*>("viewShowMenu")}) {
                    if (!m->property("visible").toBool()) {
                        continue;
                    }
                    for (QQuickItem* it: menuEntries(m)) {
                        auto* sub = it->property("subMenu").value<QObject*>();
                        if (it->objectName() == entry || (sub && sub->objectName() == entry)) {
                            target = it;
                        }
                    }
                }
            }
            ASSERT_NE(target, nullptr) << at << ": " << entry;
            click(target);
            wait(100);
        };
        auto closeMenus = [&] {
            for (int i = 0; i < 3 && (sheet()->property("visible").toBool() || viewMenu->property("visible").toBool() ||
                                      newMenu->property("visible").toBool());
                 ++i) {
                QTest::keyClick(window, Qt::Key_Escape);
                wait(200);
            }
        };
        const char* const add = phone ? "newDocumentFab" : "newDocumentButton";

        // "+": New document, New Markdown file, New text file, New folder, Import files, Import a folder
        struct Entry {
            const char* entry;
            const char* dialog;
        };
        for (const Entry& e: {Entry{"newDocumentItem", "newDocumentDialog"}, Entry{"newMarkdownItem", "textFileDialog"},
                              Entry{"newTextItem", "textFileDialog"}, Entry{"addNewFolderItem", "folderNameDialog"},
                              Entry{"addImportFilesItem", "importDialog"},
                              Entry{"addImportFolderItem", "importFolderDialog"}}) {
            openMenu(newMenu, add);
            choose(e.entry);
            QObject* d = window->findChild<QObject*>(e.dialog);
            ASSERT_NE(d, nullptr) << e.dialog;
            until([&] { return d->property("visible").toBool(); });
            EXPECT_TRUE(d->property("visible").toBool()) << at << ": " << e.entry << " opens " << e.dialog;
            if (std::string(e.entry) == "newTextItem") {
                EXPECT_EQ(d->property("extension").toString(), ".txt") << at;
            }
            QMetaObject::invokeMethod(d, "close");
            until([&] { return !d->property("visible").toBool(); });
            ASSERT_FALSE(d->property("visible").toBool()) << e.dialog;
            // (Qt 6.8's own file and folder dialogs are windows of their own; off-screen nothing gives the keys back
            // to the app's window when one closes, as a window manager does)
            window->requestActivate();
            until([&] { return QGuiApplication::focusWindow() == window; });
            ASSERT_EQ(QGuiApplication::focusWindow(), window) << e.dialog;
            wait(100);
        }

        // The star of the switch: only favourites, on and off; not in View (one place)
        auto* star = findItem("favouritesChip");
        click(star);
        EXPECT_TRUE(library->property("favouritesOnly").toBool()) << at;
        EXPECT_TRUE(star->property("chosen").toBool()) << at << ": marked while on";
        click(star);
        EXPECT_FALSE(library->property("favouritesOnly").toBool()) << at;
        EXPECT_EQ(window->findChild<QObject*>("viewFavouritesItem"), nullptr) << at;

        // View: All documents at once, Sort, Open where left off
        openMenu(viewMenu, "homeViewButton");
        choose("viewFlatItem");
        EXPECT_TRUE(library->property("flat").toBool()) << at;
        EXPECT_TRUE(findItem("homeViewButton")->property("checked").toBool()) << at << ": View marked while it filters";
        openMenu(viewMenu, "homeViewButton");
        choose("viewFlatItem");
        EXPECT_FALSE(library->property("flat").toBool()) << at;
        openMenu(viewMenu, "homeViewButton");
        choose("viewSortMenu");
        if (!phone) {
            ASSERT_TRUE(opened(window->findChild<QObject*>("viewSortMenu"), true)) << at;
        }
        choose("viewSortByRead");
        EXPECT_EQ(library->property("sortBy").toString(), "read") << at;
        library->setProperty("sortBy", "name");
        closeMenus();
        const bool resume = findItem("resumeSwitch")->property("checked").toBool();
        openMenu(viewMenu, "homeViewButton");
        choose("viewResumeItem");
        EXPECT_NE(findItem("resumeSwitch")->property("checked").toBool(), resume) << at << ": Open where left off";
        openMenu(viewMenu, "homeViewButton");
        choose("viewResumeItem");
        EXPECT_EQ(findItem("resumeSwitch")->property("checked").toBool(), resume) << at;

        // View → Show: its switches leave it open; Defaults
        openMenu(viewMenu, "homeViewButton");
        choose("viewShowMenu");
        if (!phone) {
            ASSERT_TRUE(opened(window->findChild<QObject*>("viewShowMenu"), true)) << at;
            settled(window->findChild<QObject*>("viewShowMenu"));
        }
        QQuickItem* notes = findItem("viewShowNotes");
        ASSERT_NE(notes, nullptr) << at;
        EXPECT_TRUE(shownInWindow(notes)) << at << ": the switches of Show";
        click(notes);
        EXPECT_FALSE(library->property("show").toMap().value("notes").toBool()) << at << ": notes hidden";
        EXPECT_TRUE(library->property("showFiltered").toBool()) << at;
        EXPECT_TRUE((phone ? sheet() : window->findChild<QObject*>("viewShowMenu"))->property("visible").toBool())
                << at << ": still open";
        click(findItem("viewShowDefaults"));
        EXPECT_FALSE(library->property("showFiltered").toBool()) << at << ": Defaults";
        if (phone) {
            // (Esc and back go to it, below)
            EXPECT_TRUE(sheetHasTheKeys()) << at << ": Esc and back still reach the sheet";
        }
        closeMenus();

        // View: the size of the cards (the menu stays open)
        auto* grid = findItem("libraryGrid");
        const int columns = grid->property("columns").toInt();
        openMenu(viewMenu, "homeViewButton");
        QQuickItem* smaller = findItem("viewZoomOutButton");
        ASSERT_NE(smaller, nullptr) << at;
        EXPECT_TRUE(shownInWindow(smaller)) << at;
        click(smaller);
        EXPECT_EQ(grid->property("columns").toInt(), columns + 1) << at << ": smaller cards";
        EXPECT_TRUE((phone ? sheet() : viewMenu)->property("visible").toBool()) << at << ": still open";
        click(findItem("viewZoomInButton"));
        EXPECT_EQ(grid->property("columns").toInt(), columns) << at;
        closeMenus();
        if (phone) {
            // (on a phone never fewer than two columns)
            home->setProperty("columnsNormal", 1);
            EXPECT_EQ(grid->property("columns").toInt(), 2) << at;
            home->setProperty("columnsNormal", 0);
        }

        // Recent: "+" also opens a file
        home->setProperty("page", 1);
        wait(100);
        QSignalSpy openFile(home, SIGNAL(openFileRequested()));
        openMenu(newMenu, add);
        choose("addOpenFileItem");
        EXPECT_EQ(openFile.count(), 1) << at << ": Open a file…";
        wait(100);
        click(star);  // (on Recent: the library's favourites)
        EXPECT_EQ(home->property("page").toInt(), 0) << at;
        EXPECT_TRUE(library->property("favouritesOnly").toBool()) << at;
        library->setProperty("favouritesOnly", false);
        for (QObject* o: window->findChildren<QObject*>()) {  // (the window's file dialog)
            if (o->inherits("QQuickFileDialog") && o->property("visible").toBool()) {
                QMetaObject::invokeMethod(o, "close");
            }
        }
        wait(100);
        home->setProperty("page", 0);
        wait(100);
    }
}

// On a phone the tab overview opens from the tab dots of the compact chrome (their middle; with more than 12 documents
// the count "3 / 14" is there instead, in the same place), and it has two columns
TEST_F(AdaptiveLayoutTest, theTabOverviewOpensFromTheTabDotsOnAPhone) {
    openDocument();
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "notes.xopp").string())));
    wait(200);
    resize(412, 915);
    compactChrome(true);
    wait(150);
    auto* dots = findItem("fullScreenTabs");
    ASSERT_NE(dots, nullptr);
    ASSERT_TRUE(dots->isVisible()) << "the tab dots of the compact chrome";
    QObject* overview = window->findChild<QObject*>("tabOverview");
    click(dots);
    ASSERT_TRUE(opened(overview, true)) << "a tap on the dots opens the overview";
    wait(150);
    auto* grid = findItem("tabGrid");
    EXPECT_EQ(grid->property("columns").toInt(), 2);
    expectInside("overview");
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(overview, false));
}

// (skipped in SetUp unless XQT_UI_ADAPTIVE is set: about a minute)
TEST_F(AdaptiveLayoutTest, allSizesOfTheAudit) {
    checkSizes(std::vector<WindowSize>(std::begin(xqt::uitest::auditSizes), std::end(xqt::uitest::auditSizes)), true,
               true);
}

// The sidebar hidden by hand stays hidden in that class only; without room its arrow opens it as a drawer, which a
// picked page or a tap beside it closes, and whose pin keeps it beside the page in that class. Reset: automatic again.
TEST_F(AdaptiveLayoutTest, sidebarChoicesAreKeptPerSizeClass) {
    openDocument();
    resize(1920, 1080);
    ASSERT_EQ(sizeClass(), "desktopWide");
    auto* sidebar = findItem("sidebar");
    auto* pages = findItem("sidebarArrow");  // (the arrow at the sidebar's edge, or at the canvas's left edge)
    ASSERT_NE(sidebar, nullptr);
    ASSERT_NE(pages, nullptr);
    EXPECT_TRUE(sidebar->isVisible());
    click(pages);
    EXPECT_FALSE(sidebar->isVisible()) << "hidden by hand";
    EXPECT_EQ(choice("desktopWide", "sidebar"), "hidden");

    resize(960, 1392);
    ASSERT_EQ(sizeClass(), "tabletPortrait");
    EXPECT_FALSE(sidebar->isVisible()) << "portrait: automatically hidden";
    EXPECT_EQ(choice("tabletPortrait", "sidebar"), "") << "nothing chosen here";
    auto* canvas = findItem("canvas");
    const double canvasX = canvas->mapToScene(QPointF(0, 0)).x();

    // Its button: a drawer over the page
    click(pages);
    EXPECT_TRUE(sidebar->isVisible());
    EXPECT_TRUE(flag("sidebarAsDrawer"));
    EXPECT_DOUBLE_EQ(canvas->mapToScene(QPointF(0, 0)).x(), canvasX) << "over the page, not beside it";
    auto* scrim = findItem("sidebarScrim");
    ASSERT_NE(scrim, nullptr);
    EXPECT_TRUE(scrim->isVisible());
    EXPECT_EQ(choice("tabletPortrait", "sidebar"), "") << "a drawer is for the moment";
    // a tap beside it closes it (and does not draw)
    const auto strokesBefore = controller->property("modified").toBool();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(window->width() - 100, window->height() / 2));
    wait(60);
    EXPECT_FALSE(flag("sidebarShown"));
    until([&] { return !sidebar->isVisible(); });  // (it slides out)
    EXPECT_FALSE(sidebar->isVisible());
    EXPECT_EQ(controller->property("modified").toBool(), strokesBefore) << "the tap did not draw";

    // A page picked in it: shown, and the drawer closes
    click(pages);
    ASSERT_TRUE(sidebar->isVisible());
    auto* list = findItem("sidebarList");
    ASSERT_NE(list, nullptr);
    until([&] { return list->property("count").toInt() > 2; });
    QQuickItem* third = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, third), Q_ARG(int, 2));
    ASSERT_NE(third, nullptr);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(third));
    until([&] { return controller->property("pageNumber").toInt() == 3; });
    EXPECT_EQ(controller->property("pageNumber").toInt(), 3);
    until([&] { return !sidebar->isVisible(); });
    EXPECT_FALSE(sidebar->isVisible()) << "closed after a page was picked";

    // The pin: beside the page in this class from now on
    click(pages);
    click(findItem("sidebarPin"));
    EXPECT_TRUE(sidebar->isVisible());
    EXPECT_FALSE(flag("sidebarAsDrawer"));
    EXPECT_GT(canvas->mapToScene(QPointF(0, 0)).x(), canvasX) << "beside the page";
    EXPECT_EQ(choice("tabletPortrait", "sidebar"), "shown");

    // Each class keeps its own
    resize(1920, 1080);
    EXPECT_FALSE(sidebar->isVisible()) << "the wide window's choice";
    resize(960, 1392);
    EXPECT_TRUE(sidebar->isVisible()) << "the portrait one's";
    resize(412, 915);
    EXPECT_FALSE(sidebar->isVisible()) << "a phone: nothing chosen, hidden";

    // Reset (Settings → Display): automatic everywhere
    auto* reset = findItem("resetLayoutButton");
    if (!reset) {  // (the settings sheet builds its pages when it opens)
        QMetaObject::invokeMethod(window->findChild<QObject*>("settingsPage"), "open");
        until([&] { return (reset = findItem("resetLayoutButton")) != nullptr; });
    }
    ASSERT_NE(reset, nullptr);
    EXPECT_TRUE(reset->isEnabled());
    QMetaObject::invokeMethod(reset, "clicked");
    wait(50);
    EXPECT_FALSE(reset->isEnabled()) << "nothing left to reset";
    EXPECT_EQ(choice("desktopWide", "sidebar"), "");
    EXPECT_EQ(choice("tabletPortrait", "sidebar"), "");
    QMetaObject::invokeMethod(window->findChild<QObject*>("settingsPage"), "close");
    resize(960, 1392);
    EXPECT_FALSE(sidebar->isVisible());
    resize(1920, 1080);
    EXPECT_TRUE(sidebar->isVisible());

    // "Adapt the layout" off: the desktop layout at every size
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "adaptiveLayout"), Q_ARG(QVariant, false));
    resize(960, 1392);
    EXPECT_EQ(sizeClass(), "tabletPortrait") << "the class is still known (Settings shows it)";
    EXPECT_EQ(adaptive->property("layoutClass").toString(), "desktopWide");
    EXPECT_TRUE(sidebar->isVisible());
    EXPECT_FALSE(flag("sidebarAsDrawer"));
}

// A window edge dragged a little past a limit does not change the class; while a pointer is held (a stroke) nothing
// changes, and the change comes once it is let go.
TEST_F(AdaptiveLayoutTest, hysteresisAndAHeldPointer) {
    openDocument();
    resize(1300, 800);
    ASSERT_EQ(sizeClass(), "desktopWide");
    resize(1270, 800);
    EXPECT_EQ(sizeClass(), "desktopWide") << "10 px under 1280: stays";
    resize(1240, 800);
    EXPECT_EQ(sizeClass(), "desktopNarrow");
    resize(1270, 800);
    EXPECT_EQ(sizeClass(), "desktopNarrow") << "back over the limit by less than 32 px: stays";
    resize(1320, 800);
    EXPECT_EQ(sizeClass(), "desktopWide");
    // A jump (the device turned) is no dragged edge: a Surface Pro upright at 150 % is a tablet at once
    resize(1920, 1232);
    resize(1280, 1872);
    EXPECT_EQ(sizeClass(), "tabletPortrait");
    resize(1320, 800);

    // A stroke in progress: the class and the sidebar wait
    auto* canvas = findItem("canvas");
    ASSERT_NE(canvas, nullptr);
    const QPoint p = centerOf(canvas);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, p);
    QTest::mouseMove(window, p + QPoint(30, 10));
    EXPECT_TRUE(adaptive->property("held").toBool());
    resize(960, 1392);
    EXPECT_EQ(sizeClass(), "desktopWide") << "not while the pen is on the page";
    EXPECT_TRUE(findItem("sidebar")->isVisible());
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, p + QPoint(30, 10));
    until([&] { return sizeClass() == "tabletPortrait"; });
    EXPECT_EQ(sizeClass(), "tabletPortrait") << "once it is let go";
    EXPECT_FALSE(findItem("sidebar")->isVisible());
}

// The touch profile: bigger targets after a finger, back after the mouse; the pen changes nothing; the setting wins
TEST_F(AdaptiveLayoutTest, touchProfile) {
    openDocument();
    auto touch = [&] { return adaptive->property("touchProfile").toBool(); };
    auto target = [&] { return adaptive->property("minTarget").toInt(); };
    EXPECT_FALSE(touch());
    EXPECT_EQ(target(), 40);
    auto* switchButton = findItem("sidebarPagesButton");
    ASSERT_NE(switchButton, nullptr);
    EXPECT_LT(switchButton->height(), 40);

    static QPointingDevice* finger = QTest::createTouchDevice(QInputDevice::DeviceType::TouchScreen);
    const QPoint p = centerOf(findItem("canvas"));
    QTest::touchEvent(window, finger).press(0, p);
    QTest::touchEvent(window, finger).release(0, p);
    until([&] { return touch(); });
    EXPECT_TRUE(touch()) << "a finger touched the screen";
    EXPECT_EQ(target(), 48);
    until([&] { return switchButton->height() >= 48; });  // (the layout follows in the next frame)
    EXPECT_GE(switchButton->height(), 48) << "the sidebar's switch sized for a finger";

    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(window->width() - 40, window->height() - 200));
    until([&] { return !touch(); });
    EXPECT_FALSE(touch()) << "the mouse again";

    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "on"));
    wait(20);
    EXPECT_TRUE(touch()) << "the setting: always";
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "off"));
    QTest::touchEvent(window, finger).press(0, p);
    QTest::touchEvent(window, finger).release(0, p);
    wait(50);
    EXPECT_FALSE(touch()) << "the setting: never";
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "auto"));
}

// The chrome is its own thing: full screen (F11) is the compact chrome in a full-screen window, as before; Zen is apart
// from both (qt/zen: the reader chrome and the chrome chosen per size class are gone).
TEST_F(AdaptiveLayoutTest, chromeModeApartFromTheWindowState) {
    openDocument();
    resize(1400, 850);
    auto chrome = [&] { return window->property("chromeMode").toString(); };
    auto* tabStrip = window->findChild<QQuickItem*>("tabStrip");
    auto* square = findItem("toolbox");  // (floating in the compact chrome)
    auto* viewPill = findItem("viewPill");
    ASSERT_NE(square, nullptr);
    ASSERT_NE(viewPill, nullptr);
    EXPECT_EQ(chrome(), "full");
    EXPECT_FALSE(flag("windowFullScreen"));

    QTest::keyClick(window, Qt::Key_F11);
    wait(50);
    EXPECT_TRUE(flag("fullScreenMode"));
    EXPECT_EQ(chrome(), "compact");
    EXPECT_TRUE(flag("windowFullScreen")) << "full screen is the compact chrome in a full-screen window";
    EXPECT_TRUE(square->isVisible());
    EXPECT_TRUE(square->property("floating").toBool());
    EXPECT_FALSE(findItem("sidebar")->isVisible());
    if (tabStrip) {
        EXPECT_FALSE(tabStrip->isVisible());
    }
    // ⋯ of its toolbox ends with the way out of full screen
    click(findItem("toolboxMoreButton"));
    auto* menu = window->findChild<QObject*>("toolboxMoreMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(opened(menu, true));
    QObject* back = nullptr;
    for (QQuickItem* it: menuEntries(menu)) {
        if (it->objectName() == "toolboxLeaveFullScreenItem") {
            back = it;
        }
    }
    ASSERT_NE(back, nullptr);
    EXPECT_TRUE(back->property("text").toString().startsWith("Leave full screen"));
    QMetaObject::invokeMethod(back, "triggered");
    QMetaObject::invokeMethod(menu, "close");
    EXPECT_EQ(chrome(), "full");
    EXPECT_FALSE(flag("windowFullScreen"));
    until([&] { return !menu->property("visible").toBool(); });  // (it fades out: Esc would be its until then)
    ASSERT_FALSE(menu->property("visible").toBool());
    window->showNormal();
    resize(1400, 850);  // (the off-screen "screen" is 800 x 600: full screen made the window that small)
    window->requestActivate();  // (the keys: the window has them again after full screen)
    until([&] { return window->isActive(); });
    EXPECT_EQ(findItem("chromeChoiceRow"), nullptr) << "no chrome chosen per size class any more (0.8.0)";

    // Zen: apart from the chrome and the window's state; nothing around the page
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    wait(50);
    EXPECT_TRUE(flag("zen"));
    EXPECT_EQ(chrome(), "full") << "Zen is no chrome of its own";
    EXPECT_FALSE(flag("fullChrome"));
    EXPECT_TRUE(flag("hudHidden"));
    EXPECT_FALSE(flag("windowFullScreen"));
    EXPECT_FALSE(viewPill->isVisible());
    EXPECT_FALSE(square->isVisible());
    EXPECT_TRUE(findItem("zenDot")->isVisible());
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_FALSE(flag("zen")) << "Esc leaves it";
    EXPECT_TRUE(viewPill->isVisible());

    // Presenting is still full screen, page by page; leaving full screen ends it
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    wait(50);
    EXPECT_TRUE(controller->property("presenting").toBool());
    EXPECT_TRUE(flag("windowFullScreen"));
    EXPECT_EQ(chrome(), "compact");
    window->setProperty("fullScreenMode", false);
    wait(50);
    EXPECT_FALSE(controller->property("presenting").toBool());
    EXPECT_FALSE(flag("windowFullScreen"));
}

// Every dialog fits the window at the five sizes: inside it, its confirm button too, every field and button reachable
// by scrolling the body, nothing wider than the window; a full-screen sheet on a phone (qt/adaptive-dialogs)
TEST_F(AdaptiveLayoutTest, dialogsFitTheWindow) {
    checkDialogs({{1920, 1080, "desktop-fhd"},
                  {1024, 700, "small-desktop"},
                  {1280, 500, "short-wide"},
                  {412, 915, "phone-portrait"},
                  {915, 412, "phone-landscape"}},
                 false);
}

// (skipped in SetUp unless XQT_UI_ADAPTIVE is set) All the dialogs and sheets at all 18 sizes
TEST_F(AdaptiveLayoutTest, allSizesDialogs) {
    checkDialogs(std::vector<WindowSize>(std::begin(xqt::uitest::auditSizes), std::end(xqt::uitest::auditSizes)), true);
}

// A full-screen sheet on a phone: × at the left closes it, the confirm button is at the top right (the footer is
// gone); Esc and Android's back key close a dialog
TEST_F(AdaptiveLayoutTest, aPhoneSheetAndTheBackKey) {
    openDocument();
    resize(412, 915);
    QObject* d = openDialog({"insertPagesDialog", "openAt", {1}, "form"});
    ASSERT_NE(d, nullptr);
    auto* confirm = d->property("confirmItem").value<QQuickItem*>();
    ASSERT_NE(confirm, nullptr);
    EXPECT_EQ(confirm->objectName(), "dialogConfirmButton") << "at the top right";
    EXPECT_EQ(confirm->property("text").toString(), "Insert");
    EXPECT_GT(confirm->mapToScene(QPointF(0, 0)).x(), window->width() / 2.0);
    EXPECT_LT(confirm->mapToScene(QPointF(0, 0)).y(), 80);
    auto* footer = d->property("footer").value<QQuickItem*>();
    ASSERT_NE(footer, nullptr);
    EXPECT_TRUE(footer->opacity() < 0.01 && footer->height() < 1) << "its buttons are in the title row";
    const int pages = controller->pageCount();
    click(confirm);
    until([&] { return !d->property("visible").toBool(); });
    EXPECT_EQ(controller->pageCount(), pages + 1) << "the confirm button at the top inserted the page";

    d = openDialog({"insertPagesDialog", "openAt", {1}, "form"});
    QQuickItem* close = nullptr;
    for (auto* i: itemsUnder(d->property("contentItem").value<QQuickItem*>()->parentItem())) {
        if (i->objectName() == "dialogCloseButton" && i->isVisible()) {
            close = i;
        }
    }
    ASSERT_NE(close, nullptr);
    EXPECT_LT(close->mapToScene(QPointF(0, 0)).x(), 40) << "× at the left";
    click(close);
    until([&] { return !d->property("visible").toBool(); });
    EXPECT_FALSE(d->property("visible").toBool());
    EXPECT_EQ(controller->pageCount(), pages + 1) << "× inserted nothing";

    // Back to a wide window: in the middle, with its footer again
    resize(1280, 800);
    d = openDialog({"insertPagesDialog", "openAt", {1}, "form"});
    EXPECT_EQ(d->property("placement").toString(), "centered");
    EXPECT_TRUE(footer->isVisible() && footer->opacity() > 0.99 && footer->height() > 20);
    QTest::keyClick(window, Qt::Key_Escape);
    until([&] { return !d->property("visible").toBool(); });
    EXPECT_FALSE(d->property("visible").toBool()) << "Esc";
    d = openDialog({"shareDialog", "openFor", {QString()}, "question"});
    QTest::keyClick(window, Qt::Key_Back);
    until([&] { return !d->property("visible").toBool(); });
    EXPECT_FALSE(d->property("visible").toBool()) << "the back key";
}

// Settings: tabs on a desktop; on a phone the whole screen, a list of the sections, each a page with a back arrow;
// sliders keep their width (F12.2)
TEST_F(AdaptiveLayoutTest, settingsOnAPhoneAreAListOfSections) {
    openDocument();
    auto* sheet = window->findChild<QObject*>("settingsPage");
    ASSERT_NE(sheet, nullptr);
    auto popupItem = [&]() -> QQuickItem* { return sheet->property("contentItem").value<QQuickItem*>()->parentItem(); };
    auto visibleIn = [&](const char* name) {
        for (auto* i: itemsUnder(popupItem())) {
            if (i->objectName() == name) {
                return i->isVisible();
            }
        }
        return false;
    };
    resize(1280, 800);
    QMetaObject::invokeMethod(sheet, "open");
    until([&] { return sheet->property("opened").toBool(); });
    EXPECT_TRUE(visibleIn("displayTab")) << "tabs on a desktop";
    EXPECT_FALSE(visibleIn("settingsSectionList"));
    EXPECT_LT(popupItem()->width(), window->width());
    QMetaObject::invokeMethod(sheet, "close");
    until([&] { return !sheet->property("visible").toBool(); });

    for (const WindowSize& s: {WindowSize{412, 915, "phone-portrait"}, WindowSize{915, 412, "phone-landscape"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        QMetaObject::invokeMethod(sheet, "open");
        until([&] { return sheet->property("opened").toBool(); });
        EXPECT_NEAR(popupItem()->width(), s.w, 1) << at << ": the whole screen";
        EXPECT_NEAR(popupItem()->height(), s.h, 1) << at;
        EXPECT_TRUE(visibleIn("settingsSectionList")) << at << ": the sections as a list";
        EXPECT_FALSE(visibleIn("displayTab")) << at;
        EXPECT_FALSE(visibleIn("settingsSections")) << at;
        // Pen: the section as a page, its sliders not squeezed to their knob
        QQuickItem* pen = nullptr;
        until([&] {
            for (auto* i: itemsUnder(popupItem())) {
                if (i->objectName() == "settingsSection0") {
                    pen = i;
                }
            }
            return pen != nullptr;
        });
        ASSERT_NE(pen, nullptr) << at;
        const QString shots = qEnvironmentVariable("XQT_UI_DIALOG_SHOTS");
        if (!shots.isEmpty()) {
            window->grabWindow().save(QString("%1/settingsList-%2.png").arg(shots, QString::fromStdString(at)));
        }
        click(pen);
        if (!shots.isEmpty()) {
            wait(50);
            window->grabWindow().save(QString("%1/settingsPen-%2.png").arg(shots, QString::fromStdString(at)));
        }
        EXPECT_TRUE(sheet->property("sectionShown").toBool()) << at;
        EXPECT_TRUE(visibleIn("settingsSections")) << at;
        EXPECT_FALSE(visibleIn("settingsSectionList")) << at;
        wait(50);
        int sliders = 0;
        for (auto* i: itemsUnder(popupItem())) {
            if (i->inherits("QQuickSlider") && i->isVisible()) {
                ++sliders;
                EXPECT_GE(i->width(), 120) << at << ": a slider of the Pen section";
                EXPECT_LE(i->mapToScene(QPointF(i->width(), 0)).x(), s.w + 1) << at;
            }
        }
        EXPECT_GE(sliders, 2) << at;
        // Esc (the back key): back to the list first, then closed
        QTest::keyClick(window, Qt::Key_Escape);
        wait(50);
        EXPECT_FALSE(sheet->property("sectionShown").toBool()) << at << ": back to the list";
        EXPECT_TRUE(sheet->property("visible").toBool()) << at;
        QTest::keyClick(window, Qt::Key_Escape);
        until([&] { return !sheet->property("visible").toBool(); });
        EXPECT_FALSE(sheet->property("visible").toBool()) << at;
    }
}

// The compact chrome's toolbox floats inside a short window (F6.4, F9.4), on a phone too; its ⋯ menu (Present, "Show the
// tabs and the tool bar") stays inside the window. (The classic tool square and its quick tools went in 0.8.0.)
TEST_F(AdaptiveLayoutTest, theFloatingToolboxFitsAShortWindow) {
    openDocument();
    for (const WindowSize& s: {WindowSize{1024, 700, "small-desktop"}, WindowSize{1280, 500, "short-wide"},
                               WindowSize{915, 412, "phone-landscape"}, WindowSize{412, 915, "phone-portrait"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        compactChrome(true);
        wait(150);
        auto* box = named("toolbox");
        ASSERT_TRUE(shownInWindow(box)) << at;
        EXPECT_TRUE(box->property("floating").toBool()) << at;
        EXPECT_TRUE(insideWindow(sceneRect(box))) << at << ": the toolbox";
        auto* more = findItem("toolboxMoreButton");
        ASSERT_NE(more, nullptr);
        EXPECT_TRUE(shownInWindow(more)) << at << ": its ⋯";
        click(more);
        auto* menu = window->findChild<QObject*>("toolboxMoreMenu");
        ASSERT_TRUE(opened(phoneClass() ? sheet() : menu, true)) << at;
        settled(phoneClass() ? sheet() : menu);
        EXPECT_TRUE(insideWindow(popupRect(phoneClass() ? sheet() : menu))) << at << ": its menu";
        QTest::keyClick(window, Qt::Key_Escape);
        EXPECT_TRUE(opened(phoneClass() ? sheet() : menu, false)) << at;
        compactChrome(false);
        wait(30);
    }
}

// --- the tool bar (qt/adaptive-toolbar) -----------------------------------------------------------------------------

// A portrait 2-in-1 at 125 % (720 wide): the toolbox holds every tool (on its rail or in a stack); the command bar is
// one row, and only commands go into "more tools" (the entries of ⋮ shown as buttons go back into ⋮)
TEST_F(AdaptiveLayoutTest, theToolsFitAt720) {
    openDocument();
    resize(720, 1232);
    ASSERT_EQ(sizeClass(), "tabletPortrait");
    EXPECT_EQ(toolPlan().value("layout").toString(), "row");
    checkToolBar("720x1232");
    const QStringList lowPriority{"new", "open", "save", "settings", "present", "fullScreen", "search", "editAsNotes",
                                  "openExternally", "addPage", "sticker", "record", "image", "emoji"};
    for (const QString& n: overflowNames()) {
        EXPECT_TRUE(lowPriority.contains(n)) << n.toStdString() << " is not a low-priority button";
    }
    EXPECT_LT(sceneRect(named("toolArea")).bottom(), sceneRect(named("canvas")).top() + 1) << "the bar above the page";
}

// New has one place (qt/docs/adaptive-layout.md, "One place for each action"): the tab strip's "+" where the tab strip
// is shown; a button of the tools only where it is not (the compact chrome's tools, the phone's sheet)
TEST_F(AdaptiveLayoutTest, newIsTheTabStripsPlusWhereThereIsOne) {
    openDocument();
    resize(1920, 1080);
    auto* newButton = named("newButton");
    ASSERT_NE(newButton, nullptr);
    EXPECT_TRUE(shownInWindow(named("newTabButton")));
    EXPECT_FALSE(newButton->property("offered").toBool());
    EXPECT_FALSE(newButton->isVisible()) << "not in the bar too";
    EXPECT_FALSE(overflowNames().contains("new"));
    compactChrome(true);
    wait(200);
    EXPECT_FALSE(named("newTabButton")->isVisible());
    EXPECT_TRUE(newButton->property("offered").toBool()) << "offered where there is no tab strip";
    compactChrome(false);
    wait(200);
    resize(412, 915);
    EXPECT_TRUE(newButton->property("offered").toBool()) << "the phone's sheet has it";
}

// What does not fit goes into "more tools", next to ⋮: its buttons with their names; a button used there closes it.
// (The command bar of a notes document has room for its commands down to the phones; a text document's, merged into
// its format bar, puts them there.)
TEST_F(AdaptiveLayoutTest, moreToolsHoldsWhatDoesNotFit) {
    std::ofstream(root / "kalman.md") << "# Lecture 3\n\n## Kalman filter\n\nThe **prediction** step.\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(400);
    resize(800, 600);
    const QStringList names = overflowNames();
    ASSERT_FALSE(names.isEmpty()) << "a narrow window's format bar has more than fits";
    auto* moreTools = named("moreToolsButton");
    ASSERT_TRUE(shownInWindow(moreTools));
    EXPECT_LT(std::abs(sceneRect(moreTools).right() - sceneRect(named("moreButton")).left()), 8) << "beside ⋮";
    auto* popup = window->findChild<QObject*>("moreToolsPopup");
    ASSERT_NE(popup, nullptr);
    click(moreTools);
    ASSERT_TRUE(opened(popup, true));
    EXPECT_TRUE(insideWindow(popupRect(popup)));
    for (const QString& n: names) {
        auto* label = findItem(("overflowLabel_" + n).toUtf8().constData());
        ASSERT_NE(label, nullptr) << n.toStdString();
        EXPECT_FALSE(label->property("text").toString().isEmpty()) << n.toStdString() << ": its name beside it";
        EXPECT_TRUE(shownInWindow(label)) << n.toStdString();
    }
    ASSERT_TRUE(names.contains("settings"));
    click(named("settingsButton"));
    EXPECT_TRUE(opened(popup, false)) << "used: it closes";
    auto* settingsPage = window->findChild<QObject*>("settingsPage");
    until([&] { return settingsPage->property("visible").toBool(); });
    EXPECT_TRUE(settingsPage->property("visible").toBool());
    QMetaObject::invokeMethod(settingsPage, "close");
}

// The sidebar's arrow: at the canvas's left edge it opens the sidebar; at the sidebar's edge it closes it
TEST_F(AdaptiveLayoutTest, sidebarArrowOpensAndCloses) {
    openDocument();
    resize(960, 1392);
    auto* arrow = findItem("sidebarArrow");
    auto* sidebar = findItem("sidebar");
    ASSERT_NE(arrow, nullptr);
    EXPECT_TRUE(shownInWindow(arrow));
    EXPECT_FALSE(sidebar->isVisible());
    EXPECT_NEAR(sceneRect(arrow).left(), sceneRect(named("canvas")).left(), 1) << "at the canvas's left edge";
    // clear of the view pill and the page's top and bottom
    EXPECT_FALSE(sceneRect(arrow).intersects(sceneRect(named("viewPill"))));
    click(arrow);
    EXPECT_TRUE(sidebar->isVisible()) << "open (a drawer here)";
    EXPECT_NEAR(sceneRect(arrow).left(), sceneRect(sidebar).right(), 1) << "at the sidebar's edge";
    click(arrow);
    until([&] { return !sidebar->isVisible(); });  // (it slides out)
    EXPECT_FALSE(sidebar->isVisible()) << "closed again";
    // The touch profile: a finger's size
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "on"));
    wait(50);
    EXPECT_GE(arrow->width(), 48);
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "auto"));
    // Not in Zen
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    wait(50);
    EXPECT_FALSE(arrow->isVisible());
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, false));
}

// The view pill: the contents button beside the page grid, no − / +, inside the window at the five sizes and clear of
// the reference's pill
TEST_F(AdaptiveLayoutTest, viewPillWithContentsInsideAndClearOfTheReference) {
    openDocument();
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "notes.xopp").string())));
    wait(200);
    auto* pill = named("viewPill");
    auto* contents = named("contentsButton");
    ASSERT_NE(contents, nullptr);
    bool inPill = false;
    for (QQuickItem* p = contents->parentItem(); p; p = p->parentItem()) {
        inPill = inPill || p == pill;
    }
    EXPECT_TRUE(inPill) << "the contents button is in the view pill";
    EXPECT_EQ(named("pagesButton"), nullptr) << "no Pages button in the tool bar (the arrow opens the sidebar)";
    controller->reference().showTab(0);
    wait(300);
    auto* refPill = findItem("referencePill");
    ASSERT_NE(refPill, nullptr);
    for (const WindowSize& s: {WindowSize{1920, 1080, ""}, WindowSize{1280, 800, ""}, WindowSize{960, 1392, ""},
                               WindowSize{412, 915, ""}, WindowSize{915, 412, ""}}) {
        resize(s.w, s.h);
        wait(100);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        ASSERT_TRUE(refPill->isVisible()) << at;
        EXPECT_TRUE(insideWindow(sceneRect(refPill))) << at << ": the reference's pill inside the window";
        if (phoneChrome()) {  // (the dock has the view pill's buttons, the contents in the page grid)
            EXPECT_FALSE(pill->isVisible()) << at;
            EXPECT_TRUE(shownInWindow(named("toolboxPageButton"))) << at;
            continue;
        }
        EXPECT_TRUE(insideWindow(sceneRect(pill))) << at << ": the view pill inside the window";
        EXPECT_FALSE(sceneRect(pill).intersects(sceneRect(refPill))) << at << ": clear of the reference's pill";
    }
    controller->reference().close();
}

// --- the panels (qt/adaptive-panels) --------------------------------------------------------------------------------

// The Markdown source: beside the page on a desktop and a phone held sideways, below it on a portrait tablet (half) and
// a phone (the bottom 60 %); the page keeps a sensible width beside it, and the whole width above it
TEST_F(AdaptiveLayoutTest, sourcePanelBesideOrBelowThePage) {
    openDocument();
    auto* panel = findItem("markdownPanel");
    auto* canvas = named("canvas");
    ASSERT_NE(panel, nullptr);
    struct Case {
        int w, h;
        const char* place;
    };
    for (const Case& c: {Case{1920, 1080, "side"}, Case{1280, 800, "side"}, Case{960, 1392, "bottom"},
                         Case{412, 915, "phone"}, Case{915, 412, "side"}}) {
        resize(c.w, c.h);
        const std::string at = std::to_string(c.w) + "x" + std::to_string(c.h);
        QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
        until([&] { return panel->isVisible(); });
        wait(150);
        ASSERT_TRUE(panel->isVisible()) << at;
        const QRectF p = sceneRect(panel), page = sceneRect(canvas);
        EXPECT_TRUE(insideWindow(p)) << at << ": the panel inside the window";
        if (std::string(c.place) == "side") {
            EXPECT_FALSE(flag("sourceAtBottom")) << at;
            EXPECT_NEAR(p.left(), page.right(), 1) << at << ": beside the page";
            EXPECT_NEAR(p.top(), page.top(), 1) << at;
            EXPECT_GE(page.width(), std::min(480.0, 0.6 * c.w)) << at << ": the page keeps a sensible width";
        } else {
            EXPECT_TRUE(flag("sourceAtBottom")) << at;
            EXPECT_NEAR(p.top(), page.bottom(), 1) << at << ": below the page";
            EXPECT_NEAR(p.left(), page.left(), 1) << at;
            EXPECT_NEAR(p.width(), page.width(), 1) << at;
            EXPECT_NEAR(page.width(), c.w - named("sideTools")->width(), 1)
                    << at << ": the page keeps the whole width (beside the toolbox docked at a side)";
            const double share = page.height() / (page.height() + p.height());
            EXPECT_NEAR(share, std::string(c.place) == "phone" ? 0.4 : 0.5, 0.02) << at << ": the page's share";
            EXPECT_TRUE(named("sourceDivider")->isVisible()) << at;
        }
        // The view pill stays inside the page
        if (named("viewPill")->isVisible()) {  // (the phone chrome: its dock instead)
            EXPECT_TRUE(page.adjusted(-1, -1, 1, 1).contains(sceneRect(named("viewPill")))) << at << ": the view pill";
        }
        QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, false));
        until([&] { return !panel->isVisible(); });
    }
}

// Below the page, the divider is dragged; the page's share is remembered for that size class only
TEST_F(AdaptiveLayoutTest, sourceDividerIsDraggedAndRememberedPerClass) {
    openDocument();
    resize(960, 1392);
    auto* panel = findItem("markdownPanel");
    auto* canvas = named("canvas");
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
    until([&] { return panel->isVisible(); });
    wait(100);
    auto* grip = findItem("sourceDividerGrip");
    ASSERT_NE(grip, nullptr);
    ASSERT_TRUE(shownInWindow(grip));
    const double pageBefore = canvas->height();
    auto* layer = controller->tabManager().currentSession()->getDocument()->getPage(0)->getSelectedLayer();
    const size_t elements = layer->getElements().size();
    const QPoint from = centerOf(grip);
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(window, from + QPoint(0, -20 * i));
        wait(5);
    }
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, from + QPoint(0, -200));
    wait(80);
    EXPECT_NEAR(canvas->height(), pageBefore - 200, 20) << "the page got shorter, the source taller";
    EXPECT_FALSE(choice("tabletPortrait", "sourceSplit").isEmpty()) << "remembered for this class";
    EXPECT_EQ(layer->getElements().size(), elements) << "dragging the divider drew";
    const double dragged = canvas->height();
    resize(412, 915);
    const double phonePage = canvas->height() / (canvas->height() + panel->height());
    EXPECT_NEAR(phonePage, 0.4, 0.02) << "a phone: its own (the automatic 40 %)";
    resize(960, 1392);
    EXPECT_NEAR(canvas->height(), dragged, 2) << "back on the tablet: its share again";
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, false));
}

// The reference: top and bottom in a portrait area, side by side in a landscape one; the divider's ratio stays when
// that flips; both pills lie inside their halves and never meet; in a narrow half the reference's pill is its page
// and a ⋮ with the rest
TEST_F(AdaptiveLayoutTest, referenceSplitFollowsTheAreaAndItsPillsStayApart) {
    openDocument();
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "notes.xopp").string())));
    wait(200);
    controller->reference().showTab(0);
    controller->reference().setRatio(0.6);
    wait(300);
    auto* split = findItem("referenceSplit");
    auto* canvas = named("canvas");
    auto* refCanvas = findItem("referenceCanvas");
    auto* pill = named("viewPill");
    auto* refPill = findItem("referencePill");
    ASSERT_NE(split, nullptr);
    for (const WindowSize& s: fiveSizes) {
        resize(s.w, s.h);
        wait(100);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        const QRectF area = sceneRect(split), main = sceneRect(canvas), ref = sceneRect(refCanvas);
        const bool portrait = area.height() > area.width();
        EXPECT_EQ(split->property("vertical").toBool(), portrait) << at;
        if (portrait) {
            EXPECT_NEAR(main.width(), area.width(), 1) << at << ": top and bottom";
            EXPECT_NEAR(ref.width(), area.width(), 1) << at;
            EXPECT_FALSE(main.intersects(ref)) << at;
            EXPECT_NEAR(main.height() / (area.height() - 8), 0.6, 0.01) << at << ": the ratio kept";
        } else {
            EXPECT_NEAR(main.height(), area.height(), 1) << at << ": side by side";
            EXPECT_NEAR(main.width() / (area.width() - 8), 0.6, 0.01) << at << ": the ratio kept";
        }
        ASSERT_TRUE(refPill->isVisible()) << at;
        EXPECT_TRUE(ref.adjusted(-1, -1, 1, 1).contains(sceneRect(refPill))) << at << ": the reference's pill";
        if (pill->isVisible()) {  // (the phone chrome: its dock instead)
            EXPECT_TRUE(main.adjusted(-1, -1, 1, 1).contains(sceneRect(pill))) << at << ": the view pill inside its half";
            EXPECT_FALSE(sceneRect(pill).intersects(sceneRect(refPill))) << at << ": the pills apart";
        }
        const bool narrow = ref.width() < 480;
        EXPECT_EQ(findItem("referenceMoreButton")->isVisible(), narrow) << at;
        EXPECT_EQ(findItem("referenceGridButton")->isVisible(), !narrow) << at;
        EXPECT_TRUE(findItem("referencePageButton")->isVisible()) << at;
    }
    // 1920x1080 and 960x1392 in the audit's words: side by side, then top and bottom
    resize(1920, 1080);
    EXPECT_FALSE(split->property("vertical").toBool());
    resize(960, 1392);
    EXPECT_TRUE(split->property("vertical").toBool());
    // A narrow half: its ⋮ holds the rest, and its entries work
    resize(412, 915);
    auto* more = findItem("referenceMoreButton");
    ASSERT_TRUE(more->isVisible());
    QMetaObject::invokeMethod(more, "clicked");
    QObject* s = sheet();
    ASSERT_TRUE(opened(s, true)) << "a sheet on a phone";
    settled(s);
    ASSERT_NE(sheetRow("referenceGridItem"), nullptr);
    EXPECT_NE(sheetRow("referenceCloseItem"), nullptr);
    EXPECT_EQ(sheetRow("referenceCopyItem"), nullptr) << "nothing selected: no copy";
    click(sheetRow("referenceGridItem"));
    EXPECT_TRUE(opened(s, false));
    until([&] { return controller->reference().pagesShown(); });
    EXPECT_TRUE(controller->reference().pagesShown()) << "the reference's pages";
    controller->reference().setPagesShown(false);
    controller->reference().close();
}

// The compact view pill in a canvas under 520 px: undo, redo, the page number (a tap: all pages), the contents and the
// zoom; inside the canvas
TEST_F(AdaptiveLayoutTest, compactViewPillOnAPhone) {
    openDocument();
    auto* pill = named("viewPill");
    auto* canvas = named("canvas");
    resize(1920, 1080);
    EXPECT_FALSE(pill->property("compact").toBool());
    EXPECT_TRUE(named("pageGridButton")->isVisible());
    EXPECT_FALSE(named("pageNumberButton")->isVisible());
    resize(412, 915);
    // (the phone chrome has its dock instead of the view pill: the compact pill is the compact chrome's here)
    compactChrome(true);
    wait(100);
    ASSERT_TRUE(pill->isVisible());
    ASSERT_TRUE(pill->property("compact").toBool());
    for (const char* name: {"pageNumberButton", "contentsButton", "zoomButton"}) {
        EXPECT_TRUE(shownInWindow(named(name))) << name << " in the compact pill";
    }
    // (undo and redo: the head of the toolbox, floating in the compact chrome on a phone too)
    EXPECT_TRUE(shownInWindow(named("toolboxUndoButton")));
    for (const char* name: {"undoButton", "redoButton", "pageGridButton", "layoutButton", "pageNumberLabel"}) {
        EXPECT_FALSE(named(name)->isVisible()) << name << " not in the compact pill";
    }
    EXPECT_TRUE(sceneRect(canvas).contains(sceneRect(pill))) << "inside the canvas";
    EXPECT_LE(pill->width(), canvas->width() - 16);
    auto* grid = named("pageGrid");
    click(named("pageNumberButton"));
    until([&] { return grid->isVisible(); });
    EXPECT_TRUE(grid->isVisible()) << "the page number opens all pages";
    QMetaObject::invokeMethod(grid, "close");
    until([&] { return !grid->isVisible(); });
    compactChrome(false);
    // Beside the Markdown source of a desktop window made small: the compact pill too, and ⋮ offers the page layout
    resize(1024, 700);
    auto* panel = findItem("markdownPanel");
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
    until([&] { return panel->isVisible(); });
    wait(100);
    EXPECT_EQ(pill->property("compact").toBool(), canvas->width() < 520) << canvas->width();
    EXPECT_TRUE(sceneRect(canvas).adjusted(-1, -1, 1, 1).contains(sceneRect(pill)));
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, false));
}

// Undo and redo have one place at a time (qt/undo-redo): the head of the toolbox (docked, or floating in the compact
// chrome; also while the command bar is put away), the format bar of a text document, the view pill where neither is
// shown (a text document in the compact chrome)
TEST_F(AdaptiveLayoutTest, undoAndRedoHaveOnePlaceAtATime) {
    openDocument();
    resize(1280, 800);
    auto* boxUndo = named("toolboxUndoButton");
    auto* toolUndo = named("toolUndoButton");
    auto* pillUndo = named("undoButton");
    auto* pillRedo = named("redoButton");
    ASSERT_TRUE(shownInWindow(boxUndo));
    EXPECT_FALSE(toolUndo->isVisible()) << "not in the command bar";
    EXPECT_FALSE(pillUndo->isVisible());
    controller->setToolbarHidden(true);
    wait(150);
    EXPECT_TRUE(shownInWindow(boxUndo)) << "the command bar put away: the toolbox stays";
    EXPECT_FALSE(pillUndo->isVisible());
    controller->setToolbarHidden(false);
    wait(150);
    // The compact chrome: the floating toolbox
    compactChrome(true);
    wait(150);
    EXPECT_TRUE(shownInWindow(boxUndo)) << "the compact chrome: the floating toolbox";
    EXPECT_FALSE(pillUndo->isVisible());
    compactChrome(false);
    wait(150);
    // A text document: its format bar; in the compact chrome the view pill
    std::ofstream(root / "kalman.md") << "# Lecture 3\n\nThe **prediction** step.\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(400);
    EXPECT_TRUE(shownInWindow(named("formatUndoButton"))) << "a text document: its format bar";
    EXPECT_FALSE(boxUndo->isVisible());
    EXPECT_FALSE(pillUndo->isVisible());
    compactChrome(true);
    wait(150);
    EXPECT_TRUE(shownInWindow(pillUndo)) << "a text document in the compact chrome: the view pill";
    EXPECT_TRUE(shownInWindow(pillRedo));
    EXPECT_FALSE(named("formatUndoButton")->isVisible());
    compactChrome(false);
    wait(150);
    EXPECT_FALSE(pillUndo->isVisible());
}

// The drawer: slides in; Esc and the back key close it; on a phone up to 85 % of the width
TEST_F(AdaptiveLayoutTest, sidebarDrawerKeysAndPhoneWidth) {
    openDocument();
    resize(960, 1392);
    auto* sidebar = findItem("sidebar");
    auto* arrow = findItem("sidebarArrow");
    for (Qt::Key k: {Qt::Key_Escape, Qt::Key_Back}) {
        click(arrow);
        EXPECT_TRUE(sidebar->isVisible());
        until([&] { return window->property("drawerSlide").toDouble() >= 1.0; });
        EXPECT_NEAR(sceneRect(sidebar).left(), 0, 1) << "slid in";
        EXPECT_NEAR(sidebar->width(), 210, 1) << "a tablet: the sidebar's width";
        QTest::keyClick(window, k);
        EXPECT_FALSE(flag("sidebarShown")) << k;
        until([&] { return !sidebar->isVisible(); });
        EXPECT_FALSE(sidebar->isVisible()) << k << " closes the drawer";
    }
    resize(412, 915);
    click(arrow);
    until([&] { return window->property("drawerSlide").toDouble() >= 1.0; });
    EXPECT_NEAR(sidebar->width(), std::round(412 * 0.85), 1) << "a phone: 85 % of the width";
    EXPECT_TRUE(insideWindow(sceneRect(sidebar)));
    auto* list = findItem("sidebarList");
    until([&] { return list->property("count").toInt() > 0; });
    QQuickItem* first = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, first), Q_ARG(int, 0));
    ASSERT_NE(first, nullptr);
    EXPECT_GT(first->width(), 300) << "larger thumbnails";
    // Its other modes, the same in the drawer
    click(findItem("sidebarLayersButton"));
    EXPECT_TRUE(sidebar->isVisible()) << "choosing a mode keeps it open";
    click(findItem("sidebarPagesButton"));
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(first));
    until([&] { return !sidebar->isVisible(); });
    EXPECT_FALSE(sidebar->isVisible()) << "a page picked: closed";
}

// The format bar of a text document: all its tools as buttons where there is room; the inserts in "Insert" in a
// narrower window, then the headings in one button and the commands in "more tools"; the row scrolls only once all of
// that is folded (with undo and redo at its start, below 800 px: qt/docs/toolbox.md, "Text documents"); on a phone the
// row scrolls, with a fading edge
TEST_F(AdaptiveLayoutTest, formatBarFoldsIntoInsertInsteadOfScrolling) {
    std::ofstream(root / "kalman.md") << "# Lecture 3\n\n## Kalman filter\n\nThe **prediction** step.\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(400);
    auto* bar = named("markdownFormatBar");
    ASSERT_NE(bar, nullptr);
    auto* flick = named("formatBarFlick");
    ASSERT_NE(flick, nullptr);
    auto scrolls = [&] { return flick->property("contentWidth").toDouble() > flick->width() + 0.5; };
    for (const WindowSize& s: {WindowSize{1920, 1080, ""}, WindowSize{1280, 800, ""}, WindowSize{960, 1392, ""},
                               WindowSize{800, 600, ""}, WindowSize{720, 1232, ""}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        ASSERT_TRUE(bar->isVisible()) << at;
        if (scrolls()) {  // (only once everything is folded)
            EXPECT_LE(s.w, 800) << at << ": no sideways scrolling on a wider desktop or a tablet";
            EXPECT_TRUE(bar->property("levelsInMenu").toBool()) << at << ": the headings in one button";
            EXPECT_TRUE(bar->property("insertIconOnly").toBool()) << at << ": the inserts in one button";
            EXPECT_EQ(named("formatCommands")->width(), 0) << at << ": the commands in \"more tools\"";
        }
        const bool inMenu = bar->property("insertsInMenu").toBool();
        EXPECT_EQ(named("mdInsertButton")->isVisible(), inMenu) << at;
        EXPECT_EQ(named("mdImage")->isVisible(), !inMenu) << at;
        for (const char* stays: {"mdBold", "mdItalic", "mdLink", "mdBulletList", "mdNumberedList", "mdTaskList"}) {
            EXPECT_TRUE(shownInWindow(named(stays))) << at << ": " << stays << " stays in the row";
        }
        EXPECT_TRUE(shownInWindow(named("mdParagraph")) || shownInWindow(named("mdBlockButton"))) << at;
        if (s.w >= 1920) {
            EXPECT_FALSE(inMenu) << at << ": room for all";
            EXPECT_GT(named("formatCommands")->width(), 0) << at << ": and for the commands";
        }
    }
    resize(800, 600);
    ASSERT_TRUE(bar->property("insertsInMenu").toBool()) << "800 px: the inserts in a menu";
    // Its entries work: a rule at the cursor
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(named("canvas")));
    wait(100);
    auto* insert = named("mdInsertButton");
    QMetaObject::invokeMethod(insert, "clicked");
    auto* menu = window->findChild<QObject*>("mdInsertMenu");
    ASSERT_TRUE(opened(menu, true));
    settled(menu);
    checkMenuGeometry("800x600", menu, insert, true);
    EXPECT_NE(menuEntries(menu).size(), 0u);
    const std::string before = controller->tabManager().currentSession()->currentText();
    QMetaObject::invokeMethod(window->findChild<QObject*>("mdInsertRule"), "triggered");
    QMetaObject::invokeMethod(menu, "close");
    wait(100);
    const std::string after = controller->tabManager().currentSession()->currentText();
    EXPECT_NE(after.find("---"), std::string::npos) << after;
    EXPECT_NE(after, before);
    // The heading buttons are 40 wide in the touch profile
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "on"));
    resize(1920, 1080);
    EXPECT_GE(named("mdHeading1")->width(), 40);
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "auto"));
    // A phone: the row scrolls, its right edge fades
    resize(412, 915);
    EXPECT_FALSE(bar->property("insertsInMenu").toBool()) << "a phone: nothing in menus";
    EXPECT_TRUE(scrolls());
    EXPECT_TRUE(named("formatBarFadeRight")->isVisible());
    EXPECT_FALSE(named("formatBarFadeLeft")->isVisible());
    flick->setProperty("contentX", flick->property("contentWidth").toDouble() - flick->width());
    wait(50);
    EXPECT_TRUE(named("formatBarFadeLeft")->isVisible());
    EXPECT_FALSE(named("formatBarFadeRight")->isVisible());
}

// The pills at the canvas's bottom keep clear of the view pill (audit F6): the selection pill, the back / forward
// pill, and the pen pill of the compact chrome
TEST_F(AdaptiveLayoutTest, pillsKeepClearOfTheViewPill) {
    openDocument();
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "notes.xopp").string())));  // (strokes to select)
    wait(200);
    auto* pill = named("viewPill");
    auto* selection = named("selectionBar");
    auto* nav = named("navPill");
    auto* box = named("toolbox");
    controller->selectTool("select");
    for (const WindowSize& s: fiveSizes) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        controller->selectAllOnPage();
        until([&] { return selection->isVisible(); });
        wait(50);
        ASSERT_TRUE(selection->isVisible()) << at;
        if (pill->isVisible()) {
            EXPECT_FALSE(sceneRect(selection).intersects(sceneRect(pill))) << at << ": the selection pill";
        } else {  // (the phone chrome: above its dock, inside the page)
            EXPECT_TRUE(sceneRect(named("canvas")).adjusted(-1, -1, 1, 1).contains(sceneRect(selection))) << at;
        }
        EXPECT_TRUE(insideWindow(sceneRect(selection))) << at;
        controller->clearSelection();
        controller->jumpToPage(2);
        wait(50);
        if (nav->isVisible() && pill->isVisible()) {
            EXPECT_FALSE(sceneRect(nav).intersects(sceneRect(pill))) << at << ": the back / forward pill";
        }
    }
    // The toolbox floating in the compact chrome (full screen), at the right or the bottom edge (on a phone too)
    controller->selectTool("pen");
    window->setProperty("fullScreenMode", true);
    wait(200);
    window->showNormal();
    for (const WindowSize& s: {WindowSize{915, 412, ""}, WindowSize{412, 915, ""}, WindowSize{1280, 800, ""}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        for (const char* edge: {"right", "bottom"}) {
            QMetaObject::invokeMethod(window, "chooseToolboxEdge", Q_ARG(QVariant, QString(edge)));
            wait(250);
            ASSERT_TRUE(box->isVisible()) << at;
            EXPECT_TRUE(box->property("floating").toBool()) << at;
            EXPECT_TRUE(insideWindow(sceneRect(box))) << at << " " << edge;
            if (pill->isVisible()) {
                EXPECT_FALSE(sceneRect(box).intersects(sceneRect(pill))) << at << " " << edge << ": the toolbox";
            }
        }
        QMetaObject::invokeMethod(window, "chooseLayout", Q_ARG(QVariant, QString("toolbox")), Q_ARG(QVariant, QString()));
    }
    window->setProperty("fullScreenMode", false);
}

// Pictures of the command bar and the toolbox, to look at (skipped unless XQT_TOOLBAR_SHOTS=<folder>)
TEST_F(AdaptiveLayoutTest, toolBarPictures) {
    const QString folder = qEnvironmentVariable("XQT_TOOLBAR_SHOTS");
    QDir().mkpath(folder);
    openDocument();
    auto shot = [&](const char* name) {
        wait(500);
        window->grabWindow().save(folder + "/" + name + ".png");
    };
    auto openPopup = [&](const char* button, const char* popup) {
        click(named(button));
        opened(window->findChild<QObject*>(popup), true);
        settled(window->findChild<QObject*>(popup));
    };
    auto closePopups = [&] {
        for (const char* name: {"moreToolsPopup", "toolboxMoreMenu", "fitMenu", "menuSheet", "phoneToolSheet"}) {
            if (QObject* popup = window->findChild<QObject*>(name)) {
                QMetaObject::invokeMethod(popup, "close");
            }
        }
        wait(300);
    };
    resize(1920, 1080);
    shot("bar-1920x1080");
    resize(1280, 800);
    shot("bar-1280x800");
    resize(960, 1392);
    shot("bar-960x1392");
    click(named("sidebarArrow"));
    shot("bar-960x1392-sidebar-drawer");
    click(named("sidebarArrow"));
    resize(720, 1232);
    shot("bar-720x1232");
    resize(800, 600);
    shot("bar-800x600-narrow");
    resize(412, 915);
    shot("bar-412x915-phone");
    openPopup("toolboxAllButton", "phoneToolSheet");
    shot("bar-412x915-my-tools");
    closePopups();
    // The zoom's menu, a text document's merged bar
    resize(960, 1392);
    click(named("zoomButton"));
    wait(900);
    shot("pill-960x1392-zoom-menu");
    closePopups();
    // The compact chrome: the toolbox floats (full screen in a window of this size)
    resize(1280, 800);
    window->setProperty("fullScreenMode", true);
    wait(300);
    window->showNormal();
    resize(1280, 800);
    openPopup("toolboxMoreButton", "toolboxMoreMenu");
    shot("compact-1280x800-tools");
    closePopups();
    window->setProperty("fullScreenMode", false);
    wait(300);
    window->showNormal();
    resize(960, 1392);
    std::ofstream(root / "kalman.md") << "# Lecture 3\n\n## Kalman filter\n\nThe **prediction** step, then the *update*.\n";
    ASSERT_TRUE(controller->openPath(QString::fromStdString((root / "kalman.md").string())));
    wait(400);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(named("canvas")));
    shot("markdown-960x1392-merged");
    resize(1280, 800);
    shot("markdown-1280x800-merged");
    // The format bar's forms (qt/adaptive-panels): Insert, then a plain "+", then the headings in one button
    for (const WindowSize& s: {WindowSize{1024, 700, ""}, WindowSize{800, 600, ""}, WindowSize{720, 1232, ""},
                               WindowSize{600, 800, ""}}) {
        resize(s.w, s.h);
        shot(QString("markdown-%1x%2-format-bar").arg(s.w).arg(s.h).toUtf8().constData());
    }
}

// One place for each action (qt/docs/adaptive-layout.md): ⋮ repeats no button of the tool bar, "more tools", the view
// pill or the sidebar
TEST_F(AdaptiveLayoutTest, moreMenuRepeatsNoButton) {
    openDocument();
    for (const char* gone: {"settingsItem", "fullScreenItem", "presentItem", "allPagesItem", "insertImageItem",
                            "insertStickyNoteItem", "openExternallyItem", "editAsNotesItem", "hideToolbarItem"}) {
        EXPECT_EQ(window->findChild<QObject*>(gone), nullptr) << gone << " is a button: not in ⋮ too";
    }
    for (const char* kept: {"presentCleanItem", "insertPagesItem", "readItem", "zenItem", "readOnlyItem", "allDocumentsItem",
                            "toolboxPositionMenu"}) {
        EXPECT_NE(window->findChild<QObject*>(kept), nullptr) << kept << " differs from any button: kept";
    }
    // The buttons are there instead
    for (const char* button: {"settingsButton", "fullScreenButton", "presentButton", "pageGridButton", "imageButton",
                              "contentsButton", "sidebarArrow"}) {
        EXPECT_NE(named(button), nullptr) << button;
    }
}

// --- the phone chrome (qt/phone-chrome) -----------------------------------------------------------------------------

namespace {
/// The phone chrome: the app bar and the tool dock of the phone classes, Zen in a tiny window, presenting's Zen dot, one window on Android and iOS, the library's top without breadcrumbs, the Fold 7 folded and unfolded
class PhoneChromeTest: public AdaptiveLayoutTest {
protected:
    /// Three documents open, A B C, used in that order (C is the current one)
    void openThree() {
        openDocument();
        for (const char* name: {"notes.xopp", "lecture.pdf"}) {
            ASSERT_TRUE(controller->openPath(QString::fromStdString((root / name).string())));
            wait(150);
        }
        ASSERT_EQ(controller->tabCount(), 3);
        ASSERT_EQ(controller->currentTab(), 2);
    }
    /// A swipe with the mouse along an item, from its right part to its left part (or back)
    void swipe(QQuickItem* item, bool toLeft) {
        const QRectF r = sceneRect(item);
        const QPoint from((toLeft ? r.left() + r.width() * 0.8 : r.left() + r.width() * 0.2), r.center().y());
        const QPoint to((toLeft ? r.left() + r.width() * 0.2 : r.left() + r.width() * 0.8), r.center().y());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        for (int i = 1; i <= 10; ++i) {
            QTest::mouseMove(window, from + (to - from) * i / 10);
            wait(16);
        }
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        wait(80);
    }
};
}  // namespace

// At a phone's sizes (upright, sideways, a tiny window with the full chrome chosen), with the navigation bar's 24 px at
// the bottom: no tab strip, the app bar with the library, the title, the tab dots, the tab count and ⋮ inside the
// window; the dock inside the window and above the navigation bar (a rail at the side when held sideways); every tool
// that is never hidden in the dock or its sheet of all tools
TEST_F(PhoneChromeTest, theAppBarAndTheDockAtAPhonesSizes) {
    openThree();
    window->setProperty("safeBottom", 24);
    for (const WindowSize& s: {WindowSize{412, 915, "phone-portrait"}, WindowSize{915, 412, "phone-landscape"},
                               WindowSize{340, 700, "tiny"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        if (sizeClass() == "tiny") {
            QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, false));  // (Zen of a tiny window: left)
            wait(100);
        }
        // (under load the bar and the dock lay themselves out a little after the resize)
        until([&] {
            auto* bar = named("phoneAppBar");
            if (!phoneChrome() || !bar || !named("phoneDock") || !named("phoneDock")->isVisible()) {
                return false;
            }
            for (const char* name: {"phoneHomeButton", "phoneTitle", "phoneTabCount", "moreButton", "toolboxPageButton"}) {
                auto* item = findItem(name);
                if (!item || !shownInWindow(item)) {
                    return false;
                }
            }
            return sceneRect(bar).adjusted(-1, -1, 1, 1).contains(sceneRect(findItem("moreButton")));
        });
        ASSERT_TRUE(phoneChrome()) << at;
        checkPhoneChrome(at);
        EXPECT_TRUE(shownInWindow(findItem("phoneTabDots"))) << at << ": the dots under the title";
        EXPECT_GT(sceneRect(findItem("phoneTabDots")).top(), sceneRect(findItem("phoneTitle")).center().y()) << at;
        const QRectF dock = sceneRect(named("phoneDock"));
        const QRectF canvas = sceneRect(named("canvas"));
        if (s.w > s.h) {
            EXPECT_NEAR(dock.right(), s.w, 1) << at << ": a rail at the right side";
            EXPECT_LE(canvas.right(), dock.left() + 1) << at << ": the page beside it";
        } else {
            EXPECT_NEAR(dock.bottom(), s.h, 1) << at << ": at the bottom";
            EXPECT_LE(canvas.bottom(), dock.top() + 1) << at << ": the page above it";
            EXPECT_GE(canvas.width(), s.w - 1) << at << ": the page keeps the whole width";
        }
        expectInside("doc");
    }
    window->setProperty("safeBottom", 0);
}

// A swipe along the app bar goes to the next document (to the left) or the previous one (to the right); on the page it
// does not
TEST_F(PhoneChromeTest, aSwipeOnTheAppBarSwitchesTabs) {
    openThree();
    resize(412, 915);
    auto* title = findItem("phoneTitleArea");
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(findItem("phoneTitle")->property("text").toString(), controller->title());
    swipe(title, true);
    EXPECT_EQ(controller->currentTab(), 0) << "to the left: the next one (round the end)";
    EXPECT_EQ(findItem("phoneTitle")->property("text").toString(), controller->title()) << "its title in the bar";
    swipe(title, false);
    EXPECT_EQ(controller->currentTab(), 2) << "to the right: the previous one";
    swipe(title, false);
    EXPECT_EQ(controller->currentTab(), 1);
    // Ctrl+Tab and Ctrl+Shift+Tab as before
    QTest::keyClick(window, Qt::Key_Tab, Qt::ControlModifier);
    wait(50);
    EXPECT_EQ(controller->currentTab(), 2) << "Ctrl+Tab";
    QTest::keyClick(window, Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    wait(50);
    EXPECT_EQ(controller->currentTab(), 1) << "Ctrl+Shift+Tab";
}

// The tab count: a tap shows all documents once the double-tap time is over; a double tap goes back to the document used
// before (after A → B → C: B, then C again); a long press lists the documents used lately, the current one first
TEST_F(PhoneChromeTest, theTabCountTapDoubleTapAndLongPress) {
    openThree();
    resize(412, 915);
    controller->setCurrentTab(0);  // A
    controller->setCurrentTab(1);  // B
    controller->setCurrentTab(2);  // C
    wait(50);
    auto* count = findItem("phoneTabCount");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(findItem("phoneTabCountText")->property("text").toString(), "3");
    QObject* overview = window->findChild<QObject*>("tabOverview");
    QQmlExpression(qmlContext(overview), overview, "enter = null; exit = null").evaluate();

    // A tap: the overview, after the double-tap time
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(30);
    EXPECT_FALSE(overview->property("visible").toBool()) << "not at once: a second tap may follow";
    EXPECT_TRUE(count->property("pending").toBool());
    ASSERT_TRUE(opened(overview, true)) << "then the overview";
    QMetaObject::invokeMethod(overview, "close");
    ASSERT_TRUE(opened(overview, false));

    // A double tap: the one used before, and back again
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(30);
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 150);
    EXPECT_EQ(controller->currentTab(), 1) << "C → B";
    EXPECT_FALSE(overview->property("visible").toBool()) << "no overview after a double tap";
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(30);
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 150);
    EXPECT_EQ(controller->currentTab(), 2) << "B → C";

    // A long press: the documents used lately, as a sheet (C, B, A); one picked
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    wait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 200);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, centerOf(count));
    QObject* s = sheet();
    ASSERT_TRUE(opened(s, true)) << "a long press: the documents used lately";
    EXPECT_EQ(s->property("menu").value<QObject*>(), window->findChild<QObject*>("recentTabsMenu"));
    settled(s);
    const auto rows = sheetRows();
    ASSERT_EQ(rows.size(), 3u);
    const QStringList expected{controller->tabTitle(2), controller->tabTitle(1), controller->tabTitle(0)};
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(rows[i]->property("text").toString().endsWith(expected[i]))
                << i << ": " << rows[i]->property("text").toString().toStdString() << " (the one used last first)";
    }
    EXPECT_FALSE(overview->property("visible").toBool());
    click(rows[2]);
    EXPECT_TRUE(opened(s, false));
    EXPECT_EQ(controller->currentTab(), 0) << "picked: A";
}

// A tool's editor (its colors and width), its menu and "My tools" are sheets at the bottom on a phone; a cell of "My
// tools" takes its tool (a variant: the select's lasso), the dock shows the tool in hand (qt/docs/toolbox.md, "On a
// phone"; the classic dock's color and width buttons went in 0.8.0)
TEST_F(PhoneChromeTest, theEditorTheMenuAndMyToolsAreSheets) {
    openDocument();
    resize(412, 915);
    auto atTheBottom = [&](QObject* popup, const char* what) {
        ASSERT_TRUE(opened(popup, true)) << what;
        settled(popup);
        const QRectF r = popupRect(popup);
        EXPECT_TRUE(insideWindow(r)) << what;
        EXPECT_NEAR(r.bottom(), window->height(), 1.5) << what << ": at the bottom";
        EXPECT_GE(r.width(), window->width() - 1) << what << ": across the window";
        QTest::keyClick(window, Qt::Key_Escape);
        EXPECT_TRUE(opened(popup, false)) << what;
    };
    // (the dock laid out with the toolbox in it: a loaded machine may take a moment after the resize)
    const QString pen = entryOf("pen");
    controller->applyToolEntry(pen);
    until([&] {
        auto* dock = named("phoneDock");
        auto* inHand = toolEntry(pen);
        return dock && inHand && dock->isVisible() && inHand->isVisible() &&
               sceneRect(dock).adjusted(-1, -1, 1, 1).contains(sceneRect(inHand));
    });
    click(toolEntry(pen));  // (the tool in hand: its editor)
    atTheBottom(window->findChild<QObject*>("toolEntryEditor"), "the editor");
    QMetaObject::invokeMethod(toolEntry(pen), "held", Q_ARG(QPointF, QPointF(0, 0)));
    atTheBottom(sheet(), "a tool's menu");
    click(findItem("toolboxAllButton"));
    atTheBottom(window->findChild<QObject*>("phoneToolSheet"), "my tools");

    // A variant from the sheet: the lasso; a tool of mine from it: the eraser, in the dock then
    auto* toolSheet = window->findChild<QObject*>("phoneToolSheet");
    click(findItem("toolboxAllButton"));
    ASSERT_TRUE(opened(toolSheet, true));
    settled(toolSheet);
    click(findItem("toolCell_select_selectRegion"));
    EXPECT_TRUE(opened(toolSheet, false)) << "a tool taken: back to the page";
    until([&] { return controller->property("tool").toString() == "selectRegion"; });
    EXPECT_EQ(controller->property("tool").toString(), "selectRegion");
    const QString eraser = entryOf("eraser");
    click(findItem("toolboxAllButton"));
    ASSERT_TRUE(opened(toolSheet, true));
    settled(toolSheet);
    click(findItem(("sheetEntry_" + eraser).toUtf8().constData()));
    EXPECT_TRUE(opened(toolSheet, false));
    until([&] { return controller->property("tool").toString() == "eraser"; });
    EXPECT_EQ(controller->property("tool").toString(), "eraser");
    QQuickItem* inDock = nullptr;
    until([&] { return (inDock = toolEntry(eraser)) && shownInWindow(inDock); });
    ASSERT_NE(inDock, nullptr);
    EXPECT_TRUE(sceneRect(named("phoneDock")).adjusted(-1, -1, 1, 1).contains(sceneRect(inDock)))
            << "the tool in hand is in the dock";
    // The hand: from the sheet's other tools
    click(findItem("toolboxAllButton"));
    ASSERT_TRUE(opened(toolSheet, true));
    settled(toolSheet);
    click(findItem("toolCell_hand"));
    until([&] { return controller->property("tool").toString() == "hand"; });
    EXPECT_EQ(controller->property("tool").toString(), "hand");
    controller->applyToolEntry(pen);
}

// The page number of the dock opens all pages; there the contents and the zoom (its fits, as a sheet; a fit goes back
// to the page)
TEST_F(PhoneChromeTest, thePageNumberOpensThePagesWithTheContentsAndTheZoom) {
    openDocument();
    resize(412, 915);
    auto* grid = named("pageGrid");
    click(findItem("toolboxPageButton"));
    until([&] { return grid->isVisible(); });
    ASSERT_TRUE(grid->isVisible());
    for (const char* name: {"pageGridContentsButton", "pageGridZoomButton", "selectModeButton"}) {
        EXPECT_TRUE(shownInWindow(findItem(name))) << name;
    }
    EXPECT_FALSE(findItem("pageGridColumns")->isVisible()) << "the columns follow the pinch";
    click(findItem("pageGridZoomButton"));
    QObject* s = sheet();
    ASSERT_TRUE(opened(s, true));
    EXPECT_EQ(s->property("menu").value<QObject*>(), window->findChild<QObject*>("fitMenu"));
    settled(s);
    click(sheetRow("fitWidthItem"));
    EXPECT_TRUE(opened(s, false));
    until([&] { return !grid->isVisible(); });
    EXPECT_FALSE(grid->isVisible()) << "a fit chosen: back to the page";
    click(findItem("toolboxPageButton"));
    until([&] { return grid->isVisible(); });
    click(findItem("pageGridContentsButton"));
    auto* contents = findItem("contentsOverview");
    if (contents) {
        until([&] { return contents->isVisible(); });
        EXPECT_TRUE(contents->isVisible()) << "the contents";
        QMetaObject::invokeMethod(contents, "close");
    }
    EXPECT_FALSE(grid->isVisible());
}

// Zen (only the page and the dot) is automatic only in a tiny window (under 360 px either way); a phone keeps its tools.
// Leaving it there is remembered for tiny windows (qt/docs/zen.md).
TEST_F(PhoneChromeTest, zenIsAutomaticOnlyInATinyWindow) {
    openDocument();
    auto chrome = [&] { return window->property("chromeMode").toString(); };
    for (const WindowSize& s: {WindowSize{412, 915, ""}, WindowSize{915, 412, ""}, WindowSize{1280, 500, ""},
                               WindowSize{960, 1392, ""}}) {
        resize(s.w, s.h);
        EXPECT_EQ(chrome(), "full") << s.w << "x" << s.h;
        EXPECT_FALSE(flag("zen")) << s.w << "x" << s.h;
    }
    for (const WindowSize& s: {WindowSize{340, 700, ""}, WindowSize{700, 340, ""}}) {
        resize(s.w, s.h);
        ASSERT_EQ(sizeClass(), "tiny");
        EXPECT_TRUE(flag("zen")) << s.w << "x" << s.h << ": Zen, automatically";
        EXPECT_FALSE(flag("readOnlyOn")) << "Zen writes";
        EXPECT_TRUE(flag("hudHidden"));
        EXPECT_FALSE(named("phoneAppBar")->isVisible());
        EXPECT_FALSE(named("phoneDock")->isVisible());
        EXPECT_TRUE(findItem("zenDot")->isVisible()) << "the dot";
    }
    // Its pill fits the tiny window
    click(findItem("zenDot"));
    auto* pill = findItem("zenPill");
    until([&] { return pill->isVisible(); });
    ASSERT_TRUE(pill->isVisible());
    EXPECT_TRUE(QRectF(0, 0, window->width(), window->height()).contains(sceneRect(pill))) << "the pill fits";
    click(findItem("zenShowControls"));
    EXPECT_FALSE(flag("zen")) << "Show controls";
    EXPECT_EQ(choice("tiny", "zen"), "off") << "remembered for tiny windows";
    EXPECT_TRUE(named("phoneDock")->isVisible());
    resize(340, 700);
    EXPECT_FALSE(flag("zen")) << "a tiny window of the other shape: the same class, the same choice";
    resize(412, 915);
    // Read by hand on a phone (⋮ → View → Read): Zen and read only, full screen (ReadingPhoneTest has more)
    QMetaObject::invokeMethod(window->findChild<QObject*>("readItem"), "triggered");
    wait(50);
    EXPECT_TRUE(flag("fullScreenMode"));
    EXPECT_TRUE(flag("readOnlyOn"));
    EXPECT_TRUE(flag("zen"));
    EXPECT_EQ(choice("phonePortrait", "zen"), "") << "nothing remembered outside a tiny window";
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_FALSE(flag("readOnlyOn")) << "Esc ends Read: read only";
    EXPECT_FALSE(flag("zen")) << "... Zen";
    EXPECT_FALSE(flag("fullScreenMode")) << "... and the full screen it entered";
    EXPECT_EQ(chrome(), "full");
    // Zen by hand in the tiny window again: automatic again there
    resize(340, 700);
    QMetaObject::invokeMethod(window, "setZen", Q_ARG(QVariant, true));
    wait(50);
    EXPECT_TRUE(flag("zen"));
    EXPECT_EQ(choice("tiny", "zen"), "");
    // Read in a tiny window: stays in the window; ended, Zen stays (it was on before)
    QMetaObject::invokeMethod(window, "toggleReading");
    wait(50);
    EXPECT_TRUE(flag("readOnlyOn"));
    EXPECT_FALSE(flag("fullScreenMode")) << "a tiny window stays a window";
    EXPECT_EQ(window->width(), 340);
    QMetaObject::invokeMethod(window, "toggleReading");
    wait(50);
    EXPECT_FALSE(flag("readOnlyOn"));
    EXPECT_TRUE(flag("zen")) << "Zen was on before Read";
    // From 0.7.0: the reader chrome left in a tiny window (its choice "full") stays left
    QMetaObject::invokeMethod(settings, "resetLayoutChoices");
    QMetaObject::invokeMethod(settings, "setLayoutChoice", Q_ARG(QString, "tiny"), Q_ARG(QString, "chrome"),
                              Q_ARG(QString, "full"));
    wait(50);
    EXPECT_FALSE(flag("zen")) << "the reader left there in 0.7.0";
    window->showNormal();
}

// Presenting: with the controls no dot (Ctrl+F5 or the floating toolbox's ⋯ hide them); without controls (Zen) the
// dot, whose pill brings them back (presentingWithoutControls has more)
TEST_F(PhoneChromeTest, presentingWithoutControlsHasTheZenDot) {
    openDocument();
    resize(1280, 800);
    auto* dot = findItem("zenDot");
    ASSERT_NE(dot, nullptr);
    EXPECT_EQ(findItem("presentCornerMark"), nullptr) << "presenting's own corner field is gone";
    QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
    wait(100);
    EXPECT_FALSE(dot->isVisible()) << "with the controls: no dot";
    EXPECT_TRUE(findItem("toolbox")->isVisible());
    QTest::keyClick(window, Qt::Key_F5, Qt::ControlModifier);
    wait(50);
    EXPECT_TRUE(flag("presentClean"));
    EXPECT_TRUE(flag("zen")) << "present without controls = present + Zen";
    EXPECT_TRUE(dot->isVisible());
    EXPECT_FALSE(findItem("toolbox")->isVisible());
    click(dot);
    until([&] { return findItem("zenPill")->isVisible(); });
    click(findItem("zenShowControls"));
    EXPECT_FALSE(flag("presentClean")) << "Show controls: presenting with them";
    EXPECT_TRUE(controller->property("presenting").toBool());
    EXPECT_FALSE(dot->isVisible());
    controller->setProperty("presenting", false);
    EXPECT_FALSE(flag("zen")) << "presenting's Zen ends with it";
    window->setProperty("fullScreenMode", false);
    wait(100);
    window->showNormal();
}

// Android and iOS have one window: no tab is dragged out into a window of its own, and the tab menu offers no window of
// its own (the platform, not the size: here a tablet's tab strip)
TEST_F(PhoneChromeTest, oneWindowOnAndroidAndIos) {
    openThree();
    resize(960, 1392);
    auto* strip = named("tabStrip");
    ASSERT_TRUE(strip->isVisible());
    auto* list = findItem("tabList");
    QQuickItem* tab = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, tab), Q_ARG(int, 0));
    ASSERT_NE(tab, nullptr);
    auto* undock = tab->findChild<QObject*>("undockTabItem");
    ASSERT_NE(undock, nullptr);
    QObject* drag = nullptr;
    for (QObject* o: tab->findChildren<QObject*>()) {
        if (o->inherits("QQuickDragHandler")) {
            drag = o;
        }
    }
    ASSERT_NE(drag, nullptr);
    EXPECT_TRUE(undock->property("offered").toBool()) << "a desktop: a window of its own";
    EXPECT_TRUE(drag->property("enabled").toBool());
    adaptive->setProperty("mobilePlatform", true);
    wait(50);
    EXPECT_FALSE(undock->property("offered").toBool()) << "Android: no window of its own";
    EXPECT_FALSE(drag->property("enabled").toBool()) << "Android: no tab dragged out";
    adaptive->setProperty("mobilePlatform", false);
}

// The library's top: no row of breadcrumbs that only repeats its name (every size); inside a folder they are there
TEST_F(PhoneChromeTest, theLibrarysTopHasNoBreadcrumbs) {
    QObject* library = controller->libraryModel();
    for (const WindowSize& s: {WindowSize{1920, 1080, ""}, WindowSize{412, 915, ""}, WindowSize{915, 412, ""}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        library->setProperty("folder", "");
        wait(100);
        EXPECT_FALSE(findItem("crumbArea")->isVisible()) << at;
        EXPECT_FALSE(findItem("folderUpButton")->isVisible()) << at;
        library->setProperty("folder", "Physics");
        wait(100);
        EXPECT_TRUE(findItem("crumbArea")->isVisible()) << at;
        EXPECT_TRUE(shownInWindow(findItem("folderUpButton"))) << at;
        library->setProperty("folder", "");
        wait(50);
    }
}

// The Galaxy Fold 7: folded (412 x 915) the phone chrome, unfolded (900 x 1000) the tablet's (the tab strip, two tool
// rows at the top); unfolding and folding switch at once, and back
TEST_F(PhoneChromeTest, theFold7FoldedAndUnfolded) {
    openThree();
    for (int round = 0; round < 2; ++round) {
        resize(412, 915);
        EXPECT_EQ(sizeClass(), "phonePortrait");
        EXPECT_TRUE(phoneChrome());
        EXPECT_TRUE(named("phoneAppBar")->isVisible());
        EXPECT_TRUE(named("phoneDock")->isVisible());
        EXPECT_FALSE(named("tabStrip")->isVisible());
        EXPECT_FALSE(named("topTools")->isVisible());
        for (const WindowSize& s: {WindowSize{900, 1000, "fold7-inner"}, WindowSize{960, 1392, ""}}) {
            resize(s.w, s.h);
            const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
            // (under load the bar and the toolbox lay themselves out a little after the jump from the phone's dock)
            until([&] {
                auto* box = named("toolbox");
                return shownInWindow(named("moreButton")) && box && shownInWindow(box)
                       && !box->property("compact").toBool() && shownInWindow(named("toolboxUndoButton"));
            });
            EXPECT_EQ(sizeClass(), "tabletPortrait") << at << ": at once (a jump)";
            EXPECT_FALSE(phoneChrome()) << at;
            EXPECT_FALSE(named("phoneAppBar")->isVisible()) << at;
            EXPECT_FALSE(named("phoneDock")->isVisible()) << at;
            EXPECT_TRUE(named("tabStrip")->isVisible()) << at << ": the tab strip";
            EXPECT_TRUE(named("topTools")->isVisible()) << at;
            EXPECT_EQ(toolPlan().value("layout").toString(), "row") << at << ": the command bar, one row";
            EXPECT_FALSE(named("toolbox")->property("compact").toBool()) << at << ": the toolbox docked, not the dock";
            EXPECT_TRUE(named("viewPill")->isVisible()) << at;
            checkToolBar(at);
            expectInside("doc");
        }
    }
}

// --- safe areas and the soft keyboard (qt/safe-areas-keyboard) -----------------------------------------------------

namespace {
class SafeAreasKeyboardTest: public PhoneChromeTest {
protected:
    void TearDown() override {
        if (window) {
            setInsets(0, 0, 0, 0);
            window->setProperty("fakeKeyboardHeight", 0);
        }
        PhoneChromeTest::TearDown();
    }
    double top = 0, right = 0, bottom = 0, left = 0;
    /// Fake safe area margins (as main.cpp sets them from the window's on Qt 6.9+)
    void setInsets(double t, double r, double b, double l) {
        top = t, right = r, bottom = b, left = l;
        window->setProperty("safeTop", t);
        window->setProperty("safeRight", r);
        window->setProperty("safeBottom", b);
        window->setProperty("safeLeft", l);
        wait(100);
    }
    /// A fake soft keyboard of this height at the window's bottom (0: none)
    void setKeyboard(double height) {
        window->setProperty("fakeKeyboardHeight", height);
        wait(150);
    }
    double keyboardTop() const { return window->property("keyboardTop").toDouble(); }
    QRectF safeRect() const { return QRectF(left, top, window->width() - left - right, window->height() - top - bottom); }
    /// The enabled controls under `root` (buttons, fields) whose shown part lies in the insets (or below `bottomEdge`)
    QStringList inInsets(QQuickItem* root, double bottomEdge = -1) const {
        QRectF safe = safeRect();
        if (bottomEdge >= 0) {
            safe.setBottom(std::min(safe.bottom(), bottomEdge));
        }
        QStringList found;
        std::function<void(QQuickItem*, QRectF)> walk = [&](QQuickItem* i, QRectF clip) {
            if (!i->isVisible() || i->opacity() <= 0.01) {
                return;
            }
            const QRectF r = sceneRect(i);
            const bool control = i->inherits("QQuickAbstractButton") || i->inherits("QQuickTextInput") ||
                                 i->inherits("QQuickTextEdit") || i->inherits("QQuickScrollBar");
            const QRectF shown = r.intersected(clip);
            if (control && i->isEnabled() && shown.width() > 1 && shown.height() > 1 &&
                !safe.contains(shown.adjusted(0.5, 0.5, -0.5, -0.5))) {
                found << QString("%1@%2,%3 %4x%5")
                                 .arg(xqt::uitest::labelOf(i))
                                 .arg(shown.x())
                                 .arg(shown.y())
                                 .arg(shown.width())
                                 .arg(shown.height());
            }
            const QRectF inner = i->clip() ? clip.intersected(r) : clip;
            for (QQuickItem* c: i->childItems()) {
                walk(c, inner);
            }
        };
        walk(root, QRectF(-1e6, -1e6, 2e6, 2e6));
        return found;
    }
    void expectClear(QQuickItem* root, const std::string& what, double bottomEdge = -1) {
        ASSERT_NE(root, nullptr) << what;
        const QStringList found = inInsets(root, bottomEdge);
        EXPECT_TRUE(found.isEmpty()) << what << ": in the safe area's insets: " << found.join("; ").toStdString();
    }
    void expectClear(const char* name, const std::string& at) {
        QQuickItem* item = named(name);
        if (item && item->isVisible()) {
            expectClear(item, at + ": " + name);
        }
    }
    /// An object by its name anywhere in the scene: an item, or an object declared in one (a menu in a delegate)
    QObject* anywhere(const char* name) const {
        QObject* found = nullptr;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (found) {
                return;
            }
            if (i->objectName() == name) {
                found = i;
                return;
            }
            for (QObject* o: i->children()) {
                if (o->objectName() == name) {
                    found = o;
                    return;
                }
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(window->contentItem());
        return found ? found : window->findChild<QObject*>(name);  // (in a popup that is not open)
    }
    /// The visible items of this name under `root`
    std::vector<QQuickItem*> itemsNamed(QQuickItem* root, const char* name) const {
        std::vector<QQuickItem*> items;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (i->objectName() == name && i->isVisible()) {
                items.push_back(i);
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(root);
        return items;
    }
    /// XQT_SAFE_AREA_SHOTS=<folder>: a picture of the window now (to look at; the insets are drawn as red bands)
    void shot(const std::string& name) {
        const QString dir = qEnvironmentVariable("XQT_SAFE_AREA_SHOTS");
        if (dir.isEmpty()) {
            return;
        }
        wait(250);
        QImage img = window->grabWindow();
        QPainter p(&img);
        const QColor band(255, 0, 0, 60);
        const double w = window->width(), h = window->height();
        p.fillRect(QRectF(0, 0, w, top), band);
        p.fillRect(QRectF(0, h - bottom, w, bottom), band);
        p.fillRect(QRectF(0, 0, left, h), band);
        p.fillRect(QRectF(w - right, 0, right, h), band);
        if (window->property("keyboardHeight").toDouble() > 0) {
            p.fillRect(QRectF(0, keyboardTop(), w, h - keyboardTop()), QColor(60, 60, 60, 200));
        }
        p.end();
        QDir().mkpath(dir);
        img.save(dir + "/" + QString::fromStdString(name) + ".png");
    }
    QQuickItem* popupItem(QObject* popup) const {
        auto* content = popup->property("contentItem").value<QQuickItem*>();
        return content ? content->parentItem() : nullptr;
    }
    void openMenu(QObject* menu) {
        QMetaObject::invokeMethod(menu, "openMenu", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
    }
    /// The menu opened as the phone's sheet, at the window's bottom (or on the keyboard); closed again
    void expectSheetOf(QObject* menu, const std::string& at) {
        ASSERT_NE(menu, nullptr) << at;
        QObject* s = sheet();
        ASSERT_TRUE(opened(s, true)) << at << ": " << menu->objectName().toStdString() << " as a sheet";
        EXPECT_EQ(s->property("menu").value<QObject*>(), menu) << at;
        EXPECT_FALSE(menu->property("visible").toBool()) << at << ": the menu itself stays closed";
        settled(s);
        const QRectF r = popupRect(s);
        EXPECT_TRUE(insideWindow(r)) << at;
        EXPECT_NEAR(r.bottom(), keyboardTop(), 1.5) << at << ": at the bottom";
        EXPECT_FALSE(sheetRows().empty()) << at;
        QTest::keyClick(window, Qt::Key_Escape);
        EXPECT_TRUE(opened(s, false)) << at;
    }
    /// Writing the page's Markdown on the page, the canvas with the keys
    QQuickItem* writeOnPage() {
        EXPECT_TRUE(controller->writeMarkdownOnPage());
        until([&] { return controller->markdownOnPage(); });
        auto* canvas = named("canvas");
        canvas->forceActiveFocus();
        wait(80);
        return canvas;
    }
    /// The text cursor of the canvas, in the window
    QRectF caretOnScreen(QQuickItem* canvas) const {
        const QRectF c = canvas->inputMethodQuery(Qt::ImCursorRectangle).toRectF();
        return canvas->mapRectToScene(c);
    }
};
}  // namespace

// With a status bar (32), a gesture bar (24) and, sideways, a camera cut-out at the left (40): at the Fold 7's sizes
// no control of the app bar, the tab strip, the tool bar, the dock (or its rail), the pills, the drawer, the sheets,
// a dialog and the snackbar lies in them; the pages go on under them (edge to edge)
TEST_F(SafeAreasKeyboardTest, controlsStayOutOfTheSafeArea) {
    openThree();
    struct Case {
        WindowSize size;
        double left;
    };
    for (const Case& c: {Case{{412, 915, "phone-portrait"}, 0}, Case{{915, 412, "phone-landscape"}, 40},
                         Case{{900, 1000, "fold7-inner"}, 0}}) {
        resize(c.size.w, c.size.h);
        setInsets(32, 0, 24, c.left);
        const std::string at = std::to_string(c.size.w) + "x" + std::to_string(c.size.h);
        shot("insets-" + at);
        for (const char* name: {"phoneAppBar", "tabStrip", "topTools", "phoneDock", "viewPill", "navPill", "sidebarArrow",
                                "horizontalScrollBar", "verticalScrollBar"}) {
            expectClear(name, at);
        }
        // The pages under the bars: edge to edge where no bar is in the way
        const QRectF canvas = sceneRect(named("canvas"));
        EXPECT_NEAR(canvas.left(), 0, 1) << at << ": the page goes on under a cut-out";
        if (!phoneChrome()) {
            EXPECT_NEAR(canvas.bottom(), c.size.h, 1) << at << ": the page goes on under the gesture bar";
        }
        // The snackbar
        auto* snackbar = named("snackbar");
        QMetaObject::invokeMethod(snackbar, "show", Q_ARG(QVariant, "Page deleted"), Q_ARG(QVariant, true),
                                  Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
        wait(50);
        expectClear(snackbar, at + ": the snackbar");
        snackbar->setProperty("visible", false);
        // The sidebar as a drawer
        QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
        until([&] { return window->property("drawerSlide").toDouble() >= 1; });
        shot("insets-drawer-" + at);
        expectClear(named("sidebar"), at + ": the drawer");
        EXPECT_GE(sceneRect(named("sidebar")).left(), c.left - 0.5) << at << ": the drawer beside the cut-out";
        QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, false));
        until([&] { return window->property("drawerSlide").toDouble() <= 0; });
        // ⋮: a sheet on a phone, a menu on the tablet
        click(named("moreButton"));
        if (phoneClass()) {
            ASSERT_TRUE(opened(sheet(), true)) << at;
            settled(sheet());
            shot("insets-sheet-" + at);
            expectClear(popupItem(sheet()), at + ": the menu's sheet");
            QTest::keyClick(window, Qt::Key_Escape);
            EXPECT_TRUE(opened(sheet(), false)) << at;
        } else {
            QObject* menu = window->findChild<QObject*>("moreMenu");
            ASSERT_TRUE(opened(menu, true)) << at;
            settled(menu);
            expectClear(popupItem(menu), at + ": ⋮");
            QTest::keyClick(window, Qt::Key_Escape);
            EXPECT_TRUE(opened(menu, false)) << at;
        }
        // A dialog (a question: at the bottom of a phone upright, full screen held sideways)
        QObject* dialog = window->findChild<QObject*>("shareDialog");
        QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, ""));
        ASSERT_TRUE(opened(dialog, true)) << at;
        settled(dialog);
        expectClear(popupItem(dialog), at + ": a dialog");
        QMetaObject::invokeMethod(dialog, "close");
        EXPECT_TRUE(opened(dialog, false)) << at;
        // All pages: its pill
        auto* grid = named("pageGrid");
        QMetaObject::invokeMethod(grid, "open");
        until([&] { return grid->isVisible(); });
        expectClear("pageGridPill", at);
        QMetaObject::invokeMethod(grid, "close");
        until([&] { return !grid->isVisible(); });
        // The compact chrome: the floating toolbox (on a phone too), the tab dots
        controller->selectTool("pen");
        compactChrome(true);
        wait(150);
        shot("insets-compact-" + at);
        for (const char* name: {"toolbox", "fullScreenTabs", "viewPill"}) {
            expectClear(name, at + " compact");
        }
        EXPECT_TRUE(named("toolbox")->isVisible()) << at;
        EXPECT_TRUE(named("toolbox")->property("floating").toBool()) << at;
        compactChrome(false);
        wait(100);
        expectInside("doc");
    }
}

// A soft keyboard (fake, 360 px) while Markdown is written on the page at 412 x 915: the dock goes, the format bar sits
// right above the keyboard, the page ends above it and keeps the text cursor in view while lines are typed at the
// page's bottom; a sheet, a dialog and the snackbar sit above the keyboard; without it all is back
TEST_F(SafeAreasKeyboardTest, theFormatBarDocksAboveTheKeyboardAndTheCursorStaysInView) {
    openDocument();
    resize(412, 915);
    setInsets(32, 0, 24, 0);
    auto* canvas = writeOnPage();
    auto* bar = named("markdownFormatBar");
    ASSERT_NE(bar, nullptr);
    ASSERT_TRUE(bar->isVisible());
    EXPECT_LT(sceneRect(bar).top(), 200) << "without the keyboard: at the top";
    EXPECT_TRUE(named("phoneDock")->isVisible());

    setKeyboard(360);
    const double kb = keyboardTop();
    EXPECT_NEAR(kb, 915 - 360, 0.5);
    EXPECT_FALSE(named("phoneDock")->isVisible()) << "the dock goes while the keyboard is open";
    ASSERT_TRUE(bar->isVisible());
    EXPECT_TRUE(bar->property("docked").toBool());
    // Undo and redo one tap away: at the end of the format bar (the dock's are gone with it)
    for (const char* name: {"keyboardUndoButton", "keyboardRedoButton"}) {
        auto* b = findItem(name);
        ASSERT_NE(b, nullptr) << name;
        EXPECT_TRUE(shownInWindow(b)) << name;
        EXPECT_TRUE(sceneRect(bar).adjusted(-1, -1, 1, 1).contains(sceneRect(b))) << name << " in the format bar";
    }
    EXPECT_NEAR(sceneRect(bar).bottom(), kb, 1) << "the format bar right above the keyboard";
    EXPECT_LE(sceneRect(canvas).bottom(), sceneRect(bar).top() + 1) << "the page ends above it";
    expectClear(bar, "the docked format bar", kb);

    // Lines typed: the cursor stays in view above the bar
    for (int i = 0; i < 30; ++i) {
        for (const char ch: std::string("line")) {
            QTest::keyClick(window, ch);
        }
        QTest::keyClick(window, Qt::Key_Return);
        wait(10);
        if (i % 5 == 4) {
            const QRectF caret = caretOnScreen(canvas);
            EXPECT_GE(caret.top(), sceneRect(canvas).top() - 0.5) << "line " << i;
            EXPECT_LE(caret.bottom(), sceneRect(bar).top() + 0.5) << "line " << i << ": the cursor above the bar";
        }
    }
    shot("keyboard-format-bar");
    // The keyboard opening while the cursor is low on the screen (below where the keyboard comes): the page scrolls it
    // into view
    setKeyboard(0);
    const double lowest = sceneRect(canvas).bottom() - 30;
    until([&] {  // (the layout of the text typed may still settle)
        const double down = lowest - caretOnScreen(canvas).bottom();
        QMetaObject::invokeMethod(canvas, "scrollTo", Q_ARG(qreal, canvas->property("contentX").toDouble()),
                                  Q_ARG(qreal, canvas->property("contentY").toDouble() - down));
        wait(30);
        return caretOnScreen(canvas).bottom() > kb;
    });
    ASSERT_GT(caretOnScreen(canvas).bottom(), kb) << "(without the keyboard the cursor is where the keyboard comes)";
    setKeyboard(360);
    EXPECT_LE(caretOnScreen(canvas).bottom(), sceneRect(bar).top() + 0.5) << "scrolled into view when the keyboard came";

    // The snackbar above it
    auto* snackbar = named("snackbar");
    QMetaObject::invokeMethod(snackbar, "show", Q_ARG(QVariant, "Page deleted"), Q_ARG(QVariant, true),
                              Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
    wait(50);
    EXPECT_LE(sceneRect(snackbar).bottom(), kb) << "the snackbar above the keyboard";
    snackbar->setProperty("visible", false);
    // A sheet and a dialog on the keyboard
    openMenu(window->findChild<QObject*>("moreMenu"));
    expectSheetOf(window->findChild<QObject*>("moreMenu"), "⋮ with the keyboard");
    QObject* dialog = window->findChild<QObject*>("shareDialog");
    QMetaObject::invokeMethod(dialog, "openFor", Q_ARG(QVariant, ""));
    ASSERT_TRUE(opened(dialog, true));
    settled(dialog);
    EXPECT_LE(popupRect(dialog).bottom(), kb + 0.5) << "a dialog above the keyboard";
    QMetaObject::invokeMethod(dialog, "close");
    EXPECT_TRUE(opened(dialog, false));

    // Without the keyboard: the dock again, the bar at the top
    setKeyboard(0);
    EXPECT_TRUE(named("phoneDock")->isVisible());
    EXPECT_FALSE(findItem("keyboardUndoButton")->isVisible()) << "the dock's undo again";
    EXPECT_FALSE(bar->property("docked").toBool());
    EXPECT_LT(sceneRect(bar).top(), 200);
    controller->endMarkdownOnPage();
}

// The Markdown source below the page at 412 x 915 with the keyboard: the panel ends above it, its format bar sits right
// above the keyboard, and the text's cursor stays in view while lines are typed
TEST_F(SafeAreasKeyboardTest, theSourcePanelsFormatBarAndCursorWithTheKeyboard) {
    openDocument();
    resize(412, 915);
    setInsets(32, 0, 24, 0);
    auto* panel = named("markdownPanel");
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
    until([&] { return panel->isVisible(); });
    auto* area = findItem("markdownArea");
    ASSERT_NE(area, nullptr);
    area->forceActiveFocus();
    wait(50);
    auto* bar = findItem("panelMarkdownFormatBar");
    ASSERT_NE(bar, nullptr);
    EXPECT_LE(sceneRect(panel).bottom(), 915 - 56 + 1) << "above the dock";
    const double barTopBefore = sceneRect(bar).top();

    setKeyboard(360);
    const double kb = keyboardTop();
    EXPECT_LE(sceneRect(panel).bottom(), kb + 0.5) << "the panel ends above the keyboard";
    EXPECT_NEAR(sceneRect(bar).bottom(), kb, 1.5) << "its format bar right above the keyboard";
    EXPECT_GT(sceneRect(bar).top(), barTopBefore) << "moved down from the panel's top";
    shot("keyboard-source-panel-before");
    for (int i = 0; i < 25; ++i) {
        for (const char ch: std::string("text")) {
            QTest::keyClick(window, ch);
        }
        QTest::keyClick(window, Qt::Key_Return);
        wait(5);
    }
    wait(100);
    shot("keyboard-source-panel");
    auto* scroll = area->parentItem();
    while (scroll && !scroll->inherits("QQuickFlickable")) {
        scroll = scroll->parentItem();
    }
    ASSERT_NE(scroll, nullptr);
    const QRectF caret = area->mapRectToScene(area->property("cursorRectangle").toRectF());
    const QRectF view = sceneRect(scroll);
    EXPECT_GE(caret.top(), view.top() - 0.5) << "the cursor in the text's view";
    EXPECT_LE(caret.bottom(), view.bottom() + 0.5) << "the cursor in the text's view";
    EXPECT_LE(caret.bottom(), sceneRect(bar).top() + 0.5) << "above the format bar and the keyboard";

    setKeyboard(0);
    EXPECT_NEAR(sceneRect(bar).top(), barTopBefore, 1) << "back at the panel's top";
    QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, false));
}

// The touch profile: the tab strip's buttons and the overview's card buttons (×, the star, the reference) are a
// finger's size (audit F14); the star is there without hover
TEST_F(SafeAreasKeyboardTest, touchTargetsOfTheTabStripTheOverviewAndTheSidebar) {
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "on"));
    openThree();
    resize(1280, 800);
    const double target = adaptive->property("minTarget").toDouble();
    ASSERT_EQ(target, 48);
    for (const char* name: {"overviewButton", "previousTabButton", "nextTabButton", "newTabButton", "tabCloseButton"}) {
        auto* b = findItem(name);
        ASSERT_NE(b, nullptr) << name;
        EXPECT_GE(b->width(), target) << name;
        EXPECT_GE(b->height(), target) << name;
    }
    EXPECT_TRUE(insideWindow(sceneRect(named("tabStrip"))));
    auto* overview = window->findChild<QObject*>("tabOverview");
    QMetaObject::invokeMethod(overview, "open");
    ASSERT_TRUE(opened(overview, true));
    settled(overview);
    for (const char* name: {"overviewCloseButton", "overviewStar", "overviewReferenceButton"}) {
        int seen = 0;
        for (QQuickItem* b: itemsNamed(popupItem(overview), name)) {
            ++seen;
            EXPECT_GE(b->width(), target) << name;
            EXPECT_GE(b->height(), target) << name;
        }
        EXPECT_GT(seen, 0) << name << " shown (the star too: no hover with fingers)";
    }
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(overview, false));
    // The sidebar's layers
    auto* sidebar = named("sidebar");
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
    sidebar->setProperty("mode", "layers");
    wait(150);
    for (const char* name: {"layerVisibleButton", "showAllLayersButton"}) {
        auto* b = findItem(name);
        ASSERT_NE(b, nullptr) << name;
        EXPECT_GE(b->width(), target) << name;
        EXPECT_GE(b->height(), target) << name;
    }
    sidebar->setProperty("mode", "pages");
    QMetaObject::invokeMethod(settings, "set", Q_ARG(QString, "touchProfile"), Q_ARG(QVariant, "auto"));
}

// The menus that were plain menus open as the sheet on a phone: the layer menu, the annotations' filter, a bookmark's
// menu, the look-up menu (the web addresses as the rows' second line), the table editor's cell menu, the pen pill's
// color menu; the reference's page field and the emoji picker are sheets there too
TEST_F(SafeAreasKeyboardTest, theRemainingMenusAreSheetsOnAPhone) {
    openDocument();
    ASSERT_TRUE(controller->toggleBookmark(0));
    resize(412, 915);
    setInsets(32, 0, 24, 0);
    auto* sidebar = named("sidebar");
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
    sidebar->setProperty("mode", "layers");
    wait(150);
    click(findItem("layerMenuButton"));
    expectSheetOf(anywhere("layerMenu"), "layers");
    sidebar->setProperty("mode", "annotations");
    wait(150);
    click(findItem("annotationFilter"));
    expectSheetOf(anywhere("annotationFilterMenu"), "annotations");
    sidebar->setProperty("mode", "contents");
    wait(150);
    auto* bookmark = findItem("bookmarkEntry");
    ASSERT_NE(bookmark, nullptr);
    QMetaObject::invokeMethod(bookmark, "pressAndHold");  // (a long press on it)
    expectSheetOf(anywhere("bookmarkMenu"), "a bookmark");
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, false));
    wait(250);

    QObject* lookUp = anywhere("lookUpMenu");
    ASSERT_NE(lookUp, nullptr);
    lookUp->setProperty("text", "Kalman filter");
    openMenu(lookUp);
    ASSERT_TRUE(opened(sheet(), true));
    bool detail = false;
    for (QQuickItem* row: sheetRows()) {
        for (QQuickItem* c: row->childItems()) {
            detail = detail || (c->objectName() == "menuSheetRowDetail" && c->isVisible() &&
                                !c->property("text").toString().isEmpty());
        }
    }
    EXPECT_TRUE(detail) << "the web address under the entry's name";
    shot("sheet-look-up");
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(sheet(), false));
    openMenu(lookUp);
    expectSheetOf(lookUp, "look up");

    QObject* cellMenu = anywhere("tableCellMenu");
    ASSERT_NE(cellMenu, nullptr);
    openMenu(cellMenu);
    expectSheetOf(cellMenu, "a table cell");
    QObject* entryMenu = anywhere("toolEntryMenu");  // (a tool's menu: a long press on it in the toolbox)
    ASSERT_NE(entryMenu, nullptr);
    openMenu(entryMenu);
    expectSheetOf(entryMenu, "a tool's menu");

    for (const char* name: {"referencePagePopup", "emojiPicker"}) {
        QObject* popup = anywhere(name);
        ASSERT_NE(popup, nullptr) << name;
        EXPECT_TRUE(popup->property("asSheet").toBool()) << name;
        QMetaObject::invokeMethod(popup, "open");
        ASSERT_TRUE(opened(popup, true)) << name;
        wait(100);
        const QRectF r = popupRect(popup);
        EXPECT_NEAR(r.bottom(), 915, 1.5) << name << ": at the bottom";
        EXPECT_NEAR(r.width(), 412, 1) << name << ": across the width";
        expectClear(popupItem(popup), name);
        QMetaObject::invokeMethod(popup, "close");
        EXPECT_TRUE(opened(popup, false)) << name;
    }
}

// The home screen's icon buttons have a short name, shown while a finger is held on them (IconButton's label; the
// tip keeps the longer text for the mouse)
TEST_F(SafeAreasKeyboardTest, theHomeScreensIconButtonsHaveShortLabels) {
    int seen = 0;
    std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
        if (i->inherits("QQuickToolButton") && i->property("ownHold").isValid() &&
            !i->property("iconName").toString().isEmpty()) {
            ++seen;
            const QString label = i->property("label").toString();
            EXPECT_FALSE(label.isEmpty()) << xqt::uitest::labelOf(i).toStdString() << ": a label";
            EXPECT_LE(label.size(), 30) << xqt::uitest::labelOf(i).toStdString() << ": short";
        }
        for (QQuickItem* c: i->childItems()) {
            walk(c);
        }
    };
    walk(named("homeView"));
    EXPECT_GE(seen, 10);
}

// Writing on the page chosen from the sheet of all tools keeps the focus on the page, also after the sheet's closing
// animation (a closing popup gives the focus back to what had it: the window or the button; on Android the on-screen
// keyboard closes as soon as something that takes no text has the focus); a tap on a dock button takes no focus
TEST_F(PhoneChromeTest, writingChosenFromTheSheetKeepsTheFocusOnThePage) {
    openDocument();
    resize(412, 915);
    auto* canvas = named("canvas");
    ASSERT_NE(canvas, nullptr);
    click(findItem("toolboxAllButton"));
    auto* toolSheet = window->findChild<QObject*>("phoneToolSheet");
    ASSERT_TRUE(opened(toolSheet, true));
    settled(toolSheet);
    click(findItem("toolCell_write"));
    EXPECT_TRUE(opened(toolSheet, false));
    until([&] { return controller->markdownOnPage(); });
    ASSERT_TRUE(controller->markdownOnPage());
    wait(600);  // (the sheet's closing animation is over)
    EXPECT_TRUE(canvas->hasActiveFocus()) << "the page keeps the focus while its text is written";
    EXPECT_TRUE(canvas->property("textEditing").toBool());
    EXPECT_EQ(canvas->inputMethodQuery(Qt::ImEnabled).toBool(), true);
    // Qt's Android input drops the keyboard's text for a focus object without this property (only Enter arrives)
    EXPECT_TRUE(canvas->property("inputMethodHints").isValid()) << "the keyboard's text reaches the page";
    // Enter starts a new line: with EnterKeyReturn, Qt's Android input makes it the "done" key of a multi-line text,
    // which closes the keyboard
    EXPECT_EQ(canvas->inputMethodQuery(Qt::ImEnterKeyType).toInt(), static_cast<int>(Qt::EnterKeyDefault));
    EXPECT_TRUE(canvas->inputMethodQuery(Qt::ImHints).toInt() & Qt::ImhMultiLine);

    // What Android does (the device's log): the closing sheet gives the focus back to the button that opened it, or to
    // the window; the page takes it back while its text is written
    findItem("toolboxAllButton")->forceActiveFocus();
    until([&] { return canvas->hasActiveFocus(); });
    EXPECT_TRUE(canvas->hasActiveFocus()) << "back from the button";
    window->contentItem()->forceActiveFocus();
    until([&] { return canvas->hasActiveFocus(); });
    EXPECT_TRUE(canvas->hasActiveFocus()) << "back from the window";
    // A tap on a tool button never takes the focus (Tab still reaches it)
    EXPECT_EQ(findItem("toolboxUndoButton")->property("focusPolicy").toInt(), static_cast<int>(Qt::TabFocus));
    EXPECT_EQ(findItem("toolboxAllButton")->property("focusPolicy").toInt(), static_cast<int>(Qt::TabFocus));
    // Not while nothing is written: the button keeps it then
    controller->endMarkdownOnPage();
    until([&] { return !canvas->property("textEditing").toBool(); });
    findItem("toolboxAllButton")->forceActiveFocus();
    wait(100);
    EXPECT_FALSE(canvas->hasActiveFocus()) << "no text written: the focus stays where it went";
}

// --- the color palettes (qt/color-palettes) -------------------------------------------------------------------------

namespace {
/// The colors of a tool's editor (ToolEntryEditor.qml): the palette and its roles
class ColorChooserTest: public PhoneChromeTest {
protected:
    QQuickItem* inPopup(QObject* popup, const QString& name) const {
        std::function<QQuickItem*(QQuickItem*)> walk = [&](QQuickItem* i) -> QQuickItem* {
            if (i->objectName() == name && i->isVisible()) {
                return i;
            }
            for (QQuickItem* c: i->childItems()) {
                if (QQuickItem* f = walk(c)) {
                    return f;
                }
            }
            return nullptr;
        };
        auto* content = popup->property("contentItem").value<QQuickItem*>();
        return content ? walk(content) : nullptr;
    }
    /// The role cells shown in the editor, by role key
    QStringList rolesShown(QObject* popup) const {
        QStringList keys;
        std::function<void(QQuickItem*)> walk = [&](QQuickItem* i) {
            if (i->objectName().startsWith("editorRole_") && i->isVisible()) {
                keys << i->objectName().mid(11);
            }
            for (QQuickItem* c: i->childItems()) {
                walk(c);
            }
        };
        walk(popup->property("contentItem").value<QQuickItem*>());
        return keys;
    }
    /// The editor's palette chosen, as its combo box does
    void choosePalette(QObject* editor, const QString& paletteId) {
        QQuickItem* combo = inPopup(editor, "toolEditorPalette");
        ASSERT_NE(combo, nullptr);
        const QVariantList all = controller->property("colorPalettes").toList();
        int at = -1;
        for (int i = 0; i < all.size(); ++i) {
            at = all[i].toMap().value("id") == paletteId ? i : at;
        }
        ASSERT_GE(at, 0) << paletteId.toStdString();
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, at));
        wait(80);
    }
    QColor color() const { return controller->property("color").value<QColor>(); }
    void setUpPalettes() {
        controller->setColorPalette("classic");
        controller->toolboxModel()->reset();
    }
    /// The editor of the toolbox's entry `id`, opened by a tap on it in hand
    QObject* openEditor(const QString& id) {
        controller->applyToolEntry(id);
        QQuickItem* button = nullptr;
        until([&] { return (button = toolEntry(id)) && shownInWindow(button); });
        auto* editor = window->findChild<QObject*>("toolEntryEditor");
        if (button && editor) {
            click(button);
            opened(editor, true);
            settled(editor);
        }
        return editor;
    }
};
}  // namespace

// The colors of a tool (qt/docs/color-palettes.md): its editor shows the roles of the chosen palette, with their names
// (only the roles that palette defines), and the palette can be chosen there; a color taken from it remembers its
// role, and the tool follows when another palette is chosen. (The classic tool bar's chooser with a tab per palette
// went in 0.8.0.)
TEST_F(ColorChooserTest, theEditorOffersThePalettesRoles) {
    openDocument();
    setUpPalettes();
    resize(1280, 800);
    const QString pen = entryOf("pen");
    QObject* editor = openEditor(pen);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(editor->property("visible").toBool());
    EXPECT_TRUE(insideWindow(popupRect(editor)));
    choosePalette(editor, "marker");
    EXPECT_EQ(controller->colorPalette(), "marker") << "the palette chosen in the editor (app-wide)";
    EXPECT_EQ(rolesShown(editor), QStringList({"body", "warnings", "keyTerms", "examples", "definitions", "headings",
                                               "questions", "ideas"}));
    auto* keyTerms = inPopup(editor, "editorRole_keyTerms");
    ASSERT_NE(keyTerms, nullptr);
    EXPECT_EQ(keyTerms->property("c").value<QColor>(), QColor("#E8590C")) << "the pen: ink";

    choosePalette(editor, "colorblind-6");
    EXPECT_EQ(rolesShown(editor), QStringList({"body", "warnings", "keyTerms", "examples", "headings", "questions"}))
            << "only the roles it defines";

    choosePalette(editor, "marker");
    click(inPopup(editor, "editorRole_warnings"));
    until([&] { return color() == QColor("#E03131"); });
    EXPECT_EQ(color(), QColor("#E03131"));
    EXPECT_EQ(controller->toolboxModel()->entry(pen).value("role").toString(), "warnings");
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(editor, false));

    // Another palette chosen (Settings): the tool follows its role
    controller->setColorPalette("colorblind-6");
    EXPECT_EQ(color(), QColor("#D55E00"));
    // A color of one's own: no role any more
    editor = openEditor(pen);
    auto* hex = inPopup(editor, "toolEditorHex");
    ASSERT_NE(hex, nullptr);
    hex->setProperty("text", "#00aa00");
    QMetaObject::invokeMethod(hex, "accepted");
    until([&] { return color() == QColor("#00aa00"); });
    EXPECT_EQ(controller->toolboxModel()->entry(pen).value("role").toString(), "");
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(editor, false));
    controller->setColorPalette("classic");
    controller->toolboxModel()->reset();
}

// With the highlighter the palettes give their highlight colors
TEST_F(ColorChooserTest, theHighlighterTakesHighlightColors) {
    openDocument();
    setUpPalettes();
    resize(1920, 1080);
    const QString highlighter = entryOf("highlighter");
    QObject* editor = openEditor(highlighter);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(editor->property("visible").toBool());
    EXPECT_TRUE(insideWindow(popupRect(editor)));
    choosePalette(editor, "marker");
    auto* keyTerms = inPopup(editor, "editorRole_keyTerms");
    ASSERT_NE(keyTerms, nullptr);
    EXPECT_EQ(keyTerms->property("c").value<QColor>(), QColor("#FFE066")) << "yellow 3";
    click(keyTerms);
    until([&] { return color() == QColor("#FFE066"); });
    EXPECT_EQ(color(), QColor("#FFE066"));
    EXPECT_EQ(controller->toolboxModel()->entry(highlighter).value("role").toString(), "keyTerms");
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(editor, false));
    controller->setColorPalette("classic");
    controller->toolboxModel()->reset();
}

// On a phone the editor is a sheet at the bottom with the same colors; Settings, Pen chooses the palette and says
// where it comes from
TEST_F(ColorChooserTest, onAPhoneInTheEditorsSheetAndInTheSettings) {
    openDocument();
    setUpPalettes();
    resize(412, 915);
    const QString pen = entryOf("pen");
    QObject* editor = openEditor(pen);
    ASSERT_NE(editor, nullptr);
    ASSERT_TRUE(editor->property("visible").toBool()) << "a tap on the pen in hand in the dock";
    EXPECT_TRUE(editor->property("asSheet").toBool());
    choosePalette(editor, "pastel");
    const QRectF r = popupRect(editor);
    EXPECT_TRUE(insideWindow(r));
    EXPECT_NEAR(r.bottom(), window->height(), 1.5) << "at the bottom";
    EXPECT_EQ(rolesShown(editor).size(), 8);
    for (const QString& key: rolesShown(editor)) {
        auto* cell = inPopup(editor, "editorRole_" + key);
        EXPECT_TRUE(r.adjusted(-1, -1, 1, 1).contains(sceneRect(cell)) || inScrollingArea(cell)) << key.toStdString();
    }
    click(inPopup(editor, "editorRole_headings"));
    until([&] { return color() == QColor("#4C7BB8"); });
    EXPECT_EQ(color(), QColor("#4C7BB8"));
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(editor, false));
    resize(1280, 800);

    // Settings, Pen: the palette, and where it comes from
    auto* row = named("colorPaletteRow");
    ASSERT_NE(row, nullptr);
    QQuickItem* combo = nullptr;
    for (QQuickItem* c: row->childItems()) {
        combo = c->inherits("QQuickComboBox") ? c : combo;
    }
    ASSERT_NE(combo, nullptr);
    EXPECT_EQ(combo->property("currentText").toString(), "Pastel study");
    controller->setColorPalette("marker");
    wait(50);
    EXPECT_EQ(combo->property("currentText").toString(), "Marker");
    EXPECT_TRUE(named("colorPaletteSource")->property("text").toString().contains("Open Color (MIT)"))
            << "the palette's source";
    controller->setColorPalette("colorblind-8");
    wait(50);
    EXPECT_EQ(combo->property("currentText").toString(), "Colorblind-safe (8)");
    EXPECT_EQ(color(), QColor("#332288")) << "headings, in the palette chosen";
    controller->setColorPalette("classic");
    controller->toolboxModel()->reset();
}

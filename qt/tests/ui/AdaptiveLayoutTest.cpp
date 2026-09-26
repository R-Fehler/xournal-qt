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
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
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
        // (the view pill stays inside the window since qt/adaptive-toolbar)
        // The library's header in phone landscape: its last chip reaches past the edge (F10.1, F10.4)
        {"home", "favouritesChip", "qt/adaptive-home: the library header regrouped"},
        // The library's header scrolls sideways in a narrow window; the button cut at its end reaches a pixel past the
        // window where the library's (random) name makes the row a little wider (600x800, 864x1488)
        {"home", "newFolderButton", "qt/adaptive-home: the library header regrouped"},
        {"home", "flatButton", "qt/adaptive-home: the library header regrouped"},
};

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
    /// The colors shown in the bar (the swatches of the strip)
    int swatchesShown() const {
        int n = 0;
        for (QQuickItem* c: named("colorStrip")->childItems()) {
            n += c->objectName() == "colorSwatch" && c->isVisible() ? 1 : 0;
        }
        return n;
    }
    /// ⋮ inside the window, not in a scrolling area; the tools never hidden shown (on phones: shown or in "more
    /// tools"), the colors (the current one and at least 4 more; phones: the one cycling button) and the widths
    void checkToolBar(const std::string& at) {
        auto* more = named("moreButton");
        ASSERT_NE(more, nullptr);
        EXPECT_TRUE(shownInWindow(more)) << at << ": ⋮ shown";
        EXPECT_FALSE(inScrollingArea(more)) << at << ": ⋮ outside anything that scrolls";
        const bool phone = phoneClass();
        for (const char* name: {"penButton", "eraserButton", "handButton", "touchDrawingButton", "selectButton",
                                "textButton", "textModeButton", "stickyNoteButton"}) {
            QQuickItem* b = named(name);
            ASSERT_NE(b, nullptr) << name;
            if (phone) {
                EXPECT_TRUE(shownInWindow(b) || inOverflow(b)) << at << ": " << name << " shown or in more tools";
            } else {
                EXPECT_TRUE(shownInWindow(b)) << at << ": " << name << " shown";
            }
            EXPECT_FALSE(!shownInWindow(b) && !inOverflow(b)) << at << ": " << name << " neither shown nor in more tools";
        }
        auto* colors = named("colorStrip");
        auto* widths = named("widthStrip");
        EXPECT_TRUE(shownInWindow(colors)) << at << ": the colors";
        EXPECT_TRUE(shownInWindow(widths)) << at << ": the width";
        const QString mode = colors->property("mode").toString();
        if (!phone) {
            EXPECT_NE(mode, "single") << at;
            EXPECT_GE(swatchesShown(), 5) << at << ": the current color and at least 4 more";
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
    /// ⋮: at most 12 entries at the top; each of its submenus fits too (desktop, if `submenus`)
    void checkMoreMenu(const std::string& at, bool submenus) {
        auto* button = findItem("moreButton");
        auto* more = window->findChild<QObject*>("moreMenu");
        ASSERT_NE(button, nullptr);
        ASSERT_NE(more, nullptr);
        EXPECT_LE(menuEntries(more).size(), 9u) << at << ": ⋮ has at most 9 entries at the top";
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
                EXPECT_EQ(toolPlan().value("layout").toString(), "twoRows") << at << ": two rows on a portrait tablet";
                EXPECT_TRUE(overflowNames().isEmpty()) << at << ": all tools shown (" << overflowNames().join(',').toStdString() << ")";
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
        }
        if (menus) {
            checkHomeMenus(at);
        }
        controller->setHomeVisible(false);
        wait(150);
    }
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
// comes along into the sheet and goes back into the menu. The tab menu's rename starts once the sheet is gone.
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

    // Two levels deep; Esc goes up one
    click(sheetRow("moreViewMenu"));
    ASSERT_NE(sheetRow("toolbarPositionMenu"), nullptr);
    click(sheetRow("toolbarPositionMenu"));
    EXPECT_NE(sheetRow("toolbarLeftItem"), nullptr);
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);
    EXPECT_EQ(menuShown(), "moreViewMenu") << "Esc: a level up";
    EXPECT_TRUE(s->property("opened").toBool());

    // A row triggers its entry, and the sheet closes
    click(sheetRow("allDocumentsItem"));
    EXPECT_TRUE(opened(s, false));
    auto* overview = window->findChild<QObject*>("tabOverview");
    ASSERT_NE(overview, nullptr);
    until([&] { return overview->property("visible").toBool(); });
    EXPECT_TRUE(overview->property("visible").toBool()) << "View → All open documents";
    QMetaObject::invokeMethod(overview, "close");
    until([&] { return !overview->property("visible").toBool(); });

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

    // The tab menu: its title on top; Rename starts the rename in the tab once the sheet is gone
    auto* list = findItem("tabList");
    ASSERT_NE(list, nullptr);
    QQuickItem* tab = nullptr;
    QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, tab), Q_ARG(int, controller->currentTab()));
    ASSERT_NE(tab, nullptr);
    QObject* tabMenu = tab->findChild<QObject*>("tabMenu");
    ASSERT_NE(tabMenu, nullptr);
    QMetaObject::invokeMethod(tabMenu, "openMenu", Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
    ASSERT_TRUE(opened(s, true));
    EXPECT_EQ(findItem("menuSheetTitle")->property("text").toString(), tab->property("title").toString());
    settled(s);
    click(sheetRow("renameTabItem"));
    EXPECT_TRUE(opened(s, false));
    until([&] { return tab->property("renaming").toBool(); });
    EXPECT_TRUE(tab->property("renaming").toBool()) << "the rename starts once the sheet is gone";
    QTest::keyClick(window, Qt::Key_Escape);
    wait(50);

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

// The chrome is its own thing: full screen (F11) is the compact chrome in a full-screen window, as before; the compact
// and the reader chrome can be chosen in a normal window (kept for the size class), without full screen.
TEST_F(AdaptiveLayoutTest, chromeModeApartFromTheWindowState) {
    openDocument();
    resize(1400, 850);
    auto chrome = [&] { return window->property("chromeMode").toString(); };
    auto* tabStrip = window->findChild<QQuickItem*>("tabStrip");
    auto* square = findItem("quickToolSquare");
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
    QTest::keyClick(window, Qt::Key_F11);
    wait(50);
    EXPECT_EQ(chrome(), "full");
    EXPECT_FALSE(flag("windowFullScreen"));
    window->showNormal();
    resize(1400, 850);  // (the off-screen "screen" is 800 x 600: full screen made the window that small)

    // Compact, chosen: in the window as it is
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "compact"));
    wait(50);
    EXPECT_EQ(chrome(), "compact");
    EXPECT_FALSE(flag("windowFullScreen"));
    EXPECT_FALSE(flag("fullScreenMode"));
    EXPECT_NE(window->visibility(), QWindow::FullScreen);
    EXPECT_TRUE(square->isVisible()) << "the tool square";
    EXPECT_FALSE(findItem("sidebar")->isVisible());
    if (tabStrip) {
        EXPECT_FALSE(tabStrip->isVisible());
    }
    EXPECT_EQ(choice("desktopWide", "chrome"), "compact");
    resize(960, 1392);
    EXPECT_EQ(chrome(), "full") << "another class: its own choice (none)";
    resize(1400, 850);
    EXPECT_EQ(chrome(), "compact");

    // Its tools end with the way back to the full chrome
    click(square);
    auto* back = findItem("leaveFullScreenButton");
    ASSERT_NE(back, nullptr);
    until([&] { return back->isVisible(); });
    click(back);
    EXPECT_EQ(chrome(), "full");
    EXPECT_EQ(choice("desktopWide", "chrome"), "");

    // Reader: no HUD; the corner mark brings the chrome back
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "reader"));
    wait(50);
    EXPECT_EQ(chrome(), "reader");
    EXPECT_TRUE(flag("hudHidden"));
    EXPECT_FALSE(viewPill->isVisible());
    EXPECT_FALSE(square->isVisible());
    EXPECT_FALSE(flag("windowFullScreen"));
    auto* mark = findItem("presentCornerMark");
    ASSERT_NE(mark, nullptr);
    EXPECT_TRUE(mark->isVisible());
    click(mark);
    EXPECT_EQ(chrome(), "full");
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

// The quick tools of the compact chrome (F6.4, F9.4): Present and "Show the tabs and the tool bar" stay inside a
// short window, the tools scroll
TEST_F(AdaptiveLayoutTest, quickToolsFitAShortWindow) {
    openDocument();
    for (const WindowSize& s: {WindowSize{1024, 700, "small-desktop"}, WindowSize{1280, 500, "short-wide"},
                               WindowSize{915, 412, "phone-landscape"}, WindowSize{412, 915, "phone-portrait"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "compact"));
        wait(50);
        auto* popup = window->findChild<QObject*>("quickTools");
        ASSERT_NE(popup, nullptr);
        QMetaObject::invokeMethod(popup, "open");
        until([&] { return popup->property("opened").toBool(); });
        for (const char* name: {"presentToggleButton", "leaveFullScreenButton"}) {
            auto* b = findItem(name);
            ASSERT_NE(b, nullptr) << name;
            const QRectF r = b->mapRectToScene(QRectF(0, 0, b->width(), b->height()));
            EXPECT_TRUE(QRectF(0, 0, s.w, s.h).adjusted(-1, -1, 1, 1).contains(r))
                    << at << ": " << name << " at " << r.y() << ".." << r.bottom();
        }
        QMetaObject::invokeMethod(popup, "close");
        until([&] { return !popup->property("visible").toBool(); });
        QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "full"));
        wait(30);
    }
}

// --- the tool bar (qt/adaptive-toolbar) -----------------------------------------------------------------------------

// A portrait 2-in-1 at 125 % (720 wide): two rows still show every tool that is never hidden; only low-priority buttons
// go into "more tools"
TEST_F(AdaptiveLayoutTest, twoRowsFitAt720) {
    openDocument();
    resize(720, 1232);
    ASSERT_EQ(sizeClass(), "tabletPortrait");
    EXPECT_EQ(toolPlan().value("layout").toString(), "twoRows");
    checkToolBar("720x1232");
    const QStringList lowPriority{"new", "open", "save", "settings", "present", "fullScreen", "search", "editAsNotes",
                                  "openExternally", "addPage", "image", "emoji", "pdfText", "geometry", "shape"};
    for (const QString& n: overflowNames()) {
        EXPECT_TRUE(lowPriority.contains(n)) << n.toStdString() << " is not a low-priority button";
    }
    // The two rows: the tools on the first one, the colors on the second
    const QRectF pen = sceneRect(named("penButton"));
    const QRectF colors = sceneRect(named("colorStrip"));
    EXPECT_GT(colors.top(), pen.bottom() - 1) << "the colors below the tools";
    EXPECT_LT(colors.bottom(), sceneRect(named("canvas")).top() + 1) << "both rows above the page";
    EXPECT_NE(named("colorStrip")->property("mode").toString(), "single");
}

// The place of the tool bar is chosen per size class (⋮ → View → Tool bar position); a portrait tablet has two rows at
// the top by default, "two rows at the bottom" puts them below the page; the rails stay
TEST_F(AdaptiveLayoutTest, toolBarPlaceIsChosenPerSizeClass) {
    openDocument();
    resize(960, 1392);
    ASSERT_EQ(sizeClass(), "tabletPortrait");
    auto* area = named("toolArea");
    auto* canvas = named("canvas");
    EXPECT_EQ(window->property("toolbarLayout").toString(), "twoRowsTop");
    EXPECT_LT(sceneRect(area).bottom(), sceneRect(canvas).top() + 1) << "above the page";

    auto trigger = [&](const char* name) {
        auto* item = window->findChild<QObject*>(name);
        ASSERT_NE(item, nullptr) << name;
        QMetaObject::invokeMethod(item, "triggered");
        wait(200);
    };
    trigger("toolbarTwoRowsBottomItem");
    EXPECT_EQ(choice("tabletPortrait", "toolbar"), "twoRowsBottom");
    EXPECT_TRUE(named("bottomTools")->isVisible());
    EXPECT_FALSE(named("topTools")->isVisible());
    EXPECT_GT(sceneRect(area).top(), sceneRect(canvas).bottom() - 1) << "below the page";
    checkToolBar("960x1392 bottom");

    resize(1920, 1080);
    EXPECT_EQ(window->property("toolbarLayout").toString(), "top") << "the wide window: its own place (one row)";
    EXPECT_EQ(toolPlan().value("layout").toString(), "row");
    resize(960, 1392);
    EXPECT_EQ(window->property("toolbarLayout").toString(), "twoRowsBottom") << "remembered for the portrait class";

    trigger("toolbarLeftItem");
    EXPECT_EQ(choice("tabletPortrait", "toolbar"), "railLeft");
    EXPECT_TRUE(named("sideTools")->isVisible());
    EXPECT_EQ(toolPlan().value("layout").toString(), "rail");
    auto* more = named("moreButton");
    EXPECT_TRUE(shownInWindow(more));
    EXPECT_FALSE(inScrollingArea(more)) << "⋮ at the rail's bottom, outside what could scroll";
    EXPECT_LT(sceneRect(more).left(), 110);

    trigger("toolbarAutoItem");
    EXPECT_EQ(choice("tabletPortrait", "toolbar"), "");
    EXPECT_EQ(window->property("toolbarLayout").toString(), "twoRowsTop");
    trigger("toolbarTopItem");
    EXPECT_EQ(choice("tabletPortrait", "toolbar"), "top") << "one row, chosen";
    EXPECT_EQ(toolPlan().value("layout").toString(), "row");
    checkToolBar("960x1392 one row");
}

// The colors and widths take the room there is: all of them at 1920; one width button and the recent colors (at least
// 4) where it is short; on a phone one cycling color button (a tap: the next color, a long press: the palette)
TEST_F(AdaptiveLayoutTest, colorsAndWidthsTakeTheRoomThereIs) {
    openDocument();
    auto* colors = named("colorStrip");
    auto* widths = named("widthStrip");
    const int palette = controller->property("toolbarColors").toList().size();
    resize(1920, 1080);
    EXPECT_EQ(colors->property("mode").toString(), "full");
    EXPECT_EQ(swatchesShown(), palette) << "every color of the palette";
    EXPECT_EQ(widths->property("mode").toString(), "full");
    ASSERT_NE(findItem("sizeButton4"), nullptr);
    EXPECT_TRUE(findItem("sizeButton4")->isVisible());

    resize(1280, 800);
    EXPECT_EQ(widths->property("mode").toString(), "single") << "one cycling width button";
    EXPECT_EQ(colors->property("mode").toString(), "recent");
    EXPECT_GE(colors->property("recentCount").toInt(), 4);
    EXPECT_EQ(swatchesShown(), 1 + colors->property("recentCount").toInt()) << "the current color and the recent ones";
    EXPECT_TRUE(named("paletteButton")->isVisible());
    // The width button cycles
    auto* widthButton = named("widthButton");
    ASSERT_TRUE(shownInWindow(widthButton));
    const int size = controller->size();
    click(widthButton);
    EXPECT_NE(controller->size(), size) << "a tap: the next width";

    resize(960, 1392);
    EXPECT_TRUE(colors->property("mode").toString() == "full" || colors->property("recentCount").toInt() >= 4);

    resize(412, 915);
    EXPECT_EQ(colors->property("mode").toString(), "single") << "a phone: one cycling color button";
    auto* cycle = named("colorCycleButton");
    ASSERT_TRUE(shownInWindow(cycle));
    const QColor before = controller->property("color").value<QColor>();
    click(cycle);
    EXPECT_NE(controller->property("color").value<QColor>(), before) << "a tap: the next color";
    QMetaObject::invokeMethod(cycle, "pressAndHold");
    auto* paletteSheet = window->findChild<QObject*>("colorPalette");
    ASSERT_NE(paletteSheet, nullptr);
    EXPECT_TRUE(opened(paletteSheet, true)) << "a long press: the palette";
    settled(paletteSheet);
    const QRectF r = popupRect(paletteSheet);
    EXPECT_TRUE(insideWindow(r)) << r.x() << "," << r.y() << " " << r.width() << "x" << r.height();
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(opened(paletteSheet, false));
}

// What does not fit goes into "more tools", next to ⋮: its buttons with their names; a button used there closes it
TEST_F(AdaptiveLayoutTest, moreToolsHoldsWhatDoesNotFit) {
    openDocument();
    resize(1280, 800);
    const QStringList names = overflowNames();
    ASSERT_FALSE(names.isEmpty()) << "a laptop's bar has more than fits";
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
    // Not in the reader chrome
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "reader"));
    wait(50);
    EXPECT_FALSE(arrow->isVisible());
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "full"));
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
        EXPECT_TRUE(insideWindow(sceneRect(pill))) << at << ": the view pill inside the window";
        ASSERT_TRUE(refPill->isVisible()) << at;
        EXPECT_FALSE(sceneRect(pill).intersects(sceneRect(refPill))) << at << ": clear of the reference's pill";
    }
    controller->reference().close();
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

// Pictures of the tool bar in its layouts, to look at (skipped unless XQT_TOOLBAR_SHOTS=<folder>)
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
        for (const char* name: {"moreToolsPopup", "shapeButtonVariants", "fitMenu", "menuSheet"}) {
            if (QObject* popup = window->findChild<QObject*>(name)) {
                QMetaObject::invokeMethod(popup, "close");
            }
        }
        wait(300);
        controller->selectTool("pen");
    };
    resize(1920, 1080);
    shot("bar-1920x1080");
    resize(1280, 800);
    shot("bar-1280x800");
    openPopup("moreToolsButton", "moreToolsPopup");
    shot("bar-1280x800-more-tools");
    closePopups();
    QMetaObject::invokeMethod(named("shapeButton"), "pressAndHold");
    wait(300);
    shot("bar-1280x800-shapes");
    closePopups();
    resize(960, 1392);
    shot("bar-960x1392-two-rows");
    QMetaObject::invokeMethod(window, "chooseToolbar", Q_ARG(QVariant, "twoRowsBottom"));
    shot("bar-960x1392-two-rows-bottom");
    QMetaObject::invokeMethod(window, "chooseToolbar", Q_ARG(QVariant, "railLeft"));
    shot("bar-960x1392-rail-left");
    QMetaObject::invokeMethod(window, "chooseToolbar", Q_ARG(QVariant, "twoRowsTop"));
    click(named("sidebarArrow"));
    shot("bar-960x1392-sidebar-drawer");
    click(named("sidebarArrow"));
    resize(720, 1232);
    shot("bar-720x1232-two-rows");
    openPopup("moreToolsButton", "moreToolsPopup");
    shot("bar-720x1232-more-tools");
    closePopups();
    resize(800, 600);
    shot("bar-800x600-narrow");
    resize(412, 915);
    shot("bar-412x915-phone");
    openPopup("moreToolsButton", "moreToolsPopup");
    shot("bar-412x915-more-tools");
    closePopups();
    // The zoom's menu, a text document's merged bar
    resize(960, 1392);
    click(named("zoomButton"));
    wait(900);
    shot("pill-960x1392-zoom-menu");
    closePopups();
    // The compact chrome's tools (full screen in a window of this size)
    resize(1280, 800);
    window->setProperty("fullScreenMode", true);
    wait(300);
    window->showNormal();
    resize(1280, 800);
    click(named("quickToolSquare"));
    shot("compact-1280x800-tools");
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
}

// One place for each action (qt/docs/adaptive-layout.md): ⋮ repeats no button of the tool bar, "more tools", the view
// pill or the sidebar
TEST_F(AdaptiveLayoutTest, moreMenuRepeatsNoButton) {
    openDocument();
    for (const char* gone: {"settingsItem", "fullScreenItem", "presentItem", "allPagesItem", "insertImageItem",
                            "insertStickyNoteItem", "openExternallyItem", "editAsNotesItem", "hideToolbarItem"}) {
        EXPECT_EQ(window->findChild<QObject*>(gone), nullptr) << gone << " is a button: not in ⋮ too";
    }
    for (const char* kept: {"presentCleanItem", "insertPagesItem", "readItem", "allDocumentsItem", "toolbarPositionMenu"}) {
        EXPECT_NE(window->findChild<QObject*>(kept), nullptr) << kept << " differs from any button: kept";
    }
    // The buttons are there instead
    for (const char* button: {"settingsButton", "fullScreenButton", "presentButton", "pageGridButton", "imageButton",
                              "stickyNoteButton", "contentsButton", "sidebarArrow"}) {
        EXPECT_NE(named(button), nullptr) << button;
    }
}

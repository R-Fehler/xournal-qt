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
 * What later blocks fix is listed as known (knownOutside, expectLater): the test passes now and is made stricter by
 * the block that fixes it. The walk over all 18 sizes of the audit runs with XQT_UI_ADAPTIVE=1.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>

#include <QCoreApplication>
#include <QElapsedTimer>
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
        // The view pill is anchored to the canvas's right edge and is wider than a narrow canvas (F6.1, D9); its
        // undo and redo count once there is something to undo
        {"doc", "undoButton", "qt/adaptive-panels: the compact view pill"},
        {"doc", "redoButton", "qt/adaptive-panels: the compact view pill"},
        {"doc", "layoutButton", "qt/adaptive-panels: the compact view pill"},
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
        EXPECT_LE(menuEntries(more).size(), 12u) << at << ": ⋮ has at most 12 entries at the top";
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
            if (sizeClass() == "desktopWide") {
                expectLater(moreButtonShown(), at + ": the tool bar's ⋮ shown without scrolling",
                            "qt/adaptive-toolbar: ⋮ pinned outside the scrolling row");
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
    // On a phone upright: the header one row (the library's name, View, Settings), the switch across a row of its
    // own below it, then the search
    if (sizeClass() == "phonePortrait") {
        auto* header = findItem("homeHeader");
        EXPECT_FALSE(header->property("interactive").toBool()) << at << ": the header fits without scrolling";
        EXPECT_LE(header->height(), 56) << at << ": the header is one row";
        const double row = sceneRect(findItem("homeViewButton")).center().y();
        for (const char* name: {"libraryMenuButton", "homeViewButton", "homeSettingsButton"}) {
            auto* b = findItem(name);
            EXPECT_TRUE(shownInWindow(b)) << at << ": " << name;
            EXPECT_NEAR(sceneRect(b).center().y(), row, 4) << at << ": " << name << " in the header's row";
        }
        const QRectF library = sceneRect(findItem("libraryPageButton"));
        const QRectF bookmarks = sceneRect(findItem("bookmarksPageButton"));
        EXPECT_GT(library.top(), sceneRect(header).bottom() - 1) << at << ": the switch below the header";
        EXPECT_NEAR(library.center().y(), bookmarks.center().y(), 1) << at << ": the switch is one row";
        EXPECT_GT(bookmarks.width(), 100) << at << ": its tabs share the width";
        EXPECT_GT(sceneRect(findItem("librarySearchField")).top(), library.bottom()) << at << ": the search below";
    }
    if (sizeClass() == "phoneShort") {
        // Held sideways: one header row with the breadcrumbs in it
        const double row = sceneRect(findItem("homeViewButton")).center().y();
        EXPECT_NEAR(sceneRect(findItem("crumbArea")).center().y(), row, 4) << at << ": the breadcrumbs in the header";
        EXPECT_NEAR(sceneRect(findItem("librarySearchField")).center().y(), row, 4) << at << ": the search too";
    }

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
    click(sheetRow("allPagesItem"));
    EXPECT_TRUE(opened(s, false));
    auto* grid = findItem("pageGrid");
    ASSERT_NE(grid, nullptr);
    until([&] { return grid->isVisible(); });
    EXPECT_TRUE(grid->isVisible()) << "View → All pages";
    QMetaObject::invokeMethod(grid, "close");
    until([&] { return !grid->isVisible(); });

    // The layout menu's columns come along, and go back into the menu
    auto* layoutButton = findItem("layoutButton");
    ASSERT_NE(layoutButton, nullptr);
    QMetaObject::invokeMethod(layoutButton, "pressAndHold");
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

// The library's "+" and View (qt/adaptive-home) on a phone (sheets; "+" floats at the bottom) and in a desktop window
// too narrow for every button (menus in the header): each entry does what it says, and a switch of Show or the size
// of the cards leaves the menu open
TEST_F(AdaptiveLayoutTest, theHomeScreensPlusAndViewMenusWork) {
    QObject* library = controller->libraryModel();
    QQuickItem* home = findItem("homeView");
    ASSERT_NE(home, nullptr);
    for (const WindowSize& s: {WindowSize{412, 915, "phone-portrait"}, WindowSize{1280, 800, "laptop-16x10"}}) {
        resize(s.w, s.h);
        const std::string at = std::to_string(s.w) + "x" + std::to_string(s.h);
        const bool phone = phoneClass();
        home->setProperty("page", 0);
        wait(100);
        ASSERT_FALSE(home->property("expanded").toBool()) << at << ": the header groups its actions";
        QObject* newMenu = window->findChild<QObject*>("newMenu");
        QObject* viewMenu = window->findChild<QObject*>("homeViewMenu");
        auto openMenu = [&](QObject* menu, const char* button) {
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
            wait(100);
        }

        // View: Only favourites, All documents at once, Sort, Open where left off
        openMenu(viewMenu, "homeViewButton");
        choose("viewFavouritesItem");
        EXPECT_TRUE(library->property("favouritesOnly").toBool()) << at;
        EXPECT_TRUE(findItem("homeViewButton")->property("checked").toBool()) << at << ": View marked while it filters";
        openMenu(viewMenu, "homeViewButton");
        choose("viewFavouritesItem");
        EXPECT_FALSE(library->property("favouritesOnly").toBool()) << at;
        openMenu(viewMenu, "homeViewButton");
        choose("viewFlatItem");
        EXPECT_TRUE(library->property("flat").toBool()) << at;
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
    QMetaObject::invokeMethod(window, "chooseChrome", Q_ARG(QVariant, "compact"));
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

// The sidebar hidden by hand stays hidden in that class only; without room its button opens it as a drawer, which a
// picked page or a tap beside it closes, and whose pin keeps it beside the page in that class. Reset: automatic again.
TEST_F(AdaptiveLayoutTest, sidebarChoicesAreKeptPerSizeClass) {
    openDocument();
    resize(1920, 1080);
    ASSERT_EQ(sizeClass(), "desktopWide");
    auto* sidebar = findItem("sidebar");
    auto* pages = findItem("pagesButton");
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

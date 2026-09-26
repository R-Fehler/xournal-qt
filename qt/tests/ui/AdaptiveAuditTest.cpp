/*
 * xournal-qt: the UI audit of window sizes (qt/docs/ui-adaptive-audit.md). Opt-in, not a test that passes or fails:
 *   XQT_UI_AUDIT=<folder> ./xqt-ui-tests --gtest_filter='AdaptiveAudit*'   (about 20 minutes for all sizes)
 *   (XQT_UI_AUDIT_SIZES=412x915,1280x800 limits the sizes, XQT_UI_AUDIT_SCREENS=doc,moreMenu the screens)
 * For each window size it walks the screens (library, document with the page sidebar, the ⋮ menu, tab overview,
 * settings, Markdown with its format bar, the reference split, full-screen and presentation chrome in the window,
 * dialogs), saves a picture of each and writes a line per screen to <folder>/report.tsv: what lies outside the window,
 * which buttons are hidden in a scrolled area, buttons smaller than a finger, and how tall the open menu is.
 *
 * @license GNU GPLv2 or later
 */
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/ReferenceMode.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "config-test.h"

namespace fs = std::filesystem;

namespace {
struct Size {
    int w;
    int h;
    const char* label;
};
// Logical pixels. Tablets and 2-in-1s: the screen minus a 48 px task bar where the desktop has one.
const Size allSizes[] = {
        {1920, 1080, "desktop-fhd"},        {1366, 768, "laptop"},           {1280, 800, "laptop-16x10"},
        {1024, 700, "small-desktop"},       {800, 600, "tiny-desktop"},      {600, 800, "narrow-tall"},
        {412, 915, "phone-portrait"},       {915, 412, "phone-landscape"},   {900, 1000, "fold7-inner"},
        {1280, 500, "short-wide"},          {960, 1392, "surface-200-portrait"}, {1440, 912, "surface-200-landscape"},
        {1280, 1872, "surface-150-portrait"}, {1920, 1232, "surface-150-landscape"},
        {864, 1488, "2in1-150-portrait"},   {720, 1232, "2in1-125-portrait"}, {1536, 816, "2in1-landscape"},
        {1280, 672, "2in1-125-landscape"},
};

QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

QString labelOf(QQuickItem* i) {
    if (!i->objectName().isEmpty()) {
        return i->objectName();
    }
    for (const char* p: {"iconName", "text", "tip"}) {
        const QString v = i->property(p).toString();
        if (!v.isEmpty()) {
            return QString(p) + "=" + v.left(24);
        }
    }
    return QString::fromLatin1(i->metaObject()->className());
}

class AdaptiveAuditTest: public ::testing::Test {
protected:
    /// A new window of this size, with a library of a few documents (each size starts afresh)
    void openWindow(const Size& s) {
        size = s;
        QDir().mkpath(folder);
        tmpDir = std::make_unique<QTemporaryDir>();
        ASSERT_TRUE(tmpDir->isValid());
        root = fs::path(tmpDir->path().toStdString());
        fs::create_directories(root / "Physics");
        fs::create_directories(root / "Physics" / "Semester 3 (winter)" / "Quantum mechanics" / "Exercise sheets");
        fs::create_directories(root / "Seminar with a rather long folder name");
        const fs::path pdf = fs::path(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"));
        fs::copy_file(pdf, root / "Physics" / "sheet.pdf");
        fs::copy_file(pdf, root / "lecture.pdf");
        fs::copy_file(pdf, root / "A lecture with a very long title about Kalman filters and control.pdf");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/strokes.xopp")), root / "notes.xopp");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/pages.xopp")), root / "pages.xopp");
        fs::copy_file(fs::path(GET_TESTFILE(u8"load/pages.xopp.bg_1.png")), root / "pages.xopp.bg_1.png");
        std::string text = "# Lecture 3\n\n## Kalman filter\n\nThe **prediction** step, then the *update*.\n\n";
        for (int i = 0; i < 30; ++i) {
            text += "Paragraph " + std::to_string(i) + " about the prediction step of the filter.\n\n";
        }
        std::ofstream(root / "kalman.md") << text;

        controller = std::make_unique<AppController>();
        controller->setLibraryRoot(root);
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
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
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        window->resize(s.w, s.h);  // (before anything is shown: the bindings on the width see the size first)
        wait(300);
    }
    void closeWindow() {
        if (controller) {
            controller->shutdown();
        }
        engine.reset();
        controller.reset();
        window = nullptr;
    }
    void TearDown() override { closeWindow(); }
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
    /// An object of a QML type (for those without an objectName)
    QObject* findType(const char* type) const {
        for (QObject* o: window->findChildren<QObject*>()) {
            if (QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String(type) + "_QML")) {
                return o;
            }
        }
        return nullptr;
    }
    bool wanted(const char* screen) const {
        const QString only = qEnvironmentVariable("XQT_UI_AUDIT_SCREENS");
        return only.isEmpty() || only.split(',').contains(screen);
    }
    void closePopups() {
        for (int i = 0; i < 3; ++i) {
            QTest::keyClick(window, Qt::Key_Escape);
            wait(60);
        }
        wait(250);
    }

    /// The picture, and what is wrong with the layout: a line of report.tsv
    void shot(const char* screen, const QString& extra = {}) {
        wait(450);
        const Size s = size;
        const QString name = QString("%1-%2x%3-%4").arg(screen).arg(s.w).arg(s.h).arg(s.label);
        const QImage picture = window->grabWindow();
        picture.save(folder + '/' + name + ".png");

        const QRectF win(0, 0, window->width(), window->height());
        QStringList outside, hidden, small;
        int buttons = 0;
        // Every visible item: the part of it that is shown (clipping ancestors cut it) and where it is
        std::function<void(QQuickItem*, QRectF)> walk = [&](QQuickItem* i, QRectF clip) {
            if (!i->isVisible() || i->opacity() <= 0.01) {
                return;
            }
            const QRectF r = i->mapRectToScene(QRectF(0, 0, i->width(), i->height()));
            const bool button = i->inherits("QQuickAbstractButton");
            const bool control = button || i->inherits("QQuickTextInput") || i->inherits("QQuickTextEdit") ||
                                 i->inherits("QQuickTextField") || i->inherits("QQuickComboBox") ||
                                 i->inherits("QQuickSpinBox");
            if (control && r.width() > 1 && r.height() > 1 && i->isEnabled()) {
                const QRectF shown = r.intersected(clip);
                if (button) {
                    ++buttons;
                }
                if (shown.width() < r.width() - 2 || shown.height() < r.height() - 2) {
                    // cut by a scrolling (clipping) area: hidden until scrolled
                    if (!clip.contains(win)) {
                        hidden << labelOf(i);
                    }
                }
                if (!win.contains(shown.adjusted(1, 1, -1, -1)) && !shown.isEmpty()) {
                    outside << QString("%1@%2,%3").arg(labelOf(i)).arg(int(shown.right())).arg(int(shown.bottom()));
                }
                if (button && !shown.isEmpty() && (r.width() < 40 || r.height() < 40)) {
                    small << QString("%1(%2x%3)").arg(labelOf(i)).arg(int(r.width())).arg(int(r.height()));
                }
            }
            const QRectF inner = i->clip() ? clip.intersected(r) : clip;
            for (QQuickItem* c: i->childItems()) {
                walk(c, inner);
            }
        };
        walk(window->contentItem(), QRectF(-1e6, -1e6, 2e6, 2e6));
        outside.removeDuplicates();
        hidden.removeDuplicates();
        small.removeDuplicates();

        std::ofstream report((folder + "/report.tsv").toStdString(), std::ios::app);
        report << name.toStdString() << "\t" << screen << "\t" << s.w << "x" << s.h << "\tbuttons=" << buttons
               << "\toutside=" << outside.size() << "\thidden=" << hidden.size() << "\tsmall=" << small.size() << "\t"
               << extra.toStdString() << "\tOUT[" << outside.join(' ').toStdString() << "]\tHIDDEN["
               << hidden.join(' ').toStdString() << "]\tSMALL[" << small.join(' ').toStdString() << "]\n";
        std::cerr << "shot " << name.toStdString() << "\n";
    }
    /// How a menu fits: its height against the window, whether it scrolls
    QString menuFit(QObject* menu) {
        if (!menu) {
            return "menu=none";
        }
        auto* list = menu->property("contentItem").value<QQuickItem*>();
        const double contentH = list ? list->property("contentHeight").toDouble() : -1;
        return QString("menu h=%1 content=%2 y=%3 x=%4 w=%5 scrolls=%6 opened=%7")
                .arg(menu->property("height").toDouble())
                .arg(contentH)
                .arg(menu->property("y").toDouble())
                .arg(menu->property("x").toDouble())
                .arg(menu->property("width").toDouble())
                .arg(list ? list->property("interactive").toBool() : false)
                .arg(menu->property("opened").toBool());
    }
    /// The tool bar's buttons: how many show without scrolling it, and which do not
    QString toolbarFit() {
        auto* row = findItem("toolRow");
        if (!row || !row->isVisible()) {
            return "toolbar=hidden";
        }
        QQuickItem* flick = row->parentItem();
        while (flick && !flick->inherits("QQuickFlickable")) {
            flick = flick->parentItem();
        }
        const QRectF view = flick ? flick->mapRectToScene(QRectF(0, 0, flick->width(), flick->height())) : QRectF();
        int shown = 0, all = 0;
        QStringList off;
        for (QQuickItem* c: row->childItems()) {
            if (!c->isVisible() || !c->inherits("QQuickAbstractButton")) {
                continue;
            }
            ++all;
            const QRectF r = c->mapRectToScene(QRectF(0, 0, c->width(), c->height()));
            if (view.contains(r.center()) && QRectF(0, 0, window->width(), window->height()).contains(r.center())) {
                ++shown;
            } else {
                off << labelOf(c);
            }
        }
        return QString("toolbar %1/%2 shown; row=%3x%4 view=%5x%6; off: %7")
                .arg(shown)
                .arg(all)
                .arg(int(row->implicitWidth()))
                .arg(int(row->implicitHeight()))
                .arg(int(view.width()))
                .arg(int(view.height()))
                .arg(off.join(','));
    }
    void walk();
    QString path(const char* name) const { return QString::fromStdString((root / name).string()); }

    QString folder = qEnvironmentVariable("XQT_UI_AUDIT");
    Size size{};
    std::unique_ptr<QTemporaryDir> tmpDir;
    fs::path root;
    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

void AdaptiveAuditTest::walk() {
    const Size s = size;
    // The library (home screen) as the window starts
    if (wanted("home")) {
        shot("home", QString("sidebarDefault=%1").arg(window->property("sidebarShown").toBool()));
    }
    if (wanted("libraryMenu")) {
        if (auto* b = findItem("libraryMenuButton")) {
            QMetaObject::invokeMethod(b, "clicked");
            wait(350);
            shot("libraryMenu", menuFit(find("libraryMenu")));
            closePopups();
        }
    }
    // Everything selected: the selection bar instead of the header
    if (wanted("homeSelection")) {
        QMetaObject::invokeMethod(controller->libraryModel(), "selectAll");
        wait(300);
        shot("homeSelection");
        QMetaObject::invokeMethod(controller->libraryModel(), "clearSelection");
        wait(200);
    }
    // A folder deep down: the breadcrumbs
    if (wanted("homeDeep")) {
        controller->libraryModel()->setProperty("folder", "Physics/Semester 3 (winter)/Quantum mechanics/Exercise sheets");
        wait(400);
        shot("homeDeep");
        controller->libraryModel()->setProperty("folder", "");
        wait(300);
    }
    if (wanted("newDocument")) {
        if (QObject* d = find("newDocumentDialog")) {
            QMetaObject::invokeMethod(d, "open");
            wait(400);
            shot("newDocument", QString("dialog h=%1").arg(d->property("height").toDouble()));
            closePopups();
        }
    }

    // A document, with the page sidebar as the window's width decides (and shown, to see it)
    ASSERT_TRUE(controller->openPath(path("pages.xopp")));
    wait(600);
    const bool sidebarDefault = window->property("sidebarShown").toBool();
    if (wanted("doc")) {
        shot("doc", QString("sidebarDefault=%1; %2").arg(sidebarDefault).arg(toolbarFit()));
    }
    if (wanted("docSidebar") && !sidebarDefault) {
        window->setProperty("sidebarShown", true);
        wait(300);
        shot("docSidebar", toolbarFit());
        window->setProperty("sidebarShown", false);
    }
    // The ⋮ menu, opened as a user does: the tool bar scrolled to its end first
    if (wanted("moreMenu")) {
        if (auto* more = findItem("moreButton")) {
            QQuickItem* flick = more->parentItem();
            while (flick && !flick->inherits("QQuickFlickable")) {
                flick = flick->parentItem();
            }
            if (flick) {
                flick->setProperty("contentX", std::max(0.0, flick->property("contentWidth").toDouble() - flick->width()));
                flick->setProperty("contentY",
                                   std::max(0.0, flick->property("contentHeight").toDouble() - flick->height()));
                wait(100);
            }
            QMetaObject::invokeMethod(more, "clicked");
            wait(400);
            shot("moreMenu", menuFit(find("moreMenu")));
            closePopups();
            if (flick) {
                flick->setProperty("contentX", 0);
                flick->setProperty("contentY", 0);
            }
        }
    }
    if (wanted("layoutMenu")) {
        if (auto* b = findItem("layoutButton")) {
            QMetaObject::invokeMethod(b, "pressAndHold");
            wait(350);
            shot("layoutMenu", menuFit(find("layoutMenu")));
            closePopups();
        }
    }
    if (wanted("search")) {
        if (QObject* bar = find("searchBar")) {
            QMetaObject::invokeMethod(bar, "openBar");
            wait(300);
            shot("search");
            QMetaObject::invokeMethod(bar, "closeBar");
            wait(200);
        }
    }
    if (wanted("pageGrid")) {
        QTest::keyClick(window, Qt::Key_G, Qt::ControlModifier | Qt::AltModifier);
        wait(700);
        shot("pageGrid");
        closePopups();
        if (auto* grid = find("pageGrid"); grid && grid->property("visible").toBool()) {
            QMetaObject::invokeMethod(grid, "close");
        }
        wait(200);
    }
    if (wanted("dialogs")) {
        if (QObject* d = find("insertPagesDialog")) {
            QMetaObject::invokeMethod(d, "openAt", Q_ARG(QVariant, 1));
            wait(400);
            shot("insertPages", QString("dialog h=%1 implicit=%2 y=%3")
                                        .arg(d->property("height").toDouble())
                                        .arg(d->property("implicitHeight").toDouble())
                                        .arg(d->property("y").toDouble()));
            closePopups();
        }
        if (QObject* d = findType("PageSizeDialog")) {
            QMetaObject::invokeMethod(d, "openFor", Q_ARG(QVariant, QVariantList{0}));
            wait(400);
            shot("pageSize", QString("dialog h=%1 y=%2").arg(d->property("height").toDouble()).arg(d->property("y").toDouble()));
            closePopups();
        }
    }
    // The tools in a column at the side (the setting), as a portrait tablet might want them
    if (wanted("sideToolbar")) {
        controller->setProperty("toolbarPosition", "left");
        wait(400);
        shot("sideToolbar", toolbarFit());
        controller->setProperty("toolbarPosition", "top");
        wait(300);
    }
    if (wanted("settings")) {
        if (QObject* sheet = find("settingsPage")) {
            QMetaObject::invokeMethod(sheet, "open");
            wait(500);
            shot("settings");
            closePopups();
        }
    }

    // More documents: the tab strip full, the tab overview, the reference beside the notes
    ASSERT_TRUE(controller->openPath(path("lecture.pdf")));
    ASSERT_TRUE(controller->openPath(path("A lecture with a very long title about Kalman filters and control.pdf")));
    ASSERT_TRUE(controller->openPath(path("notes.xopp")));
    wait(500);
    if (wanted("tabs")) {
        shot("tabs", toolbarFit());
    }
    if (wanted("overview")) {
        if (QObject* o = find("tabOverview")) {
            QMetaObject::invokeMethod(o, "open");
            wait(700);
            shot("overview");
            closePopups();
        }
    }
    if (wanted("reference")) {
        controller->setProperty("currentTab", 0);
        wait(200);
        controller->reference().showTab(1);
        wait(700);
        shot("reference");
        controller->reference().close();
        wait(200);
    }

    // Full-screen chrome and presenting inside a normal window of this size (the author's step 2 and 3)
    if (wanted("chrome")) {
        controller->setProperty("currentTab", 0);
        window->setProperty("fullScreenMode", true);
        wait(300);
        window->showNormal();
        window->resize(s.w, s.h);
        wait(500);
        shot("fullscreenChrome");
        if (auto* square = findItem("quickToolSquare")) {
            QMetaObject::invokeMethod(square, "forceActiveFocus");
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              square->mapToScene(QPointF(square->width() / 2, square->height() / 2)).toPoint());
            wait(400);
            QObject* tools = find("quickTools");
            shot("quickTools", tools ? QString("popup h=%1 w=%2").arg(tools->property("height").toDouble())
                                               .arg(tools->property("width").toDouble())
                                     : QString());
            closePopups();
        }
        QMetaObject::invokeMethod(window, "startPresenting", Q_ARG(QVariant, false));
        wait(300);
        window->showNormal();
        window->resize(s.w, s.h);
        wait(500);
        shot("presenting");
        window->setProperty("presentClean", true);
        wait(300);
        shot("presentClean");
        controller->setProperty("presenting", false);
        window->setProperty("fullScreenMode", false);
        wait(300);
        window->showNormal();
        window->resize(s.w, s.h);
        wait(300);
    }

    // Markdown: a .md being edited (the format bar), and the source panel beside the page
    if (wanted("markdown")) {
        ASSERT_TRUE(controller->openPath(path("kalman.md")));
        wait(700);
        if (auto* canvas = find<QQuickItem>("canvas")) {
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                              canvas->mapToScene(QPointF(canvas->width() / 2, canvas->height() / 3)).toPoint());
            wait(300);
        }
        auto* bar = qobject_cast<QQuickItem*>(findType("MarkdownFormatBar"));
        QString fit = "formatBar=none";
        if (bar) {
            QQuickItem* flick = nullptr;
            std::function<void(QQuickItem*)> first = [&](QQuickItem* i) {
                if (!flick && i->inherits("QQuickFlickable")) {
                    flick = i;
                }
                for (QQuickItem* c: i->childItems()) {
                    first(c);
                }
            };
            first(bar);
            fit = QString("formatBar visible=%1 h=%2 content=%3 view=%4")
                          .arg(bar->isVisible())
                          .arg(bar->height())
                          .arg(flick ? flick->property("contentWidth").toDouble() : -1)
                          .arg(flick ? flick->width() : -1);
        }
        shot("markdownDoc", fit + "; " + toolbarFit());
    }
    if (wanted("markdownPanel")) {
        controller->newDocument();
        wait(500);
        if (QObject* panel = find("markdownPanel")) {
            QMetaObject::invokeMethod(panel, "open", Q_ARG(QVariant, 0));
            wait(600);
            shot("markdownPanel");
            QMetaObject::invokeMethod(panel, "close", Q_ARG(QVariant, true));
            wait(300);
        }
    }
    // Back home with documents open: the tab strip over the library
    if (wanted("homeWithTabs")) {
        controller->setProperty("homeVisible", true);
        wait(500);
        shot("homeWithTabs");
    }
}

TEST_F(AdaptiveAuditTest, walkTheScreensAtAllSizes) {
    if (folder.isEmpty()) {
        GTEST_SKIP() << "set XQT_UI_AUDIT=<folder>";
    }
    const QString only = qEnvironmentVariable("XQT_UI_AUDIT_SIZES");
    for (const Size& s: allSizes) {
        if (!only.isEmpty() && !only.split(',').contains(QString("%1x%2").arg(s.w).arg(s.h))) {
            continue;
        }
        openWindow(s);
        if (HasFatalFailure()) {
            return;
        }
        walk();
        closeWindow();
    }
}

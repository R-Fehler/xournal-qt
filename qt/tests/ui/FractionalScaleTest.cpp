/*
 * xournal-qt: the real window on a screen at 125 %, 150 % (qt/docs/hidpi.md). CTest runs these once as they are and
 * once more with QT_SCALE_FACTOR=1.5 (FractionalScale.ui@150), as Qt runs on a screen at 150 % (Wayland's
 * fractional-scale-v1, X11's Xft.dpi, Windows' per-monitor DPI): the window's device pixel ratio is 1.5.
 *
 * @license GNU GPLv2 or later
 */
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "shell/AnnotationsModel.h"
#include "shell/HitPages.h"
#include "shell/MdSnippets.h"
#include "shell/PageSketches.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"
#include "shell/Thumbnails.h"

#include "AppController.h"
#include "config-test.h"

namespace {
QString fixturePath(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

QQuickItem* itemAt(QQuickItem* view, int row) {
    QQuickItem* item = nullptr;
    QMetaObject::invokeMethod(view, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, row));
    return item;
}

template <typename T = QQuickItem>
T* childNamed(QQuickItem* item, const QString& name) {
    for (auto* c: item->findChildren<T*>()) {
        if (c->objectName() == name) {
            return c;
        }
    }
    return nullptr;
}

class FractionalScale: public ::testing::Test {
protected:
    void SetUp() override {
        controller = std::make_unique<AppController>();
        qobject_cast<xqt::RecentFiles*>(controller->recentModel())->clear();
        QMetaObject::invokeMethod(controller->settingsModel(), "resetLayoutChoices");
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->addImageProvider("thumbnail", new xqt::ThumbnailProvider);
        engine->addImageProvider("sketch", new xqt::SketchProvider);
        engine->addImageProvider("preview", new xqt::PreviewProvider);
        engine->addImageProvider("hitpage", new xqt::HitPageProvider);
        engine->addImageProvider("mdsnippet", new xqt::MdSnippetProvider);
        engine->addImageProvider("annotation", new xqt::AnnotationImageProvider);
        engine->rootContext()->setContextProperty("app", controller.get());
        engine->loadFromModule("XournalQt", "Main");
        ASSERT_FALSE(engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
        ASSERT_NE(window, nullptr);
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        QTest::mouseMove(window, QPoint(-20, -20));
        wait(100);
    }
    void TearDown() override {
        controller->shutdown();
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
    static bool until(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            wait(20);
        }
        return done();
    }
    template <typename T = QQuickItem>
    T* find(const char* name) const {
        return window->findChild<T*>(name);
    }

    std::unique_ptr<AppController> controller;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;
};
}  // namespace

// A page in the sidebar is drawn with the pixels of its frame on the screen: its width times the device pixel ratio,
// once. Qt Quick multiplies an image provider's sourceSize by the pixel ratio itself; the QML multiplied it as well,
// so at 150 % the thumbnails were drawn 2.25 times as wide as their frame (5 times the pixels), at 200 % 4 times.
TEST_F(FractionalScale, sidebarThumbnailsAreDrawnWithThePixelsOfTheirFrame) {
    const double dpr = window->effectiveDevicePixelRatio();
    ASSERT_TRUE(controller->openPath(fixturePath(u8"load/pages.xopp")));
    wait(100);
    QMetaObject::invokeMethod(window, "showSidebar", Q_ARG(QVariant, true));
    auto* list = find("sidebarList");
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(until([&] { return itemAt(list, 0) != nullptr; }));
    QQuickItem* picture = childNamed(itemAt(list, 0), "sidebarThumbnail");
    ASSERT_NE(picture, nullptr);
    QQuickItem* sharp = childNamed(picture, "pageSharp");
    ASSERT_NE(sharp, nullptr);
    ASSERT_TRUE(until([&] { return picture->property("sharpShown").toBool(); })) << "the sharp thumbnail is shown";

    const double frame = picture->parentItem()->width();
    // (the image's implicit size is its pixels over the pixel ratio; the provider rounds the width up to its steps)
    const double pixels = sharp->implicitWidth() * dpr;
    EXPECT_GE(pixels, frame * dpr - 1) << "sharp on this screen";
    EXPECT_LT(pixels, frame * dpr + xqt::ThumbnailProvider::WIDTH_STEP)
            << "drawn " << pixels << " pixels wide for a frame of " << frame * dpr << " pixels (dpr " << dpr << ")";
}

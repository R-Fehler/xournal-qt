/*
 * xournal-qt: what the canvas item shows of the visible pages while and after zooming (real Qt Quick window,
 * off-screen).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <gtest/gtest.h>

#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasMemory.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "DocumentCanvasItem.h"

using namespace xqt;

namespace {
const char* QML = R"(
import QtQuick
import QtQuick.Controls
import XournalQt.Canvas
ApplicationWindow {
    width: 800; height: 700; visible: true
    background: Rectangle { color: "#404040" }
    DocumentCanvas { id: canvas; objectName: "canvas"; anchors.fill: parent }
}
)";

class CanvasItemRenderTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        for (int i = 0; i < 8; ++i) {
            session->insertNewPage(1);
        }
        view = std::make_unique<CanvasView>(*session);
        engine.loadData(QML);
        ASSERT_FALSE(engine.rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        ASSERT_NE(window, nullptr);
        canvas = window->findChild<DocumentCanvasItem*>("canvas");
        ASSERT_NE(canvas, nullptr);
        canvas->setView(view.get());
        ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
        settle(400);
    }
    void TearDown() override {
        canvas->setView(nullptr);
        view.reset();
        session.reset();
    }

    /// Events for `ms`, without waiting for renders (as the application does)
    void run(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
    }
    /// Events for `ms` and all renders done
    void settle(int ms) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }

    /// The zoom stopped: wait for the renders at the new zoom, then let the canvas compose them (a few frames)
    void zoomSettles() {
        const double zoom = view->getViewController().zoom();
        QElapsedTimer t;
        t.start();
        auto rendered = [&] {
            const auto [first, last] = view->visiblePages();
            for (size_t i = first; i <= last && i < view->pageCount(); ++i) {
                if (const auto info = view->getPage(i)->bufferInfo(); !info.valid || info.zoom != zoom) {
                    return false;
                }
            }
            return true;
        };
        while (!rendered() && t.elapsed() < 3000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        ASSERT_TRUE(rendered()) << "the visible pages were not rendered at the new zoom";
        run(150);
    }

    /// What the middle of each visible page (its part in view) shows
    enum class Shows { Page, Nothing, Preview };
    std::vector<std::pair<size_t, Shows>> visiblePagesShow() const {
        const QImage shot = window->grabWindow();
        if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
            shot.save(qEnvironmentVariable("XQT_TEST_SHOT"));
        }
        std::vector<std::pair<size_t, Shows>> shows;
        const auto [first, last] = view->visiblePages();
        for (size_t i = first; i <= last && i < view->pageCount(); ++i) {
            const QRectF r = view->pageViewRect(i).intersected(QRectF(0, 0, canvas->width(), canvas->height()));
            if (r.width() < 20 || r.height() < 20) {
                continue;
            }
            // (the lines of the ruled paper are light blue: look at a few pixels)
            Shows what = Shows::Page;
            for (int dy: {0, 7, 13}) {
                const QColor c(shot.pixel(r.center().toPoint() + QPoint(0, dy)));
                if (c == QColor(Qt::red)) {
                    what = Shows::Preview;
                } else if (c.lightness() < 150) {
                    what = Shows::Nothing;
                }
            }
            shows.emplace_back(i, what);
        }
        return shows;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    QQmlApplicationEngine engine;
    QQuickWindow* window = nullptr;
    DocumentCanvasItem* canvas = nullptr;
};
}  // namespace

// Regression test: after a zoom (pinch, Ctrl+wheel) the pages in view showed the window's grey instead of the page
// once their render at the new zoom came, until they were scrolled out of view and back. A page bigger than the
// view has tiles that are not in the scene graph (only the tiles in view are composed), and removing those from the
// page's node (Qt does not check that a node is a child in a release build) emptied the node's list of children: the
// page's white, its tiles and its preview were gone.
TEST_F(CanvasItemRenderTest, visiblePagesShowTheirRenderAfterZooming) {
    // A preview that cannot be mistaken for the page: shown until a page is rendered
    QImage red(96, 136, QImage::Format_RGB32);
    red.fill(Qt::red);
    view->setPreviewSource([red](size_t) { return red; });
    ViewController& vc = view->getViewController();
    const QPointF center(canvas->width() / 2, canvas->height() / 2);
    auto wheel = [&](double factor) {  // (Ctrl+wheel: a few steps, a frame apart)
        for (int step = 0; step < 6; ++step) {
            vc.setZoom(vc.zoom() * factor, center);
            run(16);
        }
    };

    wheel(1.1);  // in: the page is bigger than the view
    zoomSettles();
    for (const auto& [page, shows]: visiblePagesShow()) {
        EXPECT_EQ(shows, Shows::Page) << "zoomed in: page " << page + 1 << " shows "
                                      << (shows == Shows::Nothing ? "nothing" : "its preview");
    }
    wheel(0.75);  // out: several pages in view
    zoomSettles();
    const auto shown = visiblePagesShow();
    EXPECT_GE(shown.size(), 2u);
    for (const auto& [page, shows]: shown) {
        EXPECT_EQ(shows, Shows::Page) << "zoomed out: page " << page + 1 << " shows "
                                      << (shows == Shows::Nothing ? "nothing" : "its preview");
    }
}

// When the fingers of a pinch are lifted the zoom is stable: the page is rendered at once, not after the 300 ms a
// Ctrl+wheel zoom waits (it has no end).
TEST_F(CanvasItemRenderTest, aPinchThatEndedIsRenderedWithoutTheZoomWait) {
    ViewController& vc = view->getViewController();
    const QPointF center(canvas->width() / 2, canvas->height() / 2);
    vc.pinchBegin(center, 100);
    for (int step = 1; step <= 5; ++step) {
        vc.pinchUpdate(center, 100 + step * 12);
        run(16);
    }
    const double zoom = vc.zoom();
    vc.pinchEnd();
    QElapsedTimer t;
    t.start();
    auto sharp = [&] {
        const auto info = view->getPage(session->getCurrentPageNo())->bufferInfo();
        return info.valid && info.zoom == zoom;
    };
    while (!sharp() && t.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 2);
    }
    EXPECT_TRUE(sharp());
    EXPECT_LT(t.elapsed(), 200) << "the render waited for the zoom to be stable after the pinch had ended";
}

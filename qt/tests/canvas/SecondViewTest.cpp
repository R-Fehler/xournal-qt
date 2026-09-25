/*
 * xournal-qt: a second view of the same document (qt/self-reference) - what each view keeps for itself (its page, its
 * place, its zoom, its selection) and what the session's reused upstream code sees while one of them acts.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/ScrollHandler.h"
#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"

using namespace xqt;

namespace {
/// A document of six pages in a tab's view, and a second view of it beside (the reference).
class SecondView: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        for (size_t i = 1; i < 6; ++i) {
            session->insertNewPage(i);
        }
        session->setCurrentPageNo(0);
        primary = std::make_unique<CanvasView>(*session);
        primary->getViewController().setViewSize(QSizeF(800, 600));
        second = std::make_unique<CanvasView>(*session);
        second->getViewController().setViewSize(QSizeF(500, 600));
        processEvents();
    }
    void TearDown() override {
        second.reset();
        primary.reset();
        session.reset();
        app.reset();
    }
    void processEvents(int ms = 30) {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            app->getRenderService()->waitForIdle();
        }
    }
    PageRef page(size_t i) const { return session->getDocument()->getPage(i); }
    size_t elementCount(size_t i) const {
        size_t n = 0;
        for (const Layer* l: page(i)->getLayersView()) {
            n += l->getElementsView().size();
        }
        return n;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> primary;
    std::unique_ptr<CanvasView> second;
};
}  // namespace

TEST_F(SecondView, eachViewHasItsOwnPageAndPlace) {
    EXPECT_TRUE(primary->isPrimary());
    EXPECT_FALSE(second->isPrimary());
    EXPECT_EQ(session->viewCount(), 2u);

    QSignalSpy sessionPage(session.get(), &DocumentSession::currentPageChanged);
    QSignalSpy secondPage(second.get(), &CanvasView::currentPageChanged);
    second->jumpToPage(4);
    processEvents();
    EXPECT_EQ(second->currentPageNo(), 4u);
    EXPECT_EQ(session->getCurrentPageNo(), 0u) << "the page sidebar and the page number stay with the tab's view";
    EXPECT_EQ(primary->currentPageNo(), 0u);
    EXPECT_EQ(sessionPage.count(), 0);
    EXPECT_GE(secondPage.count(), 1);
    EXPECT_TRUE(second->canGoBack());
    EXPECT_FALSE(primary->canGoBack()) << "each view has its own way back";

    primary->jumpToPage(2);
    processEvents();
    EXPECT_EQ(session->getCurrentPageNo(), 2u);
    EXPECT_EQ(second->currentPageNo(), 4u) << "the second view stays where its reader is";

    // Scrolling the second view moves nothing in the first one
    const QPointF before = primary->getViewController().scrollPosition();
    second->getViewController().setScrollPosition(second->getViewController().scrollPosition() + QPointF(0, 300));
    processEvents();
    EXPECT_EQ(primary->getViewController().scrollPosition(), before);
    EXPECT_EQ(session->getCurrentPageNo(), 2u);

    // What reused upstream code asks for (undoing a page deletion, a search hit) is shown by the tab's view only
    const QPointF secondBefore = second->getViewController().scrollPosition();
    session->getScrollHandler()->scrollToPage(size_t{5});
    processEvents();
    EXPECT_EQ(session->getCurrentPageNo(), 5u);
    EXPECT_EQ(second->getViewController().scrollPosition(), secondBefore);

    // Zoom: each its own
    second->getViewController().setZoom(2.5, QPointF(0, 0));
    EXPECT_NE(primary->getViewController().zoom(), second->getViewController().zoom());
}

TEST_F(SecondView, upstreamCodeSeesTheViewThatActs) {
    second->jumpToPage(3);
    second->getViewController().setZoom(2.0, QPointF(0, 0));
    processEvents();
    XournalView* xournal = session->getWindow()->getXournal();
    EXPECT_EQ(session->getZoomControl(), primary->getZoomControl());
    EXPECT_EQ(xournal->getCurrentPage(), 0u);
    {
        const auto scope = second->actingScope();
        EXPECT_EQ(session->getZoomControl(), second->getZoomControl());
        EXPECT_EQ(session->getCurrentPageNo(), 3u) << "tools that take the current page take the second view's";
        EXPECT_EQ(session->getCurrentPage(), page(3));
        EXPECT_EQ(xournal->getCurrentPage(), 3u);
        EXPECT_DOUBLE_EQ(xournal->getZoom(), second->getZoom());
    }
    EXPECT_EQ(session->getZoomControl(), primary->getZoomControl()) << "and the tab's view again afterwards";
    EXPECT_EQ(session->getCurrentPageNo(), 0u);

    // The second view goes: the tab's view stays the view of the session
    second.reset();
    EXPECT_EQ(session->viewCount(), 1u);
    EXPECT_TRUE(primary->isPrimary());
    EXPECT_EQ(session->getZoomControl(), primary->getZoomControl());
}

TEST_F(SecondView, aLayerChangeRedrawsBothViews) {
    // The layer controller tells "the view" of the session: every view showing the page redraws it
    second->jumpToPage(0);
    processEvents(100);
    ASSERT_TRUE(primary->getPage(0)->bufferInfo().valid);
    ASSERT_TRUE(second->getPage(0)->bufferInfo().valid);
    QSignalSpy firstUpdates(primary.get(), &CanvasView::updateRequested);
    QSignalSpy secondUpdates(second.get(), &CanvasView::updateRequested);
    session->getWindow()->getXournal()->layerChanged(0);
    processEvents(100);
    EXPECT_GE(firstUpdates.count(), 1);
    EXPECT_GE(secondUpdates.count(), 1);
}

TEST_F(SecondView, aStrokeInTheSecondViewLandsOnItsPage) {
    second->jumpToPage(3);
    processEvents();
    app->getToolHandler()->selectTool(TOOL_PEN);
    CanvasInput input(*second);
    QPointingDevice pen{"test pen", 1001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3};
    const QRectF r = second->pageViewRect(3);
    const double z = second->getViewController().zoom();
    ulong time = 1000;
    auto tablet = [&](QEvent::Type type, QPointF onPage, double pressure, Qt::MouseButton button,
                      Qt::MouseButtons buttons) {
        const QPointF pos = r.topLeft() + onPage * z;
        QTabletEvent e(type, &pen, pos, pos, pressure, 0.f, 0.f, 0.f, 0.0, 0.f, Qt::NoModifier, button, buttons);
        e.setTimestamp(time += 5);
        input.tabletEvent(&e, pos);
    };
    tablet(QEvent::TabletPress, QPointF(100, 100), 0.4, Qt::LeftButton, Qt::LeftButton);
    for (int i = 1; i <= 20; ++i) {
        tablet(QEvent::TabletMove, QPointF(100 + 5 * i, 100 + 2 * i), 0.5, Qt::NoButton, Qt::LeftButton);
    }
    tablet(QEvent::TabletRelease, QPointF(200, 140), 0.0, Qt::LeftButton, Qt::NoButton);
    processEvents();

    EXPECT_EQ(elementCount(3), 1u) << "on the page of the second view";
    EXPECT_EQ(elementCount(0), 0u);
    EXPECT_EQ(session->getCurrentPageNo(), 0u) << "the tab's view did not move";
    EXPECT_EQ(session->getZoomControl(), primary->getZoomControl());
    // One history for the document: the tab's undo takes it back
    ASSERT_TRUE(session->getUndoRedoHandler()->canUndo());
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(elementCount(3), 0u);
}

// Each view has its own selection: what is selected beside the notes is not selected in them; a page change of the
// session (undo, saving) puts both back into the document.
TEST_F(SecondView, aSelectionBelongsToItsView) {
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(100, 100));
    stroke->addPoint(Point(200, 140));
    page(3)->getSelectedLayer()->addElement(std::move(stroke));
    second->jumpToPage(3);
    processEvents();
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    second->selectAllOnPage();
    ASSERT_NE(second->getSelection(), nullptr) << "selected on the second view's page (4), not the tab's (1)";
    EXPECT_EQ(primary->getSelection(), nullptr);
    EXPECT_EQ(session->getCurrentPageNo(), 0u);
    EXPECT_EQ(session->getWindow()->getXournal()->getSelection(), nullptr) << "upstream code sees the tab's view";
    session->clearSelectionEndText();
    EXPECT_EQ(second->getSelection(), nullptr);
    EXPECT_EQ(elementCount(3), 1u) << "back in the document";
}

TEST_F(SecondView, itsPageStaysTheSamePageWhenOthersComeOrGo) {
    second->jumpToPage(3);
    processEvents();
    const PageRef shown = page(3);
    session->insertNewPage(0);
    processEvents();
    EXPECT_EQ(second->currentPageNo(), 4u) << "the same page, one on";
    EXPECT_EQ(page(second->currentPageNo()), shown);
    ASSERT_TRUE(session->deletePages({0, 1}));
    processEvents();
    EXPECT_EQ(page(second->currentPageNo()), shown);
    EXPECT_EQ(second->currentPageNo(), 2u);
    // Its own page deleted: a page near it
    ASSERT_TRUE(session->deletePages({2}));
    processEvents();
    EXPECT_LT(second->currentPageNo(), session->getDocument()->getPageCount());
}

TEST_F(SecondView, swapExchangesThePlaces) {
    primary->jumpToPage(5);
    second->jumpToPage(1);
    processEvents();
    const double firstZoom = primary->getViewController().zoom();
    primary->swapPlacesWith(*second);
    processEvents();
    EXPECT_EQ(primary->currentPageNo(), 1u);
    EXPECT_EQ(second->currentPageNo(), 5u);
    EXPECT_EQ(session->getCurrentPageNo(), 1u);
    EXPECT_DOUBLE_EQ(primary->getViewController().zoom(), firstZoom) << "each side keeps its zoom";
    EXPECT_TRUE(primary->canGoBack());
    primary->navigateBack();
    processEvents();
    EXPECT_EQ(primary->currentPageNo(), 5u);

}

TEST_F(SecondView, thePrimaryViewGoesFirst) {
    // (a tab moved to another window takes its view along after the second one went; the other way round works too)
    primary.reset();
    EXPECT_TRUE(second->isPrimary());
    second->jumpToPage(2);
    processEvents();
    EXPECT_EQ(session->getCurrentPageNo(), 2u) << "it is the session's view now";
}

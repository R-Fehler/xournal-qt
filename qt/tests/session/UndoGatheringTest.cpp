/*
 * xournal-qt: several undo steps as one (SequenceUndoAction, UndoGathering; the undo stack's sink, a seam in
 * upstream's UndoRedoHandler): what a plugin command does is one undo step, and a failed one is rolled back
 * (qt/docs/decisions/0008-js-plugins.md).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/SequenceUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "config-test.h"

using namespace xqt;

namespace {
class UndoGatheringTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    /// A stroke into the page's selected layer, its undo step pushed as the pen does
    static void addStroke(DocumentSession& s, size_t pageNo, double x) {
        auto page = s.getDocument()->getPage(pageNo);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(1.41);
        stroke->addPoint(Point(x, 50, 1.0));
        stroke->addPoint(Point(x + 40, 70, 1.0));
        const Stroke* raw = stroke.get();
        Layer* layer = page->getSelectedLayer();
        s.getDocument()->lock();
        layer->addElement(std::move(stroke));
        s.getDocument()->unlock();
        s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    }
    static size_t elementCount(DocumentSession& s) {
        size_t n = 0;
        for (const Layer* l: s.getDocument()->getPage(0)->getLayers()) {
            n += l->getElementsView().size();
        }
        return n;
    }
    static size_t layerCount(DocumentSession& s) { return s.getDocument()->getPage(0)->getLayerCount(); }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(UndoGatheringTest, stepsThatDependOnEachOtherAreOneUndoStep) {
    DocumentSession session(*app);
    UndoRedoHandler* undo = session.getUndoRedoHandler();
    addStroke(session, 0, 10);  // (a step before: stays)
    const size_t layersBefore = layerCount(session);
    {
        UndoGathering gather(session, "Plot");
        ASSERT_TRUE(gather.active());
        // A new layer, then strokes into it (selected): undone in reverse order, or the strokes would be lost
        session.getLayerController()->addNewLayer(false);
        addStroke(session, 0, 100);
        addStroke(session, 0, 200);
        EXPECT_EQ(gather.count(), 3u);
        EXPECT_EQ(undo->undoDescription(), "Undo: Draw stroke");  // (nothing reached the stack yet)
        EXPECT_TRUE(gather.commit());
    }
    EXPECT_EQ(undo->undoDescription(), "Undo: Plot");
    EXPECT_EQ(layerCount(session), layersBefore + 1);
    EXPECT_EQ(elementCount(session), 3u);

    undo->undo();
    EXPECT_EQ(layerCount(session), layersBefore);
    EXPECT_EQ(elementCount(session), 1u);
    EXPECT_EQ(undo->undoDescription(), "Undo: Draw stroke");

    undo->redo();
    EXPECT_EQ(layerCount(session), layersBefore + 1);
    EXPECT_EQ(elementCount(session), 3u);
    EXPECT_EQ(undo->undoDescription(), "Undo: Plot");
}

TEST_F(UndoGatheringTest, aRollbackLeavesTheDocumentAndTheStackAsTheyWere) {
    DocumentSession session(*app);
    UndoRedoHandler* undo = session.getUndoRedoHandler();
    addStroke(session, 0, 10);
    const quint64 revision = session.pageRevision(0);
    const size_t layersBefore = layerCount(session);
    {
        UndoGathering gather(session, "Plot");
        session.getLayerController()->addNewLayer(false);
        addStroke(session, 0, 100);
        gather.rollback();
        EXPECT_FALSE(gather.commit());  // (nothing left)
    }
    EXPECT_EQ(layerCount(session), layersBefore);
    EXPECT_EQ(elementCount(session), 1u);
    EXPECT_EQ(undo->undoDescription(), "Undo: Draw stroke");
    EXPECT_NE(session.pageRevision(0), revision);  // (thumbnails follow the rollback)
    // The sink is gone: steps go onto the stack again
    addStroke(session, 0, 300);
    undo->undo();
    EXPECT_EQ(elementCount(session), 1u);
}

TEST_F(UndoGatheringTest, notCommittedIsRolledBackAndNothingGatheredPushesNothing) {
    DocumentSession session(*app);
    UndoRedoHandler* undo = session.getUndoRedoHandler();
    {
        UndoGathering gather(session, "Plot");
        addStroke(session, 0, 100);
    }  // (an exception left the command: the destructor rolls back)
    EXPECT_EQ(elementCount(session), 0u);
    EXPECT_FALSE(undo->canUndo());
    {
        UndoGathering gather(session, "Nothing");
        EXPECT_FALSE(gather.commit());
    }
    EXPECT_FALSE(undo->canUndo());
}

TEST_F(UndoGatheringTest, aSecondGatheringWhileOneRunsGathersNothing) {
    DocumentSession session(*app);
    UndoGathering outer(session, "Outer");
    {
        UndoGathering inner(session, "Inner");
        EXPECT_FALSE(inner.active());
        addStroke(session, 0, 100);
        EXPECT_FALSE(inner.commit());
    }
    EXPECT_EQ(outer.count(), 1u);  // (the outer one has it)
    EXPECT_TRUE(outer.commit());
    EXPECT_EQ(session.getUndoRedoHandler()->undoDescription(), "Undo: Outer");
}

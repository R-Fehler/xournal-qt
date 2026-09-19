/*
 * xournal-qt: upstream undo actions and UndoRedoHandler running against the shadow `Control` interface
 * (qt/compat/include/control/Control.h) instead of the GTK application object.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <gtest/gtest.h>

#include "control/Control.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

namespace {
/// Minimal per-document session: what a tab of the Qt app provides to reused upstream code.
class TestSession: public Control {
public:
    TestSession(): doc(std::make_unique<Document>(this)), undoRedo(std::make_unique<UndoRedoHandler>(this)) {}

    Settings* getSettings() const override { return nullptr; }
    ToolHandler* getToolHandler() const override { return nullptr; }
    ZoomControl* getZoomControl() const override { return nullptr; }
    Document* getDocument() const override { return doc.get(); }
    UndoRedoHandler* getUndoRedoHandler() const override { return undoRedo.get(); }
    MainWindow* getWindow() const override { return nullptr; }
    ScrollHandler* getScrollHandler() const override { return nullptr; }
    PageRef getCurrentPage() override { return doc->getPage(0); }
    size_t getCurrentPageNo() const override { return 0; }
    XournalppCursor* getCursor() const override { return nullptr; }
    PageTypeHandler* getPageTypes() const override { return nullptr; }
    LayerController* getLayerController() const override { return nullptr; }
    ActionDatabase* getActionDatabase() const override { return nullptr; }
    void clearSelectionEndText() override { ++clearSelectionCalls; }
    void setCopyCutEnabled(bool) override {}
    void insertNewPage(size_t, bool) override {}
    void insertPage(const PageRef&, size_t, bool) override {}

    std::unique_ptr<Document> doc;
    std::unique_ptr<UndoRedoHandler> undoRedo;
    int clearSelectionCalls = 0;
};

std::unique_ptr<Stroke> makeStroke() {
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.41);
    s->addPoint(Point(10, 10, 1.0));
    s->addPoint(Point(20, 25, 1.2));
    s->addPoint(Point(30, 20, 0.8));
    return s;
}
}  // namespace

TEST(QtUndoRedo, insertUndoRedo) {
    TestSession session;
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    session.doc->addPage(page);
    Layer* layer = page->getSelectedLayer();
    ASSERT_NE(layer, nullptr);

    auto stroke = makeStroke();
    const Stroke* raw = stroke.get();
    layer->addElement(std::move(stroke));
    session.undoRedo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));

    EXPECT_TRUE(session.undoRedo->canUndo());
    EXPECT_FALSE(session.undoRedo->canRedo());
    EXPECT_TRUE(session.undoRedo->isChanged());
    EXPECT_EQ(layer->getElementsView().size(), 1u);

    session.undoRedo->undo();
    EXPECT_EQ(layer->getElementsView().size(), 0u);
    EXPECT_FALSE(session.undoRedo->canUndo());
    EXPECT_TRUE(session.undoRedo->canRedo());

    session.undoRedo->redo();
    ASSERT_EQ(layer->getElementsView().size(), 1u);
    EXPECT_EQ(layer->getElementsView().front(), raw);
    EXPECT_TRUE(session.undoRedo->canUndo());
}

TEST(QtUndoRedo, savedStateTracking) {
    TestSession session;
    auto page = std::make_shared<XojPage>(595.0, 842.0);
    session.doc->addPage(page);
    Layer* layer = page->getSelectedLayer();

    auto stroke = makeStroke();
    const Stroke* raw = stroke.get();
    layer->addElement(std::move(stroke));
    session.undoRedo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    session.undoRedo->documentSaved();
    EXPECT_FALSE(session.undoRedo->isChanged());

    session.undoRedo->undo();
    EXPECT_TRUE(session.undoRedo->isChanged());
    session.undoRedo->redo();
    EXPECT_FALSE(session.undoRedo->isChanged());
}

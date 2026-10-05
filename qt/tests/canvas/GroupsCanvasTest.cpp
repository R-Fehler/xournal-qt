/*
 * xournal-qt: groups of elements in the canvas (qt/docs/groups.md): grouping and ungrouping what is selected (one
 * undo step each).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QClipboard>
#include <QGuiApplication>
#include <QTabletEvent>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include "control/ToolEnums.h"
#include "control/ToolHandler.h"
#include "control/tools/EditSelection.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "render/RenderService.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/ElementGroups.h"
#include "session/StickerFile.h"
#include "session/StickyNote.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"

using namespace xqt;

namespace {
class GroupsCanvasTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        session = std::make_unique<DocumentSession>(*app);
        view = std::make_unique<CanvasView>(*session);
        view->getViewController().setViewSize(QSizeF(1000, 1200));
        input = std::make_unique<CanvasInput>(*view);
        processEvents();
    }
    void TearDown() override {
        input.reset();
        view.reset();
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

    Layer* layer(size_t page = 0) const { return session->getDocument()->getPage(page)->getSelectedLayer(); }

    /// A stroke from (x0, y0) to (x1, y1) (page points) in the page's layer, in group `group`
    Stroke* addStroke(double x0, double y0, double x1, double y1, uint32_t group = 0, size_t page = 0) {
        auto s = std::make_unique<Stroke>();
        s->setToolType(StrokeTool::PEN);
        s->setWidth(2);
        for (int i = 0; i <= 10; ++i) {
            s->addPoint(Point(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10));
        }
        s->setGroup(group);
        Stroke* raw = s.get();
        std::unique_lock lock(*session->getDocument());
        layer(page)->addElement(std::move(s));
        return raw;
    }

    std::vector<uint32_t> groupsOnPage(size_t page = 0) const {
        std::vector<uint32_t> out;
        for (const Element* e: layer(page)->getElementsView()) {
            out.push_back(e->getGroup());
        }
        return out;
    }

    std::vector<const Element*> selected() const {
        std::vector<const Element*> out;
        if (view->getSelection()) {
            for (const Element* e: view->getSelection()->getElementsView()) {
                out.push_back(e);
            }
        }
        return out;
    }

    QPointF viewPos(size_t page, QPointF pagePoint) const {
        return view->pageViewRect(page).topLeft() + pagePoint * view->getViewController().zoom();
    }
    void mouse(QEvent::Type type, QPointF pos, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent e(type, pos, pos, button, buttons, Qt::NoModifier, &mousePointer);
        e.setTimestamp(timestamp);
        timestamp += 5;
        input->mouseEvent(&e, pos);
    }
    /// A tap with the mouse at a place of a page
    void tap(size_t page, QPointF at) {
        mouse(QEvent::MouseButtonPress, viewPos(page, at), Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, viewPos(page, at), Qt::LeftButton, Qt::NoButton);
        processEvents();
    }
    /// A drag with the mouse from a place of a page to a place of (another) page
    void drag(size_t fromPage, QPointF from, size_t toPage, QPointF to) {
        const QPointF a = viewPos(fromPage, from);
        const QPointF b = viewPos(toPage, to);
        mouse(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        for (int i = 1; i <= 20; ++i) {
            mouse(QEvent::MouseMove, a + (b - a) * i / 20.0, Qt::NoButton, Qt::LeftButton);
        }
        mouse(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        processEvents();
    }

    QTemporaryDir tmp;
    QPointingDevice mousePointer{"test mouse", 1005, QInputDevice::DeviceType::Mouse,
                                 QPointingDevice::PointerType::Generic, QInputDevice::Capability::Position, 3, 3};
    ulong timestamp = 1000;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    std::unique_ptr<CanvasView> view;
    std::unique_ptr<CanvasInput> input;
};
}  // namespace

// Ctrl+G: the selected elements become one group, one undo step; Ctrl+Shift+G: they leave it, one undo step
TEST_F(GroupsCanvasTest, groupAndUngroupAreOneUndoStepEach) {
    addStroke(100, 100, 200, 150);
    addStroke(300, 100, 400, 150);
    addStroke(100, 300, 200, 350);
    session->getUndoRedoHandler()->clearContents();
    view->selectAllOnPage();
    ASSERT_EQ(selected().size(), 3u);
    auto state = view->groupState();
    EXPECT_TRUE(state.canGroup);
    EXPECT_FALSE(state.canUngroup);
    EXPECT_FALSE(state.oneGroup);

    ASSERT_TRUE(view->groupSelection());
    std::set<uint32_t> ids;
    for (const Element* e: selected()) {
        ids.insert(e->getGroup());
    }
    ASSERT_EQ(ids.size(), 1u);
    const uint32_t id = *ids.begin();
    EXPECT_NE(id, 0u);
    state = view->groupState();
    EXPECT_FALSE(state.canGroup) << "already one group";
    EXPECT_TRUE(state.canUngroup);
    EXPECT_TRUE(state.oneGroup);
    EXPECT_FALSE(view->groupSelection()) << "nothing to do";
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), "Undo: Group");

    // Back in the layer, still grouped; undo and redo it
    view->clearSelection();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{id, id, id}));
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{0, 0, 0}));
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{id, id, id}));

    // Ungroup
    view->selectAllOnPage();
    ASSERT_TRUE(view->ungroupSelection());
    EXPECT_FALSE(view->groupState().canUngroup);
    EXPECT_FALSE(view->ungroupSelection()) << "nothing to do";
    view->clearSelection();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{0, 0, 0}));
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), "Undo: Ungroup");
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{id, id, id}));
}

// A selection of a group and loose elements can be grouped (into one new group) and ungrouped; a new group's number
// is one no group of the document has
TEST_F(GroupsCanvasTest, aGroupAndLooseElementsMakeOneNewGroup) {
    Stroke* a = addStroke(100, 100, 200, 150, 4);
    Stroke* b = addStroke(300, 100, 400, 150, 4);
    Stroke* c = addStroke(100, 300, 200, 350);
    session->insertNewPage(1);
    addStroke(100, 100, 200, 150, 9, 1);
    processEvents();
    view->selectTogether(*view->getPage(0), {}, {{layer(0), a}, {layer(0), b}, {layer(0), c}});
    ASSERT_EQ(selected().size(), 3u);
    const auto state = view->groupState();
    EXPECT_TRUE(state.canGroup);
    EXPECT_TRUE(state.canUngroup);
    EXPECT_FALSE(state.oneGroup);
    ASSERT_TRUE(view->groupSelection());
    view->clearSelection();
    const auto groups = groupsOnPage();
    EXPECT_GT(groups[0], 9u);
    EXPECT_EQ(groups, (std::vector<uint32_t>{groups[0], groups[0], groups[0]}));
}

// Nothing to group or ungroup in a view for reading only
TEST_F(GroupsCanvasTest, notInAViewForReading) {
    addStroke(100, 100, 200, 150, 4);
    addStroke(300, 100, 400, 150);
    view->selectAllOnPage();
    view->setReadingOnly(true);
    const auto state = view->groupState();
    EXPECT_FALSE(state.canGroup);
    EXPECT_FALSE(state.canUngroup);
    EXPECT_FALSE(view->groupSelection());
    EXPECT_FALSE(view->ungroupSelection());
}

// --- selecting (any member selects the group) -------------------------------------------------------------------

// A tap on any member selects the whole group; a loose element alone
TEST_F(GroupsCanvasTest, aTapOnAMemberSelectsTheWholeGroup) {
    Stroke* a = addStroke(100, 100, 200, 100, 4);
    Stroke* b = addStroke(100, 300, 200, 300, 4);
    Stroke* loose = addStroke(400, 100, 500, 100);
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    tap(0, QPointF(150, 300));
    const auto both = selected();
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(std::set<const Element*>(both.begin(), both.end()), (std::set<const Element*>{a, b}));
    EXPECT_TRUE(view->groupState().oneGroup);
    view->clearSelection();
    tap(0, QPointF(450, 100));
    ASSERT_EQ(selected().size(), 1u);
    EXPECT_EQ(selected().front(), loose);
    view->clearSelection();
    // The object select tool too
    app->getToolHandler()->selectTool(TOOL_SELECT_OBJECT);
    tap(0, QPointF(150, 100));
    EXPECT_EQ(selected().size(), 2u);
}

// A rectangle around one member selects the whole group (its other members far outside it)
TEST_F(GroupsCanvasTest, aRectangleAroundOneMemberSelectsTheWholeGroup) {
    addStroke(100, 100, 200, 120, 4);
    addStroke(500, 700, 600, 720, 4);
    addStroke(300, 300, 350, 320);
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    drag(0, QPointF(80, 80), 0, QPointF(220, 140));
    EXPECT_EQ(selected().size(), 2u);
    view->clearSelection();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{4, 4, 0}));
}

// Select more: a tap on a member adds the whole group, a second tap takes the whole group away again
TEST_F(GroupsCanvasTest, selectMoreAddsAndTakesAwayWholeGroups) {
    Stroke* loose = addStroke(400, 100, 500, 100);
    Stroke* a = addStroke(100, 100, 200, 100, 4);
    addStroke(100, 300, 200, 300, 4);
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    tap(0, QPointF(450, 100));
    ASSERT_EQ(selected().size(), 1u);
    CanvasPage& page = *view->getPage(0);
    view->toggleSelected(page, nullptr, a);
    EXPECT_EQ(selected().size(), 3u);
    view->toggleSelected(page, nullptr, a);
    ASSERT_EQ(selected().size(), 1u);
    EXPECT_EQ(selected().front(), loose);
}

// A group dragged onto another page where a group has its number (a duplicated page) stays a group of its own
TEST_F(GroupsCanvasTest, aGroupMovedToAnotherPageDoesNotJoinAGroupThere) {
    session->insertNewPage(1);
    processEvents();
    addStroke(100, 100, 200, 100, 4);
    addStroke(100, 140, 200, 140, 4);
    addStroke(100, 100, 200, 100, 4, 1);
    addStroke(100, 140, 200, 140, 4, 1);
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    tap(0, QPointF(150, 100));
    ASSERT_EQ(selected().size(), 2u);
    const std::vector<const Element*> moved = selected();
    drag(0, QPointF(150, 100), 1, QPointF(150, 400));
    view->clearSelection();
    ASSERT_EQ(layer(0)->getElementsView().size(), 0u) << "moved";
    ASSERT_EQ(layer(1)->getElementsView().size(), 4u);
    std::vector<uint32_t> movedGroups, stayedGroups;
    for (const Element* e: layer(1)->getElementsView()) {
        (std::find(moved.begin(), moved.end(), e) != moved.end() ? movedGroups : stayedGroups).push_back(e->getGroup());
    }
    EXPECT_EQ(stayedGroups, (std::vector<uint32_t>{4, 4}));
    ASSERT_EQ(movedGroups.size(), 2u);
    EXPECT_NE(movedGroups[0], 4u) << "a new number";
    EXPECT_NE(movedGroups[0], 0u);
    EXPECT_EQ(movedGroups[0], movedGroups[1]);
    // Selected again: only the moved two
    tap(1, QPointF(150, 400));
    EXPECT_EQ(selected().size(), 2u);
}

// --- the clipboard -----------------------------------------------------------------------------------------------

// Copied: upstream's data as it is (Xournal++ pastes it) and the groups beside it; pasted: a group of its own (a new
// number), never joined to the one it was copied from
TEST_F(GroupsCanvasTest, aCopiedGroupIsPastedAsANewGroup) {
    addStroke(100, 100, 200, 100, 4);
    addStroke(100, 140, 200, 140, 4);
    addStroke(400, 100, 500, 100);
    view->selectAllOnPage();
    ASSERT_TRUE(view->copySelection());
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    ASSERT_TRUE(mime->hasFormat("application/xournal"));
    ASSERT_TRUE(mime->hasFormat(groups::CLIPBOARD_MIME));
    EXPECT_EQ(mime->data(groups::CLIPBOARD_MIME).toStdString(), "4 4 0");
    view->clearSelection();
    ASSERT_TRUE(view->pasteElements());
    view->clearSelection();
    const auto groups = groupsOnPage();
    ASSERT_EQ(groups.size(), 6u);
    EXPECT_EQ(groups[3], groups[4]);
    EXPECT_NE(groups[3], 0u);
    EXPECT_NE(groups[3], 4u) << "a group of its own";
    EXPECT_EQ(groups[5], 0u);
    // Upstream's data without ours (copied in Xournal++): pasted without groups
    auto* plain = new QMimeData;
    plain->setData("application/xournal", mime->data("application/xournal"));
    QGuiApplication::clipboard()->setMimeData(plain);
    ASSERT_TRUE(view->pasteElements());
    view->clearSelection();
    const auto after = groupsOnPage();
    ASSERT_EQ(after.size(), 9u);
    EXPECT_EQ(after[6], 0u);
    EXPECT_EQ(after[7], 0u);
}

// A selection of notes with elements keeps its groups on the clipboard (the fork's format), pasted with new numbers
TEST_F(GroupsCanvasTest, groupsInACopiedSelectionOfNotesAndElements) {
    Stroke* a = addStroke(100, 100, 200, 100, 4);
    Stroke* b = addStroke(100, 140, 200, 140, 4);
    std::vector<const Element*> elements{a, b};
    const std::string bytes = sticky::serializeGroup(xoj::util::Rectangle<double>(100, 100, 100, 40), {}, elements);
    auto read = sticky::deserializeGroup(bytes.data(), bytes.size());
    ASSERT_TRUE(read);
    ASSERT_EQ(read->elements.size(), 2u);
    EXPECT_EQ(read->elements[0]->getGroup(), 4u);
    EXPECT_EQ(read->elements[1]->getGroup(), 4u);
    // Pasted (as a sticker is): one new group
    ASSERT_TRUE(view->pasteSticker(bytes));
    view->clearSelection();
    const auto groups = groupsOnPage();
    ASSERT_EQ(groups.size(), 4u);
    EXPECT_EQ(groups[2], groups[3]);
    EXPECT_NE(groups[2], 4u);
}

// --- stickers ----------------------------------------------------------------------------------------------------

// A sticker is pasted as a group: a tap on any of its strokes selects all of it; it can be ungrouped
TEST_F(GroupsCanvasTest, aPastedStickerIsAGroup) {
    sticky::Group content;
    auto s1 = std::make_unique<Stroke>();
    s1->setToolType(StrokeTool::PEN);
    s1->setWidth(2);
    s1->addPoint(Point(10, 10));
    s1->addPoint(Point(60, 10));
    auto s2 = s1->cloneStroke();
    s2->move(0, 40);
    auto s3 = s1->cloneStroke();
    s3->move(0, 80);
    s3->setGroup(9);  // (a group inside the sticker gives way: groups are flat)
    content.elements.push_back(std::move(s1));
    content.elements.push_back(std::move(s2));
    content.elements.push_back(std::move(s3));
    content.markdown = {false, false, false};
    content.bounds = xoj::util::Rectangle<double>(10, 10, 50, 80);
    auto doc = stickers::makeDocument(std::move(content), Color(0xffffffU));
    const fs::path file = fs::path(tmp.filePath("Lines.xopp").toStdString());
    ASSERT_TRUE(stickers::write(*doc, file));
    auto read = stickers::read(file);
    ASSERT_TRUE(read);
    ASSERT_TRUE(view->pasteSticker(stickers::clipboardBytes(*read)));
    EXPECT_EQ(selected().size(), 3u) << "pasted selected";
    EXPECT_TRUE(view->groupState().oneGroup) << "one group: its pill offers Ungroup";
    view->clearSelection();
    const auto groups = groupsOnPage();
    ASSERT_EQ(groups.size(), 3u);
    EXPECT_NE(groups[0], 0u);
    EXPECT_EQ(groups, (std::vector<uint32_t>{groups[0], groups[0], groups[0]}));
    // A tap on one of its strokes selects it all
    app->getToolHandler()->selectTool(TOOL_SELECT_RECT);
    const Element* first = layer()->getElementsView().front();
    const auto box = first->getBoundingBox();
    tap(0, QPointF(box.x + box.width / 2, box.y + box.height / 2));
    EXPECT_EQ(selected().size(), 3u);
    ASSERT_TRUE(view->ungroupSelection());
    view->clearSelection();
    EXPECT_EQ(groupsOnPage(), (std::vector<uint32_t>{0, 0, 0}));
}

/*
 * xournal-qt: reference mode in the tab list and the controller - a second document beside the current one.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/ReferenceMode.h"
#include "shell/TabManager.h"

#include "AppController.h"
#include "CanvasView.h"
#include "config-test.h"

using namespace xqt;

namespace {
QString fixture(const char8_t* rel) {
    const auto p = GET_TESTFILE(rel);
    return QString::fromUtf8(reinterpret_cast<const char*>(p.c_str()));
}

/// Three documents: 0, 1, 2 (the last one current)
struct ThreeTabs {
    AppController c;
    ThreeTabs() {
        c.newDocument();
        c.newDocument();
        c.newDocument();
        c.setCurrentTab(0);
    }
    TabManager& tabs() { return c.tabManager(); }
    ReferenceMode& ref() { return c.reference(); }
};
}  // namespace

TEST(ReferenceMode, aTabShowsAnotherTabBesideItself) {
    ThreeTabs t;
    EXPECT_FALSE(t.ref().active());
    EXPECT_EQ(t.ref().view(), nullptr);
    QSignalSpy changed(&t.ref(), &ReferenceMode::changed);
    t.ref().showTab(2);
    EXPECT_TRUE(t.ref().active());
    EXPECT_EQ(t.ref().tab(), 2);
    EXPECT_EQ(t.ref().view(), t.tabs().view(2));
    EXPECT_EQ(t.tabs().referenceOf(0), 2);
    EXPECT_GE(changed.count(), 1);
    EXPECT_EQ(t.c.currentTab(), 0) << "the current tab stays the main document";
    // The tab strip marks the reference of the current tab
    EXPECT_TRUE(t.tabs().data(t.tabs().index(2), TabManager::ReferenceRole).toBool());
    EXPECT_FALSE(t.tabs().data(t.tabs().index(1), TabManager::ReferenceRole).toBool());
    EXPECT_FALSE(t.tabs().data(t.tabs().index(0), TabManager::ReferenceRole).toBool());

    t.ref().showTab(0);  // not beside itself
    EXPECT_EQ(t.tabs().referenceOf(0), 2);
}

TEST(ReferenceMode, eachTabHasItsOwnReference) {
    ThreeTabs t;
    t.ref().showTab(2);
    t.c.setCurrentTab(1);
    EXPECT_FALSE(t.ref().active()) << "the other tab has no reference";
    EXPECT_FALSE(t.tabs().data(t.tabs().index(2), TabManager::ReferenceRole).toBool());
    t.ref().showTab(0);
    EXPECT_EQ(t.ref().view(), t.tabs().view(0));
    t.c.setCurrentTab(0);
    EXPECT_EQ(t.ref().view(), t.tabs().view(2)) << "switching back shows that tab's own reference";
    EXPECT_TRUE(t.tabs().data(t.tabs().index(2), TabManager::ReferenceRole).toBool());
    EXPECT_FALSE(t.tabs().data(t.tabs().index(0), TabManager::ReferenceRole).toBool());
    // Moving tabs keeps the pairs
    t.c.moveTab(2, 0);
    EXPECT_EQ(t.c.currentTab(), 1);
    EXPECT_EQ(t.ref().tab(), 0);
    EXPECT_EQ(t.ref().view(), t.tabs().view(0));
}

TEST(ReferenceMode, closingTheReferenceTabClosesTheSplit) {
    ThreeTabs t;
    t.ref().showTab(2);
    t.c.setCurrentTab(1);
    t.ref().showTab(2);
    t.c.setCurrentTab(0);
    t.c.closeTab(2);
    EXPECT_FALSE(t.ref().active());
    EXPECT_EQ(t.tabs().referenceOf(0), -1);
    EXPECT_EQ(t.tabs().referenceOf(1), -1);

    // Closing the main document leaves its reference open as a tab of its own
    t.ref().showTab(1);
    t.c.closeTab(0);
    EXPECT_EQ(t.c.tabCount(), 1);
    EXPECT_FALSE(t.ref().active());

    // "Close" in the pill: the split closes, the tab stays
    t.c.newDocument();
    t.ref().showTab(0);
    ASSERT_TRUE(t.ref().active());
    t.ref().close();
    EXPECT_FALSE(t.ref().active());
    EXPECT_EQ(t.c.tabCount(), 2);
}

TEST(ReferenceMode, aTabMovedToAnotherListLeavesItsPairs) {
    ThreeTabs t;
    t.ref().showTab(2);
    auto taken = t.tabs().takeTab(2);  // (another window takes it)
    ASSERT_NE(taken, nullptr);
    EXPECT_FALSE(t.ref().active());
    EXPECT_EQ(t.tabs().referenceOf(0), -1);

    t.ref().showTab(1);
    auto main = t.tabs().takeTab(0);
    ASSERT_NE(main, nullptr);
    EXPECT_EQ(main->reference, nullptr) << "the reference stays in this window";
}

TEST(ReferenceMode, swappingRolesMakesTheReferenceTheMainDocument) {
    ThreeTabs t;
    t.ref().showTab(2);
    CanvasView* notes = t.tabs().view(0);
    CanvasView* book = t.tabs().view(2);
    t.ref().swapRoles();
    EXPECT_EQ(t.c.currentTab(), 2);
    EXPECT_EQ(t.c.view(), book);
    EXPECT_EQ(t.ref().view(), notes);
    EXPECT_TRUE(t.tabs().data(t.tabs().index(0), TabManager::ReferenceRole).toBool());
    t.ref().swapRoles();
    EXPECT_EQ(t.c.view(), notes);
    EXPECT_EQ(t.ref().view(), book);
}

TEST(ReferenceMode, poppingOutPutsTheReferenceTabNextToTheNotesAndShowsIt) {
    ThreeTabs t;
    DocumentSession* a = t.tabs().session(0);
    DocumentSession* b = t.tabs().session(1);
    DocumentSession* c = t.tabs().session(2);
    t.ref().showTab(2);
    t.ref().popOut();
    EXPECT_EQ(t.tabs().indexOf(a), 0);
    EXPECT_EQ(t.tabs().indexOf(c), 1) << "the reference comes right after the notes";
    EXPECT_EQ(t.tabs().indexOf(b), 2);
    EXPECT_EQ(t.c.currentTab(), 1) << "the reference is shown";
    EXPECT_FALSE(t.ref().active());
    EXPECT_EQ(t.tabs().referenceOf(0), -1) << "the split of the notes is closed";
    t.c.previousTab();
    EXPECT_EQ(t.tabs().session(t.c.currentTab()), a) << "one step back: the notes";
    EXPECT_FALSE(t.ref().active());
}

TEST(ReferenceMode, poppingOutAReferenceBeforeTheNotesPutsItAfterThem) {
    ThreeTabs t;
    DocumentSession* a = t.tabs().session(0);
    DocumentSession* b = t.tabs().session(1);
    DocumentSession* c = t.tabs().session(2);
    t.c.setCurrentTab(2);
    t.ref().showTab(0);
    t.ref().popOut();
    EXPECT_EQ(t.tabs().indexOf(b), 0);
    EXPECT_EQ(t.tabs().indexOf(c), 1);
    EXPECT_EQ(t.tabs().indexOf(a), 2);
    EXPECT_EQ(t.c.currentTab(), 2);
    EXPECT_EQ(t.tabs().referenceOf(1), -1);
}

TEST(ReferenceMode, poppingOutAReferenceBesideTheNotesMovesNoTab) {
    for (const auto& [notes, reference]: {std::pair{0, 1}, std::pair{1, 0}}) {
        ThreeTabs t;
        t.c.setCurrentTab(notes);
        t.ref().showTab(reference);
        QSignalSpy moved(&t.tabs(), &QAbstractItemModel::rowsMoved);
        t.ref().popOut();
        EXPECT_EQ(moved.count(), 0) << notes << " " << reference;
        EXPECT_EQ(t.c.currentTab(), reference);
        EXPECT_EQ(t.tabs().referenceOf(notes), -1);
    }
    ThreeTabs t;
    t.ref().popOut();  // no reference: nothing happens
    EXPECT_EQ(t.c.currentTab(), 0);
}

TEST(ReferenceMode, theDividerAndTheSideAreSettingsOfTheApplication) {
    ThreeTabs t;
    EXPECT_DOUBLE_EQ(t.ref().ratio(), 0.5);
    EXPECT_TRUE(t.ref().onLeft()) << "right-handed: the reference on the left, the notes near the hand";
    QSignalSpy layout(&t.ref(), &ReferenceMode::layoutChanged);
    t.ref().setRatio(0.65);
    t.ref().swapSides();
    EXPECT_EQ(layout.count(), 2);
    EXPECT_DOUBLE_EQ(t.ref().ratio(), 0.65);
    EXPECT_FALSE(t.ref().onLeft());
    t.ref().setRatio(0.01);
    EXPECT_DOUBLE_EQ(t.ref().ratio(), ReferenceMode::MIN_RATIO) << "neither side disappears";
    t.ref().setRatio(2);
    EXPECT_DOUBLE_EQ(t.ref().ratio(), ReferenceMode::MAX_RATIO);
    // The same for another tab
    t.ref().showTab(1);
    t.c.setCurrentTab(2);
    t.ref().showTab(0);
    EXPECT_DOUBLE_EQ(t.ref().ratio(), ReferenceMode::MAX_RATIO);
    t.ref().setRatio(0.5);
    t.ref().setOnLeft(true);
}

TEST(ReferenceMode, openAsReferenceOpensTheFileBesideTheCurrentDocument) {
    AppController c;
    c.newDocument();  // untouched: it stays, for the notes
    DocumentSession* notes = c.tabManager().currentSession();
    ASSERT_TRUE(c.openAsReference(fixture(u8"test1.xoj")));
    EXPECT_EQ(c.tabCount(), 2) << "the new document was replaced";
    EXPECT_EQ(c.tabManager().currentSession(), notes);
    ASSERT_TRUE(c.reference().active());
    EXPECT_EQ(c.reference().title(), "test1.xoj");
    EXPECT_FALSE(c.homeVisible());

    // A file that is open already: its tab
    notes->insertNewPage(1);  // (written in: no longer replaced by a file opened)
    ASSERT_FALSE(c.tabManager().isPristine(c.currentTab()));
    ASSERT_TRUE(c.openPath(fixture(u8"load/strokes.xopp")));
    const int strokes = c.currentTab();
    c.setCurrentTab(c.tabManager().indexOf(notes));
    ASSERT_TRUE(c.openAsReference(fixture(u8"load/strokes.xopp")));
    EXPECT_EQ(c.tabCount(), 3);
    EXPECT_EQ(c.reference().tab(), strokes);
    EXPECT_EQ(c.tabManager().currentSession(), notes);

    // Nothing open: it is opened as the document
    AppController empty;
    ASSERT_TRUE(empty.openAsReference(fixture(u8"test1.xoj")));
    EXPECT_EQ(empty.tabCount(), 1);
    EXPECT_FALSE(empty.reference().active());
}

TEST(ReferenceMode, keysActOnTheReferenceWhileItHasTheFocus) {
    ThreeTabs t;
    t.ref().showTab(2);
    auto& mainVc = t.tabs().view(0)->getViewController();
    auto& refVc = t.tabs().view(2)->getViewController();
    mainVc.setViewSize(QSizeF(500, 600));
    refVc.setViewSize(QSizeF(500, 600));
    const double mainZoom = mainVc.zoom(), refZoom = refVc.zoom();
    t.c.zoomIn();
    EXPECT_GT(mainVc.zoom(), mainZoom);
    EXPECT_DOUBLE_EQ(refVc.zoom(), refZoom);
    t.ref().setFocused(true);
    t.c.zoomIn();
    EXPECT_GT(refVc.zoom(), refZoom) << "the focused reference did not zoom";
    t.c.fitWidth();
    EXPECT_NEAR(refVc.zoom(), refZoom, 1e-9);
    // Without a reference nothing has the focus there
    t.ref().close();
    EXPECT_FALSE(t.ref().focused());
}

namespace {
/// A stroke on the first page, as one undo step
void scribble(DocumentSession& s) {
    auto page = s.getDocument()->getPage(0);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->addPoint(Point(10, 10, 1));
    stroke->addPoint(Point(50, 40, 1));
    const Stroke* raw = stroke.get();
    Layer* layer = page->getSelectedLayer();
    layer->addElement(std::move(stroke));
    s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
}
size_t elements(DocumentSession& s) { return s.getDocument()->getPage(0)->getSelectedLayer()->getElements().size(); }
}  // namespace

TEST(ReferenceMode, writingInTheReferenceIsSwitchedOnPerReference) {
    ThreeTabs t;
    t.ref().showTab(2);
    EXPECT_FALSE(t.ref().editing()) << "for reading only at first";
    QSignalSpy changed(&t.ref(), &ReferenceMode::changed);
    t.ref().setEditing(true);
    EXPECT_TRUE(t.ref().editing());
    EXPECT_GE(changed.count(), 1);
    // Another tab, another reference: its own switch
    t.c.setCurrentTab(1);
    t.ref().showTab(2);
    EXPECT_FALSE(t.ref().editing());
    t.c.setCurrentTab(0);
    EXPECT_TRUE(t.ref().editing()) << "each tab remembers it for its reference";
    // Another reference: for reading again
    t.ref().showTab(1);
    EXPECT_FALSE(t.ref().editing());
    t.ref().setEditing(true);
    t.ref().swapRoles();
    EXPECT_FALSE(t.ref().editing()) << "the notes that became the reference are for reading at first";
    t.ref().setEditing(true);
    t.ref().close();
    EXPECT_FALSE(t.ref().editing());
}

TEST(ReferenceMode, undoActsOnTheReferenceBeingWrittenInWhileItHasTheFocus) {
    ThreeTabs t;
    t.ref().showTab(2);
    DocumentSession& notes = *t.tabs().session(0);
    DocumentSession& book = *t.tabs().session(2);
    scribble(notes);
    scribble(book);
    t.ref().setFocused(true);
    t.c.undo();  // for reading only: undo stays with the notes
    EXPECT_EQ(elements(notes), 0u);
    EXPECT_EQ(elements(book), 1u);
    t.c.redo();
    EXPECT_EQ(elements(notes), 1u);

    t.ref().setEditing(true);
    t.c.undo();
    EXPECT_EQ(elements(book), 0u) << "undo did not act on the reference being written in";
    EXPECT_EQ(elements(notes), 1u);
    t.c.redo();
    EXPECT_EQ(elements(book), 1u);
    t.ref().setFocused(false);  // a tap on the notes
    t.c.undo();
    EXPECT_EQ(elements(notes), 0u);
    EXPECT_EQ(elements(book), 1u);
}

TEST(ReferenceMode, theGridListsThePagesOfTheReferenceWhileItIsShown) {
    ThreeTabs t;
    for (int i = 0; i < 4; ++i) {
        t.tabs().session(2)->insertNewPage(1);
    }
    auto* pages = qobject_cast<QAbstractItemModel*>(t.ref().pagesModel());
    ASSERT_NE(pages, nullptr);
    t.ref().setPagesShown(true);
    EXPECT_FALSE(t.ref().pagesShown()) << "no reference: no grid";
    t.ref().showTab(2);
    t.ref().setPagesShown(true);
    EXPECT_TRUE(t.ref().pagesShown());
    EXPECT_EQ(pages->rowCount(), 5);
    auto* mainPages = qobject_cast<QAbstractItemModel*>(t.c.pagesModel());
    EXPECT_EQ(mainPages->rowCount(), 1) << "the page sidebar keeps the notes' pages";
    t.ref().goToPage(3);
    EXPECT_EQ(t.ref().pageNumber(), 4);
    t.ref().setPagesShown(false);
    EXPECT_EQ(pages->rowCount(), 0) << "it follows nothing while it is not shown";
    // Closing the reference closes its grid
    t.ref().setPagesShown(true);
    t.ref().close();
    EXPECT_FALSE(t.ref().pagesShown());
}

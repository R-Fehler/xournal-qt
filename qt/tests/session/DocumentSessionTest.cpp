/*
 * xournal-qt: headless tests of the per-tab DocumentSession (the Qt implementation of the shadow `Control`).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "config-test.h"

using namespace xqt;

namespace {
class DocumentSessionTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
    }
    fs::path tmpPath(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }

    /// Add a stroke through the upstream undo machinery, like the stroke tool does.
    static const Stroke* addStroke(DocumentSession& s, size_t pageNo) {
        auto page = s.getDocument()->getPage(pageNo);
        auto stroke = std::make_unique<Stroke>();
        stroke->setWidth(1.41);
        stroke->addPoint(Point(50, 50, 1.0));
        stroke->addPoint(Point(90, 70, 0.8));
        const Stroke* raw = stroke.get();
        Layer* layer = page->getSelectedLayer();
        s.getDocument()->lock();
        layer->addElement(std::move(stroke));
        s.getDocument()->unlock();
        s.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
        return raw;
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(DocumentSessionTest, newDocumentHasOneTemplatePage) {
    DocumentSession session(*app);
    EXPECT_EQ(session.getDocument()->getPageCount(), 1u);
    EXPECT_FALSE(session.isModified());
    EXPECT_FALSE(session.hasFilePath());
    const auto& tpl = app->getSettings()->getPageTemplateSettings();
    EXPECT_DOUBLE_EQ(session.getDocument()->getPage(0)->getWidth(), tpl.getPageWidth());
}

TEST_F(DocumentSessionTest, insertNewPageIsUndoableAndScrolls) {
    DocumentSession session(*app);
    QSignalSpy scrolls(&session, &DocumentSession::scrollToPageRequested);
    session.insertNewPage(1);
    EXPECT_EQ(session.getDocument()->getPageCount(), 2u);
    EXPECT_EQ(session.getCurrentPageNo(), 1u);
    EXPECT_EQ(scrolls.count(), 1);
    EXPECT_TRUE(session.isModified());
    EXPECT_TRUE(session.getActions().isActionEnabled(Action::DELETE_PAGE));

    session.getUndoRedoHandler()->undo();  // upstream InsertDeletePageUndoAction through the shadow Control
    EXPECT_EQ(session.getDocument()->getPageCount(), 1u);
    EXPECT_FALSE(session.isModified());
    session.getUndoRedoHandler()->redo();
    EXPECT_EQ(session.getDocument()->getPageCount(), 2u);
}

TEST_F(DocumentSessionTest, saveAsAndReload) {
    const fs::path target = tmpPath("note.xopp");
    {
        DocumentSession session(*app);
        QSignalSpy modified(&session, &DocumentSession::modifiedChanged);
        addStroke(session, 0);
        EXPECT_TRUE(session.isModified());
        EXPECT_EQ(modified.count(), 1);
        auto result = session.saveAs(target);
        ASSERT_TRUE(result.ok) << result.error;
        EXPECT_FALSE(session.isModified());
        EXPECT_EQ(session.getFilePath(), target);
        EXPECT_EQ(session.getDisplayName(), "note.xopp");
        // Second save creates and removes a backup, like upstream.
        addStroke(session, 0);
        ASSERT_TRUE(session.save().ok);
        EXPECT_FALSE(fs::exists(fs::path(target) += "~"));
    }
    auto loaded = DocumentSession::loadFile(target);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.warnings.empty());
    DocumentSession reopened(*app, std::move(loaded.document));
    auto page = reopened.getDocument()->getPage(0);
    EXPECT_EQ(page->getSelectedLayer()->getElementsView().size(), 2u);
    EXPECT_FALSE(reopened.isModified());
}

TEST_F(DocumentSessionTest, loadFixtureAndEditThroughUndo) {
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_TRUE(loaded.missingPdf.empty());
    DocumentSession session(*app, std::move(loaded.document));
    const auto pages = session.getDocument()->getPageCount();
    ASSERT_GE(pages, 2u);
    EXPECT_EQ(session.getDocument()->getPdfPageCount(), pages);

    session.setCurrentPageNo(1);
    addStroke(session, 1);
    session.getUndoRedoHandler()->undo();
    EXPECT_EQ(session.getDocument()->getPage(1)->getSelectedLayer()->getElementsView().size(), 0u);
}

TEST_F(DocumentSessionTest, annotatePdf) {
    auto loaded = DocumentSession::loadFile(GET_TESTFILE(u8"cjk/测试.pdf"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_GE(session.getDocument()->getPageCount(), 1u);
    EXPECT_TRUE(session.getDocument()->getPage(0)->getBackgroundType().isPdfPage());
    EXPECT_FALSE(session.hasFilePath());
    EXPECT_EQ(session.getDisplayName(), "测试.pdf");
}

TEST_F(DocumentSessionTest, layerControllerWorksThroughSession) {
    DocumentSession session(*app);
    LayerController* layers = session.getLayerController();
    ASSERT_NE(layers, nullptr);
    const auto before = layers->getLayerCount();
    layers->addNewLayer(false);  // upstream LayerController, unmodified
    EXPECT_EQ(layers->getLayerCount(), before + 1);
    session.getUndoRedoHandler()->undo();
    EXPECT_EQ(layers->getLayerCount(), before);
}

TEST_F(DocumentSessionTest, autosaveNextToDocument) {
    DocumentSession session(*app);
    ASSERT_TRUE(session.saveAs(tmpPath("draft.xopp")).ok);
    addStroke(session, 0);
    auto result = session.autosave();
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(session.getLastAutosaveFile(), tmpPath(".draft.autosave.xopp"));
    EXPECT_TRUE(fs::exists(session.getLastAutosaveFile()));
    session.deleteAutosaveFile();
    EXPECT_FALSE(fs::exists(tmpPath(".draft.autosave.xopp")));
}

TEST_F(DocumentSessionTest, loadErrorsAreReported) {
    auto missing = DocumentSession::loadFile(tmpPath("does-not-exist.xopp"));
    EXPECT_FALSE(missing.document);
    EXPECT_FALSE(missing.error.empty());
}

TEST_F(DocumentSessionTest, saveSuggestionForAnnotatedPdfIsNextToThePdf) {
    // Copy a PDF to a folder of its own, annotate it: "Save as" suggests lecture.xopp next to lecture.pdf.
    const fs::path pdf = tmpPath("lecture.pdf");
    fs::copy_file(fs::path(GET_TESTFILE(u8"cjk/测试.pdf")), pdf);
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession session(*app, std::move(loaded.document));
    EXPECT_EQ(session.suggestSavePath(), tmpPath("lecture.xopp"));

    // Once saved, the suggestion is the document's own path.
    ASSERT_TRUE(session.saveAs(tmpPath("renamed.xopp")).ok);
    EXPECT_EQ(session.suggestSavePath(), tmpPath("renamed.xopp"));
}

TEST_F(DocumentSessionTest, saveSuggestionForNewDocumentUsesTheLastSaveFolder) {
    app->getSettings()->setLastSavePath(fs::path(tmp.path().toStdString()));
    DocumentSession session(*app);
    const fs::path suggestion = session.suggestSavePath();
    EXPECT_EQ(suggestion.parent_path(), fs::path(tmp.path().toStdString()));
    EXPECT_EQ(suggestion.extension(), ".xopp");
}

TEST_F(DocumentSessionTest, pageOperationsAreUndoable) {
    DocumentSession session(*app);
    Document* doc = session.getDocument();
    addStroke(session, 0);
    session.insertNewPage(1);
    session.insertNewPage(2);
    ASSERT_EQ(doc->getPageCount(), 3u);
    const PageRef p0 = doc->getPage(0), p1 = doc->getPage(1), p2 = doc->getPage(2);

    // Duplicate: a copy (not the same page) after the current page, with its content.
    session.setCurrentPageNo(0);
    session.duplicatePage();
    ASSERT_EQ(doc->getPageCount(), 4u);
    EXPECT_NE(doc->getPage(1), p0);
    EXPECT_EQ(doc->getPage(1)->getSelectedLayer()->getElements().size(), 1u);
    EXPECT_EQ(session.getCurrentPageNo(), 1u);
    session.getUndoRedoHandler()->undo();
    ASSERT_EQ(doc->getPageCount(), 3u);
    EXPECT_EQ(doc->getPage(1), p1);

    // Move: the current page swaps with its neighbour and stays current.
    session.setCurrentPageNo(0);
    session.movePageTowardsEnd();
    EXPECT_EQ(doc->getPage(0), p1);
    EXPECT_EQ(doc->getPage(1), p0);
    EXPECT_EQ(session.getCurrentPageNo(), 1u);
    session.movePageTowardsBeginning();
    EXPECT_EQ(doc->getPage(0), p0);
    session.getUndoRedoHandler()->undo();
    session.getUndoRedoHandler()->undo();
    EXPECT_EQ(doc->getPage(0), p0);
    EXPECT_EQ(doc->getPage(1), p1);
    session.movePageTowardsBeginning();  // already first: nothing
    EXPECT_EQ(doc->getPage(0), p0);

    // Delete the last page: the page before becomes current; undo puts the same page back.
    session.setCurrentPageNo(2);
    session.deletePage();
    ASSERT_EQ(doc->getPageCount(), 2u);
    EXPECT_EQ(session.getCurrentPageNo(), 1u);
    session.getUndoRedoHandler()->undo();
    ASSERT_EQ(doc->getPageCount(), 3u);
    EXPECT_EQ(doc->getPage(2), p2);
}

TEST_F(DocumentSessionTest, theLastPageIsNotDeleted) {
    DocumentSession session(*app);
    session.deletePage();
    EXPECT_EQ(session.getDocument()->getPageCount(), 1u);
    EXPECT_FALSE(session.getUndoRedoHandler()->canUndo());
}

TEST_F(DocumentSessionTest, undoAndRedoReportTheChangedPage) {
    DocumentSession session(*app);
    session.insertNewPage(1);
    addStroke(session, 1);
    QSignalSpy changed(&session, &DocumentSession::pageContentChanged);
    session.getUndoRedoHandler()->undo();
    ASSERT_GE(changed.count(), 1);
    EXPECT_EQ(changed.last().at(0).toULongLong(), 1u);
    changed.clear();
    session.getUndoRedoHandler()->redo();
    ASSERT_GE(changed.count(), 1);
    EXPECT_EQ(changed.last().at(0).toULongLong(), 1u);
}

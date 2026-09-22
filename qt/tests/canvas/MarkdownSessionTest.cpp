/*
 * xournal-qt: editing the Markdown box of a page: the layer, one undo step, cancel, saving and loading.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"
#include "model/MarkdownText.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "TextFlow.h"

using namespace xqt;

namespace {
class MarkdownSessionTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
    }
    PageRef page() const { return session->getDocument()->getPage(0); }
    std::string source() const {
        Layer* layer = md::markdownLayer(page());
        const Text* box = layer ? md::boxOf(*layer) : nullptr;
        return box ? box->getText() : std::string();
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    md::Style style;
};
}  // namespace

TEST_F(MarkdownSessionTest, boxAtTheBottomAndOneUndoStep) {
    Layer* before = page()->getSelectedLayer();
    MarkdownSession edit(*session);
    EXPECT_TRUE(edit.begin(0, style).empty());
    Layer* layer = md::markdownLayer(page());
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(page()->getLayers().front(), layer) << "at the bottom: ink goes on top";
    EXPECT_EQ(page()->getSelectedLayer(), before) << "the pen still writes into the same layer";

    edit.update("# Title");
    edit.update("# Title\n\nSome *text*.");
    edit.finish();
    const Text* box = md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), "# Title\n\nSome *text*.");
    // From the top-left margin (beside the margin line of the default lined page) to the right margin
    const auto margins = TextFlow::styleFor(page(), TextFlow::Style{});
    EXPECT_GT(margins.leftMargin, TextFlow::MARGIN);
    EXPECT_DOUBLE_EQ(box->getTransformation().shift.x, margins.leftMargin);
    EXPECT_DOUBLE_EQ(box->getTransformation().shift.y, TextFlow::MARGIN);
    EXPECT_NEAR(box->getWrap(), page()->getWidth() - margins.leftMargin - margins.rightMargin, 1e-9);

    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(source(), "") << "one step for the whole edit";
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(source(), "# Title\n\nSome *text*.");

    // Again: the source is edited where it is; Cancel puts it back
    EXPECT_EQ(edit.begin(0, style), "# Title\n\nSome *text*.");
    edit.update("changed");
    EXPECT_EQ(source(), "changed");
    edit.cancel();
    EXPECT_EQ(source(), "# Title\n\nSome *text*.");
}

TEST_F(MarkdownSessionTest, nothingWrittenLeavesNoLayer) {
    const auto layers = page()->getLayerCount();
    MarkdownSession edit(*session);
    edit.begin(0, style);
    EXPECT_EQ(page()->getLayerCount(), layers + 1);
    edit.finish();
    EXPECT_EQ(page()->getLayerCount(), layers);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
}

TEST_F(MarkdownSessionTest, overflowIsReported) {
    MarkdownSession edit(*session);
    edit.begin(0, style);
    EXPECT_EQ(edit.update("# Short\n\ntext"), 0);
    std::string many;
    for (int i = 0; i < 80; ++i) {
        many += "Paragraph " + std::to_string(i) + "\n\n";
    }
    EXPECT_GT(edit.update(many), 100);
    edit.finish();
}

TEST_F(MarkdownSessionTest, savedAsAnOrdinaryTextAndLoaded) {
    const std::string src = "# Notes\n\n- one\n- **two**\n\n```cpp\nint x = 1;\n```\n";
    {
        MarkdownSession edit(*session);
        edit.begin(0, style);
        edit.update(src);
        edit.finish();
    }
    const fs::path file = fs::path(tmp.filePath("notes.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(file).ok);
    // Upstream's loader: an ordinary text element (the source) in a layer "Markdown"
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const PageRef p = loaded.document->getPage(0);
    Layer* layer = md::markdownLayer(p);
    ASSERT_NE(layer, nullptr);
    const Text* box = md::boxOf(*layer);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getText(), src);
    const auto margins = TextFlow::styleFor(p, TextFlow::Style{});
    EXPECT_NEAR(box->getWrap(), p->getWidth() - margins.leftMargin - margins.rightMargin, 0.01)
            << "the width is the wrap width";
    EXPECT_EQ(md::styleOf(*box).family, style.family);
}

TEST_F(MarkdownSessionTest, fontSizeIsTheTextsAndTheDrawingFollows) {
    md::Style small = style;
    small.size = 9;
    MarkdownSession edit(*session);
    edit.begin(0, small);
    edit.update("# Title\n\nSome text that is long enough to wrap at least once at this width, and a bit more.");
    const Text* box = md::boxOf(*md::markdownLayer(page()));
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->getFontSize(), 9) << "the source has the size the text is drawn at";
    const double before = md::contentHeight(*box);
    edit.setFontSize(14);
    EXPECT_EQ(edit.fontSize(), 14);
    EXPECT_EQ(box->getFontSize(), 14);
    EXPECT_GT(md::contentHeight(*box), before * 1.3);
    edit.finish();
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(source(), "") << "size and text: one step";
}

TEST_F(MarkdownSessionTest, textBoxesElsewhereAreNotThePagesText) {
    // A Markdown text box placed with the text tool (not at the margins)
    Layer* layer = new Layer();
    layer->setName("Markdown");
    auto free = std::make_unique<Text>();
    free->setText("a **box** elsewhere");
    free->setWrap(200);
    free->setTransformation(xoj::util::Matrix::TRANSLATION(300, 400));
    const Text* freeBox = free.get();
    layer->addElement(std::move(free));
    session->getDocument()->lock();
    page()->getLayers().insert(page()->getLayers().begin(), layer);
    session->getDocument()->unlock();

    MarkdownSession edit(*session);
    EXPECT_EQ(edit.begin(0, style), "") << "the page has no Markdown text of its own yet";
    edit.update("# Page text");
    edit.finish();
    EXPECT_EQ(freeBox->getText(), "a **box** elsewhere");
    EXPECT_EQ(layer->getElements().size(), 2u);
    EXPECT_EQ(edit.begin(0, style), "# Page text");
    edit.cancel();
    session->getUndoRedoHandler()->undo();
    ASSERT_EQ(layer->getElements().size(), 1u);
    EXPECT_EQ(layer->getElements().front().get(), freeBox) << "undo took only the page's text away";
    EXPECT_EQ(md::boxAt(*layer, 310, 405), freeBox);
}

/*
 * xournal-qt: the text mode (TextFlow): blocks laid out as Xournal++ text elements and read back, also through a
 * saved file; editing with one undo step.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"

#include "TextFlow.h"

using namespace xqt;
using Kind = TextBlock::Kind;

namespace {
TextBlock block(Kind kind, const char* text, int indent = 0) {
    TextBlock b;
    b.kind = kind;
    b.text = QString::fromUtf8(text);
    b.indent = indent;
    return b;
}

std::vector<TextBlock> sample() {
    std::vector<TextBlock> blocks{
            block(Kind::Heading1, "Lecture notes"),
            block(Kind::Paragraph, "A paragraph long enough to be wrapped at the right margin of the page, because it "
                                   "goes on and on and on, much longer than one line of an A4 page could ever be."),
            block(Kind::Heading2, "Points"),
            block(Kind::Bullet, "first point"),
            block(Kind::Bullet, "a sub point", 1),
            block(Kind::Bullet, "second point"),
            block(Kind::Numbered, "step one"),
            block(Kind::Numbered, "step two"),
            block(Kind::Paragraph, ""),
            block(Kind::Heading3, "Details"),
    };
    TextBlock styled = block(Kind::Paragraph, "bold, italic, bigger and red");
    styled.bold = true;
    styled.italic = true;
    styled.size = 16;
    styled.color = Color(255, 0, 0);
    blocks.push_back(styled);
    return blocks;
}

class TextFlowTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
    }
    PageRef page() const { return session->getDocument()->getPage(0); }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
    TextFlow::Style style;
};
}  // namespace

TEST_F(TextFlowTest, blocksAreLaidOutAndReadBack) {
    double overflow = -1;
    auto elements = TextFlow::layout(sample(), 595.28, 841.89, style, &overflow);
    EXPECT_EQ(overflow, 0) << "fits on the page";
    // One element per block, two per list item (marker and text), none for the empty paragraph
    EXPECT_EQ(elements.size(), 5u + 5 * 2);
    for (const auto& e: elements) {
        const auto& box = e->getBoundingBox();
        EXPECT_GE(box.x, TextFlow::MARGIN - 0.01);
        EXPECT_LE(box.x + box.width, 595.28 - TextFlow::MARGIN + 1) << "wrapped at the right margin";
    }
    const auto* para = dynamic_cast<const Text*>(elements[1].get());
    ASSERT_NE(para, nullptr);
    EXPECT_GT(para->getBoundingBox().height, 20) << "several lines";
    // Line breaks instead of a wrap width: the same in all Xournal++ versions
    EXPECT_EQ(para->getWrap(), Text::NO_WRAP);
    EXPECT_NE(para->getText().find('\n'), std::string::npos);
    EXPECT_NE(para->getText().find(" \n"), std::string::npos) << "the space stays at the end of the line";

    TextFlowSession flow(*session);
    ASSERT_TRUE(flow.begin(0, style).empty());
    flow.update(sample());
    EXPECT_EQ(TextFlow::read(page(), style), sample());
    flow.finish();
}

TEST_F(TextFlowTest, textLayerAtTheBottomAndOneUndoStep) {
    Layer* before = page()->getSelectedLayer();
    TextFlowSession flow(*session);
    flow.begin(0, style);
    Layer* layer = TextFlow::textLayer(page());
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(page()->getLayers().front(), layer) << "at the bottom: ink goes on top";
    EXPECT_EQ(page()->getSelectedLayer(), before) << "the pen still writes into the same layer";

    flow.update({block(Kind::Paragraph, "one")});
    flow.update({block(Kind::Paragraph, "one two")});
    flow.finish();
    EXPECT_TRUE(session->getUndoRedoHandler()->canUndo());
    session->getUndoRedoHandler()->undo();
    EXPECT_TRUE(TextFlow::read(page(), style).empty()) << "one step for the whole edit";
    session->getUndoRedoHandler()->redo();
    ASSERT_EQ(TextFlow::read(page(), style).size(), 1u);
    EXPECT_EQ(TextFlow::read(page(), style)[0].text, "one two");

    // Cancel: as before
    flow.begin(0, style);
    flow.update({block(Kind::Heading1, "not kept")});
    flow.cancel();
    EXPECT_EQ(TextFlow::read(page(), style)[0].text, "one two");
}

TEST_F(TextFlowTest, nothingWrittenLeavesNoLayer) {
    const auto layers = page()->getLayerCount();
    TextFlowSession flow(*session);
    flow.begin(0, style);
    EXPECT_EQ(page()->getLayerCount(), layers + 1);
    flow.finish();
    EXPECT_EQ(page()->getLayerCount(), layers);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
}

TEST_F(TextFlowTest, savedAsXournalFileAndReadBack) {
    {
        TextFlowSession flow(*session);
        flow.begin(0, style);
        flow.update(sample());
        flow.finish();
    }
    const fs::path file = fs::path(tmp.filePath("notes.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(file).ok);
    // Upstream's loader: ordinary text elements in a layer "Text"
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const PageRef p = loaded.document->getPage(0);
    Layer* layer = TextFlow::textLayer(p);
    ASSERT_NE(layer, nullptr);
    EXPECT_EQ(p->getLayers().front(), layer);
    size_t texts = 0;
    for (const auto& e: layer->getElementsView()) {
        texts += e->getType() == ELEMENT_TEXT;
    }
    EXPECT_EQ(texts, 15u);
    EXPECT_EQ(TextFlow::read(p, style), sample()) << "the same blocks after saving and loading";
}

TEST_F(TextFlowTest, textStartsBesideTheMarginLineOfRuledPages) {
    PageType lined(PageTypeFormat::Lined);
    page()->setBackgroundType(lined);
    EXPECT_GT(TextFlow::styleFor(page(), style).leftMargin, 72) << "right of the margin line";
    lined.config = "m1=-72";
    page()->setBackgroundType(lined);
    const auto right = TextFlow::styleFor(page(), style);
    EXPECT_DOUBLE_EQ(right.leftMargin, TextFlow::MARGIN);
    EXPECT_GT(right.rightMargin, 72);
    page()->setBackgroundType(PageType(PageTypeFormat::Graph));
    EXPECT_DOUBLE_EQ(TextFlow::styleFor(page(), style).leftMargin, TextFlow::MARGIN);
}

TEST_F(TextFlowTest, overflowIsReported) {
    std::vector<TextBlock> many(80, block(Kind::Paragraph, "line"));
    double overflow = 0;
    TextFlow::layout(many, 595.28, 841.89, style, &overflow);
    EXPECT_GT(overflow, 100);
}

// Opt-in: XQT_WRITE_TEXTFLOW=<file.xopp> writes the sample as a Xournal++ file (e.g. to open in Xournal++)
TEST_F(TextFlowTest, writeSampleFile) {
    const QString out = qEnvironmentVariable("XQT_WRITE_TEXTFLOW");
    if (out.isEmpty()) {
        GTEST_SKIP() << "set XQT_WRITE_TEXTFLOW";
    }
    TextFlowSession flow(*session);
    flow.begin(0, style);
    flow.update(sample());
    flow.finish();
    ASSERT_TRUE(session->saveAs(fs::path(out.toStdString())).ok);
}

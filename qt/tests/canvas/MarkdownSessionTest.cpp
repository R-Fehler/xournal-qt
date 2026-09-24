/*
 * xournal-qt: editing the Markdown box of a page: the layer, one undo step, cancel, saving and loading.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <set>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "../SearchHits.h"
#include "session/DocumentSession.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"
#include "model/MarkdownText.h"

#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPaginate.h"
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

TEST_F(MarkdownSessionTest, onlyABlockHigherThanAPageGoesBelowIt) {
    MarkdownSession edit(*session);
    edit.begin(0, style);
    EXPECT_EQ(edit.update("# Short\n\ntext"), 0);
    std::string many;
    for (int i = 0; i < 80; ++i) {
        many += "Paragraph " + std::to_string(i) + "\n\n";
    }
    EXPECT_EQ(edit.update(many), 0) << "it flows onto the next pages";
    std::string quote;  // one quote, higher than a page: it cannot be split
    for (int i = 0; i < 80; ++i) {
        quote += "> quoted paragraph " + std::to_string(i) + "\n>\n";
    }
    EXPECT_GT(edit.update(quote), 100);
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

namespace {
std::string longText(int paragraphs) {
    std::string s = "# Long text\n\n";
    for (int i = 0; i < paragraphs; ++i) {
        s += "Paragraph " + std::to_string(i) +
             ": enough words to fill a line or two of the page, so that the text needs more than one page.\n\n";
    }
    return s;
}
std::vector<std::string> pageTexts(Document& doc) {
    std::vector<std::string> texts;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const Layer* layer = md::markdownLayer(doc.getPage(i));
        const Text* box = layer ? md::boxOf(*layer) : nullptr;
        texts.push_back(box ? box->getText() : std::string());
    }
    return texts;
}
}  // namespace

TEST_F(MarkdownSessionTest, thePagesTextFlowsOntoNewPages) {
    Document& doc = *session->getDocument();
    ASSERT_EQ(doc.getPageCount(), 1u);
    const std::string text = longText(40);
    MarkdownSession edit(*session);
    edit.begin(0, style);
    EXPECT_EQ(edit.update(text), 0) << "nothing goes below a page";
    const size_t pages = doc.getPageCount();
    EXPECT_GE(pages, 3u);
    EXPECT_EQ(edit.pageIndex(), 0u);
    EXPECT_EQ(edit.lastPageIndex(), pages - 1);
    const auto texts = pageTexts(doc);
    EXPECT_EQ(md::join(texts), text) << "the pages hold the text";
    for (size_t i = 1; i < texts.size(); ++i) {
        EXPECT_TRUE(md::continues(texts[i])) << "page " << i;
        EXPECT_NE(doc.getPage(i)->getSelectedLayer(), md::markdownLayer(doc.getPage(i))) << "the pen's layer";
    }
    // Shorter again: the pages added go again
    edit.update(longText(2));
    EXPECT_EQ(doc.getPageCount(), 1u);
    edit.update(text);
    EXPECT_EQ(doc.getPageCount(), pages);
    edit.finish();

    // One undo step: text and pages
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(doc.getPageCount(), 1u);
    EXPECT_EQ(source(), "");
    session->getUndoRedoHandler()->redo();
    EXPECT_EQ(doc.getPageCount(), pages);
    EXPECT_EQ(md::join(pageTexts(doc)), text);

    // Opened on a later page: the whole text, from its first page
    EXPECT_EQ(edit.begin(2, style), text);
    EXPECT_EQ(edit.pageIndex(), 0u);
    EXPECT_EQ(edit.lastPageIndex(), pages - 1);
    edit.cancel();
}

TEST_F(MarkdownSessionTest, pagesThatOnlyHeldTheTextGoWhenItGetsShorter) {
    Document& doc = *session->getDocument();
    MarkdownSession edit(*session);
    edit.begin(0, style);
    edit.update(longText(40));
    edit.finish();
    const size_t pages = doc.getPageCount();
    ASSERT_GE(pages, 3u);

    // A stroke on the second page keeps that page
    auto stroke = std::make_unique<Stroke>();
    stroke->addPoint(Point(100, 500));
    stroke->addPoint(Point(200, 520));
    doc.getPage(1)->getSelectedLayer()->addElement(std::move(stroke));

    edit.begin(0, style);
    edit.update("# Short now\n");
    EXPECT_EQ(doc.getPageCount(), pages) << "pages from before stay while typing (emptied)";
    edit.finish();
    EXPECT_EQ(doc.getPageCount(), 2u) << "the empty ones at the end went; the one with ink stays";
    EXPECT_EQ(pageTexts(doc)[1], "");
    session->getUndoRedoHandler()->undo();
    EXPECT_EQ(doc.getPageCount(), pages);
    EXPECT_EQ(md::join(pageTexts(doc)), longText(40));
}

TEST_F(MarkdownSessionTest, cancelTakesAddedPagesAway) {
    Document& doc = *session->getDocument();
    MarkdownSession edit(*session);
    edit.begin(0, style);
    edit.update(longText(40));
    ASSERT_GT(doc.getPageCount(), 1u);
    edit.cancel();
    EXPECT_EQ(doc.getPageCount(), 1u);
    EXPECT_EQ(source(), "");
}

TEST_F(MarkdownSessionTest, flowingTextIsSavedAndLoaded) {
    const std::string text = longText(30) + "```cpp\n" + std::string(60 * 1, ' ') + "\n";  // (a code block at the end)
    MarkdownSession edit(*session);
    edit.begin(0, style);
    edit.update(text);
    edit.finish();
    const size_t pages = session->getDocument()->getPageCount();
    ASSERT_GE(pages, 2u);
    const fs::path file = fs::path(tmp.filePath("flow.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(file).ok);
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_EQ(loaded.document->getPageCount(), pages);
    DocumentSession other(*app, std::move(loaded.document));
    MarkdownSession again(other);
    EXPECT_EQ(again.begin(pages - 1, style), text) << "the same text, joined from the pages of the file";
    again.cancel();
}

namespace {
/// Where the characters [from, to) of a laid out text are drawn (box coordinates): one rectangle per line, from
/// Pango's extents of each character.
std::vector<QRectF> glyphRects(const md::Item& it, int from, int to) {
    std::vector<QRectF> out;
    PangoLayoutIter* iter = pango_layout_get_iter(it.layout.get());
    const PangoLayoutLine* line = nullptr;
    do {
        const int index = pango_layout_iter_get_index(iter);
        if (index < from || index >= to) {
            continue;
        }
        PangoRectangle r;
        pango_layout_iter_get_char_extents(iter, &r);
        const QRectF g(it.x + r.x / double(PANGO_SCALE), it.y + r.y / double(PANGO_SCALE), r.width / double(PANGO_SCALE),
                       r.height / double(PANGO_SCALE));
        if (pango_layout_iter_get_line_readonly(iter) != line || out.empty()) {
            out.push_back(g);
            line = pango_layout_iter_get_line_readonly(iter);
        } else {
            out.back() = out.back().united(g);
        }
    } while (pango_layout_iter_next_char(iter));
    pango_layout_iter_free(iter);
    return out;
}

/// Where a (lower case, ASCII) word is drawn on each page of a document: its Markdown boxes' layouts (page points).
std::vector<std::pair<size_t, QRectF>> drawnWords(Document& doc, const std::string& word) {
    std::vector<std::pair<size_t, QRectF>> out;
    for (size_t p = 0; p < doc.getPageCount(); ++p) {
        const Layer* layer = md::markdownLayer(doc.getPage(p));
        if (!layer) {
            continue;
        }
        for (const auto* e: layer->getElementsView()) {
            const auto* box = static_cast<const Text*>(e);
            const auto& at = box->getTransformation().shift;
            const md::Layout& laid = md::cachedLayout(box->getText(), md::styleOf(*box));
            for (const md::Item& it: laid.items) {
                if (it.kind != md::Item::Kind::Text) {
                    continue;
                }
                const std::string t = pango_layout_get_text(it.layout.get());
                for (size_t i = t.find(word); i != std::string::npos; i = t.find(word, i + 1)) {
                    for (const QRectF& r: glyphRects(it, int(i), int(i + word.size()))) {
                        out.emplace_back(p, r.translated(at.x, at.y));
                    }
                }
            }
        }
    }
    return out;
}

void searchFor(DocumentSession& s, const QString& text) {
    QSignalSpy finished(&s.search(), &DocumentSearch::finished);
    s.search().setQuery(text, false);
    if (s.search().isRunning()) {
        ASSERT_TRUE(finished.wait(5000));
    }
}

/// Every hit covers a drawn word and every drawn word has its hit.
void expectHitsOnDrawnWords(DocumentSession& s, const std::string& word, size_t pages) {
    const auto drawn = drawnWords(*s.getDocument(), word);
    searchFor(s, QString::fromStdString(word));
    const auto hits = xqt::test::placedHits(s.search());
    ASSERT_EQ(hits.size(), drawn.size());
    std::set<size_t> onPages;
    for (const auto& [page, rect]: drawn) {
        bool found = false;
        for (const auto& h: hits) {
            found = found || (h.page == page && std::abs(h.rect.x() - rect.x()) < 0.5 &&
                              std::abs(h.rect.y() - rect.y()) < 0.5 && std::abs(h.rect.width() - rect.width()) < 0.5 &&
                              std::abs(h.rect.height() - rect.height()) < 0.5);
        }
        EXPECT_TRUE(found) << "\"" << word << "\" drawn on page " << page << " at " << rect.x() << ", " << rect.y()
                           << " (" << rect.width() << " x " << rect.height() << ") has no hit there";
        onPages.insert(page);
    }
    EXPECT_EQ(onPages.size(), pages) << "on every page of the text";
}
}  // namespace

// The search marks a word where it is drawn in the page's Markdown text: in a heading (bigger), in emphasis, in a
// list, in code, and on the page the text flows onto; also after saving and loading.
TEST_F(MarkdownSessionTest, searchHitsAreOnTheDrawnWords) {
    std::string src = "# A needle in the heading\n\nSome *emphasised needle* and `needle` code.\n\n"
                      "- one\n- a needle in a list\n\n```py\nx = 1  # needle in code\n```\n\n";
    for (int i = 0; i < 60; ++i) {
        src += "Filler paragraph " + std::to_string(i) + " with a few words to fill the page.\n\n";
    }
    src += "## The last needle\n\nAnd one more needle on the last page.\n";
    {
        MarkdownSession edit(*session);
        edit.begin(0, style);
        edit.update(src);
        edit.finish();
    }
    ASSERT_GE(session->getDocument()->getPageCount(), 2u) << "the text flows onto a second page";
    expectHitsOnDrawnWords(*session, "needle", 2);

    const fs::path file = fs::path(tmp.filePath("needles.xopp").toStdString());
    ASSERT_TRUE(session->saveAs(file).ok);
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document) << loaded.error;
    DocumentSession again(*app, std::move(loaded.document));
    expectHitsOnDrawnWords(again, "needle", 2);
}

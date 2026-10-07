/*
 * xournal-qt: a page template's file (qt/docs/features/templates.md): a .xopp of one page; with its background as it is
 * (a PDF page as upstream's attached PDF of one page next to it), without it plain paper marked as such; with or
 * without its content; opened by upstream's loader without a warning.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <memory>
#include <string>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"
#include "session/StickyNote.h"
#include "session/TemplateFile.h"
#include "support/TestSupport.h"

using xqt::test::makeTextPdf;

using namespace xqt;

namespace {

ElementPtr stroke() {
    auto s = std::make_unique<Stroke>();
    s->setToolType(StrokeTool::PEN);
    s->setColor(Color(0x10, 0x20, 0xc0));
    s->setWidth(2);
    s->addPoint(Point(100, 200));
    s->addPoint(Point(180, 260));
    return s;
}

/// A page of ruled paper (or of `pdfPage` of the document's PDF) with a stroke, a Markdown box and a sticky note
PageRef samplePage(size_t pdfPage = npos) {
    auto page = std::make_shared<XojPage>(595, 842);
    if (pdfPage != npos) {
        page->setBackgroundType(PageType(PageTypeFormat::Pdf));
        page->setBackgroundPdfPageNr(pdfPage);
    } else {
        page->setBackgroundType(PageType(PageTypeFormat::Ruled));
        page->setBackgroundColor(Color(0xff, 0xf8, 0xe0));
    }
    page->getLayers()[0]->addElement(stroke());
    auto* md = new Layer();
    md->setName(std::string(xoj::markdown::LAYER_NAME));
    auto t = std::make_unique<Text>();
    t->setText("# Week plan");
    t->setFont(XojFont("Sans", 12));
    t->setWrap(200);
    t->setMarkdown(true);
    md->addElement(std::move(t));
    page->getLayers().insert(page->getLayers().begin(), md);
    page->getLayers().push_back(sticky::makeNote({{300, 300, 80, 60}, sticky::presetColors()[0], false}));
    page->setBookmark(std::string("Mine"));
    return page;
}

size_t elementCount(const XojPage& page) {
    size_t n = 0;
    for (const Layer* l: page.getLayersView()) {
        n += l->getElementsView().size();
    }
    return n;
}

class TemplateFileTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path file(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
};

}  // namespace

TEST_F(TemplateFileTest, withItsBackgroundAndContentTheTemplateIsThePage) {
    const fs::path target = file("Week.xopp");
    ASSERT_TRUE(templates::write(templates::makePage(samplePage(), {}), {}, target));
    EXPECT_FALSE(fs::exists(templates::attachedPdfOf(target))) << "no PDF page: no PDF";

    std::vector<std::string> warnings;
    LoadHandler handler(&warnings);
    auto loaded = handler.loadDocument(target);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    ASSERT_EQ(loaded->getPageCount(), 1u);
    const PageRef page = loaded->getPage(0);
    EXPECT_EQ(page->getBackgroundType(), PageType(PageTypeFormat::Ruled));
    EXPECT_EQ(page->getBackgroundColor(), Color(0xff, 0xf8, 0xe0));
    EXPECT_FALSE(templates::withoutBackground(*page));
    EXPECT_EQ(page->getLayerCount(), 3u) << "Markdown, the ink, the note";
    EXPECT_EQ(elementCount(*page), 3u);
    EXPECT_FALSE(page->getBookmark()) << "a copy starts without the bookmark";
}

TEST_F(TemplateFileTest, withoutItsBackgroundPlainPaperMarkedAsSuch) {
    const fs::path target = file("Header.xopp");
    ASSERT_TRUE(templates::write(templates::makePage(samplePage(), {false, true}), {}, target));
    auto loaded = DocumentSession::loadFile(target);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const PageRef page = loaded.document->getPage(0);
    EXPECT_EQ(page->getBackgroundType(), PageType(PageTypeFormat::Plain));
    EXPECT_EQ(page->getBackgroundColor(), Colors::white);
    EXPECT_TRUE(templates::withoutBackground(*page)) << "its background's name says so (saved)";
    EXPECT_EQ(elementCount(*page), 3u);
    EXPECT_DOUBLE_EQ(page->getWidth(), 595);
}

TEST_F(TemplateFileTest, withoutItsContentTheLayersStayEmpty) {
    const fs::path target = file("Paper.xopp");
    ASSERT_TRUE(templates::write(templates::makePage(samplePage(), {true, false}), {}, target));
    auto loaded = DocumentSession::loadFile(target);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const PageRef page = loaded.document->getPage(0);
    EXPECT_EQ(page->getBackgroundType(), PageType(PageTypeFormat::Ruled));
    EXPECT_EQ(elementCount(*page), 0u);
    EXPECT_EQ(page->getLayerCount(), 1u) << "no note, no Markdown layer";
}

TEST_F(TemplateFileTest, aPdfPageGoesAlongAsTheAttachedPdfOfOnePage) {
    const fs::path pdf = file("lecture.pdf");
    makeTextPdf(pdf, {"lectureone", "lecturetwo", "lecturethree"});
    std::string onePage;
    ASSERT_TRUE(MergedPdf::extract(pdf, {1}, onePage).ok);

    const fs::path target = file("Slide.xopp");
    ASSERT_TRUE(templates::write(templates::makePage(samplePage(1), {}), onePage, target));
    const fs::path attached = templates::attachedPdfOf(target);
    EXPECT_EQ(attached, file("Slide.xopp.bg.pdf"));
    ASSERT_TRUE(fs::exists(attached));

    // Upstream's loader: the attached PDF, its one page shown on the page
    std::vector<std::string> warnings;
    LoadHandler handler(&warnings);
    auto loaded = handler.loadDocument(target);
    ASSERT_TRUE(loaded);
    EXPECT_TRUE(warnings.empty()) << warnings.front();
    EXPECT_FALSE(handler.isAttachedPdfMissing());
    EXPECT_EQ(loaded->getPdfFilepath(), attached);
    EXPECT_TRUE(loaded->isAttachPdf());
    EXPECT_EQ(loaded->getPdfPageCount(), 1u);
    ASSERT_TRUE(loaded->getPage(0)->getBackgroundType().isPdfPage());
    EXPECT_EQ(loaded->getPage(0)->getPdfPageNr(), 0u);
    EXPECT_FALSE(DocumentSearch::findOnPage(*loaded, 0, "lecturetwo").empty()) << "its text, searchable";
    EXPECT_EQ(elementCount(*loaded->getPage(0)), 3u);
}

TEST_F(TemplateFileTest, aPdfPageWithoutItsPdfIsNotWritten) {
    const fs::path target = file("Broken.xopp");
    std::string error;
    EXPECT_FALSE(templates::write(templates::makePage(samplePage(0), {}), {}, target, &error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(fs::exists(target));
    EXPECT_FALSE(fs::exists(templates::attachedPdfOf(target)));
}

TEST_F(TemplateFileTest, aStaleAttachedPdfIsReplaced) {
    const fs::path pdf = file("other.pdf");
    makeTextPdf(pdf, {"stalepage", "freshpage"});
    std::string stale, fresh;
    ASSERT_TRUE(MergedPdf::extract(pdf, {0}, stale).ok);
    ASSERT_TRUE(MergedPdf::extract(pdf, {1}, fresh).ok);
    const fs::path target = file("Again.xopp");
    {
        std::ofstream(templates::attachedPdfOf(target), std::ios::binary) << stale;
    }
    ASSERT_TRUE(templates::write(templates::makePage(samplePage(0), {}), fresh, target));
    auto loaded = DocumentSession::loadFile(target);
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_FALSE(DocumentSearch::findOnPage(*loaded.document, 0, "freshpage").empty());
}

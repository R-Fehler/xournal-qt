/*
 * xournal-qt: recognised handwriting as invisible text in PDFs with notes and archive PDFs (InkTextLayer.h), so other
 * PDF viewers find it: poppler finds the words where the ink is; opening the file again does not take them for PDF
 * text; an incremental save writes only the pages whose words changed.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/HybridPdf.h"
#include "session/InkText.h"
#include "session/InkTextLayer.h"

using namespace xqt;

namespace {
std::shared_ptr<const ink::PageText> handwriting(QPointF at, std::vector<std::pair<const char*, float>> words,
                                                 float conf = 0.9f) {
    auto line = std::make_shared<ink::LineResult>();
    double x = 0;
    for (const auto& [text, p]: words) {
        ink::Word w;
        w.box = QRectF(x, 0, 60, 14);
        w.conf = conf;
        w.text = QString::fromUtf8(text);
        w.candidates.push_back(ink::candidate(w.text, p));
        line->words.push_back(std::move(w));
        x += 70;
    }
    return ink::PageText::assemble({{at, line}});
}

std::string qpdfCheck(const fs::path& pdf, int& code) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    code = job.getExitCode();
    return out.str() + err.str();
}

class InkTextLayerTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        session = std::make_unique<DocumentSession>(*app);
        session->insertNewPage(1);
    }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    DocumentTextIndex& index() { return session->search().textIndex(); }
    static QString textOf(const fs::path& pdf, int page) { return PdfLayoutReader(pdf).layout(page).text; }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<DocumentSession> session;
};
}  // namespace

TEST(InkTextLayerWords, onlySureReadingsGoIn) {
    auto page = handwriting({0, 0}, {{"Kalman", 0.9f}, {"blurb", 0.1f}, {"filter.", 0.5f}});
    const auto words = InkTextLayer::wordsOf(*page);
    ASSERT_EQ(words.size(), 2u);
    EXPECT_EQ(words[0].text, QStringLiteral("Kalman"));
    EXPECT_EQ(words[1].text, QStringLiteral("filter."));
    EXPECT_TRUE(InkTextLayer::wordsOf(*handwriting({0, 0}, {{"unsure", 1.0f}}, 0.1f)).empty());
    // The stream: invisible text, the word over its box
    const std::string content = InkTextLayer::contentOf(words, 842, "");
    EXPECT_NE(content.find("3 Tr"), std::string::npos);
    EXPECT_NE(content.find("/XqtInkText 14 Tf"), std::string::npos);
    EXPECT_NE(content.find("<004B0061006C006D0061006E0020> Tj"), std::string::npos) << content;
    EXPECT_NE(content.find("1 0 0 1 0 830.8 Tm"), std::string::npos) << content;  // (baseline: 1/5 above the bottom)
    EXPECT_NE(content.find("1 0 0 1 140 830.8 Tm"), std::string::npos) << content;  // ("blurb" left out)
    // The font program: a TrueType font of 8 tables
    const std::string& font = InkTextLayer::glyphlessFont();
    ASSERT_GT(font.size(), 12u);
    EXPECT_EQ(font.substr(0, 4), std::string("\0\1\0\0", 4));
    EXPECT_EQ(static_cast<int>(font[5]), 8);
}

TEST_F(InkTextLayerTest, otherViewersFindTheHandwriting) {
    index().setInk(1, handwriting({100, 300}, {{"Kalman", 0.9f}, {"filter", 0.8f}, {"zzq", 0.1f}}));
    const fs::path out = path("notes.pdf");
    ASSERT_TRUE(session->saveAsHybrid(out).ok);
    int code = -1;
    const std::string report = qpdfCheck(out, code);
    EXPECT_EQ(code, 0) << report;
    // poppler (as Okular, Evince) finds the words, where the ink is
    const PdfPageLayout layout = PdfLayoutReader(out).layout(1);
    EXPECT_TRUE(layout.text.contains(QStringLiteral("Kalman filter"))) << layout.text.toStdString();
    EXPECT_FALSE(layout.text.contains(QStringLiteral("zzq")));  // (unsure: only searched in the app)
    const qsizetype at = layout.text.indexOf(QStringLiteral("Kalman"));
    const auto rects = layout.rects(at, at + 6);
    ASSERT_EQ(rects.size(), 1u);
    EXPECT_NEAR(rects[0].left(), 100, 1.5);
    EXPECT_NEAR(rects[0].right(), 160, 1.5);
    EXPECT_NEAR(rects[0].top(), 300, 2);
    EXPECT_NEAR(rects[0].bottom(), 314, 2);
    EXPECT_TRUE(textOf(out, 0).trimmed().isEmpty());
    // Opened again: the text layer is not part of the PDF the document shows (the app searches the handwriting)
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_FALSE(textOf(opened.base, 1).contains(QStringLiteral("Kalman")));
}

TEST_F(InkTextLayerTest, anIncrementalSaveWritesTheChangedWords) {
    HybridPdf::compactAbove = 1000;  // (a small test file: never written anew for growing)
    index().setInk(0, handwriting({50, 50}, {{"first", 0.9f}}));
    index().setInk(1, handwriting({50, 50}, {{"turbine", 0.9f}}));
    const fs::path out = path("notes.pdf");
    ASSERT_TRUE(session->saveAsHybrid(out).ok);
    EXPECT_TRUE(textOf(out, 1).contains(QStringLiteral("turbine")));
    // The recogniser read the page again: other words
    index().setInk(1, handwriting({50, 50}, {{"turbines", 0.9f}, {"spin", 0.9f}}));
    session->getDocument()->getPage(1);  // (nothing else changed)
    const auto r = session->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.incremental);
    EXPECT_TRUE(textOf(out, 1).contains(QStringLiteral("turbines spin")));
    EXPECT_EQ(textOf(out, 0).count(QStringLiteral("first")), 1);  // (page 1 as it was: once)
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    // Its words gone: the text goes too
    index().setInk(1, nullptr);
    ASSERT_TRUE(session->save().ok);
    EXPECT_FALSE(textOf(out, 1).contains(QStringLiteral("turbines")));
    EXPECT_TRUE(textOf(out, 0).contains(QStringLiteral("first")));
    HybridPdf::compactAbove = 0.25;
}

TEST_F(InkTextLayerTest, anArchivePdfCarriesItToo) {
    index().setInk(0, handwriting({80, 120}, {{"archived", 0.9f}, {"words", 0.9f}}));
    const fs::path out = path("archive.pdf");
    const auto r = session->exportArchive(out);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.pdfa) << (r.notPdfA.empty() ? std::string() : r.notPdfA.front());
    EXPECT_TRUE(textOf(out, 0).contains(QStringLiteral("archived words")));
    int code = -1;
    EXPECT_EQ((qpdfCheck(out, code), code), 0);
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document) << opened.error;
    EXPECT_FALSE(textOf(opened.base, 0).contains(QStringLiteral("archived")));
}

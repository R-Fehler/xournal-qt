/*
 * xournal-qt: "Export library as archive…" (LibraryArchive): every document as an archive PDF with the folder
 * structure, other files copied, a README, links between the archive PDFs, the library untouched; progress and cancel.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <memory>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4
#endif
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/LibraryArchive.h"

using namespace xqt;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void makeTextPdf(const fs::path& p, int pages) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (int i = 0; i < pages; ++i) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, ("page " + std::to_string(i + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// A PDF whose font (Helvetica) is not embedded: never PDF/A.
void makeOldPdf(const fs::path& p) {
    fs::create_directories(p.parent_path());
    QPDF q;
    q.emptyPDF();
    QPDFObjectHandle page = q.makeIndirectObject(QPDFObjectHandle::parse(
            "<< /Type /Page /MediaBox [0 0 595 842] /Resources << /Font << /F1 << /Type /Font /Subtype /Type1 "
            "/BaseFont /Helvetica >> >> >> >>"));
    page.replaceKey("/Contents", QPDFObjectHandle::newStream(&q, "BT /F1 24 Tf 72 700 Td (Hello) Tj ET\n"));
    QPDFPageDocumentHelper(q).addPage(page, false);
    QPDFWriter w(q, p.string().c_str());
    w.write();
}

void addStroke(Document& doc, size_t page) {
    auto s = std::make_unique<Stroke>();
    s->setColor(Color(0xffcc0000U));
    s->setWidth(2);
    s->addPoint(Point(100, 200));
    s->addPoint(Point(200, 260));
    s->getBoundingBox();
    doc.getPage(page)->getSelectedLayer()->addElement(std::move(s));
}

/// Every file below `dir` with its size and time (to see that nothing changed).
std::map<fs::path, std::string> snapshot(const fs::path& dir) {
    std::map<fs::path, std::string> out;
    for (auto it = fs::recursive_directory_iterator(dir); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file()) {
            out[it->path()] = std::to_string(it->file_size()) + "@" +
                              std::to_string(it->last_write_time().time_since_epoch().count());
        }
    }
    return out;
}

class LibraryArchiveTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        lib = fs::path(tmp.filePath("Studies").toStdString());
        out = fs::path(tmp.filePath("Backup").toStdString());
        fs::create_directories(out);
        // Lectures/lecture.pdf with its notes (lecture.xopp)
        makeTextPdf(lib / "Lectures" / "lecture.pdf", 3);
        {
            auto loaded = DocumentSession::loadFile(lib / "Lectures" / "lecture.pdf");
            ASSERT_TRUE(loaded.document) << loaded.error;
            addStroke(*loaded.document, 1);
            ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, lib / "Lectures" / "lecture.xopp").ok);
        }
        // notes.xopp: a note with a stroke and a Markdown link to the lecture's page 3
        {
            Document doc(nullptr);
            auto page = std::make_shared<XojPage>(595, 842);
            doc.addPage(page);
            addStroke(doc, 0);
            auto* markdown = new Layer();
            markdown->setName(std::string(xoj::markdown::LAYER_NAME));
            page->getLayers().insert(page->getLayers().begin(), markdown);
            auto box = std::make_unique<Text>();
            box->setText("See [the lecture](Lectures/lecture.xopp#page=3&pdfpage=3).");
            box->setFont(XojFont("Sans", 10));
            box->setWrap(400);
            box->setTransformation(xoj::util::Matrix::TRANSLATION(56, 56));
            markdown->addElement(std::move(box));
            ASSERT_TRUE(DocumentSession::writeDocument(doc, lib / "notes.xopp").ok);
        }
        makeTextPdf(lib / "paper.pdf", 1);
        makeOldPdf(lib / "Old" / "scan.pdf");
        writeFile(lib / "todo.md", "# To do\n\n- archive\n");
        writeFile(lib / "Old" / "Deeper" / "data.csv", "a,b\n1,2\n");
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::blue);
        ASSERT_TRUE(img.save(QString::fromStdString((lib / "photo.png").string())));
        writeFile(lib / ".xournal_library" / "index", "not copied");
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    fs::path lib, out;
};

}  // namespace

TEST_F(LibraryArchiveTest, everyDocumentBecomesAnArchivePdfAndOtherFilesAreCopied) {
    const auto before = snapshot(lib);
    std::string error;
    const LibraryArchive::Plan plan = LibraryArchive::plan(lib, out, lib, "Studies", error);
    ASSERT_TRUE(error.empty()) << error;
    EXPECT_EQ(plan.target.parent_path(), fs::weakly_canonical(out));
    EXPECT_EQ(plan.target.filename().string().rfind("Studies archive ", 0), 0u) << plan.target;
    EXPECT_EQ(plan.documents.size(), 4u) << "lecture (pdf + xopp), notes, paper, scan";
    EXPECT_EQ(plan.copies.size(), 3u) << "todo.md, photo.png, data.csv";

    std::atomic<bool> cancel{false};
    std::vector<int> steps;
    const auto s = LibraryArchive::run(plan, cancel, [&](int done, int total, const fs::path&) {
        steps.push_back(done);
        EXPECT_EQ(total, 8);
    });
    EXPECT_EQ(steps.size(), 8u);
    EXPECT_FALSE(s.cancelled);
    EXPECT_EQ(s.archived, 4);
    EXPECT_EQ(s.pdfa, 3);
    EXPECT_EQ(s.copied, 3);
    EXPECT_TRUE(s.failed.empty()) << s.failed.front().first << ": " << s.failed.front().second;
    ASSERT_EQ(s.notPdfA.size(), 1u);
    EXPECT_EQ(s.notPdfA[0].first, fs::path("Old/scan.pdf"));
    EXPECT_NE(s.notPdfA[0].second.at(0).find("Helvetica"), std::string::npos);

    const fs::path t = plan.target;
    for (const char* f: {"Lectures/lecture.pdf", "notes.pdf", "paper.pdf", "Old/scan.pdf"}) {
        EXPECT_TRUE(HybridPdf::isArchive(t / f)) << f;
    }
    EXPECT_EQ(readFile(t / "todo.md"), readFile(lib / "todo.md"));
    EXPECT_EQ(readFile(t / "photo.png"), readFile(lib / "photo.png"));
    EXPECT_EQ(readFile(t / "Old/Deeper/data.csv"), "a,b\n1,2\n");
    EXPECT_EQ(fs::last_write_time(t / "todo.md"), fs::last_write_time(lib / "todo.md")) << "copied as they were";
    EXPECT_FALSE(fs::exists(t / ".xournal_library"));
    EXPECT_FALSE(fs::exists(t / "Lectures/lecture.xopp")) << "the notes are in the archive PDF";
    const std::string readme = readFile(t / "README.txt");
    EXPECT_NE(readme.find("PDF/A-3"), std::string::npos) << readme;
    EXPECT_NE(readme.find("xournal-qt"), std::string::npos);
    EXPECT_NE(readme.find("Old/scan.pdf: The source PDF has fonts that are not embedded: Helvetica"), std::string::npos)
            << readme;

    // The lecture's notes are in its archive; the note links to the lecture's archive PDF at its page 3
    auto lecture = DocumentSession::loadFile(t / "Lectures/lecture.pdf");
    ASSERT_TRUE(lecture.document) << lecture.error;
    EXPECT_EQ(lecture.document->getPageCount(), 3u);
    const auto& strokes = lecture.document->getPage(1)->getSelectedLayer()->getElementsView();
    EXPECT_EQ(std::distance(strokes.begin(), strokes.end()), 1);
    QPDF notes;
    notes.processFile((t / "notes.pdf").string().c_str());
    std::vector<std::string> links;
    for (auto& a: QPDFPageDocumentHelper(notes).getAllPages().at(0).getAnnotations()) {
        QPDFObjectHandle act = a.getObjectHandle().getKey("/A");
        links.push_back(act.getKey("/F").getUTF8Value() + " " +
                        std::to_string(act.getKey("/D").getArrayItem(0).getIntValue()));
    }
    EXPECT_EQ(links, std::vector<std::string>{"Lectures/lecture.pdf 2"});

    EXPECT_EQ(snapshot(lib), before) << "the library is not written into";
}

TEST_F(LibraryArchiveTest, neverIntoTheLibraryAndAFolderAlone) {
    std::string error;
    LibraryArchive::plan(lib, lib / "Lectures", lib, "Studies", error);
    EXPECT_NE(error.find("never goes into the library"), std::string::npos) << error;
    error.clear();
    LibraryArchive::plan(lib / "Lectures", lib, lib, "Lectures", error);
    EXPECT_FALSE(error.empty());
    error.clear();
    LibraryArchive::plan(lib, fs::path(tmp.filePath("missing").toStdString()), lib, "Studies", error);
    EXPECT_FALSE(error.empty());

    // The current folder: its documents only, its structure from there
    error.clear();
    const auto plan = LibraryArchive::plan(lib / "Old", out, lib, "Old", error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(plan.documents.size(), 1u);
    EXPECT_EQ(plan.documents[0].archive, plan.target / "scan.pdf");
    ASSERT_EQ(plan.copies.size(), 1u);
    EXPECT_EQ(plan.copies[0].second, plan.target / "Deeper" / "data.csv");
    // A second export the same day: a folder of its own
    fs::create_directories(plan.target);
    const auto again = LibraryArchive::plan(lib / "Old", out, lib, "Old", error);
    EXPECT_EQ(again.target.filename().string(), plan.target.filename().string() + " (2)");
}

TEST_F(LibraryArchiveTest, runsInTheBackgroundWithProgressAndCanBeCancelled) {
    LibraryArchive task;
    QSignalSpy finished(&task, &LibraryArchive::finished);
    std::string error;
    ASSERT_TRUE(task.start(lib, out, lib, "Studies", error)) << error;
    EXPECT_TRUE(task.running());
    EXPECT_EQ(task.total(), 8);
    EXPECT_FALSE(task.start(lib, out, lib, "Studies", error)) << "one at a time";
    ASSERT_TRUE(finished.wait(60000));
    EXPECT_FALSE(task.running());
    EXPECT_EQ(task.done(), 8);
    const QVariantMap summary = finished.at(0).at(0).toMap();
    EXPECT_EQ(summary["archived"].toInt(), 4);
    EXPECT_EQ(summary["pdfa"].toInt(), 3);
    EXPECT_EQ(summary["copied"].toInt(), 3);
    EXPECT_EQ(summary["notPdfA"].toStringList().size(), 1);
    EXPECT_FALSE(summary["cancelled"].toBool());

    // Cancelled at once: stops before the next file, the README says it is incomplete
    ASSERT_TRUE(task.start(lib, out, lib, "Studies", error)) << error;
    task.cancel();
    ASSERT_TRUE(finished.wait(60000));
    const QVariantMap cancelled = finished.at(1).at(0).toMap();
    EXPECT_TRUE(cancelled["cancelled"].toBool());
    EXPECT_LT(cancelled["archived"].toInt() + cancelled["copied"].toInt(), 7);
    const fs::path target(cancelled["target"].toString().toStdString());
    EXPECT_NE(readFile(target / "README.txt").find("incomplete"), std::string::npos);
}

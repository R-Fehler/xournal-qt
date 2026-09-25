/*
 * xournal-qt: the incremental update of a PDF (IncrementalPdf.h): written in the file's style after its bytes, only
 * what changed, readable by qpdf and poppler with the previous revision intact; a failed write leaves the file as it
 * was.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/DLL.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "pdf/base/XojPdfDocument.h"
#include "session/IncrementalPdf.h"

using namespace xqt;

namespace {
void makeTextPdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

int qpdfCheck(const fs::path& pdf, std::string& output) {
    std::ostringstream out, err;
    QPDFJob job;
    auto logger = QPDFLogger::create();
    logger->setOutputStreams(&out, &err);
    job.setLogger(logger);
    const std::string file = pdf.string();
    const char* argv[] = {"qpdf", "--check", file.c_str(), nullptr};
    job.initializeFromArgv(argv);
    job.run();
    output = out.str() + err.str();
    return job.getExitCode();
}

std::string fileBytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

size_t countOf(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) {
        ++n;
    }
    return n;
}

class IncrementalPdfTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    void TearDown() override { IncrementalPdf::failWriteAt = nullptr; }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    /// A text PDF of three pages written by qpdf, with a cross-reference stream or a classic table.
    fs::path written(bool streams) {
        makeTextPdf(path("source.pdf"), {"one", "two", "three"});
        const fs::path file = path(streams ? "streams.pdf" : "table.pdf");
        QPDF q;
        q.processFile(path("source.pdf").string().c_str());
        QPDFWriter w(q, file.string().c_str());
        w.setObjectStreamMode(streams ? qpdf_o_generate : qpdf_o_disable);
        w.write();
        return file;
    }
    QTemporaryDir tmp;
};
}  // namespace

// An update in the file's own style: the old bytes kept, only what changed written, the previous revision readable
TEST_F(IncrementalPdfTest, writesAnUpdateInTheFilesStyle) {
    for (const bool streams: {false, true}) {
        const fs::path file = written(streams);
        const std::string before = fileBytes(file);
        IncrementalPdf::Tail tail;
        std::string error;
        ASSERT_TRUE(IncrementalPdf::readTail(file, tail, error)) << error;
        EXPECT_EQ(tail.xrefStream, streams);
        EXPECT_EQ(tail.size, before.size());
        std::string firstId;
        IncrementalPdf::Stats stats;
        std::string update;
        {
            QPDF q;
            q.processFile(file.string().c_str());
            firstId = q.getTrailer().getKey("/ID").getArrayItem(0).getStringValue();
            IncrementalPdf::Update u(q);
            auto pages = QPDFPageDocumentHelper(q).getAllPages();
            QPDFObjectHandle page = pages[1].getObjectHandle();
            u.touch(page);
            QPDFObjectHandle note = u.add(QPDFObjectHandle::parse(
                    "<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (appended) >>"));
            page.replaceKey("/Annots", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{note}));
            u.touch(pages[2].getObjectHandle());  // (touched, not changed: not written)
            update = u.serialize(tail, &stats);
        }
        EXPECT_EQ(stats.changed, 1u);
        EXPECT_EQ(stats.added, 1u);
        const auto r = IncrementalPdf::append(file, tail, update);
        ASSERT_TRUE(r.ok) << r.error;
        const std::string after = fileBytes(file);
        EXPECT_EQ(after.size(), r.size);
        EXPECT_EQ(after.substr(0, before.size()), before) << "the file's bytes stay, the update follows them";
        const std::string added = after.substr(before.size());
        EXPECT_EQ(countOf(added, "trailer"), streams ? 0u : 1u);
        EXPECT_EQ(countOf(added, "/Type /XRef"), streams ? 1u : 0u);
        EXPECT_EQ(countOf(added, "/Type /ObjStm"), streams ? 1u : 0u) << "the new dictionaries in an object stream";
        EXPECT_EQ(added.substr(added.size() - 6), "%%EOF\n");
        std::string check;
        EXPECT_EQ(qpdfCheck(file, check), 0) << check;
        QPDF q;
        q.processFile(file.string().c_str());
        EXPECT_EQ(q.getTrailer().getKey("/Prev").getIntValue(), static_cast<long long>(tail.startxref));
        EXPECT_EQ(q.getTrailer().getKey("/ID").getArrayItem(0).getStringValue(), firstId);
        EXPECT_NE(q.getTrailer().getKey("/ID").getArrayItem(1).getStringValue(), firstId);
        auto pages = QPDFPageDocumentHelper(q).getAllPages();
        ASSERT_EQ(pages[1].getAnnotations().size(), 1u);
        EXPECT_EQ(pages[1].getAnnotations()[0].getObjectHandle().getKey("/Contents").getUTF8Value(), "appended");
        // The previous revision is still there, whole
        const fs::path old = path("old.pdf");
        std::ofstream(old, std::ios::binary) << before;
        QPDF previous;
        previous.processFile(old.string().c_str());
        EXPECT_TRUE(QPDFPageDocumentHelper(previous).getAllPages()[1].getAnnotations().empty());
        XojPdfDocument pdf;
        ASSERT_TRUE(pdf.load(file, "", nullptr)) << "poppler reads it";
        EXPECT_FALSE(pdf.getPage(2)->findText("three").empty());
    }
}

// New streams, copies of streams and objects copied from another PDF (with what they refer to, not its pages)
TEST_F(IncrementalPdfTest, addsStreamsAndCopiesOfOtherPdfs) {
    const fs::path file = written(true);
    makeTextPdf(path("other.pdf"), {"copied"});
    IncrementalPdf::Tail tail;
    std::string error;
    ASSERT_TRUE(IncrementalPdf::readTail(file, tail, error));
    std::string update;
    {
        QPDF q;
        q.processFile(file.string().c_str());
        IncrementalPdf::Update u(q);
        QPDF other;
        other.processFile(path("other.pdf").string().c_str());
        QPDFPageDocumentHelper(other).pushInheritedAttributesToPage();
        // The other PDF's page as a fourth page (its fonts along), and a stream added to page 1's content
        QPDFObjectHandle root = q.getRoot().getKey("/Pages");
        u.touch(root);
        QPDFObjectHandle page = u.copy(QPDFPageDocumentHelper(other).getAllPages()[0].getObjectHandle());
        page.replaceKey("/Parent", root);
        std::vector<QPDFObjectHandle> kids = root.getKey("/Kids").getArrayAsVector();
        kids.push_back(page);
        root.replaceKey("/Kids", QPDFObjectHandle::newArray(kids));
        root.replaceKey("/Count", QPDFObjectHandle::newInteger(4));
        QPDFObjectHandle first = kids[0];
        u.touch(first);
        QPDFObjectHandle stroke = u.addStream(QPDFObjectHandle::newDictionary(), "q 1 0 0 RG 4 w 100 700 m 300 650 l S Q\n");
        EXPECT_TRUE(u.isStream(stroke));
        QPDFObjectHandle again = u.copyStream(stroke);
        EXPECT_TRUE(u.isStream(again));
        std::vector<QPDFObjectHandle> contents{first.getKey("/Contents"), stroke, again};
        first.replaceKey("/Contents", QPDFObjectHandle::newArray(contents));
        update = u.serialize(tail);
    }
    ASSERT_TRUE(IncrementalPdf::append(file, tail, update).ok);
    std::string check;
    EXPECT_EQ(qpdfCheck(file, check), 0) << check;
    XojPdfDocument pdf;
    ASSERT_TRUE(pdf.load(file, "", nullptr));
    ASSERT_EQ(pdf.getPageCount(), 4u);
    EXPECT_FALSE(pdf.getPage(3)->findText("copied").empty()) << "the copied page, with its font";
    EXPECT_FALSE(pdf.getPage(0)->findText("one").empty());
}

// A failed write (a full disk, a crash) leaves the file as it was, byte for byte, and no temporary file; one left by
// a crash before goes with the next write
TEST_F(IncrementalPdfTest, aFailedWriteLeavesTheFileAsItWas) {
    const fs::path file = written(true);
    const std::string before = fileBytes(file);
    IncrementalPdf::Tail tail;
    std::string error;
    ASSERT_TRUE(IncrementalPdf::readTail(file, tail, error));
    std::string update;
    {
        QPDF q;
        q.processFile(file.string().c_str());
        IncrementalPdf::Update u(q);
        QPDFObjectHandle info = q.getTrailer().getKey("/Info");
        u.touch(info);
        info.replaceKey("/Title", QPDFObjectHandle::newString("changed"));
        update = u.serialize(tail);
    }
    auto noTemporaryFile = [&] {
        for (auto& e: fs::directory_iterator(tmp.path().toStdString())) {
            EXPECT_NE(e.path().extension(), ".part") << e.path();
        }
    };
    // Before anything of the update is written, and when all of it is written but not yet in place
    for (const uint64_t at: {uint64_t(0), uint64_t(1)}) {
        IncrementalPdf::failWriteAt = [at](uint64_t done) { return done >= at; };
        const auto r = IncrementalPdf::append(file, tail, update);
        EXPECT_FALSE(r.ok);
        EXPECT_FALSE(r.error.empty());
        EXPECT_EQ(fileBytes(file), before);
        noTemporaryFile();
    }
    IncrementalPdf::failWriteAt = nullptr;
    // Changed by another app meanwhile: not written
    IncrementalPdf::Tail other = tail;
    other.size += 1;
    EXPECT_FALSE(IncrementalPdf::append(file, other, update).ok);
    EXPECT_EQ(fileBytes(file), before);
    // A temporary file a crash left behind (older than ten minutes) goes
    const fs::path stale = fs::path(tmp.path().toStdString()) / ("." + file.filename().string() + ".4242-1.part");
    std::ofstream(stale) << before.substr(0, 100);
    fs::last_write_time(stale, fs::file_time_type::clock::now() - std::chrono::hours(1));
    ASSERT_TRUE(IncrementalPdf::append(file, tail, update).ok);
    EXPECT_FALSE(fs::exists(stale));
    noTemporaryFile();
    QPDF q;
    q.processFile(file.string().c_str());
    EXPECT_EQ(q.getTrailer().getKey("/Info").getKey("/Title").getUTF8Value(), "changed");
}

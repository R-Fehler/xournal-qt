/*
 * xournal-qt: the revisions of a PDF read back (PdfRevisions.h): the chain of cross-reference sections of our own
 * updates and of other apps', where each revision ends, its date, and each prefix as the file was then; a damaged tail
 * and a file written anew.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "session/IncrementalPdf.h"
#include "session/PdfRevisions.h"

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

class PdfRevisionsTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path path(const std::string& name) const { return fs::path(tmp.filePath(QString::fromStdString(name)).toStdString()); }
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
    /// One update as our appender writes it: one more annotation on page 2 and a new date. Returns the file's size.
    uint64_t appendOne(const fs::path& file, int n) {
        IncrementalPdf::Tail tail;
        std::string error;
        EXPECT_TRUE(IncrementalPdf::readTail(file, tail, error)) << error;
        std::string update;
        {
            QPDF q;
            q.processFile(file.string().c_str());
            IncrementalPdf::Update u(q);
            QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages()[1].getObjectHandle();
            u.touch(page);
            std::vector<QPDFObjectHandle> annots;
            if (page.getKey("/Annots").isArray()) {
                annots = page.getKey("/Annots").getArrayAsVector();
            }
            annots.push_back(u.add(QPDFObjectHandle::parse("<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (" +
                                                           std::to_string(n) + ") >>")));
            page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
            QPDFObjectHandle info = q.getTrailer().getKey("/Info");
            if (!info.isDictionary()) {
                info = u.add(QPDFObjectHandle::newDictionary());
                q.getTrailer().replaceKey("/Info", info);
            } else {
                u.touch(info);
            }
            char date[32];
            std::snprintf(date, sizeof date, "D:202610%02d120000Z", n);
            info.replaceKey("/ModDate", QPDFObjectHandle::newString(date));
            update = u.serialize(tail);
        }
        const auto r = IncrementalPdf::append(file, tail, update);
        EXPECT_TRUE(r.ok) << r.error;
        return r.size;
    }
    size_t annotsIn(const fs::path& file, uint64_t end) {
        QPDF q;
        PdfRevisions::open(q, file, end);
        return QPDFPageDocumentHelper(q).getAllPages()[1].getAnnotations().size();
    }
    QTemporaryDir tmp;
};
}  // namespace

// Eight appended saves: nine revisions, each a prefix of the file that is a valid PDF as it was saved then
TEST_F(PdfRevisionsTest, eightAppendedSavesGiveNineRevisions) {
    for (const bool streams: {false, true}) {
        const fs::path file = written(streams);
        std::vector<uint64_t> sizes{fs::file_size(file)};
        for (int n = 1; n <= 8; ++n) {
            sizes.push_back(appendOne(file, n));
        }
        const auto chain = PdfRevisions::read(file);
        ASSERT_TRUE(chain.ok()) << chain.error;
        EXPECT_FALSE(chain.garbage);
        ASSERT_EQ(chain.revisions.size(), 9u);
        for (size_t k = 0; k < 9; ++k) {
            const auto& r = chain.revisions[k];
            EXPECT_EQ(r.end, sizes[k]) << k;
            EXPECT_EQ(r.start, k == 0 ? 0 : sizes[k - 1]) << k;
            EXPECT_EQ(r.xrefStream, streams) << k;
            EXPECT_FALSE(r.objects.empty());
            EXPECT_EQ(annotsIn(file, r.end), k) << "the prefix is the file as it was";
            const fs::path prefix = path("prefix-" + std::to_string(k) + ".pdf");
            std::string error;
            ASSERT_TRUE(PdfRevisions::extract(file, r.end, prefix, error)) << error;
            EXPECT_EQ(fs::file_size(prefix), r.end);
            std::string check;
            EXPECT_EQ(qpdfCheck(prefix, check), 0) << check;
            EXPECT_EQ(PdfRevisions::read(prefix).revisions.size(), k + 1);
            if (k > 0) {
                char date[32];
                std::snprintf(date, sizeof date, "2026-10-%02dT12:00:00Z", static_cast<int>(k));
                EXPECT_EQ(PdfRevisions::dateOf(file, r.end), date);
                // An update defines the page it changed, the new annotation and the information
                EXPECT_GE(r.objects.size(), 3u);
                const auto tail = PdfRevisions::tailOf(file, chain.revisions[k - 1]);
                EXPECT_EQ(tail.size, sizes[k - 1]);
                EXPECT_TRUE(tail.eol);
            }
        }
    }
}

// Another app's update with a classic table, written its own way (other line ends, a comment, two subsections)
TEST_F(PdfRevisionsTest, anotherAppsClassicUpdateIsARevision) {
    const fs::path file = written(false);
    const uint64_t ours = appendOne(file, 1);
    std::string bytes = fileBytes(file);
    QPDF q;
    q.processFile(file.string().c_str());
    const int size = static_cast<int>(q.getTrailer().getKey("/Size").getIntValue());
    const QPDFObjGen info = q.getTrailer().getKey("/Info").getObjGen();
    const QPDFObjGen root = q.getRoot().getObjGen();
    const IncrementalPdf::Tail tail = [&] {
        IncrementalPdf::Tail t;
        std::string error;
        IncrementalPdf::readTail(file, t, error);
        return t;
    }();
    std::string update = "% another app\r\n";
    const size_t infoAt = bytes.size() + update.size();
    update += std::to_string(info.getObj()) + " 0 obj\r<< /Producer (Other) /ModDate (D:20261009080000+02'00') >>\rendobj\r";
    const size_t noteAt = bytes.size() + update.size();
    update += std::to_string(size) + " 0 obj\r<< /Type /Annot /Subtype /Text /Rect [0 0 9 9] >>\rendobj\r";
    const size_t xrefAt = bytes.size() + update.size();
    char line[32];
    update += "xref\r" + std::to_string(info.getObj()) + " 1\r";
    std::snprintf(line, sizeof line, "%010zu 00000 n\r\n", infoAt);
    update += line;
    update += std::to_string(size) + " 1\r";
    std::snprintf(line, sizeof line, "%010zu 00000 n\r\n", noteAt);
    update += line;
    update += "trailer\r<< /Size " + std::to_string(size + 1) + " /Root " + std::to_string(root.getObj()) + " 0 R /Info " +
              std::to_string(info.getObj()) + " 0 R /Prev " + std::to_string(tail.startxref) + " >>\rstartxref\r" +
              std::to_string(xrefAt) + "\r%%EOF\r";
    std::ofstream(file, std::ios::binary | std::ios::app) << update;
    std::string check;
    ASSERT_EQ(qpdfCheck(file, check), 0) << check;

    const auto chain = PdfRevisions::read(file);
    ASSERT_TRUE(chain.ok()) << chain.error;
    ASSERT_EQ(chain.revisions.size(), 3u);
    EXPECT_EQ(chain.revisions[1].end, ours);
    EXPECT_EQ(chain.revisions[2].start, ours);
    EXPECT_EQ(chain.revisions[2].end, fs::file_size(file));
    EXPECT_FALSE(chain.revisions[2].xrefStream);
    EXPECT_EQ(chain.revisions[2].objects.size(), 2u);
    EXPECT_EQ(PdfRevisions::dateOf(file, chain.revisions[2].end), "2026-10-09T06:00:00Z");
    EXPECT_FALSE(chain.garbage);
}

// A damaged tail (an unfinished update, a "%%EOF" that names no section): the last good revision is found
TEST_F(PdfRevisionsTest, aDamagedTailFindsTheLastGoodRevision) {
    for (const bool streams: {false, true}) {
        const fs::path file = written(streams);
        const uint64_t good = appendOne(file, 1);
        std::ofstream(file, std::ios::binary | std::ios::app)
                << "99 0 obj\n<< /Type /Annot\nxref\n0 1\nstartxref\n12345678\n%%EOF\ngarbage";
        const auto chain = PdfRevisions::read(file);
        ASSERT_TRUE(chain.ok()) << chain.error;
        EXPECT_TRUE(chain.garbage);
        ASSERT_EQ(chain.revisions.size(), 2u);
        EXPECT_EQ(chain.end(), good);
        EXPECT_EQ(annotsIn(file, chain.end()), 1u);
    }
}

// Written anew (compacted, or linearized): one revision
TEST_F(PdfRevisionsTest, aFileWrittenAnewHasOneRevision) {
    const fs::path file = written(true);
    for (int n = 1; n <= 3; ++n) {
        appendOne(file, n);
    }
    ASSERT_EQ(PdfRevisions::read(file).revisions.size(), 4u);
    for (const bool linearize: {false, true}) {
        const fs::path out = path(linearize ? "linear.pdf" : "compact.pdf");
        QPDF q;
        q.processFile(file.string().c_str());
        QPDFWriter w(q, out.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        w.setLinearization(linearize);
        w.write();
        const auto chain = PdfRevisions::read(out);
        ASSERT_TRUE(chain.ok()) << chain.error;
        ASSERT_EQ(chain.revisions.size(), 1u) << (linearize ? "linearized" : "compacted");
        EXPECT_EQ(chain.end(), fs::file_size(out));
        EXPECT_EQ(chain.revisions[0].start, 0u);
        EXPECT_FALSE(chain.garbage);
        EXPECT_EQ(annotsIn(out, chain.end()), 3u);
    }
}

TEST_F(PdfRevisionsTest, pdfDatesAsUtc) {
    EXPECT_EQ(PdfRevisions::isoOfPdfDate("D:20261004143000+02'00'"), "2026-10-04T12:30:00Z");
    EXPECT_EQ(PdfRevisions::isoOfPdfDate("D:20261004143000Z"), "2026-10-04T14:30:00Z");
    EXPECT_EQ(PdfRevisions::isoOfPdfDate("D:20261004"), "2026-10-04T00:00:00Z");
    EXPECT_EQ(PdfRevisions::isoOfPdfDate("D:20261004083000-05'30"), "2026-10-04T14:00:00Z");
    EXPECT_EQ(PdfRevisions::isoOfPdfDate("garbage"), "");
}

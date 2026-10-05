/*
 * xournal-qt: version history inside a PDF with notes (PdfHistory.h, qt/docs/hybrid-pdf.md "Version history"): one
 * version per day plus milestones, the day's version replaced by later saves that day, never compacted while on (a
 * fallback appends the whole document), version 0 as received, another app's revision never cut away; every version
 * opens as the document saved then.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFJob.hh>
#include <qpdf/QPDFLogger.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/IncrementalPdf.h"
#include "session/PdfHistory.h"
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
        cairo_rectangle(cr, 50, 150, 400, 300);  // (some content)
        cairo_stroke(cr);
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

/// A stroke on a page, as the pen adds it.
void drawOn(DocumentSession& s, size_t pageNo, double y) {
    PageRef page = s.getDocument()->getPage(pageNo);
    auto stroke = std::make_unique<Stroke>();
    stroke->setWidth(1.41);
    stroke->setColor(Color(0xff008000U));
    for (int j = 0; j < 12; ++j) {
        stroke->addPoint(Point(80 + j * 20, y + 6 * std::sin(j / 2.0), 1 + (j % 4) / 4.0));
    }
    stroke->getBoundingBox();
    s.getDocument()->lock();
    page->getSelectedLayer()->addElement(std::move(stroke));
    s.getDocument()->unlock();
}

size_t strokesOf(Document& doc) {
    size_t n = 0;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (auto it = l->getElementsView().begin(); it != l->getElementsView().end(); ++it) {
                ++n;
            }
        }
    }
    return n;
}

/// 2026-10-<day> at <hour>:00 local time.
std::time_t at(int day, int hour) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

class PdfHistoryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        HybridPdf::compactAbove = 0.0001;  // (without history every save here would write the file anew)
        clockNow = at(4, 10);
        PdfHistory::clock = [this] { return clockNow; };
    }
    void TearDown() override {
        HybridPdf::compactAbove = 0.25;
        PdfHistory::clock = nullptr;
        IncrementalPdf::failWriteAt = nullptr;
    }
    fs::path path(const std::string& name) const {
        return fs::path(tmp.filePath(QString::fromStdString(name)).toStdString());
    }
    /// A session of a three-page PDF with a stroke on page 1 (not saved).
    std::unique_ptr<DocumentSession> lecture(const fs::path& pdf) {
        makeTextPdf(pdf, {"lectureone", "lecturetwo", "lecturethree"});
        auto loaded = DocumentSession::loadFile(pdf);
        EXPECT_TRUE(loaded.document) << loaded.error;
        auto s = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        drawOn(*s, 0, 300);
        return s;
    }
    DocumentSession::SaveResult save(DocumentSession& s, const std::string& message = {}) {
        DocumentSession::SaveRequest r;
        r.kind = DocumentSession::SaveKind::Save;
        r.message = message;
        auto result = s.saveNow(std::move(r));
        EXPECT_TRUE(result.ok) << result.error;
        return result;
    }
    /// Each listed version cut out of the file: a valid PDF that opens as the document with `strokes[i]` strokes,
    /// whose embedded .xopp is the one its entry's checksum names.
    void expectVersionsOpen(const fs::path& pdf, const std::vector<size_t>& strokes) {
        const auto listed = PdfHistory::list(pdf);
        ASSERT_EQ(listed.versions.size(), strokes.size());
        for (size_t k = 0; k < strokes.size(); ++k) {
            const auto& v = listed.versions[k];
            const fs::path cut = path("version-" + std::to_string(v.id) + "-" + std::to_string(k) + ".pdf");
            std::string error;
            ASSERT_TRUE(PdfRevisions::extract(pdf, v.end, cut, error)) << error;
            std::string check;
            EXPECT_EQ(qpdfCheck(cut, check), 0) << "version " << v.id << ": " << check;
            if (v.kind == PdfHistory::Kind::RECEIVED) {
                EXPECT_FALSE(HybridPdf::isHybrid(cut));
                continue;
            }
            QPDF q;
            q.processFile(cut.string().c_str());
            auto spec = QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(HybridPdf::DATA_NAME);
            ASSERT_TRUE(spec) << "version " << v.id;
            auto buffer = spec->getEmbeddedFileStream().getStreamData();
            bool ok = false;
            const std::string xml = PdfHistory::gunzip(
                    std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()), ok);
            ASSERT_TRUE(ok);
            EXPECT_EQ(PdfHistory::sha256(xml), v.sha) << "version " << v.id;
            auto opened = HybridPdf::open(cut);
            ASSERT_TRUE(opened.document) << opened.error;
            EXPECT_EQ(strokesOf(*opened.document), strokes[k]) << "version " << v.id;
        }
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::time_t clockNow = 0;
};
}  // namespace

// One version per day: the first save of a day appends one, later saves that day replace it (one revision each);
// a save with a message is a milestone, never replaced; the saves after it start a new version
TEST_F(PdfHistoryTest, oneVersionPerDayPlusMilestones) {
    const fs::path out = path("notes.pdf");
    auto s = lecture(path("lecture.pdf"));
    s->setKeepsVersions(true);
    ASSERT_TRUE(s->saveAsHybrid(out).ok);  // (a new file: version 1, written in full)
    EXPECT_TRUE(HybridPdf::markerOf(out).history);
    EXPECT_FALSE(s->versionsChoicePending()) << "the file says it now";
    EXPECT_TRUE(s->keepsVersions());
    auto listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 1u);
    EXPECT_EQ(listed.versions[0].id, 1);
    EXPECT_TRUE(listed.on);

    clockNow = at(4, 11);
    drawOn(*s, 1, 300);
    auto r = save(*s);
    EXPECT_TRUE(r.incremental);
    EXPECT_EQ(r.version, 2) << "the first version was written in full: not cut away";
    EXPECT_FALSE(r.replacedVersion);
    clockNow = at(4, 12);
    drawOn(*s, 1, 400);
    r = save(*s);
    EXPECT_EQ(r.version, 2);
    EXPECT_TRUE(r.replacedVersion) << "the same day: the day's version replaced";
    listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.chain.revisions.size(), 2u) << "one revision per version";
    EXPECT_TRUE(listed.others.empty());

    clockNow = at(4, 13);
    drawOn(*s, 2, 300);
    r = save(*s, "Before the exam");
    EXPECT_EQ(r.version, 2) << "the milestone takes the day's version's place";
    clockNow = at(4, 14);
    drawOn(*s, 2, 400);
    r = save(*s);
    EXPECT_EQ(r.version, 3) << "after a milestone: a new version";
    EXPECT_FALSE(r.replacedVersion);

    clockNow = at(5, 9);
    drawOn(*s, 0, 500);
    r = save(*s);
    EXPECT_EQ(r.version, 4) << "a new day";
    clockNow = at(5, 18);
    drawOn(*s, 0, 600);
    r = save(*s);
    EXPECT_EQ(r.version, 4);
    EXPECT_TRUE(r.replacedVersion);

    listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 4u);
    EXPECT_EQ(listed.removed, 0);
    EXPECT_TRUE(listed.others.empty());
    EXPECT_EQ(listed.chain.revisions.size(), 4u);
    EXPECT_EQ(listed.versions[1].message, "Before the exam");
    EXPECT_TRUE(listed.versions[1].milestone());
    EXPECT_EQ(listed.versions[1].day, PdfHistory::localDay(at(4, 13)));
    EXPECT_EQ(listed.versions[3].date, PdfHistory::isoUtc(at(5, 18)));
    EXPECT_EQ(HybridPdf::markerOf(out).versions, 4);
    std::string check;
    EXPECT_EQ(qpdfCheck(out, check), 0) << check;
    expectVersionsOpen(out, {1, 4, 5, 7});
    auto opened = HybridPdf::open(out);
    ASSERT_TRUE(opened.document);
    EXPECT_EQ(strokesOf(*opened.document), 7u);
}

// While history is on the file is never written anew, however much it grew (compactAbove is tiny here)
TEST_F(PdfHistoryTest, neverCompactedWhileOn) {
    const fs::path out = path("notes.pdf");
    auto s = lecture(path("lecture.pdf"));
    s->setKeepsVersions(true);
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    for (int day = 5; day < 11; ++day) {
        clockNow = at(day, 10);
        drawOn(*s, static_cast<size_t>(day % 3), 100 + 40 * day);
        const auto r = save(*s);
        EXPECT_TRUE(r.incremental) << "day " << day;
    }
    EXPECT_EQ(PdfHistory::list(out).versions.size(), 7u);
    // Off: the list goes with the next save, and the rules of compaction are back
    s->setKeepsVersions(false);
    drawOn(*s, 0, 700);
    const auto r = save(*s);
    EXPECT_FALSE(r.incremental) << "written anew: it grew far beyond compactAbove";
    EXPECT_FALSE(HybridPdf::markerOf(out).history);
    EXPECT_FALSE(s->keepsVersions());
    EXPECT_TRUE(PdfHistory::list(out).versions.empty());
}

// Notes written into a PDF of the user's: version 0 is the PDF as received, byte for byte; the document is appended on
// top of it, its pages not copied again
TEST_F(PdfHistoryTest, versionZeroIsThePdfAsReceived) {
    const fs::path pdf = path("lecture.pdf");
    auto s = lecture(pdf);
    const std::string received = fileBytes(pdf);
    DocumentMode::setKeepVersionsOfNewPdfs(*app->getSettings(), true);
    EXPECT_TRUE(s->keepsVersions()) << "the setting for new PDFs with notes";
    const auto r = s->saveAsHybrid(pdf);  // (notes saved into the PDF itself)
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.version, 1);
    const std::string after = fileBytes(pdf);
    EXPECT_EQ(after.substr(0, received.size()), received) << "the PDF as received stays, the notes follow";
    const auto listed = PdfHistory::list(pdf);
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.versions[0].kind, PdfHistory::Kind::RECEIVED);
    EXPECT_EQ(listed.versions[0].end, received.size());
    // Its pages are referred to, not copied: a full write of the same document is bigger than what was appended
    ASSERT_TRUE(HybridPdf::write(*s->getDocument(), path("full.pdf")).ok);
    const auto full = fs::file_size(path("full.pdf"));
    EXPECT_LT(after.size() - received.size(), full) << "appended " << after.size() - received.size() << ", full " << full;
    std::string check;
    EXPECT_EQ(qpdfCheck(pdf, check), 0) << check;
    expectVersionsOpen(pdf, {0, 1});
    // The next day's save appends to it as usual
    clockNow = at(5, 10);
    drawOn(*s, 1, 300);
    const auto next = save(*s);
    EXPECT_TRUE(next.incremental);
    EXPECT_EQ(next.version, 2);
    expectVersionsOpen(pdf, {0, 1, 2});
}

// Another app saved the file: its revision is listed as such and never cut away; the save that cannot build on the
// file appends the whole document, and every earlier version stays
TEST_F(PdfHistoryTest, anotherAppsRevisionIsKeptAndTheFallbackKeepsTheVersions) {
    const fs::path out = path("notes.pdf");
    auto s = lecture(path("lecture.pdf"));
    s->setKeepsVersions(true);
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    clockNow = at(4, 11);
    drawOn(*s, 1, 300);
    ASSERT_EQ(save(*s).version, 2);
    // Another app adds a comment (its own update)
    {
        IncrementalPdf::Tail tail;
        std::string error;
        ASSERT_TRUE(IncrementalPdf::readTail(out, tail, error));
        QPDF q;
        q.processFile(out.string().c_str());
        IncrementalPdf::Update u(q);
        QPDFObjectHandle page = QPDFPageDocumentHelper(q).getAllPages()[2].getObjectHandle();
        u.touch(page);
        std::vector<QPDFObjectHandle> annots;
        if (page.getKey("/Annots").isArray()) {
            annots = page.getKey("/Annots").getArrayAsVector();
        }
        annots.push_back(u.add(QPDFObjectHandle::parse("<< /Type /Annot /Subtype /Text /Rect [10 10 30 30] /Contents (other) >>")));
        page.replaceKey("/Annots", QPDFObjectHandle::newArray(annots));
        QPDFObjectHandle info = q.getTrailer().getKey("/Info");
        u.touch(info);
        info.replaceKey("/ModDate", QPDFObjectHandle::newString("D:20261004113000Z"));
        ASSERT_TRUE(IncrementalPdf::append(out, tail, u.serialize(tail)).ok);
    }
    const uint64_t otherEnd = fs::file_size(out);
    auto listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 2u);
    ASSERT_EQ(listed.others.size(), 1u);
    EXPECT_EQ(listed.others[0].date, "2026-10-04T11:30:00Z");
    // The same day: the day's version is not the last revision any more, so not replaced. The file is not the
    // version the session wrote: the whole document is appended
    clockNow = at(4, 12);
    drawOn(*s, 1, 400);
    const auto r = save(*s);
    EXPECT_TRUE(r.incremental) << "appended, not written anew";
    EXPECT_FALSE(r.replacedVersion);
    EXPECT_EQ(r.version, 3);
    EXPECT_FALSE(fileBytes(out).empty());
    EXPECT_GT(fs::file_size(out), otherEnd);
    listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 3u);
    ASSERT_EQ(listed.others.size(), 1u) << "the other app's revision is still there";
    EXPECT_EQ(listed.others[0].end, otherEnd);
    expectVersionsOpen(out, {1, 2, 3});
    // The session builds on what it appended: the next day appends as usual
    clockNow = at(5, 9);
    drawOn(*s, 0, 500);
    const auto next = save(*s);
    EXPECT_TRUE(next.incremental);
    EXPECT_EQ(next.version, 4);
    expectVersionsOpen(out, {1, 2, 3, 4});
}

// History turned on for a PDF with notes saved before: version 0 is the file as it was
TEST_F(PdfHistoryTest, historyBeginsOnAPdfWithNotes) {
    const fs::path out = path("notes.pdf");
    auto s = lecture(path("lecture.pdf"));
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    EXPECT_FALSE(s->keepsVersions());
    EXPECT_TRUE(PdfHistory::list(out).versions.empty());
    s->setKeepsVersions(true);
    EXPECT_TRUE(s->versionsChoicePending());
    drawOn(*s, 1, 300);
    const auto r = save(*s);
    EXPECT_EQ(r.version, 1);
    EXPECT_FALSE(s->versionsChoicePending());
    const auto listed = PdfHistory::list(out);
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.versions[0].id, 0);
    EXPECT_EQ(listed.versions[0].kind, PdfHistory::Kind::FULL);
    expectVersionsOpen(out, {1, 2});
}

TEST_F(PdfHistoryTest, theListReadsBack) {
    PdfHistory::Version a;
    a.id = 3;
    a.date = "2026-10-04T12:30:00Z";
    a.day = "2026-10-04";
    a.message = "Before the exam \"quoted\"\nsecond line";
    a.start = 1234567;
    a.end = 2345678;
    a.sha = PdfHistory::sha256("x");
    a.kind = PdfHistory::Kind::DELTA;
    a.base = 2;
    a.pages = 20;
    PdfHistory::Version b;
    b.id = 4;
    const auto back = PdfHistory::fromJsonLines(PdfHistory::toJsonLines({a, b}) + "not json\n");
    ASSERT_EQ(back.size(), 2u);
    EXPECT_EQ(back[0].message, a.message);
    EXPECT_EQ(back[0].start, a.start);
    EXPECT_EQ(back[0].end, a.end);
    EXPECT_EQ(back[0].sha, a.sha);
    EXPECT_EQ(back[0].kind, a.kind);
    EXPECT_EQ(back[0].base, 2);
    EXPECT_EQ(back[0].pages, 20);
    EXPECT_EQ(back[1].id, 4);
    EXPECT_EQ(back[1].end, 0u);
    EXPECT_EQ(back[1].base, -1);
}

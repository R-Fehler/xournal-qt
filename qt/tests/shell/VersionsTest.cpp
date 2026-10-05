/*
 * xournal-qt: the version history of a document for the History panel (VersionsModel), restoring a version (one undo
 * step, the next save a new version), messages changed later, the cache of versions cut out of the file
 * (VersionCache: its limit), and xournal-qt-cli export-xopp.
 *
 * @license GNU GPLv2 or later
 */
#include <cmath>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <QCoreApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfHistory.h"
#include "session/VersionCache.h"
#include "shell/VersionsModel.h"
#include "undo/UndoRedoHandler.h"

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
    doc.lock_shared();
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (auto it = l->getElementsView().begin(); it != l->getElementsView().end(); ++it) {
                ++n;
            }
        }
    }
    doc.unlock_shared();
    return n;
}

std::time_t at(int day, int hour) {
    std::tm tm{};
    tm.tm_year = 2026 - 1900;
    tm.tm_mon = 9;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_isdst = -1;
    return std::mktime(&tm);
}

class VersionsTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        clockNow = at(4, 10);
        PdfHistory::clock = [this] { return clockNow; };
    }
    void TearDown() override { PdfHistory::clock = nullptr; }
    fs::path path(const std::string& name) const {
        return fs::path(tmp.filePath(QString::fromStdString(name)).toStdString());
    }
    /// A PDF with notes that kept three versions (4, 5 and 6 October; the one of the 5th a milestone), with 1, 2 and
    /// 3 strokes.
    std::unique_ptr<DocumentSession> withThreeVersions() {
        makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
        auto loaded = DocumentSession::loadFile(path("lecture.pdf"));
        EXPECT_TRUE(loaded.document);
        auto s = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        s->setKeepsVersions(true);
        drawOn(*s, 1, 300);
        EXPECT_TRUE(s->saveAsHybrid(path("notes.pdf")).ok);
        clockNow = at(5, 10);
        drawOn(*s, 1, 400);
        DocumentSession::SaveRequest r;
        r.message = "Before the exam";
        EXPECT_TRUE(s->saveNow(r).ok);
        clockNow = at(6, 10);
        drawOn(*s, 2, 400);
        EXPECT_TRUE(s->save().ok);
        return s;
    }
    void waitFor(VersionsModel& m) {
        EXPECT_TRUE(QTest::qWaitFor([&] { return !m.busy(); }, 10000));
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::time_t clockNow = 0;
};
}  // namespace

TEST_F(VersionsTest, theModelListsTheVersionsNewestFirst) {
    auto s = withThreeVersions();
    VersionsModel m;
    m.setSession(s.get());
    EXPECT_EQ(m.rowCount(), 0) << "read only while shown";
    m.setActive(true);
    waitFor(m);
    EXPECT_TRUE(m.available());
    EXPECT_TRUE(m.on());
    EXPECT_FALSE(m.pending());
    ASSERT_EQ(m.rowCount(), 3);
    EXPECT_EQ(m.versionCount(), 3);
    auto cell = [&](int row, int role) { return m.data(m.index(row), role); };
    EXPECT_EQ(cell(0, VersionsModel::VersionIdRole).toInt(), 3);
    EXPECT_TRUE(cell(0, VersionsModel::CurrentRole).toBool());
    EXPECT_EQ(cell(0, VersionsModel::TitleRole).toString(),
              VersionsModel::titleFor(PdfHistory::isoUtc(clockNow), clockNow));
    EXPECT_EQ(cell(1, VersionsModel::MessageRole).toString(), "Before the exam");
    EXPECT_TRUE(cell(1, VersionsModel::MilestoneRole).toBool());
    EXPECT_EQ(cell(2, VersionsModel::VersionIdRole).toInt(), 1);
    EXPECT_FALSE(m.sizeText().isEmpty());
    m.setMilestonesOnly(true);
    ASSERT_EQ(m.rowCount(), 1);
    EXPECT_EQ(cell(0, VersionsModel::VersionIdRole).toInt(), 2);
    m.setMilestonesOnly(false);
    EXPECT_EQ(m.rowCount(), 3);
    // Dates for people
    const std::time_t now = at(6, 15);
    EXPECT_TRUE(VersionsModel::titleFor(PdfHistory::isoUtc(at(6, 9)), now).startsWith("Today"));
    EXPECT_TRUE(VersionsModel::titleFor(PdfHistory::isoUtc(at(5, 9)), now).startsWith("Yesterday"));
    EXPECT_FALSE(VersionsModel::titleFor(PdfHistory::isoUtc(at(1, 9)), now).startsWith("Today"));
    // A .xopp document: versions need a PDF with notes
    ASSERT_TRUE(s->saveAs(path("notes.xopp")).ok);
    EXPECT_FALSE(m.available());
    EXPECT_TRUE(m.needsPdf());
    EXPECT_FALSE(m.unavailableReason().isEmpty());
}

// Restoring a version: its pages replace the document's in one undo step; the next save is a new version with a
// message saying which, never in place of the day's version
TEST_F(VersionsTest, restoreIsOneUndoStepAndTheNextSaveANewVersion) {
    auto s = withThreeVersions();
    VersionsModel m;
    m.setSession(s.get());
    m.setActive(true);
    waitFor(m);
    ASSERT_EQ(strokesOf(*s->getDocument()), 3u);
    QSignalSpy restored(&m, &VersionsModel::restored);
    ASSERT_TRUE(m.restore(1));
    EXPECT_TRUE(QTest::qWaitFor([&] { return restored.count() > 0; }, 20000));
    ASSERT_EQ(restored.count(), 1);
    ASSERT_TRUE(restored.first().at(0).toBool()) << restored.first().at(1).toString().toStdString();
    EXPECT_EQ(strokesOf(*s->getDocument()), 1u) << "the pages of version 1";
    EXPECT_EQ(s->getDocument()->getPageCount(), 3u);
    EXPECT_TRUE(s->isModified());
    s->getUndoRedoHandler()->undo();
    EXPECT_EQ(strokesOf(*s->getDocument()), 3u) << "one undo step";
    s->getUndoRedoHandler()->redo();
    EXPECT_EQ(strokesOf(*s->getDocument()), 1u);
    // The same day as version 3: still a new version
    clockNow = at(6, 16);
    const auto r = s->save();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.version, 4);
    EXPECT_FALSE(r.replacedVersion);
    waitFor(m);
    const auto listed = PdfHistory::list(path("notes.pdf"));
    ASSERT_EQ(listed.versions.size(), 4u);
    EXPECT_EQ(listed.versions.back().message.rfind("Restored the version of", 0), 0u) << listed.versions.back().message;
    // The save after it is the day's again
    drawOn(*s, 0, 500);
    clockNow = at(6, 17);
    const auto again = s->save();
    EXPECT_EQ(again.version, 5) << "after a milestone: a new version";
}

// A message given later: only the marker is appended, part of the current version (not another app's revision); the
// next save still appends to the file
TEST_F(VersionsTest, aMessageChangedLater) {
    auto s = withThreeVersions();
    std::string error;
    ASSERT_TRUE(s->setVersionMessage(1, "The first lecture", error)) << error;
    ASSERT_TRUE(s->setVersionMessage(3, "Week 1", error)) << error;
    auto listed = PdfHistory::list(path("notes.pdf"));
    ASSERT_EQ(listed.versions.size(), 3u);
    EXPECT_EQ(listed.versions[0].message, "The first lecture");
    EXPECT_EQ(listed.versions[2].message, "Week 1");
    EXPECT_TRUE(listed.others.empty()) << "the marker's update is part of the current version";
    EXPECT_TRUE(listed.lastIsOurs);
    clockNow = at(6, 18);
    drawOn(*s, 0, 600);
    const auto r = s->save();
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.incremental) << "the session still builds on the file";
    EXPECT_EQ(r.version, 4) << "version 3 is a milestone now";
    listed = PdfHistory::list(path("notes.pdf"));
    ASSERT_EQ(listed.versions.size(), 4u);
    EXPECT_TRUE(listed.others.empty());
    EXPECT_EQ(listed.versions[2].message, "Week 1");
}

// The versions cut out of files: the last five used are kept, the rest removed; all go when the app quits
TEST_F(VersionsTest, theVersionCacheKeepsTheLastFive) {
    auto s = withThreeVersions();
    for (int day = 7; day < 12; ++day) {
        clockNow = at(day, 10);
        drawOn(*s, 1, 100 + 40 * day);
        ASSERT_TRUE(s->save().ok);
    }
    VersionCache& cache = VersionCache::instance();
    std::vector<fs::path> files;
    for (int id = 1; id <= 8; ++id) {
        std::string error;
        files.push_back(cache.get(path("notes.pdf"), id, error));
        ASSERT_FALSE(files.back().empty()) << error;
        EXPECT_TRUE(cache.contains(files.back()));
        EXPECT_EQ(files.back().filename().string(), "notes (version " + std::to_string(id) + ").pdf");
        auto loaded = DocumentSession::loadFile(files.back());
        ASSERT_TRUE(loaded.document) << loaded.error;
        EXPECT_EQ(strokesOf(*loaded.document), static_cast<size_t>(id)) << "version " << id;
    }
    size_t kept = 0;
    for (const auto& f: files) {
        kept += fs::exists(f) ? 1 : 0;
    }
    EXPECT_EQ(kept, 5u);
    EXPECT_TRUE(fs::exists(files.back()));
    std::string error;
    EXPECT_EQ(cache.get(path("notes.pdf"), 8, error), files.back()) << "made once";
    cache.clear();
    EXPECT_FALSE(fs::exists(cache.folder()));
}

TEST_F(VersionsTest, theCliExportsAVersion) {
    withThreeVersions();
    const QString cli = QCoreApplication::applicationDirPath() + "/xournal-qt-cli";
    QProcess p;
    p.start(cli, {"export-xopp", QString::fromStdString(path("notes.pdf").string()), "--version", "2", "-o",
                  QString::fromStdString(path("v2.xopp").string())});
    ASSERT_TRUE(p.waitForFinished(60000));
    EXPECT_EQ(p.exitCode(), 0) << p.readAllStandardError().toStdString();
    auto loaded = DocumentSession::loadFile(path("v2.xopp"));
    ASSERT_TRUE(loaded.document) << loaded.error;
    EXPECT_EQ(strokesOf(*loaded.document), 2u);
    QProcess none;
    none.start(cli, {"export-xopp", QString::fromStdString(path("notes.pdf").string()), "--version", "9"});
    ASSERT_TRUE(none.waitForFinished(60000));
    EXPECT_NE(none.exitCode(), 0);
}

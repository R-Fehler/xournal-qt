/*
 * xournal-qt: writing a PDF's tags (PdfKeywords::write) next to the app's own saves of it (qt/docs/features/tags.md,
 * "Tags in files"): a save that starts while the tags are written waits for them (one writer of a file at a time), and
 * on a PDF with notes with version history the tags' update is ours, not "another app's" (the day's version is still
 * replaced, a version still takes a message).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <cmath>
#include <ctime>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
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
#include "session/PdfKeywords.h"

using namespace xqt;

namespace {
void makeTextPdf(const fs::path& p, const std::vector<std::string>& words) {
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_set_font_size(cr, 24);
    for (const auto& w: words) {
        cairo_move_to(cr, 72, 100);
        cairo_show_text(cr, w.c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
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
            n += static_cast<size_t>(std::distance(l->getElementsView().begin(), l->getElementsView().end()));
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

class TagsWriteTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 2);
        clockNow = at(4, 10);
        PdfHistory::clock = [this] { return clockNow; };
    }
    void TearDown() override {
        PdfHistory::clock = nullptr;
        pdfkeywords::beforeAppend = nullptr;
    }
    fs::path path(const std::string& name) const {
        return fs::path(tmp.filePath(QString::fromStdString(name)).toStdString());
    }
    /// A session of a three-page PDF with a stroke on page 1 (not saved).
    std::unique_ptr<DocumentSession> lecture() {
        const fs::path pdf = path("lecture.pdf");
        makeTextPdf(pdf, {"one", "two", "three"});
        auto loaded = DocumentSession::loadFile(pdf);
        EXPECT_TRUE(loaded.document) << loaded.error;
        auto s = std::make_unique<DocumentSession>(*app, std::move(loaded.document));
        drawOn(*s, 0, 300);
        return s;
    }
    DocumentSession::SaveResult save(DocumentSession& s) {
        auto result = s.save();
        EXPECT_TRUE(result.ok) << result.error;
        return result;
    }
    size_t strokesIn(const fs::path& pdf) {
        auto opened = HybridPdf::open(pdf);
        EXPECT_TRUE(opened.document) << opened.error;
        return opened.document ? strokesOf(*opened.document) : 0;
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::time_t clockNow = 0;
};
}  // namespace

// Ctrl+S while the tags of the same PDF are written (AppTags.cpp: on a worker): the save waits until they are in the
// file, then writes the document on top of them. Neither is lost, neither fails.
TEST_F(TagsWriteTest, aSaveWaitsForATagWriteOfTheSameFile) {
    const fs::path out = path("notes.pdf");
    auto s = lecture();
    ASSERT_TRUE(s->saveAsHybrid(out).ok);
    drawOn(*s, 1, 300);  // (a second stroke: saved by the Ctrl+S below)

    std::promise<void> reached;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    pdfkeywords::beforeAppend = [&] {
        reached.set_value();
        released.wait();
    };
    bool tagged = false;
    std::string tagError;
    std::thread tagger([&] { tagged = pdfkeywords::write(out, {QStringLiteral("exam")}, tagError); });
    reached.get_future().wait();  // (the tags are read and about to be appended)

    bool saved = false;
    DocumentSession::SaveResult result;
    DocumentSession::SaveRequest request;
    request.kind = DocumentSession::SaveKind::Save;
    request.done = [&](const DocumentSession::SaveResult& r) {
        saved = true;
        result = r;
    };
    s->saveInBackground(std::move(request));
    // (a bounded wait for what must not happen: the save finishing while the tags are being written)
    QElapsedTimer waited;
    waited.start();
    while (!saved && waited.elapsed() < 300) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool savedMeanwhile = saved;
    release.set_value();
    tagger.join();
    pdfkeywords::beforeAppend = nullptr;
    s->waitForSaves();

    EXPECT_FALSE(savedMeanwhile) << "the save wrote the file while its tags were being written";
    EXPECT_TRUE(tagged) << "the tags were not written: " << tagError;
    ASSERT_TRUE(saved);
    EXPECT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(pdfkeywords::tagsOf(out).contains(QStringLiteral("exam"))) << "the tags are in the file";
    EXPECT_EQ(strokesIn(out), 2u) << "the save is in the file";
}

// Tags written on a PDF with notes that keeps its versions: their update is the current version's last revision,
// not another app's. Later saves that day still replace the day's version (and keep the tags), and a version still
// takes a message ("changed by another app" before).
TEST_F(TagsWriteTest, tagsOnAPdfWithVersionHistoryAreNotAnotherAppsRevision) {
    const fs::path out = path("notes.pdf");
    auto s = lecture();
    s->setKeepsVersions(true);
    ASSERT_TRUE(s->saveAsHybrid(out).ok);  // (version 1, written in full)
    clockNow = at(4, 11);
    drawOn(*s, 1, 300);
    EXPECT_EQ(save(*s).version, 2);

    std::string error;
    ASSERT_TRUE(pdfkeywords::write(out, {QStringLiteral("exam")}, error)) << error;
    auto listed = PdfHistory::list(out);
    EXPECT_TRUE(listed.lastIsOurs) << "the tags' update counts as another app's revision";
    EXPECT_TRUE(listed.others.empty()) << "the version list shows the tags' update as another app's revision";
    EXPECT_EQ(listed.versions.size(), 2u);

    clockNow = at(4, 12);
    drawOn(*s, 2, 300);
    const auto r = save(*s);
    EXPECT_EQ(r.version, 2);
    EXPECT_TRUE(r.replacedVersion) << "the same day: the day's version replaced";
    EXPECT_TRUE(pdfkeywords::tagsOf(out).contains(QStringLiteral("exam"))) << "the tags stay";
    EXPECT_EQ(strokesIn(out), 3u);

    EXPECT_TRUE(s->setVersionMessage(2, "Before the exam", error)) << error;
    listed = PdfHistory::list(out);
    EXPECT_TRUE(listed.others.empty());
    ASSERT_EQ(listed.versions.size(), 2u);
    EXPECT_EQ(listed.versions.back().message, "Before the exam");
}

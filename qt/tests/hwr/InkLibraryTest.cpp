/*
 * xournal-qt: handwriting in the library (InkTextStore: the pack "ink-text"; LibraryIndex's search).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <memory>

#include <QCborArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "hwr/FakeRecognizer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/FuzzyQuery.h"
#include "shell/DocumentFiles.h"
#include "shell/InkTextStore.h"
#include "shell/FileStamps.h"
#include "shell/LibraryIndex.h"
#include "shell/LibraryInkJob.h"
#include "shell/LibraryShare.h"
#include "shell/LibraryUnzip.h"
#include "support/TestSupport.h"

using xqt::test::waitFor;

using namespace xqt;

namespace {
std::shared_ptr<const ink::LineResult> lineOf(std::vector<std::vector<std::pair<const char*, float>>> words) {
    auto line = std::make_shared<ink::LineResult>();
    double x = 0;
    for (const auto& readings: words) {
        ink::Word w;
        w.box = QRectF(x, 0, 40, 12);
        w.conf = 0.8f;
        w.text = QString::fromUtf8(readings.front().first);
        for (const auto& [text, p]: readings) {
            w.candidates.push_back(ink::candidate(QString::fromUtf8(text), p));
        }
        line->words.push_back(std::move(w));
        x += 50;
    }
    return line;
}

/// A .xopp with `pages` pages (one stroke each, so it has content)
fs::path writeXopp(const QTemporaryDir& dir, const char* name, size_t pages) {
    Document doc(nullptr);
    for (size_t i = 0; i < pages; ++i) {
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        auto s = std::make_unique<Stroke>();
        s->addPoint(Point(10, 10));
        s->addPoint(Point(20, 20));
        page->getSelectedLayer()->addElement(std::move(s));
        doc.addPage(std::move(page));
    }
    const fs::path file = fs::path(dir.filePath(name).toStdString());
    EXPECT_TRUE(DocumentSession::writeDocument(doc, file).ok);
    return file;
}

DocumentItem itemOf(const fs::path& xopp) {
    DocumentItem item;
    item.xopp = xopp;
    return item;
}

InkDoc inkDoc(const fs::path& file, std::vector<std::vector<hwr::LineRef>> pages, bool complete = true) {
    InkDoc d;
    d.stamp = fileStamp(file);
    d.recognizer = QStringLiteral("fake/1");
    d.complete = complete;
    d.pages = std::move(pages);
    return d;
}

class InkLibraryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(InkLibraryTest, anEntryIsStoredAndReadBack) {
    InkDoc d;
    d.stamp = QStringLiteral("123:456");
    d.recognizer = QStringLiteral("trocr/abc/seg1");
    d.complete = false;
    auto line = lineOf({{{"Kalman", 0.6f}, {"Kalmar", 0.4f}}, {{"filter", 1.0f}}});
    d.pages = {{{42, {100.25, 200.5}, line}, {7, {0, 300}, nullptr}}, {}, {{42, {10, 20}, line}}};
    const QCborMap stored = InkTextStore::encode(d);
    // (a line on two pages is stored once)
    EXPECT_EQ(stored.value(QStringLiteral("lines")).toArray().size(), 1);
    auto back = InkTextStore::decode(stored);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->stamp, d.stamp);
    EXPECT_EQ(back->recognizer, d.recognizer);
    EXPECT_FALSE(back->complete);
    ASSERT_EQ(back->pages.size(), 3u);
    ASSERT_EQ(back->pages[0].size(), 2u);
    EXPECT_EQ(back->pages[0][0].hash, 42u);
    EXPECT_NEAR(back->pages[0][0].origin.x(), 100.3, 0.06);
    EXPECT_EQ(back->pages[0][1].result, nullptr);  // (not read)
    ASSERT_TRUE(back->pages[0][0].result);
    EXPECT_EQ(back->pages[0][0].result, back->pages[2][0].result);
    const ink::Word& w = back->pages[0][0].result->words[0];
    EXPECT_EQ(w.text, QStringLiteral("Kalman"));
    ASSERT_EQ(w.candidates.size(), 2u);
    EXPECT_EQ(words::textOf(w.candidates[1].word), QStringLiteral("kalmar"));
    EXPECT_NEAR(w.candidates[0].p, 0.6, 0.005);
    EXPECT_NEAR(w.conf, 0.8, 0.005);
    // Put together for the search
    ASSERT_EQ(back->texts.size(), 3u);
    ASSERT_TRUE(back->texts[0]);
    EXPECT_EQ(back->texts[0]->words.size(), 2u);
    EXPECT_FALSE(back->texts[1]);
    EXPECT_NEAR(back->texts[2]->words[1].box.left(), 60, 0.06);
    EXPECT_FALSE(InkTextStore::decode(QCborMap{{QStringLiteral("x"), 1}}));
}

// A line written at an angle keeps it in the pack (its words are stored upright); a line without one stores none
TEST_F(InkLibraryTest, theAngleOfALineIsStored) {
    InkDoc d;
    d.stamp = QStringLiteral("1:2");
    d.recognizer = QStringLiteral("fake/1");
    auto line = lineOf({{{"margin", 1.0f}}, {{"note", 1.0f}}});
    d.pages = {{{42, {40, 330}, line, -90}, {43, {100, 100}, line, 0}, {44, {150, 300}, line, 35.25}}};
    const QCborMap stored = InkTextStore::encode(d);
    const QCborArray refs = stored.value(QStringLiteral("pages")).toArray().at(0).toArray();
    ASSERT_EQ(refs.size(), 3);
    EXPECT_EQ(refs.at(1).toArray().size(), 3) << "no angle stored for a horizontal line";
    auto back = InkTextStore::decode(stored);
    ASSERT_TRUE(back);
    ASSERT_EQ(back->pages.at(0).size(), 3u);
    EXPECT_EQ(back->pages[0][0].angle, -90);
    EXPECT_EQ(back->pages[0][1].angle, 0);
    EXPECT_NEAR(back->pages[0][2].angle, 35.25, 0.005);
    // Put together turned: "margin" goes up from (40, 330)
    const auto& text = back->texts.at(0);
    ASSERT_TRUE(text);
    EXPECT_EQ(text->words[0].angle, -90);
    const QRectF b = ink::boundsOf(text->words[0]);
    EXPECT_NEAR(b.left(), 40, 0.06);
    EXPECT_NEAR(b.bottom(), 330, 0.06);
    EXPECT_GT(b.height(), b.width());
}

TEST_F(InkLibraryTest, theLibrarySearchFindsHandwriting) {
    const fs::path a = writeXopp(tmp, "a.xopp", 2);
    const fs::path b = writeXopp(tmp, "b.xopp", 1);
    {
        LibraryIndex index(root);
        index.update({itemOf(a), itemOf(b)});
        index.waitForDone();
        auto sure = lineOf({{{"Kalman", 0.9f}, {"Kalmar", 0.1f}}, {{"filter", 1.0f}}});
        auto unsure = lineOf({{{"Kolmar", 0.7f}, {"Kalman", 0.3f}}});
        index.inkText().put(a, inkDoc(a, {{}, {{1, {50, 100}, sure}}}));
        index.inkText().put(b, inkDoc(b, {{{2, {50, 100}, unsure}}}));
        // Plain search: both found; b only through a reading the recogniser was unsure of: after a
        auto hits = index.search(QStringLiteral("kalman"));
        ASSERT_EQ(hits.size(), 2u);
        EXPECT_EQ(hits[0].file, a);
        EXPECT_FALSE(hits[0].fuzzyOnly);
        EXPECT_EQ(hits[0].firstPage, 1);
        ASSERT_EQ(hits[0].pageHits.size(), 1u);
        EXPECT_EQ(hits[0].snippet, QStringLiteral("Kalman filter"));
        EXPECT_EQ(hits[1].file, b);
        EXPECT_TRUE(hits[1].fuzzyOnly);
        // A typo with the fuzzy search off
        EXPECT_EQ(index.search(QStringLiteral("filtre")).size(), 1u);
        // The fuzzy search: its expression with the handwriting's terms
        hits = index.search(FuzzyQuery(QStringLiteral("klmn filter")));
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].file, a);
        EXPECT_EQ(hits[0].count, 2);
        hits = index.search(FuzzyQuery(QStringLiteral("kalman !filter")));
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].file, b);
        EXPECT_TRUE(hits[0].fuzzyOnly);
        index.flush();
    }
    // Written into the folder's cache, and read again by the next index
    EXPECT_TRUE(fs::exists(root / DocumentFiles::META_DIR / "ink-text.pack"));
    LibraryIndex again(root);
    again.update({itemOf(a), itemOf(b)});
    again.waitForDone();
    EXPECT_EQ(again.search(QStringLiteral("filter")).size(), 1u);
    ASSERT_TRUE(again.inkOf(a));
    EXPECT_EQ(again.inkOf(a)->recognizer, QStringLiteral("fake/1"));
}

TEST_F(InkLibraryTest, aChangedDocumentsHandwritingIsNotSearched) {
    const fs::path a = writeXopp(tmp, "a.xopp", 1);
    LibraryIndex index(root);
    index.update({itemOf(a)});
    index.waitForDone();
    index.inkText().put(a, inkDoc(a, {{{1, {0, 0}, lineOf({{{"turbine", 1.0f}}})}}}));
    EXPECT_EQ(index.search(QStringLiteral("turbine")).size(), 1u);
    // Written by another program: its handwriting is not that of the file any more
    writeXopp(tmp, "a.xopp", 2);
    index.update({itemOf(a)});
    index.waitForDone();
    EXPECT_EQ(index.inkOf(a), nullptr);
    EXPECT_EQ(index.search(QStringLiteral("turbine")).size(), 0u);
}

TEST_F(InkLibraryTest, handwritingFollowsAMove) {
    const fs::path a = writeXopp(tmp, "a.xopp", 1);
    LibraryIndex index(root);
    index.update({itemOf(a)});
    index.waitForDone();
    index.inkText().put(a, inkDoc(a, {{{1, {0, 0}, lineOf({{{"turbine", 1.0f}}})}}}));
    fs::create_directories(root / "sub");
    const fs::path moved = root / "sub" / "b.xopp";
    fs::rename(a, moved);
    index.moved({{a, moved}});
    index.update({itemOf(moved)});
    index.waitForDone();
    const auto hits = index.search(QStringLiteral("turbine"));
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].file, moved);
}

namespace {
/// A .xopp with handwriting: `lines` lines of 3 zigzag words per page, each line a little different
fs::path writeNotes(const QTemporaryDir& dir, const char* name, size_t pages, int lines, int shape) {
    Document doc(nullptr);
    for (size_t p = 0; p < pages; ++p) {
        auto page = std::make_shared<XojPage>(595, 842);
        page->setBackgroundType(PageType(PageTypeFormat::Plain));
        for (int l = 0; l < lines; ++l) {
            for (int w = 0; w < 3; ++w) {
                auto s = std::make_unique<Stroke>();
                for (int i = 0; i <= 8; ++i) {
                    s->addPoint(Point(50 + 60 * w + i * 3.0,
                                      100 + 40 * l + (i % 2 ? 10 : 0) + (i == 1 ? 0.3 * (shape + l + 10 * p) : 0)));
                }
                page->getSelectedLayer()->addElement(std::move(s));
            }
        }
        doc.addPage(std::move(page));
    }
    const fs::path file = fs::path(dir.filePath(name).toStdString());
    EXPECT_TRUE(DocumentSession::writeDocument(doc, file).ok);
    return file;
}

}  // namespace

TEST_F(InkLibraryTest, theLibrarysHandwritingIsReadOnMainsPower) {
    const fs::path a = writeNotes(tmp, "a.xopp", 2, 2, 1);
    const fs::path b = writeNotes(tmp, "b.xopp", 1, 1, 7);
    const fs::path c = writeXopp(tmp, "c.xopp", 1);  // (one stroke: a line of one word)
    LibraryIndex index(root);
    index.update({itemOf(a), itemOf(b), itemOf(c)});
    index.waitForDone();
    auto fake = std::make_shared<hwr::FakeRecognizer>();
    fake->setScript([](const hwr::LineInput&, size_t word) -> hwr::FakeRecognizer::Readings {
        return {{word == 2 ? QStringLiteral("turbine") : QStringLiteral("w%1").arg(word), 1.0f}};
    });
    hwr::InkRecognitionService service;
    service.setRecognizer(fake);
    bool mains = false;
    LibraryInkJob::setPowerSource([&] { return mains; });
    LibraryInkJob job(service);
    job.setIndex(&index);
    job.setEnabled(true);
    QCoreApplication::processEvents();
    EXPECT_FALSE(job.running());  // on battery: nothing
    EXPECT_EQ(fake->calls(), 0);
    mains = true;
    job.check();
    ASSERT_TRUE(waitFor([&] { return !job.running() && index.inkOf(a) && index.inkOf(b) && index.inkOf(c); }, 10000));
    EXPECT_EQ(fake->calls(), 6);  // (2 x 2 lines, 1, 1)
    EXPECT_TRUE(index.inkOf(a)->complete);
    EXPECT_EQ(index.inkOf(a)->pages.size(), 2u);
    const auto hits = index.search(QStringLiteral("turbine"));
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].file, a);  // (4 hits before 1)
    EXPECT_EQ(hits[0].count, 4);
    // Nothing is read twice
    job.check();
    QCoreApplication::processEvents();
    EXPECT_FALSE(job.running());
    EXPECT_EQ(fake->calls(), 6);
    LibraryInkJob::setPowerSource({});
}

TEST_F(InkLibraryTest, aSharedZipUnpackedWithOtherTimesIsNotReadAgain) {
    // (qt/docs/features/library.md, "Sharing a folder or the library": the readings go along, and survive any unzip)
    fs::create_directories(root / "Lib");
    const fs::path a = writeNotes(tmp, "Lib/a.xopp", 2, 2, 1);
    const fs::path b = writeNotes(tmp, "Lib/b.xopp", 1, 1, 7);
    auto fake = std::make_shared<hwr::FakeRecognizer>();
    fake->setScript([](const hwr::LineInput&, size_t word) -> hwr::FakeRecognizer::Readings {
        return {{word == 2 ? QStringLiteral("turbine") : QStringLiteral("w%1").arg(word), 1.0f}};
    });
    hwr::InkRecognitionService service;
    service.setRecognizer(fake);
    LibraryInkJob::setPowerSource([] { return true; });
    fs::path zip;
    {
        LibraryIndex index(root / "Lib");
        index.update({itemOf(a), itemOf(b)});
        index.waitForDone();
        LibraryInkJob job(service);
        job.setIndex(&index);
        job.setEnabled(true);
        job.check();
        ASSERT_TRUE(waitFor([&] { return !job.running() && index.inkOf(a) && index.inkOf(b); }, 10000));
        ASSERT_EQ(fake->calls(), 5);
        job.setIndex(nullptr);
        index.update({itemOf(a), itemOf(b)});  // (the content hashes, after the documents)
        index.waitForDone();
        zip = root / "Lib.zip";
        std::string error;
        const auto plan = LibraryShare::plan(root / "Lib", root / "Lib", "Lib", zip, {}, &index, error);
        ASSERT_TRUE(error.empty()) << error;
        std::atomic<bool> cancel{false};
        const auto s = LibraryShare::run(plan, cancel);
        ASSERT_TRUE(s.error.empty()) << s.error;
        EXPECT_EQ(s.readings, 2);
    }
    // Unpacked by another app, which gives the files other times (Explorer: local time, two seconds)
    const fs::path recv = root / "Recv";
    std::atomic<bool> cancel{false};
    const auto r = LibraryUnzip::unpack(zip, recv / "Inbox", CacheLocation(recv), {}, cancel);
    ASSERT_TRUE(r.ok) << r.error;
    for (const fs::path& f: {r.folder / "a.xopp", r.folder / "b.xopp"}) {
        QFile file(QString::fromStdString(f.string()));
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-3600 * 5 + 1), QFileDevice::FileModificationTime);
    }
    const int before = fake->calls();
    LibraryIndex received(recv);
    received.update({itemOf(r.folder / "a.xopp"), itemOf(r.folder / "b.xopp")});
    received.waitForDone();
    EXPECT_EQ(received.entriesAdopted(), 2);
    EXPECT_EQ(received.documentsRead(), 0);
    EXPECT_TRUE(received.inkOf(r.folder / "a.xopp"));
    EXPECT_EQ(received.search(QStringLiteral("turbine")).size(), 2u);
    LibraryInkJob job(service);
    job.setIndex(&received);
    job.setEnabled(true);
    job.check();
    QCoreApplication::processEvents();
    EXPECT_FALSE(job.running()) << "nothing to read";
    EXPECT_EQ(fake->calls(), before) << "no line is recognised again";
    // Another content (same name): read again
    {
        QTemporaryDir other;
        const fs::path changed = writeNotes(other, "a.xopp", 1, 1, 30);
        fs::copy_file(changed, r.folder / "a.xopp", fs::copy_options::overwrite_existing);
    }
    received.update({itemOf(r.folder / "a.xopp"), itemOf(r.folder / "b.xopp")});
    received.waitForDone();
    EXPECT_EQ(received.documentsRead(), 1);
    job.check();
    ASSERT_TRUE(waitFor([&] { return !job.running() && received.inkOf(r.folder / "a.xopp"); }, 10000));
    EXPECT_GT(fake->calls(), before) << "its new handwriting is read";
    LibraryInkJob::setPowerSource({});
}

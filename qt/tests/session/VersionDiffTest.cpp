/*
 * xournal-qt: the pages that differ between two documents (VersionDiff.h, comparing versions): nothing for equal
 * documents, the page written on, a page inserted or removed marks only itself, a page that moved is no change, and
 * every change has a page to show on both sides.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/VersionDiff.h"

using namespace xqt;
namespace vd = xqt::versiondiff;

namespace {
/// A page with `strokes` strokes (each a little lower than the one before)
PageRef pageWith(int strokes, double width = 595, double height = 842) {
    auto page = std::make_shared<XojPage>(width, height);
    for (int i = 0; i < strokes; ++i) {
        auto s = std::make_unique<Stroke>();
        s->setWidth(2);
        s->setColor(Color(0xff0000ccU));
        s->addPoint(Point(50, 50 + 20 * i, 1.0));
        s->addPoint(Point(250, 60 + 20 * i, 1.0));
        page->getLayers().front()->addElement(std::move(s));
    }
    return page;
}

/// Pages with 1, 2, 3, ... strokes: every page differs from the others
std::unique_ptr<Document> documentOf(int pages) {
    auto doc = std::make_unique<Document>(nullptr);
    for (int i = 0; i < pages; ++i) {
        doc->addPage(pageWith(i + 1));
    }
    return doc;
}

vd::Result diff(Document& older, Document& newer) {
    return vd::compare(vd::wholeOf(vd::sigsOf(older)), vd::wholeOf(vd::sigsOf(newer)));
}

std::vector<int> marked(const std::vector<char>& flags) {
    std::vector<int> out;
    for (size_t i = 0; i < flags.size(); ++i) {
        if (flags[i]) {
            out.push_back(static_cast<int>(i));
        }
    }
    return out;
}
}  // namespace

TEST(VersionDiff, equalDocumentsHaveNoChange) {
    auto a = documentOf(5), b = documentOf(5);
    const auto r = diff(*a, *b);
    EXPECT_TRUE(r.changes.empty());
    EXPECT_TRUE(marked(r.newerChanged).empty());
    EXPECT_TRUE(marked(r.olderChanged).empty());
    const auto sa = vd::sigsOf(*a);
    EXPECT_EQ(sa[2].elements.size(), 3u) << "each element has its own signature";
}

TEST(VersionDiff, aStrokeAddedMarksItsPage) {
    auto older = documentOf(6), newer = documentOf(6);
    auto s = std::make_unique<Stroke>();
    s->setWidth(1);
    s->addPoint(Point(10, 10, 1.0));
    s->addPoint(Point(20, 20, 1.0));
    newer->getPage(3)->getLayers().front()->addElement(std::move(s));
    const auto r = diff(*older, *newer);
    EXPECT_EQ(marked(r.newerChanged), std::vector<int>{3});
    EXPECT_EQ(marked(r.olderChanged), std::vector<int>{3});
    ASSERT_EQ(r.changes.size(), 1u);
    EXPECT_EQ(r.changes[0].newerPage, 3u);
    EXPECT_EQ(r.changes[0].olderPage, 3u);
    EXPECT_TRUE(r.changes[0].inNewer && r.changes[0].inOlder);
}

TEST(VersionDiff, aChangedColourOrSizeOrBackgroundCounts) {
    auto older = documentOf(3), newer = documentOf(3);
    newer->getPage(0)->setBackgroundColor(Color(0xffeeeeeeU));
    newer->getPage(2)->setSize(842, 595);
    EXPECT_EQ(marked(diff(*older, *newer).newerChanged), (std::vector<int>{0, 2}));
}

TEST(VersionDiff, anInsertedPageMarksOnlyItself) {
    auto older = documentOf(8), newer = documentOf(8);
    newer->insertPage(pageWith(40), 2);
    const auto r = diff(*older, *newer);
    EXPECT_EQ(marked(r.newerChanged), std::vector<int>{2});
    EXPECT_TRUE(marked(r.olderChanged).empty()) << "the pages after it are not changed, only moved on";
    ASSERT_EQ(r.changes.size(), 1u);
    EXPECT_FALSE(r.changes[0].inOlder) << "added";
    EXPECT_EQ(r.changes[0].newerPage, 2u);
    EXPECT_EQ(r.changes[0].olderPage, 2u) << "shown where it would be: before the old page 3";
    EXPECT_EQ(r.added(), 1u);
}

TEST(VersionDiff, aRemovedPageAndAChangedOneAfterIt) {
    auto older = documentOf(8), newer = documentOf(8);
    newer->deletePage(1);
    newer->getPage(5)->getLayers().front()->clearNoFree();  // (old page 7)
    const auto r = diff(*older, *newer);
    EXPECT_EQ(marked(r.olderChanged), (std::vector<int>{1, 6}));
    EXPECT_EQ(marked(r.newerChanged), std::vector<int>{5});
    ASSERT_EQ(r.changes.size(), 2u);
    EXPECT_FALSE(r.changes[0].inNewer) << "removed";
    EXPECT_EQ(r.changes[0].olderPage, 1u);
    EXPECT_EQ(r.changes[0].newerPage, 1u) << "where it was";
    EXPECT_EQ(r.changes[1].newerPage, 5u);
    EXPECT_EQ(r.changes[1].olderPage, 6u) << "paired with its old page, not by number";
    EXPECT_EQ(r.removed(), 1u);
}

TEST(VersionDiff, aPageThatOnlyMovedIsNoChange) {
    auto older = documentOf(6), newer = documentOf(6);
    // Page 2 moved to the end
    auto moved = newer->getPage(1);
    newer->deletePage(1);
    newer->addPage(moved);
    const auto r = diff(*older, *newer);
    EXPECT_TRUE(r.changes.empty());
    EXPECT_TRUE(marked(r.newerChanged).empty());
    EXPECT_TRUE(marked(r.olderChanged).empty());
}

TEST(VersionDiff, pagesThatAreAllNewPairInOrder) {
    // Plain lists of signatures: everything replaced, one more page in the newer
    const auto r = vd::compare({1, 2, 3}, {7, 8, 9, 10});
    ASSERT_EQ(r.changes.size(), 4u);
    for (size_t k = 0; k < 3; ++k) {
        EXPECT_EQ(r.changes[k].newerPage, k);
        EXPECT_EQ(r.changes[k].olderPage, k);
    }
    EXPECT_FALSE(r.changes[3].inOlder);
    EXPECT_EQ(r.changes[3].olderPage, 2u) << "clamped to the last page there is";
    // An empty side
    const auto none = vd::compare({}, {5, 6});
    ASSERT_EQ(none.changes.size(), 2u);
    EXPECT_EQ(none.changes[1].olderPage, 0u);
}

// A saved version read back is the same as the document in memory: strokes drawn with pressure, a background colour
// (its alpha is not in the file) - the signatures are of what the file holds
TEST(VersionDiff, aDocumentSavedAndReadBackHasNoChange) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    auto doc = documentOf(3);
    auto s = std::make_unique<Stroke>();
    s->setWidth(1.37);
    s->setColor(Color(0xff123456U));
    s->addPoint(Point(10.123456, 20.987654, 0.333333));
    s->addPoint(Point(33.3333333, 44.4444444, 0.777777));
    doc->getPage(1)->getLayers().front()->addElement(std::move(s));
    doc->getPage(2)->setBackgroundColor(Color(0x00fafafaU));
    const fs::path file = fs::path(dir.path().toStdString()) / "v.xopp";
    const auto saved = DocumentSession::writeDocument(*doc, file);
    ASSERT_TRUE(saved.ok) << saved.error;
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document) << loaded.error;
    const auto r = diff(*loaded.document, *doc);
    EXPECT_TRUE(r.changes.empty()) << r.changes.size() << " changes";
}

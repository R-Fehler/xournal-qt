/*
 * xournal-qt: two views scrolled together (ScrollLock, the reference view's locked scrolling): by page with the offset
 * they had when locked, by the place within the page relative to its size, zoom relative to the fitting width; no
 * loops; unlocked, each on its own again.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QCoreApplication>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"

#include "DocumentLayout.h"
#include "ScrollLock.h"
#include "ViewController.h"

using namespace xqt;

namespace {
/// A document of `count` pages of one size, its layout and a view of it
struct Side {
    Side(int count, QSizeF page, QSizeF view): doc(nullptr) {
        for (int i = 0; i < count; ++i) {
            auto p = std::make_shared<XojPage>(page.width(), page.height());
            doc.addPage(p);
            refs.push_back(p);
        }
        layout.update(doc, refs, {});
        vc = std::make_unique<ViewController>(&layout);
        vc->setViewSize(view);
        vc->fitWidth(0);
    }
    QPointF middle() const { return QPointF(vc->viewSize().width() / 2, vc->viewSize().height() / 2); }
    ViewController::Place place() const { return *vc->placeAt(ScrollLock::anchorOf(*vc)); }
    double relativeZoom() const { return vc->zoom() / vc->fitWidthZoom(place().page); }
    Document doc;
    std::vector<PageRef> refs;
    DocumentLayout layout;
    std::unique_ptr<ViewController> vc;
};

constexpr QSizeF A4(595.27559, 841.88976);

void expectSamePlace(const Side& a, const Side& b, int offset, const char* what) {
    const auto pa = a.place(), pb = b.place();
    EXPECT_EQ(static_cast<int>(pb.page), static_cast<int>(pa.page) + offset) << what;
    EXPECT_NEAR(pb.relative.y(), pa.relative.y(), 1e-6) << what;
    EXPECT_NEAR(pb.relative.x(), pa.relative.x(), 1e-6) << what;
}
}  // namespace

TEST(ScrollLock, scrollingOneMovesTheOtherWithTheOffsetOfWhenItWasLocked) {
    Side a(12, A4, QSizeF(500, 700)), b(12, A4, QSizeF(500, 700));
    a.vc->scrollToPage(4);
    b.vc->scrollToPage(1);
    a.vc->panBy(QPointF(0, -100));  // (into page 5 a little)
    ScrollLock lock;
    QSignalSpy lockedChanged(&lock, &ScrollLock::lockedChanged);
    lock.lock(a.vc.get(), b.vc.get());
    EXPECT_TRUE(lock.locked());
    EXPECT_EQ(lockedChanged.count(), 1);
    EXPECT_EQ(lock.pageOffset(), -3) << "page 5 here beside page 2 there";
    expectSamePlace(a, b, -3, "b comes to a's place within the page when locked");

    // Scrolling either side moves the other, a page jump too
    a.vc->panBy(QPointF(0, -900));
    expectSamePlace(a, b, -3, "a scrolled");
    b.vc->panBy(QPointF(0, 450));
    expectSamePlace(a, b, -3, "b scrolled");
    a.vc->scrollToPage(9);
    expectSamePlace(a, b, -3, "a went to page 10");

    // Unlocked: each on its own again
    lock.unlock();
    EXPECT_FALSE(lock.locked());
    EXPECT_EQ(lockedChanged.count(), 2);
    const QPointF bAt = b.vc->scrollPosition();
    a.vc->panBy(QPointF(0, 700));
    EXPECT_EQ(b.vc->scrollPosition(), bAt);
}

TEST(ScrollLock, pagesOfOtherSizesLineUpByTheirRelativePlace) {
    // Letter-sized slides beside A4 pages, in halves of other widths
    Side a(6, A4, QSizeF(520, 700)), b(6, QSizeF(1024, 576), QSizeF(380, 700));
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());
    EXPECT_EQ(lock.pageOffset(), 0);
    for (int step = 0; step < 8; ++step) {
        a.vc->panBy(QPointF(0, -237));
        expectSamePlace(a, b, 0, "a scrolled");
    }
    // A page jump on the other side
    b.vc->scrollToPage(2);
    EXPECT_EQ(a.place().page, 2u);
    // Clamped at the end of the shorter document: the other one stays where it can be
    a.vc->scrollToPage(5);
    EXPECT_LE(b.place().page, 5u);
}

TEST(ScrollLock, noLoopAndNothingMovesTwice) {
    Side a(8, A4, QSizeF(500, 700)), b(8, A4, QSizeF(500, 700));
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());
    QSignalSpy aChanged(a.vc.get(), &ViewController::changed);
    QSignalSpy bChanged(b.vc.get(), &ViewController::changed);
    a.vc->panBy(QPointF(0, -300));
    EXPECT_EQ(aChanged.count(), 1);
    EXPECT_EQ(bChanged.count(), 1) << "moved once, and it did not move a back";
    b.vc->panBy(QPointF(0, -300));
    EXPECT_EQ(aChanged.count(), 2);
    EXPECT_EQ(bChanged.count(), 2);
    // Where it already is: not moved at all
    lock.lock(a.vc.get(), b.vc.get());
    EXPECT_EQ(bChanged.count(), 2);
}

TEST(ScrollLock, zoomFollowsRelativeToTheFittingWidth) {
    Side a(6, A4, QSizeF(600, 700)), b(6, QSizeF(1024, 576), QSizeF(400, 700));
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());  // (both fit their width)
    a.vc->zoomBy(1.5, a.middle());
    EXPECT_NEAR(b.relativeZoom(), a.relativeZoom(), 1e-6) << "the page is as much wider than its half on both sides";
    EXPECT_NEAR(a.relativeZoom(), 1.5, 1e-6);
    expectSamePlace(a, b, 0, "and at the same place");
    b.vc->fitWidth();
    EXPECT_NEAR(a.relativeZoom(), 1.0, 1e-6) << "a fit on one side fits the other";
    // Another ratio when locked stays
    lock.unlock();
    b.vc->zoomBy(2, b.middle());
    lock.lock(a.vc.get(), b.vc.get());
    a.vc->zoomBy(1.25, a.middle());
    EXPECT_NEAR(b.relativeZoom() / a.relativeZoom(), 2.0, 1e-6);
}

TEST(ScrollLock, aViewWhoseSizeChangedFollowsAndDoesNotMoveTheOther) {
    Side a(8, A4, QSizeF(500, 700)), b(8, A4, QSizeF(500, 700));
    a.vc->scrollToPage(3);
    b.vc->scrollToPage(1);
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());
    ASSERT_EQ(lock.pageOffset(), -2);
    const QPointF aAt = a.vc->scrollPosition();
    b.vc->setViewSize(QSizeF(300, 700));  // (the divider)
    EXPECT_EQ(a.vc->scrollPosition(), aAt);
    expectSamePlace(a, b, -2, "b follows a after its new size");
    a.vc->setViewSize(QSizeF(700, 700));
    expectSamePlace(a, b, -2, "a follows b after its new size");
    a.vc->panBy(QPointF(0, -200));
    expectSamePlace(a, b, -2, "and goes on following");
}

TEST(ScrollLock, showPagesSetsANewPair) {
    Side a(10, A4, QSizeF(500, 700)), b(10, A4, QSizeF(500, 700));
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());
    lock.showPages(6, 4);  // (a page was removed before: the next change)
    EXPECT_EQ(lock.pageOffset(), -2);
    EXPECT_EQ(a.place().page, 6u);
    EXPECT_EQ(b.place().page, 4u);
    a.vc->panBy(QPointF(0, -400));
    expectSamePlace(a, b, -2, "the new pair stays");
}

TEST(ScrollLock, aViewThatGoesUnlocks) {
    Side a(4, A4, QSizeF(500, 700));
    auto b = std::make_unique<Side>(4, A4, QSizeF(500, 700));
    ScrollLock lock;
    lock.lock(a.vc.get(), b->vc.get());
    b.reset();
    EXPECT_FALSE(lock.locked());
    a.vc->panBy(QPointF(0, -300));  // (nothing to follow; no crash)
}

// A reference locked before it was shown (a remembered pair opened again): it opens where it was sent, the notes stay
TEST(ScrollLock, aViewLockedBeforeItHadASizeOpensWhereItWasSent) {
    Side a(10, A4, QSizeF(500, 700));
    a.vc->scrollToPage(2);
    Side b(10, A4, QSizeF());
    b.vc->scrollToPage(6);  // (before it has a size: when it gets one)
    ScrollLock lock;
    lock.lock(a.vc.get(), b.vc.get());
    const QPointF aAt = a.vc->scrollPosition();
    b.vc->setViewSize(QSizeF(400, 700));
    QCoreApplication::processEvents();
    EXPECT_EQ(a.vc->scrollPosition(), aAt) << "the notes did not move";
    EXPECT_EQ(b.place().page, 6u);
    EXPECT_EQ(lock.pageOffset(), 4) << "the pair is taken from where it opened";
    a.vc->panBy(QPointF(0, -300));
    expectSamePlace(a, b, 4, "and kept from then on");
}

/*
 * xournal-qt: text search in a document (PDF text and text elements), its text index and its matcher.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <fstream>
#include <memory>

#include <QElapsedTimer>
#include <QSignalSpy>
#include <iostream>
#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/TextMatch.h"
#include "session/Vocabulary.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "../SearchHits.h"
#include "MdBox.h"

#include "config-test.h"

using namespace xqt;
using xqt::test::placedHits;
using xqt::test::waitForCounts;

namespace {
class DocumentSearchTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    }
    void TearDown() override {
        DocumentTextIndex::setSeeder({});
        DocumentTextIndex::setWordsInBackground(false);
    }
    std::unique_ptr<DocumentSession> open(const fs::path& file) {
        auto r = DocumentSession::loadFile(file);
        EXPECT_TRUE(r.document) << r.error;
        return std::make_unique<DocumentSession>(*app, std::move(r.document));
    }
    std::unique_ptr<DocumentSession> open(const char8_t* fixture) { return open(GET_TESTFILE(fixture)); }
    static void search(DocumentSession& s, const QString& text, bool jump = true) {
        s.search().setQuery(text, jump);
        ASSERT_TRUE(waitForCounts(s.search()));
    }
    static bool waitFor(const std::function<bool()>& done, int ms = 5000) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        }
        return done();
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};

/// A PDF whose text runs across line breaks: per page "every page" twice (once across a line break), a word broken
/// at a line end ("hyphen-" / "ated"), and "ﬁ" ligatures. `pages` pages.
fs::path makeLinesPdf(const QTemporaryDir& dir, int pages) {
    const fs::path file = fs::path(dir.filePath("lines.pdf").toStdString());
    cairo_surface_t* surface = cairo_pdf_surface_create(file.c_str(), 595, 842);
    cairo_t* cr = cairo_create(surface);
    cairo_select_font_face(cr, "DejaVu Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    const char* lines[] = {"The search index keeps the text of every",
                           "page. A phrase across a line break is found,",
                           "and so is a word broken at the end of a hyphen-",
                           "ated line. The \xef\xac\x81rst \xef\xac\x81nding is marked.",
                           "Again every page, all on one line.",
                           "PAGE NUMBER"};
    for (int p = 0; p < pages; ++p) {
        double y = 80;
        for (const char* line: lines) {
            cairo_move_to(cr, 60, y);
            cairo_show_text(cr, line);
            y += 18;
        }
        cairo_move_to(cr, 60, y + 20);
        cairo_show_text(cr, ("page " + std::to_string(p + 1)).c_str());
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return file;
}
}  // namespace

// --- the matcher -----------------------------------------------------------------------------------------------

TEST(TextMatch, matchesAsTheSearchPromises) {
    using textmatch::count;
    using textmatch::prepare;
    EXPECT_EQ(count(u"Page 7, page 8, PAGE 9", prepare("page")), 3) << "case-insensitive";
    EXPECT_EQ(count(u"every page", prepare("  every\n page ")), 1) << "whitespace runs are one space";
    EXPECT_EQ(count(u"aaaa", prepare("aa")), 2) << "not overlapping";
    EXPECT_EQ(count(u"the ﬁrst ﬁnding", prepare("first")), 1) << "a ligature matches its letters";
    EXPECT_EQ(count(u"the ﬁrst", prepare("ﬁrst")), 1);
    EXPECT_EQ(count(u"a hyphen- ated word", prepare("hyphenated")), 1) << "broken at a line end";
    EXPECT_EQ(count(u"a hyphen- ated word", prepare("hyphen-ated")), 1);
    EXPECT_EQ(count(u"a hyphen- ated word", prepare("hyphen- ated")), 1) << "also as it is";
    EXPECT_EQ(count(u"pre- and post-processing", prepare("pre- and")), 1);
    EXPECT_EQ(count(u"a - b", prepare("ab")), 0) << "a dash is no line break";
    EXPECT_EQ(count(u"New- York", prepare("NewYork")), 0) << "a capital after it: no broken word";
    EXPECT_EQ(count(u"soft­hyphen", prepare("softhyphen")), 1);
    EXPECT_EQ(count(u"one box\nanother box", prepare("box another")), 0) << "not across the pieces of a page";
    EXPECT_EQ(count(u"Größe GRÖSSE", prepare("größe")), 1);
    EXPECT_EQ(count(u"", prepare("x")), 0);
    EXPECT_EQ(count(u"x", prepare("")), 0);

    const auto spans = textmatch::find(u"x hyphen- ated y", prepare("hyphenated"));
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].start, 2);
    EXPECT_EQ(spans[0].end, 14) << "from its first to its last letter";

    const auto s = textmatch::simplify(u"  two\n\n words ");
    EXPECT_EQ(s.text, QStringLiteral("two words"));
    EXPECT_EQ(s.text, QStringLiteral("  two\n\n words ").simplified());
    EXPECT_EQ(s.origin[0], 2);
    EXPECT_EQ(s.origin[4], 8) << "'w'";
    EXPECT_EQ(s.origin[static_cast<size_t>(s.text.size())], 13) << "the end of the last character";
}

// --- the search ------------------------------------------------------------------------------------------------

TEST_F(DocumentSearchTest, findsTextElementsOnAllPages) {
    auto s = open(u8"load/pages.xopp");
    search(*s, "p1");  // p1, p10, p11
    const auto hits = placedHits(s->search());
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(s->search().hitCount(), 3);
    EXPECT_EQ(hits[0].page, 0u);
    EXPECT_EQ(hits[1].page, 9u);
    EXPECT_EQ(hits[2].page, 10u);
    EXPECT_GT(hits[0].rect.width(), 0);
    EXPECT_EQ(s->search().currentHit(), 0);

    search(*s, "P7");  // case-insensitive
    ASSERT_EQ(s->search().hitCount(), 1);
    EXPECT_EQ(s->search().pages()[0].page, 6u);
    search(*s, "nothing like this");
    EXPECT_EQ(s->search().hitCount(), 0);
    EXPECT_EQ(s->search().currentHit(), -1);
    search(*s, "");
    EXPECT_EQ(s->search().hitCount(), 0);
    EXPECT_TRUE(s->search().pages().empty());
}

TEST_F(DocumentSearchTest, findsPdfText) {
    auto s = open(u8"packaged_xopp/pdfBackground/old.xopp");
    search(*s, "xournal");
    ASSERT_GE(s->search().hitCount(), 1);
    EXPECT_EQ(s->search().pages()[0].page, 0u);
    search(*s, "Page 2");
    ASSERT_GE(s->search().hitCount(), 1);
    EXPECT_EQ(s->search().pages()[0].page, 1u);
    const auto hits = placedHits(s->search());
    ASSERT_EQ(static_cast<int>(hits.size()), s->search().hitCount());
    // Where poppler's own search finds it
    const auto poppler = DocumentSearch::findOnPage(*s->getDocument(), 1, "Page 2");
    ASSERT_FALSE(poppler.empty());
    EXPECT_NEAR(hits[0].rect.left(), poppler[0].left(), 1.0);
    EXPECT_NEAR(hits[0].rect.top(), poppler[0].top(), 2.0);
    EXPECT_NEAR(hits[0].rect.right(), poppler[0].right(), 1.0);
    EXPECT_NEAR(hits[0].rect.bottom(), poppler[0].bottom(), 2.0);
}

TEST_F(DocumentSearchTest, fuzzyQueryCountsAndMarksItsTerms) {
    auto s = open(u8"load/pages.xopp");  // page i: "p<i+1>"
    auto fuzzy = [&](const char* query) {
        s->search().setQuery(QString::fromUtf8(query), false, true);
        EXPECT_TRUE(waitForCounts(s->search()));
        std::vector<size_t> pages;
        for (const auto& h: s->search().pages()) {
            pages.push_back(h.page);
        }
        return pages;
    };
    using P = std::vector<size_t>;
    EXPECT_EQ(fuzzy("p1 | p5"), (P{0, 4, 9, 10})) << "the hits of every term";
    EXPECT_EQ(s->search().hitCount(), 4);
    EXPECT_EQ(static_cast<int>(placedHits(s->search()).size()), 4) << "counted and marked alike";
    EXPECT_EQ(s->search().countCorrections(), 0);
    EXPECT_EQ(fuzzy("'p1'"), (P{0})) << "a whole word";
    EXPECT_EQ(fuzzy("^p1"), (P{0, 9, 10})) << "a word that starts with it";
    EXPECT_EQ(fuzzy("p1$"), (P{0})) << "a word that ends with it";
    EXPECT_EQ(fuzzy("p1 !p10"), (P{0, 9, 10})) << "negated terms are not marked";
    EXPECT_FALSE(s->search().matches(u"pages")) << "p10 is in the document";
    EXPECT_EQ(fuzzy("p1 !p12"), (P{0, 9, 10}));
    EXPECT_TRUE(s->search().matches(u"pages"));
    EXPECT_TRUE(s->search().hint().isEmpty());

    // The whole expression: over the document; on a page, where its terms are on that page (or in the name)
    fuzzy("p2 p3");
    EXPECT_TRUE(s->search().matches(u"pages")) << "both are in the document";
    auto matching = [&](const char16_t* name) {
        P pages;
        for (const auto& h: s->search().matchingPages(name)) {
            pages.push_back(h.page);
        }
        return pages;
    };
    EXPECT_EQ(matching(u"pages"), (P{1, 2})) << "on no page together: all pages with hits";
    fuzzy("p1 pages");
    EXPECT_TRUE(s->search().matches(u"pages")) << "a term found in the name";
    EXPECT_FALSE(s->search().matches(u"other"));
    EXPECT_EQ(matching(u"pages"), (P{0, 9, 10}));
    fuzzy("(p1 | p2) !p3");
    EXPECT_EQ(matching(u"x"), (P{0, 1, 9, 10}));
    fuzzy("p4 | (p5 xyz)");
    EXPECT_TRUE(s->search().matches(u"x"));
    EXPECT_EQ(matching(u"x"), (P{3})) << "p5 is on a page, but not with xyz";

    // Not valid: plain text, with a hint
    EXPECT_EQ(fuzzy("(p1"), P{});
    EXPECT_FALSE(s->search().hint().isEmpty());
    EXPECT_FALSE(s->search().matches(u"pages"));
    // The same text without the syntax: plain
    search(*s, "p1 | p5");
    EXPECT_EQ(s->search().hitCount(), 0);
    EXPECT_FALSE(s->search().fuzzy());
    s->search().setQuery("p1 | p5", false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), 4) << "the same text, now fuzzy";
}

TEST_F(DocumentSearchTest, fuzzyQueryInPdfText) {
    auto s = open(makeLinesPdf(tmp, 2));
    s->search().setQuery(QStringLiteral("'every' ^hyphen"), false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    // Per page: "every" twice (once across a line break), "hyphenated" once (broken at a line end)
    EXPECT_EQ(s->search().countOn(0), 3);
    EXPECT_EQ(static_cast<int>(placedHits(s->search()).size()), s->search().hitCount());
    EXPECT_EQ(s->search().countCorrections(), 0);
    s->search().setQuery(QStringLiteral("^ated"), false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), 0) << "a broken word is one word";
    s->search().setQuery(QStringLiteral("hyphen$"), false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), 0);
    s->search().setQuery(QStringLiteral("'hyphenated'"), false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), 2);
}

TEST_F(DocumentSearchTest, currentHitStartsAtTheCurrentPageAndCycles) {
    auto s = open(u8"load/pages.xopp");
    s->setCurrentPageNo(5);
    QSignalSpy scrolls(s.get(), &DocumentSession::scrollToRectRequested);
    search(*s, "p1");
    EXPECT_EQ(s->search().currentHit(), 1) << "first hit from page 6 on: p10";
    EXPECT_EQ(s->getCurrentPageNo(), 9u);
    ASSERT_TRUE(waitFor([&] { return scrolls.count() >= 1; }));
    EXPECT_EQ(scrolls.last().at(0).toULongLong(), 9u);

    s->search().next();
    EXPECT_EQ(s->search().currentHit(), 2);
    s->search().next();
    EXPECT_EQ(s->search().currentHit(), 0) << "wraps around";
    s->search().previous();
    EXPECT_EQ(s->search().currentHit(), 2);

    // Search without jumping (tab overview), then jump from the current page later.
    search(*s, "p5", false);
    EXPECT_EQ(s->search().currentHit(), -1);
    s->setCurrentPageNo(0);
    s->search().jumpToFirstFromCurrentPage();
    EXPECT_EQ(s->search().currentHit(), 0);
    EXPECT_EQ(s->getCurrentPageNo(), 4u);
}

TEST_F(DocumentSearchTest, editsAreSearchedAgain) {
    auto s = open(u8"load/pages.xopp");
    search(*s, "p1");
    ASSERT_EQ(s->search().hitCount(), 3);

    // A new text element through the undo machinery (like the text tool).
    auto page = s->getDocument()->getPage(3);
    auto text = std::make_unique<Text>();
    text->setText("p1 again");
    text->move(50, 300);
    const Text* raw = text.get();
    Layer* layer = page->getSelectedLayer();
    s->getDocument()->lock();
    layer->addElement(std::move(text));
    s->getDocument()->unlock();
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    ASSERT_TRUE(waitFor([&] { return s->search().hitCount() == 4; }));
    EXPECT_EQ(s->search().pages()[1].page, 3u);
    EXPECT_EQ(s->search().textIndex().pdfPagesRead(), 0) << "no PDF read for it";

    // Undo takes it away again
    s->getUndoRedoHandler()->undo();
    ASSERT_TRUE(waitFor([&] { return s->search().hitCount() == 3; }));
}

// Pages that come, go or move take their text along: nothing is read again, the counts follow at once.
TEST_F(DocumentSearchTest, hitsFollowPagesThatComeGoAndMove) {
    auto s = open(u8"load/pages.xopp");  // "p1" .. "p11" on pages 0 .. 10
    search(*s, "p1", false);
    const auto pagesWithHits = [&] {
        std::vector<size_t> out;
        for (const auto& h: s->search().pages()) {
            out.push_back(h.page);
        }
        return out;
    };
    ASSERT_EQ(pagesWithHits(), (std::vector<size_t>{0, 9, 10}));

    ASSERT_TRUE(s->deletePages({0}));
    EXPECT_EQ(pagesWithHits(), (std::vector<size_t>{8, 9})) << "at once";
    s->getPageUndoRedoHandler()->undo();
    EXPECT_EQ(pagesWithHits(), (std::vector<size_t>{0, 9, 10})) << "undone";

    ASSERT_TRUE(s->movePages({10}, 0));  // p11 first
    EXPECT_EQ(pagesWithHits(), (std::vector<size_t>{0, 1, 10}));
    const auto hits = placedHits(s->search());
    ASSERT_EQ(hits.size(), 3u);
    EXPECT_EQ(s->search().countCorrections(), 0);
    s->getPageUndoRedoHandler()->undo();
    EXPECT_EQ(pagesWithHits(), (std::vector<size_t>{0, 9, 10}));
}

// The count comes from the text index, the marks from where the characters are drawn: both match the same text the
// same way, also across line breaks, at a hyphen at the end of a line and with ligatures.
TEST_F(DocumentSearchTest, countsAndMarksAgreeAcrossLineBreaks) {
    const int pages = 6;
    auto s = open(makeLinesPdf(tmp, pages));
    struct Case {
        const char* query;
        int perPage;
    };
    for (const Case& c: {Case{"every page", 2}, Case{"hyphenated", 1}, Case{"first", 1}, Case{"finding", 1},
                         Case{"page", 4}, Case{"a line break is", 1}}) {
        search(*s, c.query, false);
        EXPECT_EQ(s->search().hitCount(), pages * c.perPage) << c.query;
        const auto hits = placedHits(s->search());
        EXPECT_EQ(static_cast<int>(hits.size()), pages * c.perPage) << c.query << ": marked as counted";
        for (const auto& h: hits) {
            EXPECT_FALSE(h.rect.isEmpty()) << c.query;
            EXPECT_TRUE(QRectF(0, 0, 595, 842).contains(h.rect)) << c.query;
            EXPECT_GT(h.rect.top(), 60) << c.query << ": the top of the page is at 0 (not the bottom)";
            EXPECT_LT(h.rect.top(), 250) << c.query;
        }
    }
    EXPECT_EQ(s->search().countCorrections(), 0) << "no page was marked differently than counted";

    // A hit across a line break is marked on both lines
    search(*s, "every page", false);
    const auto places = test::placesOn(s->search(), 0);
    ASSERT_EQ(places.size(), 2u);
    const bool acrossLines = !places[0].more.isNull() || !places[1].more.isNull();
    EXPECT_TRUE(acrossLines) << "one of them is on two lines";
    EXPECT_EQ(s->search().textIndex().pdfPagesRead(), pages) << "each page's text read once";
}

// Fuzzy terms match whole words (WordMatch.h), counted from the pages' vocabularies and marked in their text: the
// count and the marks agree, and the whole word is marked.
TEST_F(DocumentSearchTest, fuzzyWordsAreCountedAndMarkedWhole) {
    const int pages = 3;
    auto s = open(makeLinesPdf(tmp, pages));
    // A text element on the first page, edited in memory (unsaved): its words are searched too
    auto text = std::make_unique<Text>();
    text->setText("Turbine blades, two turbines and a tur- bine");
    text->move(60, 400);
    const Text* raw = text.get();
    auto page = s->getDocument()->getPage(0);
    Layer* layer = page->getSelectedLayer();
    s->getDocument()->lock();
    layer->addElement(std::move(text));
    s->getDocument()->unlock();
    s->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));

    struct Case {
        const char* query;
        int onFirst;   ///< hits on the first page
        int perPage;   ///< on each other page
    };
    for (const Case& c: {Case{"evry", 2, 2},        // every (letters left out)
                         Case{"hyphnated", 1, 1},   // hyphen- ated: one word
                         Case{"frst", 1, 1},        // ﬁrst: a ligature
                         Case{"pge", 4, 4},         // page, page, PAGE, page
                         Case{"brokne", 1, 1},      // broken (swapped letters)
                         Case{"tbine", 3, 0},       // the text element: Turbine, turbines, tur- bine (read as a
                                                    // word broken at a line end)
                         Case{"evry | tbine", 5, 2},
                         Case{"evry 'the", 2 + 4, 2 + 4},  // with a substring: "the" in The, the, the, The
                         Case{"tb", 0, 0}}) {         // too short: a substring, as before
        s->search().setQuery(QString::fromUtf8(c.query), false, true);
        ASSERT_TRUE(waitForCounts(s->search())) << c.query;
        EXPECT_EQ(s->search().countOn(0), c.onFirst) << c.query;
        EXPECT_EQ(s->search().countOn(1), c.perPage) << c.query;
        const auto hits = placedHits(s->search());
        EXPECT_EQ(static_cast<int>(hits.size()), s->search().hitCount()) << c.query << ": marked as counted";
    }
    EXPECT_EQ(s->search().countCorrections(), 0) << "no page was marked differently than counted";

    // The whole word is marked: as the plain search marks the word itself
    auto rects = [&](const char* query, bool fuzzy) {
        s->search().setQuery(QString::fromUtf8(query), false, fuzzy);
        std::vector<QRectF> out;
        for (const auto& h: placedHits(s->search())) {
            out.push_back(h.rect);
        }
        return out;
    };
    const auto fuzzyEvery = rects("evry", true);
    ASSERT_EQ(fuzzyEvery.size(), static_cast<size_t>(2 * pages));
    EXPECT_EQ(fuzzyEvery, rects("every", false));
    EXPECT_EQ(rects("hyphnated", true), rects("hyphenated", false)) << "on both lines";
    EXPECT_EQ(rects("frst", true), rects("first", false));

    // An edit changes the words of its page: counted again
    s->search().setQuery(QStringLiteral("tbine"), false, true);
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), 3);
    s->getUndoRedoHandler()->undo();
    ASSERT_TRUE(waitFor([&] { return s->search().hitCount() == 0; }));
}

// With the fuzzy search on, the vocabularies of the PDF text are made on the text index's worker once the text is
// known (read, or already known when it is turned on): the first fuzzy search of a long document finds them made
// instead of making them on the UI thread (about 160 ms for 1,300 pages). Off, nothing is made before a fuzzy search.
TEST_F(DocumentSearchTest, vocabulariesAreMadeInTheBackgroundWhenTheFuzzySearchIsOn) {
    const int pages = 12;
    const fs::path pdf = makeLinesPdf(tmp, pages);
    auto off = open(pdf);
    DocumentTextIndex& offIndex = off->search().textIndex();
    offIndex.start();
    ASSERT_TRUE(waitFor([&] { return offIndex.complete(); }));
    waitFor([] { return false; }, 200);
    EXPECT_EQ(offIndex.pdfPagesWithWords(), 0u) << "off: not made";

    DocumentTextIndex::setWordsInBackground(true);
    ASSERT_TRUE(waitFor([&] { return offIndex.pdfPagesWithWords() == static_cast<size_t>(pages); }))
            << "turned on: made for the text known";
    auto on = open(pdf);
    DocumentTextIndex& index = on->search().textIndex();
    index.start();
    ASSERT_TRUE(waitFor([&] { return index.complete() && index.pdfPagesWithWords() == static_cast<size_t>(pages); }))
            << "on: made once the text is read";
    const size_t bytes = index.vocabularyBytes();
    on->search().setQuery(QStringLiteral("evry"), false, true);
    ASSERT_TRUE(waitForCounts(on->search()));
    EXPECT_EQ(on->search().hitCount(), 2 * pages) << "counted from them";
    EXPECT_EQ(on->search().countCorrections(), 0);
    EXPECT_EQ(on->search().hitCount(), [&] {
        off->search().setQuery(QStringLiteral("evry"), false, true);
        return off->search().hitCount();
    }());
    EXPECT_LE(index.vocabularyBytes(), bytes + 4096) << "the PDF pages' were not made again (only the empty "
                                                        "vocabularies of the pages' text elements)";
}

// The PDF text is read in the background, from the current page outwards, and the counts grow meanwhile.
TEST_F(DocumentSearchTest, pdfTextIsReadInTheBackground) {
    const int pages = 40;
    auto s = open(makeLinesPdf(tmp, pages));
    s->setCurrentPageNo(30);
    QSignalSpy finished(&s->search(), &DocumentSearch::finished);
    s->search().setQuery("hyphenated");
    EXPECT_TRUE(s->search().isRunning()) << "nothing read yet";
    ASSERT_TRUE(waitFor([&] { return s->search().currentHit() >= 0; }));
    EXPECT_EQ(s->search().currentPage(), 30u) << "the first hit from the current page, read first";
    ASSERT_TRUE(waitForCounts(s->search()));
    EXPECT_EQ(s->search().hitCount(), pages);
    EXPECT_GE(finished.count(), 1);
    EXPECT_EQ(s->search().textIndex().pdfPagesRead(), pages);
    EXPECT_EQ(s->search().textIndex().pdfPagesSeeded(), 0);
}

// Opened again with the text the library index read before: the counts are there at once, nothing is read.
TEST_F(DocumentSearchTest, theTextIndexIsSeededFromTheLibrary) {
    const int pages = 8;
    const fs::path pdf = makeLinesPdf(tmp, pages);
    std::map<int, QString> known;
    {
        auto first = open(pdf);
        search(*first, "page");
        known = first->search().textIndex().pdfTexts();
    }
    ASSERT_EQ(known.size(), static_cast<size_t>(pages));
    fs::path asked;
    DocumentTextIndex::setSeeder([&](const fs::path& file) {
        asked = file;
        return known;
    });
    auto s = open(pdf);
    s->search().setQuery("hyphenated", false);
    EXPECT_EQ(asked, pdf);
    EXPECT_FALSE(s->search().isRunning()) << "all counts known at once";
    EXPECT_EQ(s->search().hitCount(), pages);
    EXPECT_EQ(s->search().textIndex().pdfPagesSeeded(), pages);
    EXPECT_EQ(s->search().textIndex().pdfPagesRead(), 0);
    const auto hits = placedHits(s->search());
    EXPECT_EQ(static_cast<int>(hits.size()), pages);
    EXPECT_EQ(s->search().countCorrections(), 0);
}

// XQT_BENCH_PDF=<pdf>: the search of an open document before (poppler's search of every page, on the UI thread,
// for every query) and now (the text index), and what the index costs.
TEST_F(DocumentSearchTest, benchSearch) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    const auto rss = [] {
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.rfind("VmRSS:", 0) == 0) {
                return std::stol(line.substr(6)) / 1024.0;  // MB
            }
        }
        return 0.0;
    };
    auto r = DocumentSession::loadFile(fs::path(pdf.toStdString()));
    ASSERT_TRUE(r.document) << r.error;
    DocumentSession s(*app, std::move(r.document));
    const size_t pages = s.getDocument()->getPageCount();
    const std::vector<QString> typed{"p", "pg", "pgf", "pgfk", "pgfke", "pgfkey", "pgfkeys"};

    // Before: every query searched every page with poppler, 8 ms of pages per event loop pass
    {
        const std::string query = "pgfkeys";
        QElapsedTimer t;
        t.start();
        qint64 first = -1, longestPass = 0;
        int hits = 0;
        for (size_t p = 0; p < pages;) {
            QElapsedTimer pass;
            pass.start();
            while (p < pages && pass.elapsed() < 8) {
                const int n = static_cast<int>(DocumentSearch::findOnPage(*s.getDocument(), p++, query).size());
                if (n > 0 && first < 0) {
                    first = t.elapsed();
                }
                hits += n;
            }
            longestPass = std::max(longestPass, pass.elapsed());
        }
        std::cout << "before: \"" << query << "\" in " << pages << " pages: first hit " << first << " ms, all "
                  << t.elapsed() << " ms (and again for each key typed), " << hits
                  << " hits, longest UI pass " << longestPass << " ms\n";
    }

    // Now: reading the text once, in the background
    const double before = rss();
    QElapsedTimer t;
    t.start();
    qint64 longestPass = 0;
    qint64 firstHit = -1;
    s.search().setQuery("pgfkeys", false);
    const qint64 setQuery = t.elapsed();
    int passes = 0;
    while (s.search().isRunning()) {
        QElapsedTimer pass;
        pass.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        longestPass = std::max(longestPass, pass.nsecsElapsed() / 1000);
        ++passes;
        if (firstHit < 0 && s.search().hitCount() > 0) {
            firstHit = t.elapsed();
        }
        QThread::msleep(1);
    }
    const qint64 all = t.elapsed();
    DocumentTextIndex& index = s.search().textIndex();
    std::cout << "now, not in a library (reading the text in the background): first hit " << firstHit
              << " ms, all counts " << all << " ms (" << index.pdfPagesRead() << " pages read), "
              << s.search().hitCount() << " hits on " << s.search().pages().size()
              << " pages, setQuery " << setQuery << " ms, longest UI pass " << longestPass << " µs (of " << passes
              << ")\n";
    std::cout << "index: text " << index.textBytes() / 1024 << " KiB; process memory +" << (rss() - before)
              << " MB while reading (poppler instance of the worker included)\n";

    // Typing: each key searches the whole index again (on the UI thread)
    for (const QString& q: typed) {
        QElapsedTimer k;
        k.start();
        s.search().setQuery(q, true);
        const double scan = k.nsecsElapsed() / 1e6;
        std::cout << "  typed \"" << q.toStdString() << "\": " << scan << " ms, " << s.search().hitCount()
                  << " hits on " << s.search().pages().size() << " pages\n";
    }
    // The same with the fuzzy search: fuzzy terms match words (the first one prepares the words of the pages)
    for (const QString& q: {QStringLiteral("pgfkeys"), QStringLiteral("pgfkeys"), QStringLiteral("pgfkyes"),
                            QStringLiteral("nde"), QStringLiteral("tikz !node"), QStringLiteral("pgfkeys | shdng")}) {
        QElapsedTimer k;
        k.start();
        s.search().setQuery(q, true, true);
        const double scan = k.nsecsElapsed() / 1e6;
        std::cout << "  fuzzy \"" << q.toStdString() << "\": " << scan << " ms, " << s.search().hitCount()
                  << " hits on " << s.search().pages().size() << " pages\n";
        s.search().setQuery(QString(), false);
    }
    std::cout << "  vocabularies of the pages " << index.vocabularyBytes() / 1024 << " KiB, dictionary "
              << words::dictionarySize() << " words, " << words::dictionaryBytes() / 1024 << " KiB\n";
    s.search().setQuery("pgfkeys", false);
    // Marks: the pages with hits, placed one after the other (the pages in view are a handful)
    QElapsedTimer m;
    m.start();
    int placed = 0;
    int pagesPlaced = 0;
    for (const auto& h: std::vector<DocumentSearch::PageHits>(s.search().pages())) {
        placed += static_cast<int>(test::placesOn(s.search(), h.page).size());
        if (++pagesPlaced == 100) {
            break;
        }
    }
    std::cout << "marks of " << pagesPlaced << " pages (the worker reads where their text is): " << m.elapsed() << " ms, " << placed << " hits, "
              << s.search().countCorrections() << " pages marked differently than counted; kept layouts "
              << index.layoutBytes() / 1024 << " KiB\n";
    EXPECT_EQ(s.search().countCorrections(), 0);
    // Many hits on every page (line breaks, hyphens, ligatures of the manual's text): the marks as counted, also of
    // fuzzy terms (words)
    for (const QString& q: {QStringLiteral("the"), QStringLiteral("fi"), QStringLiteral("node"), QStringLiteral("~nde"),
                            QStringLiteral("~pgfkyes | the")}) {
        const bool fuzzy = q.startsWith(u'~');
        s.search().setQuery(fuzzy ? q.mid(1) : q, false, fuzzy);
        int counted = 0, marked = 0, n = 0;
        for (const auto& h: std::vector<DocumentSearch::PageHits>(s.search().pages())) {
            counted += h.count;
            marked += static_cast<int>(test::placesOn(s.search(), h.page).size());
            if (++n == 150) {
                break;
            }
        }
        std::cout << "\"" << q.toStdString() << "\" on the first " << n << " pages with hits: " << counted
                  << " counted, " << marked << " marked\n";
        EXPECT_EQ(counted, marked) << q.toStdString();
    }
    EXPECT_EQ(s.search().countCorrections(), 0);

    // Opened again from a library that read it before
    const auto known = index.pdfTexts();
    DocumentTextIndex::setSeeder([&](const fs::path&) { return known; });
    auto r2 = DocumentSession::loadFile(fs::path(pdf.toStdString()));
    DocumentSession again(*app, std::move(r2.document));
    QElapsedTimer seeded;
    seeded.start();
    again.search().setQuery("pgfkeys", false);
    std::cout << "now, in a library: all counts " << seeded.elapsed() << " ms (running: " << again.search().isRunning()
              << "), " << again.search().hitCount() << " hits\n";
}

// A Markdown box: the hit is where the word is drawn, not where it is in the source
TEST_F(DocumentSearchTest, hitsInMarkdownBoxesAreWhereTheTextIsDrawn) {
    DocumentSession s(*app);
    const PageRef page = s.getDocument()->getPage(0);
    auto* layer = new Layer();
    layer->setName("Markdown");
    auto box = std::make_unique<Text>();
    box->setText("# A heading\n\nthe **needle** in the text");
    box->setWrap(400);
    box->setTransformation(xoj::util::Matrix::TRANSLATION(60, 80));
    const Text* raw = box.get();
    layer->addElement(std::move(box));
    s.getDocument()->lock();
    page->getLayers().insert(page->getLayers().begin(), layer);  // (the page owns it)
    s.getDocument()->unlock();

    search(s, "needle");
    const auto hits = placedHits(s.search());
    ASSERT_EQ(hits.size(), 1u);
    const QRectF hit = hits[0].rect;
    const auto drawn = md::findText(*raw, "needle");
    ASSERT_EQ(drawn.size(), 1u);
    EXPECT_NEAR(hit.x(), drawn[0].x, 0.01);
    EXPECT_NEAR(hit.y(), drawn[0].y, 0.01);
    const auto inSource = raw->findText("needle");
    ASSERT_EQ(inSource.size(), 1u);
    EXPECT_GT(std::abs(hit.x() - inSource[0].x1), 5) << "\"the \" is drawn before it, not \"the **\"";
    EXPECT_GT(hit.y(), 80 + 20) << "below the heading";
    search(s, "**needle");
    EXPECT_EQ(s.search().hitCount(), 0) << "the marks are not searched";
    search(s, "the needle");
    EXPECT_EQ(s.search().hitCount(), 1) << "the text as it is shown";
}

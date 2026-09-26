/*
 * xournal-qt: the library's fuzzy search (fzf's syntax, names fuzzy and ranked, text per term; FuzzyQuery.h).
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cairo-pdf.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/DocumentTextIndex.h"
#include "session/FuzzyQuery.h"
#include "shell/DocumentFiles.h"
#include "shell/HitPages.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/TabManager.h"
#include "AppController.h"
#include "config-test.h"

using namespace xqt;

namespace {
/// A PDF with text ("xournal" on page 1, "Page 2" on page 2), copied from the fixtures.
void makePdf(const fs::path& p) {
    fs::create_directories(p.parent_path());
    fs::copy_file(GET_TESTFILE(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), p);
}

/// A one-page PDF with `words` on it.
void makeWordPdf(const fs::path& p, const char* words) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, words);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// "<pdf>.xopp" annotating a PDF, with a text element on each page (`texts`, by page)
void makeNotes(const fs::path& pdf, const fs::path& xopp, const std::vector<const char*>& texts) {
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    for (size_t p = 0; p < texts.size(); ++p) {
        auto t = std::make_unique<Text>();
        t->setText(texts[p]);
        t->move(100, 300);
        loaded.document->getPage(p)->getSelectedLayer()->addElement(std::move(t));
    }
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}

void waitFor(const std::function<bool()>& cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

class LibraryFuzzyTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    /// A: Uni/Kalman lecture.pdf ("xournal" / "Page 2"); B: Work/report.pdf ("kalman filter"); C: draft notes.pdf
    /// ("kalman"); D: paper.xopp + paper.pdf ("xournal", "kalman" / "Page 2", "filter")
    void makeLibrary() {
        makePdf(root / "Uni" / "Kalman lecture.pdf");
        makeWordPdf(root / "Work" / "report.pdf", "kalman filter");
        makeWordPdf(root / "draft notes.pdf", "kalman");
        makePdf(root / "paper.pdf");
        makeNotes(root / "paper.pdf", root / "paper.xopp", {"kalman", "filter"});
    }
    /// The file names (without folder) of the hits, in order
    std::vector<std::string> files(const std::vector<LibraryIndex::Hit>& hits) {
        std::vector<std::string> out;
        for (const auto& h: hits) {
            out.push_back(h.file.filename().string());
        }
        return out;
    }
    QTemporaryDir tmp;
    fs::path root;
};

using Names = std::vector<std::string>;
}  // namespace

TEST_F(LibraryFuzzyTest, indexEvaluatesTheExpressionOverNamesAndText) {
    makeLibrary();
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    auto search = [&](const char* query) { return index.search(FuzzyQuery(QString::fromUtf8(query))); };

    auto hits = search("kalman");
    ASSERT_EQ(hits.size(), 4u);
    EXPECT_EQ(hits[0].file.filename(), "Kalman lecture.pdf") << "the name first";
    EXPECT_GT(hits[0].nameScore, 0);
    EXPECT_EQ(hits[0].nameMarks, (std::vector<int>{0, 1, 2, 3, 4, 5}));
    EXPECT_EQ(hits[1].nameScore, 0);

    // AND over the whole document; its pages: where both are, else all pages with hits
    hits = search("kalman filter");
    ASSERT_EQ(files(hits).size(), 2u);
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.file < b.file; });
    EXPECT_EQ(files(hits), (Names{"report.pdf", "paper.xopp"}));
    EXPECT_EQ(hits[0].file.filename(), "report.pdf");
    ASSERT_EQ(hits[0].pageHits.size(), 1u);
    EXPECT_EQ(hits[0].count, 2);
    ASSERT_EQ(hits[1].pageHits.size(), 2u) << "on no page together: both pages";
    EXPECT_EQ(hits[1].count, 2);

    struct Case {
        const char* query;
        Names found;  ///< sorted
    };
    const std::vector<Case> cases = {
            {"kalman !draft", {"Kalman lecture.pdf", "paper.xopp", "report.pdf"}},
            {"kalman !uni", {"draft notes.pdf", "paper.xopp", "report.pdf"}},  // "Uni": the folder
            {"xournal | filter", {"Kalman lecture.pdf", "paper.xopp", "report.pdf"}},
            {"^pap", {"paper.xopp"}},
            {"'lecture'", {"Kalman lecture.pdf"}},
            {"'lect'", {}},  // not a whole word
            {"'filt", {"paper.xopp", "report.pdf"}},
            {"^filt", {"paper.xopp", "report.pdf"}},  // a word in the text starts with it
            {"ilter$", {"paper.xopp", "report.pdf"}},
            {"^ilter", {}},
            {"!(kalman | xournal)", {}},
            {"(xournal page) !kalman", {}},  // A has "kalman" in its name, D in its text
            {"(xournal page) !paper", {"Kalman lecture.pdf"}},
            {"wrk rep", {"report.pdf"}},  // "wrk": fuzzy in the folder path Work
            {"kalman (", {}},             // not valid: the plain text "kalman ("
    };
    for (const Case& c: cases) {
        auto found = files(search(c.query));
        std::sort(found.begin(), found.end());
        EXPECT_EQ(found, c.found) << c.query;
    }

    // Per page: the terms on the page, or in the name. A: "kalman" in the name, "page" on its page 2
    hits = search("kalman page");
    const auto a = std::find_if(hits.begin(), hits.end(), [](const auto& h) { return h.file.filename() == "Kalman lecture.pdf"; });
    ASSERT_NE(a, hits.end());
    ASSERT_EQ(a->pageHits.size(), 1u);
    EXPECT_EQ(a->pageHits[0].page, 1);
    EXPECT_EQ(a->firstPage, 1);
    EXPECT_TRUE(a->snippet.contains("Page 2", Qt::CaseInsensitive)) << a->snippet.toStdString();
}

TEST_F(LibraryFuzzyTest, modelRanksHighlightsAndFallsBack) {
    makeLibrary();
    // Of two documents with as many hits, the newer comes first
    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(root / "Work" / "report.pdf", now - std::chrono::hours(48));
    fs::last_write_time(root / "draft notes.pdf", now - std::chrono::hours(1));
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    auto names = [&] {
        Names out;
        for (int i = 0; i < model.count(); ++i) {
            out.push_back(model.data(model.index(i), LibraryModel::NameRole).toString().toStdString());
        }
        return out;
    };

    model.setSearchQuery("klmn");
    EXPECT_TRUE(names().empty()) << "not fuzzy: no such text";
    EXPECT_TRUE(model.searchHint().isEmpty());

    QSignalSpy toggled(&model, &LibraryModel::fuzzySearchChanged);
    model.setFuzzySearch(true);
    EXPECT_EQ(toggled.count(), 1);
    ASSERT_EQ(names().size(), 4u) << "the name, and the word kalman in the text (its letters close together)";
    EXPECT_EQ(names().front(), "Kalman lecture") << "the letters in this order in the name first";
    const QVariantList marks = model.data(model.index(0), LibraryModel::NameMarksRole).toList();
    EXPECT_EQ(marks, (QVariantList{0, 2, 3, 5}));

    model.setSearchQuery("kalman");
    ASSERT_EQ(model.count(), 4);
    EXPECT_EQ(names()[0], "Kalman lecture") << "names first";
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::NameMatchRole).toBool());
    const Names all = names();
    const Names rest(all.begin() + 1, all.end());
    // (paper has one "kalman", as do report and draft notes: by date)
    const auto draft = std::find(rest.begin(), rest.end(), "draft notes");
    const auto report = std::find(rest.begin(), rest.end(), "report");
    ASSERT_NE(draft, rest.end());
    ASSERT_NE(report, rest.end());
    EXPECT_LT(draft, report) << "the newer first";
    EXPECT_TRUE(model.data(model.index(1), LibraryModel::HitPageBaseRole).toString().startsWith("image://hitpage/"));

    // Folders by their names and paths first, then documents
    model.setSearchQuery("wrk");
    EXPECT_EQ(names(), (Names{"Work", "report"}));
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::IsFolderRole).toBool());
    EXPECT_EQ(model.data(model.index(0), LibraryModel::NameMarksRole).toList(), (QVariantList{0, 2, 3}));
    EXPECT_TRUE(model.data(model.index(1), LibraryModel::NameMarksRole).toList().isEmpty()) << "found in its folder";

    // Names only
    model.setSearchQuery("kalman | paper");
    EXPECT_EQ(model.count(), 4);
    model.setNamesOnly(true);
    EXPECT_EQ(names(), (Names{"Kalman lecture", "paper"}));
    model.setNamesOnly(false);

    // Not valid: a hint, and the plain search of the text
    QSignalSpy searched(&model, &LibraryModel::searchChanged);
    model.setSearchQuery("kalman (");
    EXPECT_FALSE(model.searchHint().isEmpty());
    EXPECT_EQ(model.count(), 0);
    model.setSearchQuery("kalman filter");
    EXPECT_TRUE(model.searchHint().isEmpty());
    EXPECT_EQ(model.count(), 2);
    model.setFuzzySearch(false);
    EXPECT_EQ(model.count(), 1) << "the phrase";
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::NameMarksRole).toList().isEmpty());
}

TEST_F(LibraryFuzzyTest, hitPagesMarkTheTermsOfTheQuery) {
    const std::vector<textmatch::Term> terms{{"kalman", textmatch::WordStart}, {"filter", textmatch::Anywhere}};
    const QString marks = HitPageProvider::marksOf(terms);
    EXPECT_EQ(HitPageProvider::termsOf(marks), terms);
    EXPECT_EQ(HitPageProvider::termsOf("  Kalman   Filter "),
              (std::vector<textmatch::Term>{{"kalman filter", textmatch::Anywhere}}))
            << "a plain query: one term, as before";
    makeWordPdf(root / "report.pdf", "kalman and filter");
    const fs::path file = root / "report.pdf";
    const QImage plain = HitPageProvider::render(file, 0, "nothing", 256);
    const QImage marked = HitPageProvider::render(file, 0, marks, 256);
    ASSERT_FALSE(plain.isNull());
    ASSERT_EQ(plain.size(), marked.size());
    EXPECT_NE(plain, marked) << "the terms are marked";
    const QImage one = HitPageProvider::render(file, 0, HitPageProvider::marksOf({{"kalman"}}), 256);
    EXPECT_NE(one, marked) << "both terms are marked, not one";
}

// Fuzzy terms match words of the text (WordMatch.h): `tbine` finds "turbine", the whole word is marked on the page
// pictures, as often as it is counted, and documents with the word itself come first.
TEST_F(LibraryFuzzyTest, fuzzyTermsFindWordsInText) {
    makeWordPdf(root / "energy.pdf", "wind turbine blades");
    makeWordPdf(root / "code.pdf", "the tbine module");
    makeWordPdf(root / "music.pdf", "tambourine and timberline");
    makeWordPdf(root / "short.pdf", "tb and tbn");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    auto search = [&](const char* query) { return index.search(FuzzyQuery(QString::fromUtf8(query))); };

    auto hits = search("tbine");
    ASSERT_EQ(files(hits), (Names{"code.pdf", "energy.pdf"})) << "the word itself first, then fuzzy matches";
    EXPECT_FALSE(hits[0].fuzzyOnly);
    EXPECT_TRUE(hits[1].fuzzyOnly);
    EXPECT_EQ(hits[1].count, 1);
    ASSERT_EQ(hits[1].pageHits.size(), 1u);
    EXPECT_TRUE(hits[1].snippet.contains("turbine")) << hits[1].snippet.toStdString();
    EXPECT_EQ(files(search("turbnie")), (Names{"energy.pdf"})) << "a typo";
    EXPECT_EQ(files(search("tbine !'module")), (Names{"energy.pdf"}));
    auto sorted = [&](const char* query) {
        Names out = files(search(query));
        std::sort(out.begin(), out.end());
        return out;
    };
    EXPECT_EQ(sorted("tb"), (Names{"code.pdf", "short.pdf"})) << "a short term: a substring, as before";
    EXPECT_TRUE(files(search("'tbine")).size() == 1) << "exact: only the word itself";
    EXPECT_GT(index.vocabularyBytes(), 0u);

    // The whole word is marked on the page's picture, once, as counted
    const auto terms = FuzzyQuery(QStringLiteral("tbine")).markTerms();
    auto loaded = DocumentSession::loadFile(root / "energy.pdf");
    ASSERT_TRUE(loaded.document);
    PdfLayoutReader reader(root / "energy.pdf");
    const auto rects = termRects(*loaded.document->getPage(0), &reader, terms);
    ASSERT_EQ(rects.size(), 1u);
    const auto plain = termRects(*loaded.document->getPage(0), &reader, {{"turbine", textmatch::Anywhere}});
    ASSERT_EQ(plain.size(), 1u);
    EXPECT_EQ(rects[0], plain[0]) << "the whole word \"turbine\"";
    const fs::path file = root / "energy.pdf";
    const QImage none = HitPageProvider::render(file, 0, HitPageProvider::marksOf({{"nothing", textmatch::Anywhere}}), 256);
    const QImage marked = HitPageProvider::render(file, 0, HitPageProvider::marksOf(terms), 256);
    const QImage asWord = HitPageProvider::render(file, 0, HitPageProvider::marksOf({{"turbine", textmatch::Word}}), 256);
    EXPECT_NE(none, marked);
    EXPECT_EQ(marked, asWord) << "marked like the word itself";
}

// A search hit opened from the library with the fuzzy search on: the document's search reads the same syntax.
TEST_F(LibraryFuzzyTest, openedHitsSearchTheDocumentTheSameWay) {
    makeLibrary();
    AppController c;
    c.setLibraryRoot(root);
    auto* model = qobject_cast<LibraryModel*>(c.libraryModel());
    ASSERT_NE(model, nullptr);
    model->searchIndex()->waitForDone();
    model->setFuzzySearch(true);
    model->setSearchQuery("xournal | filter");
    const QString paper = QString::fromStdString((root / "paper.xopp").string());
    ASSERT_TRUE(c.openSearchHitAt(paper, model->searchQuery(), 1));
    DocumentSession* s = c.tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    waitFor([&] { return !s->search().isRunning(); });
    EXPECT_TRUE(s->search().fuzzy());
    EXPECT_EQ(s->search().hitCount(), 2) << "xournal on page 1, filter on page 2";
    EXPECT_EQ(s->search().currentPage(), 1u);
    // Refined in the document's own search bar it stays fuzzy; cleared, the next search follows the fuzzy setting
    // (qt/doc-search-fuzzy: one setting for the library and the document's search bar): on, then off
    c.setSearchQuery("xournal | kalman");
    EXPECT_TRUE(s->search().fuzzy());
    waitFor([&] { return !s->search().isRunning(); });
    EXPECT_EQ(s->search().hitCount(), 2);
    c.setSearchQuery("");
    c.setSearchQuery("xournal | kalman");
    EXPECT_TRUE(s->search().fuzzy()) << "the setting is on";
    waitFor([&] { return !s->search().isRunning(); });
    EXPECT_EQ(s->search().hitCount(), 2);
    model->setFuzzySearch(false);
    c.setSearchQuery("");
    c.setSearchQuery("xournal | kalman");
    EXPECT_FALSE(s->search().fuzzy()) << "the setting is off: plain";
    waitFor([&] { return !s->search().isRunning(); });
    EXPECT_EQ(s->search().hitCount(), 0);
    model->setFuzzySearch(true);

    // The toggle is remembered (an app-wide setting)
    AppController again;
    EXPECT_TRUE(qobject_cast<LibraryModel*>(again.libraryModel())->fuzzySearch());
    model->setFuzzySearch(false);
    AppController third;
    EXPECT_FALSE(qobject_cast<LibraryModel*>(third.libraryModel())->fuzzySearch());
}

// Measured on a generated library: XQT_BENCH_FUZZY=<documents> (e.g. 3000); a few big Markdown files among them.
TEST_F(LibraryFuzzyTest, benchFuzzySearch) {
    const int documents = qEnvironmentVariableIntValue("XQT_BENCH_FUZZY");
    if (documents <= 0) {
        GTEST_SKIP() << "set XQT_BENCH_FUZZY=<documents>";
    }
    std::mt19937 rng(7);
    const std::vector<std::string> words{"kalman", "filter", "lecture", "notes", "signals", "systems", "control",
                                         "robust", "linear", "algebra", "sheet", "exam", "draft", "paper", "thesis",
                                         "analysis", "quantum", "optics", "report", "summary", "physics", "math"};
    // Besides these, a vocabulary of 40,000 made-up words (a quarter of the text), so the text has as many distinct
    // words as a real library
    std::vector<std::string> rare;
    for (int i = 0; i < 40000; ++i) {
        std::string w;
        const int length = 4 + static_cast<int>(rng() % 9);
        for (int k = 0; k < length; ++k) {
            w += static_cast<char>('a' + rng() % 26);
        }
        rare.push_back(std::move(w));
    }
    auto word = [&] { return rng() % 4 == 0 ? rare[rng() % rare.size()] : words[rng() % words.size()]; };
    for (int i = 0; i < documents; ++i) {
        const fs::path dir = root / ("folder " + std::to_string(i % 40)) / ("sub " + std::to_string(i % 7));
        fs::create_directories(dir);
        std::ofstream f(dir / (word() + " " + word() + " " + std::to_string(i) + ".md"));
        f << "# " << word() << "\n\n";
        const int paragraphs = i % 500 == 0 ? 8000 : 3;  // some big ones (~1-2 MB)
        for (int p = 0; p < paragraphs; ++p) {
            for (int w = 0; w < 30; ++w) {
                f << word() << ' ';
            }
            f << "\n\n";
        }
    }
    LibraryModel model;
    QElapsedTimer t;
    t.start();
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    std::cout << documents << " documents indexed in " << t.elapsed() << " ms" << std::endl;
    {
        // The first fuzzy search of the text (it may prepare what later ones use)
        QElapsedTimer first;
        first.start();
        const size_t found = model.searchIndex()->search(FuzzyQuery(QStringLiteral("signals"))).size();
        std::cout << "first fuzzy search: " << found << " documents, " << first.elapsed() << " ms" << std::endl;
    }
    // (the best of three runs: the machine may be busy)
    auto measure = [&](bool fuzzy, const char* query) {
        model.setFuzzySearch(fuzzy);
        qint64 modelMs = -1, indexMs = -1;
        size_t found = 0;
        for (int run = 0; run < 3; ++run) {
            model.setSearchQuery("");
            QElapsedTimer m;
            m.start();
            model.setSearchQuery(QString::fromUtf8(query));
            modelMs = modelMs < 0 ? m.elapsed() : std::min(modelMs, m.elapsed());
            m.restart();
            const FuzzyQuery parsed(QString::fromUtf8(query));
            found = fuzzy ? model.searchIndex()->search(parsed).size() : model.searchIndex()->search(query).size();
            indexMs = indexMs < 0 ? m.elapsed() : std::min(indexMs, m.elapsed());
        }
        std::cout << (fuzzy ? "fuzzy " : "plain ") << '"' << query << "\": " << model.count() << " rows, model "
                  << modelMs << " ms, index alone " << indexMs << " ms" << std::endl;
        return found;
    };
    measure(false, "kalman");
    measure(true, "kalman");
    measure(false, "kalman filter");
    measure(true, "kalman filter");
    measure(true, "klmn");
    measure(false, "klman");
    measure(true, "klman");     // a typo
    measure(true, "sgnals");    // letters left out
    measure(true, "kalman");    // again: the words' matches are kept
    measure(true, "(kalman | robust) !draft ^lin");
    measure(true, "'quantum' optics$");
    SUCCEED();
}

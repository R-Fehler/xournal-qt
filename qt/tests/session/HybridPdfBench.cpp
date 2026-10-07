/*
 * xournal-qt: measurements of the PDF with notes (skipped unless their environment variable is set: XQT_BENCH_HYBRID,
 * XQT_BENCH_SAVE).
 *
 * @license GNU GPLv2 or later
 */
#include "HybridPdfTestSupport.h"

using namespace xqt;
using namespace xqt::test::hybrid;
using xqt::test::makeTextPdf;
using xqt::test::readFile;

// --- measurements ---------------------------------------------------------------------------------------------------

// XQT_BENCH_HYBRID=<pdf>: notes on every 25th page (strokes with pressure, a highlighter, a text); the time and size
// of our PDF export and of the hybrid PDF, opening it (clean copy made, then from the cache) and saving it again.
TEST_F(HybridPdfTest, benchSaveAndOpen) {
    const char* source = std::getenv("XQT_BENCH_HYBRID");
    if (!source) {
        GTEST_SKIP() << "set XQT_BENCH_HYBRID=<pdf>";
    }
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) {
        return std::chrono::duration<double, std::milli>(Clock::now() - a).count();
    };
    auto size = [](const fs::path& p) { return static_cast<double>(fs::file_size(p)) / 1024; };
    auto t = Clock::now();
    auto loaded = DocumentSession::loadFile(source);
    ASSERT_TRUE(loaded.document);
    Document& doc = *loaded.document;
    const double openPlain = ms(t);
    size_t noted = 0;
    for (size_t i = 0; i < doc.getPageCount(); i += 25, ++noted) {
        Layer* layer = doc.getPage(i)->getSelectedLayer();
        for (int k = 0; k < 20; ++k) {
            std::vector<Point> pts;
            for (int j = 0; j < 60; ++j) {
                pts.emplace_back(60 + j * 6, 100 + k * 20 + 5 * std::sin(j / 3.0), 1 + (j % 10) / 5.0);
            }
            addStroke(layer, StrokeTool::PEN, Color(0xff000080U), 1.41, pts);
        }
        addStroke(layer, StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 80), Point(400, 80)});
        addText(layer, "a note on page " + std::to_string(i + 1), 60, 600);
    }
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        for (const Layer* l: doc.getPage(i)->getLayers()) {
            for (const auto& e: l->getElementsView()) {
                e->getBoundingBox();
            }
        }
    }
    const fs::path exported = path("export.pdf"), hybrid = path("hybrid.pdf");
    t = Clock::now();
    ExportHelper::exportPdf(&doc, exported, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
    const double exportMs = ms(t);
    t = Clock::now();
    const auto r = HybridPdf::write(doc, hybrid);
    const double hybridMs = ms(t);
    ASSERT_TRUE(r.ok) << r.error;
    t = Clock::now();
    auto first = DocumentSession::loadFile(hybrid);
    const double openFirst = ms(t);
    ASSERT_TRUE(first.document);
    t = Clock::now();
    auto second = DocumentSession::loadFile(hybrid);
    const double openCached = ms(t);
    t = Clock::now();
    const auto again = HybridPdf::write(*second.document, path("again.pdf"));
    const double againMs = ms(t);
    ASSERT_TRUE(again.ok);
    std::cout << source << ": " << doc.getPageCount() << " pages, notes on " << noted << " (" << r.annotations
              << " annotations)\n"
              << "  source " << size(source) << " KB, opened as a plain PDF in " << openPlain << " ms\n"
              << "  our PDF export: " << exportMs << " ms, " << size(exported) << " KB\n"
              << "  hybrid PDF:     " << hybridMs << " ms, " << size(hybrid) << " KB\n"
              << "  open hybrid:    " << openFirst << " ms (clean copy made), " << openCached << " ms (cached)\n"
              << "  save again from the clean copy: " << againMs << " ms, " << size(path("again.pdf")) << " KB\n";
}

// XQT_BENCH_HYBRID=<pdf>: Ctrl+S after a small edit, as a hybrid and as an archive PDF: written in full (as before)
// and appended; the bytes appended. XQT_INCREMENTAL_SAMPLES=<folder>: the files after the incremental saves and a full
// write of the same document go there (to compare them in other renderers).
TEST_F(IncrementalSaveTest, benchCtrlS) {
    const char* source = std::getenv("XQT_BENCH_HYBRID");
    if (!source) {
        GTEST_SKIP() << "set XQT_BENCH_HYBRID=<pdf>";
    }
    HybridPdf::compactAbove = 0.25;
    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a) { return std::chrono::duration<double, std::milli>(Clock::now() - a).count(); };
    auto kb = [](uint64_t bytes) { return static_cast<double>(bytes) / 1024; };
    const char* samples = std::getenv("XQT_INCREMENTAL_SAMPLES");
    auto keep = [&](const fs::path& file, const std::string& name) {
        if (samples) {
            std::error_code ec;
            fs::create_directories(samples, ec);
            fs::copy_file(file, fs::path(samples) / name, fs::copy_options::overwrite_existing, ec);
        }
    };
    int sourceCode = -1;
    qpdfCheck(source, sourceCode);
    for (const bool archive: {false, true}) {
        auto loaded = DocumentSession::loadFile(source);
        ASSERT_TRUE(loaded.document);
        Document& doc = *loaded.document;
        size_t noted = 0;
        for (size_t i = 0; i < doc.getPageCount(); i += 25, ++noted) {
            Layer* layer = doc.getPage(i)->getSelectedLayer();
            for (int k = 0; k < 20; ++k) {
                std::vector<Point> pts;
                for (int j = 0; j < 60; ++j) {
                    pts.emplace_back(60 + j * 6, 100 + k * 20 + 5 * std::sin(j / 3.0), 1 + (j % 10) / 5.0);
                }
                addStroke(layer, StrokeTool::PEN, Color(0xff000080U), 1.41, pts);
            }
            addStroke(layer, StrokeTool::HIGHLIGHTER, Color(0xffffff00U), 12, {Point(60, 80), Point(400, 80)});
            addText(layer, "a note on page " + std::to_string(i + 1), 60, 600);
        }
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            for (const Layer* l: doc.getPage(i)->getLayers()) {
                for (const auto& e: l->getElementsView()) {
                    e->getBoundingBox();
                }
            }
        }
        const fs::path file = path(archive ? "bench.archive.pdf" : "bench.pdf");
        auto t = Clock::now();
        const auto first = archive ? HybridPdf::writeArchive(doc, file) : HybridPdf::write(doc, file);
        ASSERT_TRUE(first.ok) << first.error;
        const double firstMs = ms(t);
        loaded.document.reset();
        t = Clock::now();
        auto opened = DocumentSession::loadFile(file);  // (the clean copy is made)
        const double openMs = ms(t);
        ASSERT_TRUE(opened.document);
        DocumentSession s(*app, std::move(opened.document));
        const char* kind = archive ? "archive PDF" : "hybrid PDF";
        std::cout << source << " as a " << kind << ": " << s.getDocument()->getPageCount() << " pages, notes on "
                  << noted << ", " << kb(fs::file_size(file)) << " KB; written in " << firstMs << " ms, opened in "
                  << openMs << " ms\n";
        // Ctrl+S after one stroke: appended (now), three times; then written in full (as before)
        DocumentSession::SaveResult r;
        for (int n = 0; n < 3; ++n) {
            drawOn(s, 10 + 30 * static_cast<size_t>(n), 400);
            t = Clock::now();
            r = s.save();
            const double saveMs = ms(t);
            ASSERT_TRUE(r.ok) << r.error;
            EXPECT_TRUE(r.incremental);
            int code = -1;
            const std::string check = qpdfCheck(file, code);
            // (qpdf 12 also warns about what the source PDF has, e.g. pgfmanual's unsorted name tree: warnings pass
            // when the source has some too, errors never)
            const bool ok = code == 0 || (code == 3 && sourceCode == 3);
            EXPECT_TRUE(ok) << check;
            std::cout << "  Ctrl+S appended: " << saveMs << " ms, " << kb(r.appended) << " KB appended (file "
                      << kb(fs::file_size(file)) << " KB), qpdf --check "
                      << (ok ? (code == 0 ? "ok" : "ok (the source's warnings)") : "FAILED") << "\n";
        }
        t = Clock::now();
        auto again = DocumentSession::loadFile(file);
        std::cout << "  opened again: " << ms(t) << " ms (the clean copy kept)\n";
        ASSERT_TRUE(again.document);
        drawOn(s, 100, 300);
        DocumentSession::SaveRequest full;
        full.compact = true;
        t = Clock::now();
        r = s.saveNow(full);
        ASSERT_TRUE(r.ok) << r.error;
        std::cout << "  Ctrl+S written in full (as before): " << ms(t) << " ms, " << kb(fs::file_size(file))
                  << " KB\n";
        drawOn(s, 130, 300);
        t = Clock::now();
        r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        std::cout << "  Ctrl+S right after that, appended: " << ms(t) << " ms, " << kb(r.appended) << " KB\n";
        const fs::path fullFile = path(archive ? "full.archive.pdf" : "full.pdf");
        ASSERT_TRUE((archive ? HybridPdf::writeArchive(*s.getDocument(), fullFile)
                             : HybridPdf::write(*s.getDocument(), fullFile))
                            .ok);
        keep(file, archive ? "incremental.archive.pdf" : "incremental.pdf");
        keep(fullFile, archive ? "full.archive.pdf" : "full.pdf");
        for (size_t page: {size_t(0), size_t(10), size_t(40), size_t(70)}) {
            cairo_surface_t* a = render(file, page);
            cairo_surface_t* b = render(fullFile, page);
            const Diff d = compare(a, b);
            EXPECT_LT(d.mean, 0.5) << "page " << page + 1;
            cairo_surface_destroy(a);
            cairo_surface_destroy(b);
        }
    }
}

// XQT_BENCH_SAVE=<a PDF with notes>: one Ctrl+S after a stroke on page 11 of a copy of it (XQT_HYBRID_TIMES=1: steps)
TEST_F(IncrementalSaveTest, benchOneSave) {
    const char* source = std::getenv("XQT_BENCH_SAVE");
    if (!source) {
        GTEST_SKIP() << "set XQT_BENCH_SAVE=<pdf with notes>";
    }
    HybridPdf::compactAbove = 0.25;
    const fs::path file = path("copy.pdf");
    fs::copy_file(source, file);
    auto loaded = DocumentSession::loadFile(file);
    ASSERT_TRUE(loaded.document);
    DocumentSession s(*app, std::move(loaded.document));
    for (int n = 0; n < 2; ++n) {
        drawOn(s, 10, 300 + 20 * n);
        const auto t = std::chrono::steady_clock::now();
        const auto r = s.save();
        ASSERT_TRUE(r.ok) << r.error;
        std::cout << "Ctrl+S: " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count()
                  << " ms, " << (r.incremental ? "appended " : "written in full, ") << r.appended << " bytes\n";
    }
}

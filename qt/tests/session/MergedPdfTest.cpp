/*
 * xournal-qt: the merged background PDF (qpdf): pages of other PDFs appended, unused pages dropped, the mark.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <QTemporaryDir>
#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "pdf/base/XojPdfDocument.h"
#include "session/MergedPdf.h"

using namespace xqt;

namespace {
/// A PDF with one page per word, each word as real text.
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

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// The word on each page, as poppler finds it (the first of `candidates` that is on the page, "" if none).
std::vector<std::string> wordsOf(XojPdfDocument& pdf, const std::vector<std::string>& candidates) {
    std::vector<std::string> out;
    for (size_t i = 0; i < pdf.getPageCount(); ++i) {
        std::string found;
        for (const auto& c: candidates) {
            if (!pdf.getPage(i)->findText(c).empty()) {
                found = c;
                break;
            }
        }
        out.push_back(found);
    }
    return out;
}

XojPdfDocument load(const fs::path& p) {
    XojPdfDocument pdf;
    GError* error = nullptr;
    EXPECT_TRUE(pdf.load(p, "", &error)) << p;
    if (error) {
        g_error_free(error);
    }
    return pdf;
}

const std::vector<std::string> WORDS{"lectureone", "lecturetwo", "lecturethree", "pastedalpha", "pastedbeta",
                                     "pastedgamma"};

class MergedPdfTest: public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(tmp.isValid()); }
    fs::path path(const char* name) const { return fs::path(tmp.filePath(name).toStdString()); }
    QTemporaryDir tmp;
};
}  // namespace

TEST_F(MergedPdfTest, namesNextToTheDocument) {
    EXPECT_EQ(MergedPdf::sidecarOf("/a/lecture.xopp"), fs::path("/a/.lecture.pages.pdf"));
    EXPECT_EQ(MergedPdf::pairOf("/a/lecture.xopp"), fs::path("/a/lecture.pdf"));
    EXPECT_TRUE(MergedPdf::isSidecarName("/a/.lecture.pages.pdf"));
    EXPECT_FALSE(MergedPdf::isSidecarName("/a/lecture.pages.pdf"));
    EXPECT_FALSE(MergedPdf::isSidecarName("/a/lecture.pdf"));
}

TEST_F(MergedPdfTest, pagesOfOtherPdfsAreAppendedWithTheirText) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree"});
    makeTextPdf(path("other.pdf"), {"pastedalpha", "pastedbeta"});
    makeTextPdf(path("third.pdf"), {"pastedgamma"});
    const std::string original = bytesOf(path("lecture.pdf"));

    std::string copied;
    ASSERT_TRUE(MergedPdf::extract(path("other.pdf"), {1}, copied).ok);
    auto r = MergedPdf::append(path("lecture.pdf"), copied, path(".lecture.pages.pdf"), MergedPdf::Kind::WithSource);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.first, 3u);
    EXPECT_EQ(r.pages, 4u);
    // A second paste from another PDF goes into the same file.
    ASSERT_TRUE(MergedPdf::extract(path("third.pdf"), {0}, copied).ok);
    r = MergedPdf::append(path(".lecture.pages.pdf"), copied, path(".lecture.pages.pdf"), MergedPdf::Kind::WithSource);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.first, 4u);

    XojPdfDocument merged = load(path(".lecture.pages.pdf"));
    EXPECT_EQ(wordsOf(merged, WORDS),
              (std::vector<std::string>{"lectureone", "lecturetwo", "lecturethree", "pastedbeta", "pastedgamma"}));
    EXPECT_EQ(MergedPdf::kindOf(path(".lecture.pages.pdf")), MergedPdf::Kind::WithSource);
    EXPECT_EQ(MergedPdf::kindOf(path("lecture.pdf")), MergedPdf::Kind::None) << "the user's PDF is not marked";
    EXPECT_EQ(bytesOf(path("lecture.pdf")), original) << "the user's PDF is never changed";
    EXPECT_FALSE(fs::exists(path("..lecture.pages.pdf.part"))) << "no temporary file left";
}

TEST_F(MergedPdfTest, aDocumentWithoutPdfGetsOneOfItsOwn) {
    makeTextPdf(path("other.pdf"), {"pastedalpha", "pastedbeta"});
    std::string copied;
    ASSERT_TRUE(MergedPdf::extract(path("other.pdf"), {1, 0}, copied).ok);
    const auto r = MergedPdf::append({}, copied, path("notes.pdf"), MergedPdf::Kind::Own);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.first, 0u);
    XojPdfDocument merged = load(path("notes.pdf"));
    EXPECT_EQ(wordsOf(merged, WORDS), (std::vector<std::string>{"pastedbeta", "pastedalpha"}));
    EXPECT_EQ(MergedPdf::kindOf(path("notes.pdf")), MergedPdf::Kind::Own);
}

TEST_F(MergedPdfTest, unusedPagesAreDropped) {
    makeTextPdf(path("lecture.pdf"), {"lectureone", "lecturetwo", "lecturethree", "pastedalpha", "pastedbeta"});
    std::string none;  // (no pages: only the mark)
    ASSERT_TRUE(MergedPdf::extract(path("lecture.pdf"), {}, none).ok);
    ASSERT_TRUE(MergedPdf::append(path("lecture.pdf"), none, path("m.pdf"), MergedPdf::Kind::WithSource).ok);
    const auto before = fs::file_size(path("m.pdf"));

    const auto r = MergedPdf::keepOnly(path("m.pdf"), {0, 2, 4}, path("m.pdf"));
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.pages, 3u);
    XojPdfDocument merged = load(path("m.pdf"));
    EXPECT_EQ(wordsOf(merged, WORDS), (std::vector<std::string>{"lectureone", "lecturethree", "pastedbeta"}));
    EXPECT_EQ(MergedPdf::kindOf(path("m.pdf")), MergedPdf::Kind::WithSource) << "still marked";
    EXPECT_LT(fs::file_size(path("m.pdf")), before);
}

TEST_F(MergedPdfTest, copiedPagesLoadFromMemory) {
    makeTextPdf(path("other.pdf"), {"pastedalpha", "pastedbeta", "pastedgamma"});
    std::string copied;
    const auto r = MergedPdf::extract(path("other.pdf"), {2}, copied);
    ASSERT_TRUE(r.ok) << r.error;
    XojPdfDocument pdf;
    GError* error = nullptr;
    ASSERT_TRUE(pdf.load(std::make_unique<std::string>(copied), "", &error));
    EXPECT_EQ(wordsOf(pdf, WORDS), (std::vector<std::string>{"pastedgamma"}));
    EXPECT_FALSE(MergedPdf::extract(path("other.pdf"), {3}, copied).ok) << "no such page";
    EXPECT_FALSE(MergedPdf::extract(path("missing.pdf"), {0}, copied).ok);
}

TEST_F(MergedPdfTest, aHundredPagesMergeQuickly) {
    std::vector<std::string> many;
    for (int i = 0; i < 100; ++i) {
        many.push_back("page" + std::to_string(i));
    }
    makeTextPdf(path("big.pdf"), many);
    makeTextPdf(path("lecture.pdf"), {"lectureone"});
    const auto start = std::chrono::steady_clock::now();
    std::vector<size_t> all(100);
    for (size_t i = 0; i < all.size(); ++i) {
        all[i] = i;
    }
    std::string copied;
    ASSERT_TRUE(MergedPdf::extract(path("big.pdf"), all, copied).ok);
    ASSERT_TRUE(MergedPdf::append(path("lecture.pdf"), copied, path("m.pdf"), MergedPdf::Kind::WithSource).ok);
    const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    std::cout << "100 pages copied and merged: " << ms << " ms\n";
    EXPECT_LT(ms, 1000);
    EXPECT_EQ(load(path("m.pdf")).getPageCount(), 101u);
}

// Sizes of a real case: XQT_BENCH_MERGE="<lecture.pdf>:<other.pdf>" pastes three pages of the other PDF (one paste
// each) into the lecture's merged PDF, then drops one lecture page and one pasted page as a save would.
TEST_F(MergedPdfTest, benchSizes) {
    const QByteArray env = qgetenv("XQT_BENCH_MERGE");
    const int colon = env.indexOf(':');
    if (colon < 0) {
        GTEST_SKIP() << "XQT_BENCH_MERGE=<lecture.pdf>:<other.pdf>";
    }
    const fs::path lecture = env.left(colon).toStdString(), other = env.mid(colon + 1).toStdString();
    const fs::path merged = path(".lecture.pages.pdf");
    const auto t0 = std::chrono::steady_clock::now();
    fs::path base = lecture;
    for (size_t page: {2, 5, 9}) {
        std::string copied;
        ASSERT_TRUE(MergedPdf::extract(other, {page}, copied).ok);
        std::cout << "page " << page + 1 << " copied: " << copied.size() / 1024 << " KiB\n";
        const auto r = MergedPdf::append(base, copied, merged, MergedPdf::Kind::WithSource);
        ASSERT_TRUE(r.ok) << r.error;
        base = merged;
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0);
    const size_t pages = load(merged).getPageCount();
    std::cout << "lecture " << fs::file_size(lecture) / 1024 << " KiB, " << pages - 3 << " pages; other "
              << fs::file_size(other) / 1024 << " KiB; merged with 3 pasted pages: " << fs::file_size(merged) / 1024
              << " KiB (" << ms.count() << " ms for the three pastes)\n";
    std::vector<size_t> keep;
    for (size_t i = 0; i < pages; ++i) {
        if (i != 1 && i != pages - 2) {
            keep.push_back(i);
        }
    }
    ASSERT_TRUE(MergedPdf::keepOnly(merged, keep, merged).ok);
    std::cout << "after dropping a lecture page and a pasted page: " << fs::file_size(merged) / 1024 << " KiB\n";
}

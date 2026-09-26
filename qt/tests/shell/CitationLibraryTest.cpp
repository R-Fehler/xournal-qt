/*
 * xournal-qt: finding the paper of a reference in the library by its title (qt/docs/citations.md). The papers of the
 * test library are named by numbers, as arXiv names them: their titles are in the PDF's /Title, or only on the first
 * page (in the largest font, next to the arXiv stamp in the margin).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QCborMap>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/Citation.h"
#include "session/PdfTitle.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"

#include "../CitationPdfs.h"

using namespace xqt;

namespace {
class CitationLibraryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        fs::create_directories(root / "Papers");
        fs::create_directories(root / "Lectures");
        // /Title and the same title on the page, with arXiv's stamp in the margin (larger than the title)
        test::makePaper((root / "Papers" / "1706.03762.pdf").string(), "Attention Is All You Need",
                        "Attention Is All You Need", "Ashish Vaswani, Noam Shazeer, Niki Parmar",
                        "arXiv:1706.03762v7 [cs.CL] 2 Aug 2023");
        // No /Title: only the page
        test::makePaper((root / "Papers" / "2005.14165.pdf").string(), "", "Language Models are Few-Shot Learners",
                        "Tom B. Brown, Benjamin Mann", "arXiv:2005.14165v4 [cs.CL] 22 Jul 2020");
        // A /Title that is no title (a file name): the page's
        test::makePaper((root / "Papers" / "1512.03385.pdf").string(), "Microsoft Word - resnet_final.docx",
                        "Deep Residual Learning for Image Recognition", "Kaiming He, Xiangyu Zhang");
        test::makePaper((root / "Papers" / "1412.6980.pdf").string(), "", "Adam: A Method for Stochastic Optimization",
                        "Diederik P. Kingma, Jimmy Lei Ba");
        // Another paper whose title has the same words, and more
        test::makePaper((root / "Papers" / "2010.13154.pdf").string(), "",
                        "Attention is All You Need in Speech Separation", "Cem Subakan, Mirco Ravanelli");
        // Lecture notes: the title is plain text on the first page (all in one size)
        test::makePdf((root / "Lectures" / "0042.pdf").string(), "untitled",
                      {{"Lecture 7: A New Approach to Linear Filtering and Prediction Problems", 10, 100},
                       {"The Kalman filter estimates the state of a linear system.", 10, 120}});
        std::ofstream(root / "Lectures" / "notes.md") << "# Time, Clocks, and the Ordering of Events\n\nNotes.\n";
    }
    void index(LibraryIndex& idx) {
        idx.update(DocumentFiles::scanRecursive(root));
        idx.waitForDone();
    }
    std::string top(const LibraryIndex& idx, const QString& entry) {
        const cite::TitleGuess g = cite::guessTitle(entry);
        const auto hits = idx.findTitle(g.title, g.raw, 1);
        return hits.empty() ? std::string("(none)") : hits.front().file.filename().string();
    }

    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(CitationLibraryTest, aPdfsTitleIsItsTitleOrItsLargestText) {
    const auto attention = pdftitle::read(root / "Papers" / "1706.03762.pdf");
    EXPECT_EQ(attention.meta, "Attention Is All You Need");
    EXPECT_EQ(attention.heading, "Attention Is All You Need") << "the arXiv stamp in the margin is left out";
    const auto gpt = pdftitle::read(root / "Papers" / "2005.14165.pdf");
    EXPECT_EQ(gpt.meta, "");
    EXPECT_EQ(gpt.heading, "Language Models are Few-Shot Learners");
    EXPECT_EQ(pdftitle::read(root / "Papers" / "1512.03385.pdf").meta, "") << "a file name is no title";
    EXPECT_FALSE(pdftitle::plausibleMeta("untitled"));
    EXPECT_FALSE(pdftitle::plausibleMeta("paper.dvi"));
    EXPECT_FALSE(pdftitle::plausibleMeta("1706.03762", "1706.03762.pdf"));
    EXPECT_FALSE(pdftitle::plausibleMeta("arXiv:1706.03762v7"));
    EXPECT_TRUE(pdftitle::plausibleMeta("Kalman Filtering"));
    EXPECT_EQ(pdftitle::read(root / "missing.pdf").heading, "");
}

TEST_F(CitationLibraryTest, referencesFindTheirPapersByTitleNotByFileName) {
    LibraryIndex idx(root);
    index(idx);
    // IEEE (quoted), with a typo in the reference
    EXPECT_EQ(top(idx, "[1] A. Vaswani et al., \"Attention is all you need,\" in Proc. NeurIPS, 2017."), "1706.03762.pdf");
    EXPECT_EQ(top(idx, "[1] A. Vaswani et al., \"Attenton is all you ned,\" in Proc. NeurIPS, 2017."), "1706.03762.pdf")
            << "typos";
    // arXiv style: found by the first page's largest text (no /Title)
    EXPECT_EQ(top(idx, "Tom B. Brown, Benjamin Mann, et al. Language models are few-shot learners. arXiv preprint "
                       "arXiv:2005.14165, 2020."),
              "2005.14165.pdf");
    // APA; the /Title of this one is a file name
    EXPECT_EQ(top(idx, "He, K., Zhang, X., Ren, S., & Sun, J. (2016). Deep residual learning for image recognition. "
                       "In CVPR (pp. 770–778)."),
              "1512.03385.pdf");
    // IEEE without quotes (the title is not guessed right): the whole entry has the document's title
    EXPECT_EQ(top(idx, "[3] D. P. Kingma and J. Ba, Adam: a method for stochastic optimization, in Proc. ICLR, 2015."),
              "1412.6980.pdf");
    // Plain text on the first page; a Markdown file by its first heading
    EXPECT_EQ(top(idx, "Kalman, R. E. (1960). A new approach to linear filtering and prediction problems. Journal of "
                       "Basic Engineering, 82(1), 35–45."),
              "0042.pdf");
    EXPECT_EQ(top(idx, "Leslie Lamport. 1978. Time, clocks, and the ordering of events in a distributed system. "
                       "Commun. ACM 21, 7, 558–565."),
              "notes.md");
    // Nothing like it
    EXPECT_EQ(top(idx, "Hochreiter, S., & Schmidhuber, J. (1997). Long short-term memory. Neural Computation."), "(none)");
}

TEST_F(CitationLibraryTest, hitsHaveTheirTitleAndAScoreAndTheBestComesFirst) {
    LibraryIndex idx(root);
    index(idx);
    const auto hits = idx.findTitle("Attention is all you need", "", 1);
    ASSERT_GE(hits.size(), 2u);
    EXPECT_EQ(hits[0].file, root / "Papers" / "1706.03762.pdf");
    EXPECT_EQ(hits[0].title, "Attention Is All You Need");
    EXPECT_DOUBLE_EQ(hits[0].score, 1.0);
    EXPECT_EQ(hits[1].file, root / "Papers" / "2010.13154.pdf") << "the longer title with the same words";
    EXPECT_EQ(hits[1].title, "Attention is All You Need in Speech Separation");
    EXPECT_LT(hits[1].score, hits[0].score);
    for (const auto& h: hits) {
        EXPECT_GE(h.score, 0.5);
    }
}

TEST_F(CitationLibraryTest, titlesAreKeptInTheIndexAndReadOnceForOldEntries) {
    {
        LibraryIndex idx(root);
        index(idx);
        idx.flush();
    }
    // Read back: nothing is read again
    {
        LibraryIndex again(root);
        index(again);
        EXPECT_EQ(again.documentsRead(), 0);
        EXPECT_EQ(top(again, "Language models are few-shot learners"), "2005.14165.pdf");
    }
    // Entries written before titles were kept: only their titles are read, once
    const fs::path cache = root / "Papers" / DocumentFiles::META_DIR;
    auto notes = Packs::read(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
    ASSERT_TRUE(notes.has_value());
    for (auto it = notes->begin(); it != notes->end(); ++it) {
        QCborMap entry = it.value().toMap();
        ASSERT_TRUE(entry.contains(QStringLiteral("title"))) << it.key().toString().toStdString();
        entry.remove(QStringLiteral("title"));
        entry.remove(QStringLiteral("heading"));
        it.value() = entry;
    }
    ASSERT_TRUE(Packs::write(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT, *notes, true));
    LibraryIndex old(root);
    index(old);
    EXPECT_EQ(old.titlesRead(), 5) << "the five papers of the folder";
    EXPECT_EQ(old.documentsRead(), 0) << "not the documents";
    EXPECT_EQ(old.pdfPagesRead(), 0);
    EXPECT_EQ(top(old, "Language models are few-shot learners"), "2005.14165.pdf");
    old.flush();
    LibraryIndex later(root);
    index(later);
    EXPECT_EQ(later.titlesRead(), 0) << "once";
}

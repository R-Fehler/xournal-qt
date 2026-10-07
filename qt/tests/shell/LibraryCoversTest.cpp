/*
 * xournal-qt: the covers of the library's documents: drawn once, stored per folder in one pack, written again only when
 * the title page changed.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryTestSupport.h"

TEST_F(LibraryTest, previewsAreRenderedOnceAndStoredInTheLibrary) {
    makePdf(root / "lecture.pdf");
    DocumentCovers::setLibrary(CacheLocation(root));
    const DocumentItem item = DocumentFiles::itemOf(root / "lecture.pdf");
    EXPECT_TRUE(DocumentCovers::stored(item).isNull()) << "not made yet";
    const QImage img = DocumentCovers::cover(item);
    EXPECT_EQ(img.width(), DocumentCovers::WIDTH);
    EXPECT_GT(img.height(), 0);
    EXPECT_TRUE(DocumentCovers::url(item).startsWith("image://cover/"));
    DocumentCovers::flush();
    EXPECT_EQ(packKeys(root, DocumentCovers::PACK, DocumentCovers::FORMAT), QStringList{"lecture.pdf"})
            << "in the pack of its folder";
    // Stored: read back (not drawn again)
    DocumentCovers::setLibrary(CacheLocation(root));
    EXPECT_EQ(DocumentCovers::stored(item).size(), img.size());
    DocumentCovers::prune({});
    DocumentCovers::flush();
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "previews.pack"));
    DocumentCovers::setLibrary({});
}

TEST_F(LibraryTest, previewsOfAFolderAreOnePackReadWhenFirstWanted) {
    makePdf(root / "Physics" / "a.pdf");
    makePdf(root / "Physics" / "b.pdf");
    makeWordPdf(root / "Physics" / "c.pdf", "zebra");
    makePdf(root / "top.pdf");
    DocumentCovers::setLibrary(CacheLocation(root));
    DocumentCovers::setWriteDelays(50, 500);
    for (const auto& item: DocumentFiles::scanRecursive(root)) {
        ASSERT_FALSE(DocumentCovers::cover(item).isNull());
    }
    waitFor([&] { return fs::exists(root / "Physics" / DocumentFiles::META_DIR / "previews.pack"); });
    DocumentCovers::flush();
    EXPECT_EQ(packKeys(root / "Physics", DocumentCovers::PACK, DocumentCovers::FORMAT),
              (QStringList{"a.pdf", "b.pdf", "c.pdf"}));
    EXPECT_EQ(packKeys(root, DocumentCovers::PACK, DocumentCovers::FORMAT), QStringList{"top.pdf"});

    // Read once per folder
    DocumentCovers::setLibrary(CacheLocation(root));
    const int reads = DocumentCovers::packsRead();
    const QImage a = DocumentCovers::stored(DocumentFiles::itemOf(root / "Physics" / "a.pdf"));
    const QImage c = DocumentCovers::stored(DocumentFiles::itemOf(root / "Physics" / "c.pdf"));
    EXPECT_FALSE(a.isNull());
    EXPECT_NE(a, c);
    EXPECT_EQ(DocumentCovers::packsRead(), reads + 1);

    // A changed document: its new preview takes the place of the old one
    fs::remove(root / "Physics" / "a.pdf");
    makeWordPdf(root / "Physics" / "a.pdf", "giraffe");
    const DocumentItem changed = DocumentFiles::itemOf(root / "Physics" / "a.pdf");
    EXPECT_TRUE(DocumentCovers::stored(changed).isNull()) << "the stored one is of the old file";
    EXPECT_NE(DocumentCovers::cover(changed), a);
    // Renamed in the app: the preview goes along
    fs::rename(root / "Physics" / "b.pdf", root / "b.pdf");
    DocumentCovers::moved({{root / "Physics" / "b.pdf", root / "b.pdf"}});
    EXPECT_FALSE(DocumentCovers::stored(DocumentFiles::itemOf(root / "b.pdf")).isNull());
    DocumentCovers::flush();
    EXPECT_EQ(packKeys(root / "Physics", DocumentCovers::PACK, DocumentCovers::FORMAT), (QStringList{"a.pdf", "c.pdf"}));
    EXPECT_EQ(packKeys(root, DocumentCovers::PACK, DocumentCovers::FORMAT), (QStringList{"b.pdf", "top.pdf"}));
    DocumentCovers::setWriteDelays(WriteScheduler::QUIET_MS, WriteScheduler::MAX_DELAY_MS);
    DocumentCovers::setLibrary({});
}

/// A preview as stored (PNG) and as drawn compare equal.
static QImage argb(const QImage& img) { return img.convertToFormat(QImage::Format_ARGB32); }

// A saved document gets a new stamp, so its preview is drawn again. When its first page looks as before, the
// folder's previews.pack (0.3-0.6 MB, uploaded by sync clients) is not written again; only when it changed.
TEST_F(LibraryTest, previewsPackIsRewrittenOnlyWhenTheFirstPageChanged) {
    const fs::path xopp = root / "notes.xopp";
    fs::copy_file(fixture(u8"load/pages.xopp"), xopp);
    makePdf(root / "other.pdf");
    const fs::path pack = root / DocumentFiles::META_DIR / "previews.pack";
    DocumentCovers::setLibrary(CacheLocation(root));
    const QImage first = DocumentCovers::cover(DocumentFiles::itemOf(xopp));
    ASSERT_FALSE(first.isNull());
    ASSERT_FALSE(DocumentCovers::cover(DocumentFiles::itemOf(root / "other.pdf")).isNull());
    DocumentCovers::flush();
    ASSERT_TRUE(fs::exists(pack));
    const auto packTime = fs::last_write_time(pack);
    const int writes = DocumentCovers::packsWritten();
    const int stampWrites = DocumentCovers::stampPacksWritten();

    // Page 3 edited: the preview is drawn again (a new stamp), looks the same, and previews.pack is left alone
    QThread::msleep(20);  // (a new modification time)
    addText(xopp, 2, "unicorn");
    const DocumentItem edited = DocumentFiles::itemOf(xopp);
    EXPECT_TRUE(DocumentCovers::stored(edited).isNull()) << "not known yet whether the stored one still fits";
    EXPECT_EQ(argb(DocumentCovers::cover(edited)), argb(first));
    DocumentCovers::flush();
    EXPECT_EQ(DocumentCovers::packsWritten(), writes) << "previews.pack not written again";
    EXPECT_EQ(DocumentCovers::stampPacksWritten(), stampWrites + 1);
    EXPECT_EQ(fs::last_write_time(pack), packTime);
    const fs::path stamps = root / DocumentFiles::META_DIR / "preview-stamps.pack";
    EXPECT_TRUE(fs::exists(stamps)) << "the new stamp is kept in the small pack";
    std::error_code sizeError;
    EXPECT_LT(fs::file_size(stamps, sizeError), 1024u);
    EXPECT_EQ(argb(DocumentCovers::stored(edited)), argb(first)) << "the stored preview is valid for the new version";
    // ... also when the library is opened again (kept on disk)
    DocumentCovers::setLibrary(CacheLocation(root));
    EXPECT_EQ(argb(DocumentCovers::stored(edited)), argb(first));
    EXPECT_FALSE(DocumentCovers::stored(DocumentFiles::itemOf(root / "other.pdf")).isNull());
    EXPECT_EQ(DocumentCovers::packsWritten(), writes);

    // Page 1 edited: a new preview, written into previews.pack
    QThread::msleep(20);
    addText(xopp, 0, "giraffe");
    const DocumentItem firstPage = DocumentFiles::itemOf(xopp);
    const QImage changed = DocumentCovers::cover(firstPage);
    EXPECT_NE(argb(changed), argb(first));
    DocumentCovers::flush();
    EXPECT_EQ(DocumentCovers::packsWritten(), writes + 1);
    EXPECT_NE(fs::last_write_time(pack), packTime);
    EXPECT_FALSE(fs::exists(stamps)) << "folded into previews.pack";
    DocumentCovers::setLibrary(CacheLocation(root));
    EXPECT_EQ(argb(DocumentCovers::stored(firstPage)), argb(changed));
    EXPECT_FALSE(DocumentCovers::stored(DocumentFiles::itemOf(root / "other.pdf")).isNull());
    DocumentCovers::setLibrary({});
}

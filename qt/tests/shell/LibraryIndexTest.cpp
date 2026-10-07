/*
 * xournal-qt: keeping the search index up to date: what is read again after a change, documents renamed or moved (also
 * while an update runs), attached PDFs.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryTestSupport.h"

TEST_F(LibraryTest, onlyTheXoppIsReadAgainWhenAnnotationsChange) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    ASSERT_EQ(index.documentsRead(), 1);
    ASSERT_EQ(index.pdfPagesRead(), 2);

    addText(root / "lecture.xopp", 1, "unicorn");  // an annotation on page 2
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_EQ(index.documentsRead(), 2) << "the .xopp is read again";
    EXPECT_EQ(index.pdfPagesRead(), 2) << "the PDF text is kept";
    auto hits = index.search("unicorn");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].firstPage, 1);
    ASSERT_EQ(index.search("page 2").size(), 1u) << "the PDF text is still there";

    // Nothing changed: nothing is read, also not by a new index (from the stored files)
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_EQ(index.documentsRead(), 2);
    index.flush();
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0);
    EXPECT_EQ(again.search("unicorn").size(), 1u);

    // A new PDF version: its text is read again
    fs::remove(root / "lecture.pdf");
    makeWordPdf(root / "lecture.pdf", "zebra");
    fs::resize_file(root / "lecture.pdf", fs::file_size(root / "lecture.pdf"));  // (only the content changed)
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.search("zebra").size(), 1u);
    EXPECT_TRUE(again.search("page 2").empty());
    EXPECT_EQ(again.search("unicorn").size(), 1u) << "the text elements stay";
}

TEST_F(LibraryTest, renamedAndMovedDocumentsKeepTheirIndex) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    addText(root / "lecture.xopp", 1, "unicorn");
    makePdf(root / "Archive" / "old.pdf");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    LibraryIndex* index = model.searchIndex();
    index->waitForDone();
    ASSERT_EQ(index->documentsRead(), 2);
    const int pdfPagesAtStart = index->pdfPagesRead();
    auto rowOf = [&](const fs::path& p) { return model.rowOf(QString::fromStdString(p.string())); };
    auto foundIn = [&](const char* query) {
        index->waitForDone();
        const auto hits = index->search(query);
        return hits.empty() ? fs::path() : hits.front().file;
    };

    ASSERT_TRUE(model.rename(rowOf(root / "lecture.xopp"), "Week 1"));
    EXPECT_EQ(foundIn("unicorn"), root / "Week 1.xopp");
    ASSERT_TRUE(model.moveTo(rowOf(root / "Week 1.xopp"), "Archive"));
    EXPECT_EQ(foundIn("unicorn"), root / "Archive" / "Week 1.xopp");
    ASSERT_TRUE(model.createFolder("Semester"));
    ASSERT_TRUE(model.moveTo(rowOf(root / "Archive"), "Semester"));  // a folder with both documents
    EXPECT_EQ(foundIn("unicorn"), root / "Semester" / "Archive" / "Week 1.xopp");
    EXPECT_EQ(index->pdfPagesRead(), pdfPagesAtStart) << "no PDF text is read again";
    EXPECT_EQ(index->documentsRead(), 3) << "only the .xopp written again by the rename (new PDF path); the move "
                                            "writes the same bytes again (the PDF's path relative to it stays), so its "
                                            "entry is taken over by its content hash; the folder move reads nothing";
    EXPECT_EQ(index->entriesAdopted(), 1);
    EXPECT_EQ(index->search("xournal").size(), 2u);

    index->flush();
    EXPECT_EQ(packKeys(root / "Semester" / "Archive", LibraryIndex::NOTES_PACK),
              (QStringList{"Week 1.xopp", "old.pdf"}))
            << "the entries moved along";
    EXPECT_TRUE(packKeys(root / "Semester" / "Archive", LibraryIndex::PDF_TEXT_PACK).contains("Week 1.xopp"));
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack")) << "no documents left at the top";

    // Renamed by another program: the PDF text is taken over (same file: size and time), only the .xopp is read
    const int pdfPages = index->pdfPagesRead();
    fs::rename(root / "Semester" / "Archive" / "old.pdf", root / "Semester" / "Archive" / "older.pdf");
    model.refresh();
    index->waitForDone();
    EXPECT_EQ(index->pdfPagesRead(), pdfPages);
    EXPECT_EQ(index->search("xournal").size(), 2u);
    EXPECT_EQ(model.searchIndex()->pageCount(root / "Semester" / "Archive" / "older.pdf"), 2);
}

TEST_F(LibraryTest, aDocumentMovedWhileAnUpdateLooksAtItKeepsItsEntry) {
    // What made renamedAndMovedDocumentsKeepTheirIndex flaky: an update (of a folder made just before) still runs
    // when the app moves a folder; the document is found on disk, then it is gone. Its entry must stay for the move.
    makePdf(root / "Archive" / "lecture.pdf");
    makeAnnotation(root / "Archive" / "lecture.pdf", root / "Archive" / "lecture.xopp");
    addText(root / "Archive" / "lecture.xopp", 1, "unicorn");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    const int docs = index.documentsRead(), pages = index.pdfPagesRead();
    ASSERT_EQ(pages, 2);

    fs::create_directories(root / "Semester");
    const auto listed = DocumentFiles::scanRecursive(root);  // (made before the move)
    bool movedOnce = false;
    index.setCheckHook([&](const fs::path& file) {
        if (!movedOnce && file == root / "Archive" / "lecture.xopp") {
            movedOnce = true;
            fs::rename(root / "Archive", root / "Semester" / "Archive");
        }
    });
    index.update(listed);
    index.waitForDone();
    index.setCheckHook({});
    ASSERT_TRUE(movedOnce);
    index.moved({{root / "Archive", root / "Semester" / "Archive"}});
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();

    EXPECT_EQ(index.pdfPagesRead(), pages) << "no PDF text is read again";
    EXPECT_EQ(index.documentsRead(), docs) << "nothing is read";
    const auto hits = index.search("unicorn");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].file, root / "Semester" / "Archive" / "lecture.xopp");
    EXPECT_EQ(index.search("xournal").size(), 1u);
}

TEST_F(LibraryTest, aRenameSeenBeforeItIsToldKeepsTheEntry) {
    // The file system watcher can make the library look again before the index was told about a move: the
    // documents are found under their new names first. Their entries are taken over by size and time.
    makePdf(root / "sheet.pdf");
    std::ofstream(root / "notes.md") << "# Diary\n\nA walrus on the beach.\n";
    fs::create_directories(root / "Archive");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    const int docs = index.documentsRead(), pages = index.pdfPagesRead();
    ASSERT_EQ(docs, 2);

    const std::vector<std::pair<fs::path, fs::path>> moves{{root / "sheet.pdf", root / "Week 1.pdf"},
                                                           {root / "notes.md", root / "Archive" / "Diary.md"}};
    for (const auto& [from, to]: moves) {
        fs::rename(from, to);
    }
    index.update(DocumentFiles::scanRecursive(root));  // the rescan first
    index.waitForDone();
    EXPECT_EQ(index.documentsRead(), docs) << "found again by size and time, under another name";
    index.moved(moves);  // then the move
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();

    EXPECT_EQ(index.documentsRead(), docs);
    EXPECT_EQ(index.pdfPagesRead(), pages);
    ASSERT_EQ(index.search("walrus").size(), 1u);
    EXPECT_EQ(index.search("walrus")[0].file, root / "Archive" / "Diary.md");
    ASSERT_EQ(index.search("xournal").size(), 1u);
    EXPECT_EQ(index.search("xournal")[0].file, root / "Week 1.pdf");
    EXPECT_EQ(index.pageCount(root / "Week 1.pdf"), 2);
    index.flush();
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"Week 1.pdf"});
    EXPECT_EQ(packKeys(root / "Archive", LibraryIndex::NOTES_PACK), QStringList{"Diary.md"});
}

TEST_F(LibraryTest, anotherDocumentWithTheSameSizeAndTimeIsNotTakenForAMovedOne) {
    // Two different files can have the same size and time (e.g. unpacked from an archive): the entry of one that is
    // gone is not taken over by the other (its content is compared too), whatever their names.
    std::ofstream(root / "alpha.md") << "The alpha notes.\n";
    std::ofstream(root / "sigma.md") << "The sigma notes.\n";
    const auto time = fs::file_time_type::clock::now() - std::chrono::hours(1);
    fs::last_write_time(root / "alpha.md", time);
    fs::last_write_time(root / "sigma.md", time);
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    const int docs = index.documentsRead();
    ASSERT_EQ(index.search("sigma").size(), 1u);

    // Both gone; others with the same size and time come: one with another name, one with the same name in another
    // folder
    fs::remove(root / "alpha.md");
    fs::remove(root / "sigma.md");
    fs::create_directories(root / "Archive");
    std::ofstream(root / "gamma.md") << "The gamma notes.\n";
    std::ofstream(root / "Archive" / "alpha.md") << "The delta notes.\n";
    fs::last_write_time(root / "gamma.md", time);
    fs::last_write_time(root / "Archive" / "alpha.md", time);
    ASSERT_EQ(fs::file_size(root / "gamma.md"), 17u);
    ASSERT_EQ(fs::file_size(root / "Archive" / "alpha.md"), 17u);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();

    EXPECT_EQ(index.documentsRead(), docs + 2) << "both are read";
    EXPECT_TRUE(index.search("alpha notes").empty());
    EXPECT_TRUE(index.search("sigma").empty());
    ASSERT_EQ(index.search("gamma").size(), 1u);
    EXPECT_EQ(index.search("gamma")[0].file, root / "gamma.md");
    ASSERT_EQ(index.search("delta").size(), 1u);
    EXPECT_EQ(index.search("delta")[0].file, root / "Archive" / "alpha.md");
}

TEST_F(LibraryTest, changesOfAttachedOrOtherPdfsAreNoticed) {
    // Attached PDF ("name.xopp.bg.pdf")
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp"), root / "att.xopp");
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), root / "att.xopp.bg.pdf");
    // A PDF outside the library
    QTemporaryDir outside;
    const fs::path script = fs::path(outside.path().toStdString()) / "script.pdf";
    makePdf(script);
    makeAnnotation(script, root / "notes.xopp");

    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    ASSERT_EQ(index.search("xournal").size(), 2u);

    fs::remove(root / "att.xopp.bg.pdf");
    makeWordPdf(root / "att.xopp.bg.pdf", "zebra");
    fs::remove(script);
    makeWordPdf(script, "giraffe");
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    ASSERT_EQ(index.search("zebra").size(), 1u);
    EXPECT_EQ(index.search("zebra")[0].file, root / "att.xopp");
    ASSERT_EQ(index.search("giraffe").size(), 1u);
    EXPECT_EQ(index.search("giraffe")[0].file, root / "notes.xopp");
    EXPECT_TRUE(index.search("xournal").empty());
}

// Opt-in timing: XQT_BENCH_PDF=<a long PDF>
// Starting with a big library (e.g. Downloads): everything is in the store, nothing is read again.
TEST_F(LibraryTest, benchIndexStartup) {
    if (!qEnvironmentVariableIsSet("XQT_BENCH_STARTUP")) {
        GTEST_SKIP() << "set XQT_BENCH_STARTUP=<number of documents>";
    }
    const int count = std::max(1, qEnvironmentVariableIntValue("XQT_BENCH_STARTUP"));
    if (const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF"); !pdf.isEmpty()) {
        fs::copy_file(fs::path(pdf.toStdString()), root / "source.pdf");  // a real (big) PDF
    } else {
        makePdf(root / "source.pdf");
    }
    for (int i = 0; i < count; ++i) {
        const fs::path pdf = root / ("paper" + std::to_string(i) + ".pdf");
        fs::copy_file(root / "source.pdf", pdf);
        makeAnnotation(pdf, root / ("paper" + std::to_string(i) + ".xopp"));
    }
    const fs::path dir = root / DocumentFiles::META_DIR;
    QElapsedTimer t;
    {
        LibraryIndex first(root);
        t.start();
        first.update(DocumentFiles::scanRecursive(root));
        first.waitForDone();
        std::cout << count << " documents, first indexing: " << t.elapsed() << " ms\n";
    }
    // Starting again: the stored index
    LibraryIndex again(root);
    t.restart();
    const auto items = DocumentFiles::scanRecursive(root);
    const qint64 scanned = t.elapsed();
    again.update(items);
    again.waitForDone();
    std::cout << "starting again: " << t.elapsed() << " ms (scanning the folder: " << scanned
              << " ms), documents read again: " << again.documentsRead() << "\n";
    std::cout << "index folder: " << [&] {
        uintmax_t bytes = 0;
        for (const auto& f: fs::directory_iterator(dir)) {
            bytes += fs::file_size(f);
        }
        return bytes / 1024;
    }() << " KiB\n";
    EXPECT_EQ(again.documentsRead(), 0);
}

TEST_F(LibraryTest, benchIndexUpdates) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    fs::copy_file(fs::path(pdf.toStdString()), root / "long.pdf");
    makeAnnotation(root / "long.pdf", root / "long.xopp");
    LibraryIndex index(root);
    QElapsedTimer t;
    t.start();
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    std::cout << "first indexing: " << t.elapsed() << " ms (" << index.pdfPagesRead() << " PDF pages)\n";
    addText(root / "long.xopp", 9, "unicorn");
    t.restart();
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    std::cout << "after a text on page 10: " << t.elapsed() << " ms (" << index.pdfPagesRead() << " PDF pages in all)\n";
    t.restart();
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    std::cout << "nothing changed: " << t.elapsed() << " ms\n";
}

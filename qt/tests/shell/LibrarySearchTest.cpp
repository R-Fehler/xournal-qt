/*
 * xournal-qt: the library search: text and names, folder names, names only, hit pages, the open documents' text shared
 * with the index.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryTestSupport.h"

TEST_F(LibraryTest, indexFindsTextAndNames) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    fs::copy_file(fixture(u8"load/pages.xopp"), root / "Page 2 notes.xopp");  // text elements "p1".."p10"
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_FALSE(index.busy());
    EXPECT_EQ(index.indexed(), 2);

    auto hits = index.search("PAGE 2");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_TRUE(hits[0].inName) << "name matches first";
    EXPECT_EQ(hits[1].file, root / "lecture.xopp");
    EXPECT_EQ(hits[1].firstPage, 1);
    EXPECT_FALSE(hits[1].snippet.isEmpty());
    hits = index.search("p7");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].firstPage, 6);
    EXPECT_TRUE(index.search("nothing like this").empty());
    EXPECT_EQ(index.pageCount(root / "lecture.xopp"), 2);

    // Stored: another index reads it back
    index.flush();
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.search("p7").size(), 1u);
    EXPECT_EQ(again.documentsRead(), 0);
    // Removed documents are dropped
    fs::remove(root / "Page 2 notes.xopp");
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_TRUE(again.search("p7").empty());
    again.flush();
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"lecture.xopp"});
}

TEST_F(LibraryTest, searchFindsFolderNames) {
    makePdf(root / "Physics" / "sheet.pdf");
    fs::create_directories(root / "Math" / "Physics Lab");
    fs::create_directories(root / "Chemistry");
    touch(root / "physics notes.xopp");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    model.setSearchQuery("PHYS");
    ASSERT_EQ(model.count(), 3) << "two folders, then the document with that name";
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::IsFolderRole).toBool());
    EXPECT_TRUE(model.data(model.index(1), LibraryModel::IsFolderRole).toBool());
    EXPECT_FALSE(model.data(model.index(2), LibraryModel::IsFolderRole).toBool());
    const int lab = model.rowOf(QString::fromStdString((root / "Math" / "Physics Lab").string()));
    ASSERT_GE(lab, 0);
    EXPECT_EQ(model.data(model.index(lab), LibraryModel::LocationRole).toString(), "Math");
}

// The reduced search: names only - of documents, and of folders unless the flat list is shown - not the text in
// the documents.
TEST_F(LibraryTest, searchCanLookAtNamesOnly) {
    touch(root / "Xournal diary.xopp");
    makePdf(root / "lecture.pdf");  // its text has "xournal"
    fs::create_directories(root / "Old" / "xournal things");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    const auto names = [&] {
        QStringList out;
        for (int i = 0; i < model.count(); ++i) {
            out << model.data(model.index(i), LibraryModel::NameRole).toString();
        }
        return out;
    };

    model.setSearchQuery("xournal");
    EXPECT_EQ(model.count(), 3) << "the folder, the name, and the text: " << names().join(", ").toStdString();
    EXPECT_TRUE(names().contains("lecture")) << "found in its text";

    model.setNamesOnly(true);
    EXPECT_EQ(names(), (QStringList{"xournal things", "Xournal diary"})) << "the folder and the name, not the text";
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::IsFolderRole).toBool());

    model.setFlat(true);
    EXPECT_EQ(names(), QStringList{"Xournal diary"}) << "the flat list shows no folders";

    model.setNamesOnly(false);
    model.setFlat(false);
    EXPECT_EQ(model.count(), 3) << "the full search again";
}

TEST_F(LibraryTest, hitPagesAreMarkedAndKept) {
    makePdf(root / "lecture.pdf");
    HitPageProvider::clearCaches();
    const int before = HitPageProvider::renderCount();
    const QImage plain = HitPageProvider::render(root / "lecture.pdf", 1, "", 190);
    ASSERT_FALSE(plain.isNull());
    EXPECT_EQ(plain.width(), 192) << "widths in steps of 64";
    const QImage marked = HitPageProvider::render(root / "lecture.pdf", 1, "page 2", 180);
    EXPECT_EQ(HitPageProvider::renderCount(), before + 1) << "the second request only adds the marks";
    ASSERT_EQ(marked.size(), plain.size());
    EXPECT_NE(marked, plain) << "the hit is marked";
    // Marks are yellow-ish: some pixel lost blue but not red
    bool yellow = false;
    for (int y = 0; y < marked.height() && !yellow; ++y) {
        for (int x = 0; x < marked.width() && !yellow; ++x) {
            const QColor c = marked.pixelColor(x, y), o = plain.pixelColor(x, y);
            yellow = c != o && c.blue() < o.blue() && c.red() >= o.red() - 2;
        }
    }
    EXPECT_TRUE(yellow);
    EXPECT_TRUE(HitPageProvider::render(root / "lecture.pdf", 5, "x", 180).isNull()) << "no such page";
    EXPECT_TRUE(HitPageProvider::baseUrl(DocumentFiles::itemOf(root / "lecture.pdf"), "page 2").startsWith("image://hitpage/"));
}

// Opt-in timing: XQT_BENCH_PDF=<a long PDF> XQT_BENCH_QUERY=<text>
TEST_F(LibraryTest, benchHitPages) {
    const QString pdf = qEnvironmentVariable("XQT_BENCH_PDF");
    if (pdf.isEmpty()) {
        GTEST_SKIP() << "set XQT_BENCH_PDF";
    }
    const QString query = qEnvironmentVariable("XQT_BENCH_QUERY", "e");
    HitPageProvider::clearCaches();
    QElapsedTimer t;
    t.start();
    HitPageProvider::render(fs::path(pdf.toStdString()), 0, query, 256);
    std::cout << "first page (load + draw + marks): " << t.elapsed() << " ms\n";
    t.restart();
    for (int p = 1; p <= 20; ++p) {
        HitPageProvider::render(fs::path(pdf.toStdString()), p, query, 256);
    }
    std::cout << "20 more pages: " << t.elapsed() << " ms\n";
    t.restart();
    for (int p = 1; p <= 20; ++p) {
        HitPageProvider::render(fs::path(pdf.toStdString()), p, query + "x", 256);
    }
    std::cout << "the same 20 pages, other search (marks only): " << t.elapsed() << " ms\n";
}

// The PDF text the index read is what an open document of the same PDF searches, as long as the PDF is the same file
// (size and time); a saved document hands its entry over, so the index does not read the .xopp again.
TEST_F(LibraryTest, openDocumentsShareTheirTextWithTheIndex) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    ASSERT_EQ(index.documentsRead(), 1);
    const auto known = index.knownPdfText(root / "lecture.pdf");
    ASSERT_EQ(known.size(), 2u) << "both pages";
    EXPECT_TRUE(known.at(1).contains("Page 2"));
    EXPECT_TRUE(index.knownPdfText(root / "other.pdf").empty());

    // Annotated and saved in the app: the entry comes from the document in memory
    auto loaded = DocumentSession::loadFile(root / "lecture.xopp");
    ASSERT_TRUE(loaded.document);
    auto t = std::make_unique<Text>();
    t->setText("unicorn");
    t->move(100, 100);
    loaded.document->getPage(1)->getSelectedLayer()->addElement(std::move(t));
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, root / "lecture.xopp").ok);
    ASSERT_TRUE(index.documentSaved(root / "lecture.xopp", *loaded.document, known));
    EXPECT_EQ(index.savedTakenOver(), 1);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_EQ(index.documentsRead(), 1) << "the saved .xopp is not read again";
    auto hits = index.search("unicorn");
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].firstPage, 1);
    EXPECT_EQ(index.search("page 2").size(), 1u) << "with its PDF text";
    index.flush();
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0) << "and stored";
    EXPECT_EQ(again.search("unicorn").size(), 1u);

    // Outside the library, or without its PDF text: not taken over (read as usual)
    EXPECT_FALSE(index.documentSaved(fs::path(tmp.path().toStdString()) / ".." / "elsewhere.xopp", *loaded.document,
                                     known));

    // A changed PDF: its old text is not handed out
    fs::remove(root / "lecture.pdf");
    makeWordPdf(root / "lecture.pdf", "zebra");
    EXPECT_TRUE(index.knownPdfText(root / "lecture.pdf").empty());
}

// In the app: a library document opened for the first time searches the text its library read (all counts at once,
// no PDF read), and saving it hands its entry to the library.
TEST_F(LibraryTest, theAppSeedsTheSearchOfOpenDocumentsFromTheLibrary) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    AppController c;
    c.setLibraryRoot(root);
    LibraryIndex* index = qobject_cast<LibraryModel*>(c.libraryModel())->searchIndex();
    ASSERT_NE(index, nullptr);
    waitFor([&] { return !index->busy() && index->indexed() == 1; });
    index->waitForDone();
    ASSERT_TRUE(c.openPath(QString::fromStdString((root / "lecture.xopp").string())));
    DocumentSession* s = c.tabManager().currentSession();
    s->search().setQuery("Page 2", false);
    EXPECT_FALSE(s->search().isRunning()) << "all counts at once";
    EXPECT_EQ(s->search().hitCount(), 1);
    EXPECT_EQ(s->search().textIndex().pdfPagesSeeded(), 2);
    EXPECT_EQ(s->search().textIndex().pdfPagesRead(), 0);

    auto t = std::make_unique<Text>();
    t->setText("unicorn");
    t->move(100, 100);
    s->getDocument()->lock();
    s->getDocument()->getPage(0)->getSelectedLayer()->addElement(std::move(t));
    s->getDocument()->unlock();
    ASSERT_TRUE(c.save());
    EXPECT_EQ(index->savedTakenOver(), 1);
    EXPECT_EQ(index->search("unicorn").size(), 1u) << "found in the library at once";
}

// The library search matches text as the search of an open document does (so their counts agree): a word broken at a
// line end is found whole, a ligature as its letters.
TEST_F(LibraryTest, theLibrarySearchMatchesAsTheDocumentSearch) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    addText(root / "lecture.xopp", 1, "a hyphen-\nated word, the \xef\xac\x81rst one");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    auto hits = index.search("hyphenated");
    ASSERT_EQ(hits.size(), 1u) << "broken at the line end";
    EXPECT_EQ(hits[0].firstPage, 1);
    EXPECT_TRUE(hits[0].snippet.contains("hyphen- ated")) << hits[0].snippet.toStdString();
    EXPECT_EQ(index.search("first one").size(), 1u) << "the ligature";
    EXPECT_EQ(index.search("PAGE 2").size(), 1u);
}

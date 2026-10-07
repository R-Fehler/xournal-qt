/*
 * xournal-qt: title pages, reading positions and when documents were last read (DocumentPlaces): in the config folder,
 * never in a cache.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryTestSupport.h"

// The title page and the page a document was left at are kept beside it: for its library in the config folder, by
// its path in the library; they follow it when it is renamed or moved. The preview shows the title page.
TEST_F(LibraryTest, titleAndLastPagesAreKeptPerLibraryInTheConfig) {
    makePdf(root / "Physics" / "sheet.pdf");
    makePdf(root / "lecture.pdf");  // two pages: "xournal" / "Page 2"
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    const fs::path sheet = root / "Physics" / "sheet.pdf";
    EXPECT_EQ(DocumentPlaces::titlePage(sheet), 0) << "the first page unless chosen";
    EXPECT_EQ(DocumentPlaces::lastPage(sheet), -1) << "not known yet";

    DocumentPlaces::setTitlePage(sheet, 1);
    DocumentPlaces::setLastPage(sheet, 1);
    EXPECT_EQ(DocumentPlaces::titlePage(sheet), 1);
    EXPECT_EQ(DocumentPlaces::lastPage(sheet), 1);
    QFile stored(QString::fromStdString((Library(root).configDir() / "pages.json").string()));
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly)) << "in the config folder";
    EXPECT_NE(Library(root).configDir().string().find("libraries"), std::string::npos);
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "pages.json")) << "not in the cache";
    const QByteArray json = stored.readAll();
    EXPECT_TRUE(json.contains("Physics/sheet.pdf")) << "by its path in the library: " << json.toStdString();

    // Renamed folder: the entry follows
    DocumentPlaces::moved({{root / "Physics", root / "Science"}});
    EXPECT_EQ(DocumentPlaces::titlePage(root / "Science" / "sheet.pdf"), 1);
    EXPECT_EQ(DocumentPlaces::titlePage(sheet), 0) << "nothing left at the old place";

    // Outside the library: kept elsewhere (here: in the test's folder)
    QTemporaryDir other;  // (not in the library: that is the whole temporary folder of the test)
    const fs::path outsideFile = fs::path(other.filePath("outside.json").toStdString());
    DocumentPlaces::setOutsideFile(outsideFile);
    const fs::path elsewhere = fs::path(other.filePath("elsewhere.xopp").toStdString());
    DocumentPlaces::setTitlePage(elsewhere, 3);
    EXPECT_EQ(DocumentPlaces::titlePage(elsewhere), 3);
    EXPECT_TRUE(fs::exists(outsideFile));

    // The preview shows the title page (a new URL: QML shows it anew); the first page keeps the URL it always had
    const DocumentItem lecture = DocumentFiles::itemOf(root / "lecture.pdf");
    const QString firstUrl = DocumentCovers::url(lecture);
    const QImage first = DocumentCovers::cover(lecture);
    ASSERT_FALSE(first.isNull());
    DocumentPlaces::setTitlePage(lecture.main(), 1);
    EXPECT_NE(DocumentCovers::url(lecture), firstUrl);
    EXPECT_TRUE(DocumentCovers::stored(lecture).isNull()) << "the stored one shows another page";
    const QImage second = DocumentCovers::cover(lecture);
    ASSERT_FALSE(second.isNull());
    EXPECT_NE(first, second) << "another page";
    DocumentPlaces::setTitlePage(lecture.main(), 0);
    EXPECT_EQ(DocumentCovers::url(lecture), firstUrl);
}

// Reading positions are not cache: removing the cache folders keeps them.
TEST_F(LibraryTest, readingPositionsSurviveRemovingTheCache) {
    makePdf(root / "Physics" / "sheet.pdf");
    makePdf(root / "lecture.pdf");
    {
        LibraryModel model;
        model.setLibrary(std::make_unique<Library>(root));
        DocumentPlaces::setLastPage(root / "Physics" / "sheet.pdf", 1);
        model.searchIndex()->waitForDone();
    }
    for (const fs::path& f: {root, root / "Physics"}) {
        fs::remove_all(f / DocumentFiles::META_DIR);
    }
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    EXPECT_EQ(DocumentPlaces::lastPage(root / "Physics" / "sheet.pdf"), 1);

}

// Documents outside a library keep their reading positions (and stars) in the app's config folder, not in its cache
TEST_F(LibraryTest, readingPositionsOutsideALibraryAreKeptInTheConfig) {
    const fs::path file = DocumentPlaces::defaultOutsideFile();
    const auto below = [](const fs::path& p, const fs::path& folder) {
        const fs::path rel = p.lexically_normal().lexically_relative(folder.lexically_normal());
        return !rel.empty() && *rel.begin() != "..";
    };
    EXPECT_TRUE(below(file, Util::getConfigSubfolder())) << file;
    EXPECT_FALSE(below(file, Util::getCacheSubfolder())) << file;

    DocumentPlaces::setOutsideFile(file);
    const fs::path elsewhere = root / "elsewhere.xopp";
    DocumentPlaces::setLastPage(elsewhere, 2);
    EXPECT_TRUE(fs::exists(file));
    EXPECT_EQ(DocumentPlaces::lastPage(elsewhere), 2);
    DocumentPlaces::setLastPage(elsewhere, -1);
}

// The library sorts by when its documents were last read in the app, and shows when and at which page.
TEST_F(LibraryTest, documentsAreSortedByWhenTheyWereLastRead) {
    makePdf(root / "alpha.pdf");
    makePdf(root / "beta.pdf");
    touch(root / "gamma.xopp");
    fs::create_directories(root / "Folder");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    DocumentPlaces::setRead(root / "alpha.pdf", 1000);
    DocumentPlaces::setLastPage(root / "alpha.pdf", 1);
    DocumentPlaces::setRead(root / "gamma.xopp", 2000);
    model.setSortBy("read");
    EXPECT_EQ(model.sortBy(), "read");
    auto names = [&] {
        QStringList out;
        for (int i = 0; i < model.count(); ++i) {
            out << model.data(model.index(i), LibraryModel::NameRole).toString();
        }
        return out;
    };
    EXPECT_EQ(names(), (QStringList{"Folder", "gamma", "alpha", "beta"}))
            << "folders first, then the last read; never read at the end";
    const int alpha = model.rowOf(QString::fromStdString((root / "alpha.pdf").string()));
    EXPECT_EQ(model.data(model.index(alpha), LibraryModel::LastReadRole).toDateTime().toSecsSinceEpoch(), 1000);
    EXPECT_EQ(model.data(model.index(alpha), LibraryModel::LastPageRole).toInt(), 1);
    const int beta = model.rowOf(QString::fromStdString((root / "beta.pdf").string()));
    EXPECT_TRUE(model.data(model.index(beta), LibraryModel::LastReadRole).isNull()) << "never read";

    // Read again: to the front once the library is shown again
    DocumentPlaces::setRead(root / "beta.pdf", 3000);
    model.placesChanged();
    EXPECT_EQ(names(), (QStringList{"Folder", "beta", "gamma", "alpha"}));
}

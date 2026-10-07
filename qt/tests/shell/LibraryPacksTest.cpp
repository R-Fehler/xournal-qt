/*
 * xournal-qt: the index in per-folder packs, where the cache is kept (the folders or the app cache) and removing it; a
 * benchmark of the cache on a generated library.
 *
 * @license GNU GPLv2 or later
 */
#include "LibraryTestSupport.h"

// --- the index in per-folder packs ---

namespace {
/// A library with documents at the top and in two levels of folders, and a folder without documents.
void makeFolders(const fs::path& root) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    makePdf(root / "Physics" / "sheet.pdf");
    fs::copy_file(fixture(u8"load/pages.xopp"), root / "Physics" / "notes.xopp");  // text elements "p1".."p10"
    makeWordPdf(root / "Physics" / "Mechanics" / "forces.pdf", "zebra");
    fs::create_directories(root / "Empty");
}
struct FileState {
    uintmax_t size;
    fs::file_time_type time;
};
std::map<fs::path, FileState> snapshot(const std::vector<fs::path>& files) {
    std::map<fs::path, FileState> s;
    for (const auto& f: files) {
        std::error_code ec;
        s[f] = {fs::file_size(f, ec), fs::last_write_time(f, ec)};
    }
    return s;
}
/// Bytes and files written between two snapshots (new or changed files).
std::pair<uintmax_t, int> written(const std::map<fs::path, FileState>& before, const std::map<fs::path, FileState>& after) {
    uintmax_t bytes = 0;
    int files = 0;
    for (const auto& [f, st]: after) {
        auto it = before.find(f);
        if (it == before.end() || it->second.size != st.size || it->second.time != st.time) {
            bytes += st.size;
            ++files;
        }
    }
    return {bytes, files};
}
fs::file_time_type anHourAgo() { return fs::file_time_type::clock::now() - std::chrono::hours(1); }
}  // namespace

TEST_F(LibraryTest, eachFolderKeepsTheIndexOfItsOwnDocuments) {
    makeFolders(root);
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
    }  // (written when closed)
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"lecture.xopp"});
    EXPECT_EQ(packKeys(root / "Physics", LibraryIndex::NOTES_PACK), (QStringList{"notes.xopp", "sheet.pdf"}));
    EXPECT_EQ(packKeys(root / "Physics", LibraryIndex::PDF_TEXT_PACK), QStringList{"sheet.pdf"})
            << "only documents with PDF pages have PDF text";
    EXPECT_EQ(packKeys(root / "Physics" / "Mechanics", LibraryIndex::NOTES_PACK), QStringList{"forces.pdf"});
    EXPECT_FALSE(fs::exists(root / "Empty" / DocumentFiles::META_DIR)) << "no documents, no cache folder";
    // Nothing else in the cache folders
    for (const fs::path& f: {root, root / "Physics", root / "Physics" / "Mechanics"}) {
        for (const auto& e: fs::directory_iterator(f / DocumentFiles::META_DIR)) {
            const std::string name = e.path().filename().string();
            EXPECT_TRUE(name == "notes.pack" || name == "pdf-text.pack") << name;
        }
    }

    // A subfolder opened as a library of its own: its packs are there, nothing is read
    LibraryIndex physics(root / "Physics");
    physics.update(DocumentFiles::scanRecursive(root / "Physics"));
    physics.waitForDone();
    EXPECT_EQ(physics.documentsRead(), 0);
    EXPECT_EQ(physics.search("zebra").size(), 1u);
    EXPECT_EQ(physics.search("p7").size(), 1u);
    EXPECT_TRUE(physics.search("lecture").empty());
}

TEST_F(LibraryTest, anEditedXoppWritesOnlyTheNotesOfItsFolder) {
    makeFolders(root);
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    std::vector<fs::path> packs;
    for (const auto& e: fs::recursive_directory_iterator(root)) {
        if (e.path().extension() == ".pack") {
            packs.push_back(e.path());
            fs::last_write_time(e.path(), anHourAgo());
        }
    }
    ASSERT_EQ(packs.size(), 6u) << "notes and PDF text in three folders";
    const auto before = snapshot(packs);
    const int writes = index.packsWritten();

    addText(root / "Physics" / "notes.xopp", 0, "unicorn");
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    EXPECT_EQ(index.search("unicorn").size(), 1u);
    EXPECT_EQ(index.packsWritten(), writes + 1);
    const auto after = snapshot(packs);
    for (const auto& f: packs) {
        const bool changed = after.at(f).time != before.at(f).time;
        EXPECT_EQ(changed, f == root / "Physics" / DocumentFiles::META_DIR / "notes.pack") << f;
    }
}

TEST_F(LibraryTest, packsAreWrittenAWhileAfterTheLastChange) {
    makeFolders(root);
    LibraryIndex index(root);
    index.setWriteDelays(100, 1000);
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack")) << "not yet";
    waitFor([&] { return index.packsWritten() >= 6; });
    EXPECT_TRUE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack"));
    processEventsFor(150);
    EXPECT_EQ(index.packsWritten(), 6) << "each pack once";
}

TEST_F(LibraryTest, foldersAndDocumentsMovedByAnotherProgramKeepTheirIndex) {
    makeFolders(root);
    fs::copy_file(fixture(u8"load/pages.xopp"), root / "loose.xopp");
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    const int read = index.documentsRead();

    // A folder renamed, a document moved into another folder (as a file manager does it: size and time stay)
    fs::rename(root / "Physics", root / "Science");
    fs::rename(root / "loose.xopp", root / "Science" / "Mechanics" / "loose.xopp");
    index.update(DocumentFiles::scanRecursive(root));
    index.waitForDone();
    EXPECT_EQ(index.documentsRead(), read) << "nothing is read again";
    ASSERT_EQ(index.search("zebra").size(), 1u);
    EXPECT_EQ(index.search("zebra")[0].file, root / "Science" / "Mechanics" / "forces.pdf");
    const auto p7 = index.search("p7");
    ASSERT_EQ(p7.size(), 2u);
    index.flush();
    EXPECT_EQ(packKeys(root / "Science" / "Mechanics", LibraryIndex::NOTES_PACK),
              (QStringList{"forces.pdf", "loose.xopp"}));
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"lecture.xopp"}) << "gone from the top";

    // The same from the stored packs, in a new index
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0);
}

TEST_F(LibraryTest, theLastDocumentGoneTakesItsCacheFolderAlong) {
    makeFolders(root);
    LibraryIndex index(root);
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    const fs::path mechanics = root / "Physics" / "Mechanics" / DocumentFiles::META_DIR;
    const fs::path physics = root / "Physics" / DocumentFiles::META_DIR;
    ASSERT_TRUE(fs::exists(mechanics));
    touch(physics / "mine.txt");  // not the app's

    fs::remove(root / "Physics" / "Mechanics" / "forces.pdf");
    fs::remove(root / "Physics" / "sheet.pdf");
    fs::remove(root / "Physics" / "notes.xopp");
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    EXPECT_FALSE(fs::exists(mechanics));
    EXPECT_TRUE(fs::exists(physics / "mine.txt")) << "a cache folder with other files stays";
    EXPECT_FALSE(fs::exists(physics / "notes.pack"));
    EXPECT_TRUE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack"));
}

TEST_F(LibraryTest, packsOfAnotherFormatAreReadAgain) {
    makeFolders(root);
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
    }
    ASSERT_TRUE(Packs::write(root / "Physics" / DocumentFiles::META_DIR, LibraryIndex::NOTES_PACK,
                             LibraryIndex::FORMAT + 1, {{QStringLiteral("notes.xopp"), 1}}, true));
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 2) << "the two documents of that folder";
    EXPECT_EQ(again.search("p7").size(), 1u);
}

// --- where the cache is kept, and removing it (the settings) ---

TEST_F(LibraryTest, theCacheMovesToTheAppCacheAndBack) {
    makeFolders(root);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->flush();
    const DocumentItem sheet = DocumentFiles::itemOf(root / "Physics" / "sheet.pdf");
    ASSERT_FALSE(DocumentCovers::cover(sheet).isNull());
    DocumentCovers::flush();
    EXPECT_FALSE(model.cacheInAppCache()) << "in the folders by default";
    const fs::path app = Library(root).cacheLocation().appCacheDir();
    EXPECT_EQ(model.appCachePath().toStdString(), app.string());

    model.setCacheInAppCache(true);
    EXPECT_TRUE(model.cacheInAppCache());
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::AppCache) << "kept as a setting of the library";
    for (const fs::path& f: {root, root / "Physics", root / "Physics" / "Mechanics"}) {
        EXPECT_FALSE(fs::exists(f / DocumentFiles::META_DIR)) << f;
    }
    EXPECT_TRUE(fs::exists(app / DocumentFiles::META_DIR / "notes.pack"));
    EXPECT_TRUE(fs::exists(app / "Physics" / DocumentFiles::META_DIR / "previews.pack"));
    model.searchIndex()->waitForDone();
    EXPECT_EQ(model.searchIndex()->documentsRead(), 0) << "moved, not made anew";
    EXPECT_EQ(model.searchIndex()->search("zebra").size(), 1u);
    EXPECT_FALSE(DocumentCovers::stored(sheet).isNull());
    // New entries go there too
    fs::copy_file(fixture(u8"load/pages.xopp"), root / "Physics" / "Mechanics" / "more.xopp");
    model.refresh();
    model.searchIndex()->flush();
    EXPECT_EQ(Packs::read(app / "Physics" / "Mechanics" / DocumentFiles::META_DIR, LibraryIndex::NOTES_PACK,
                          LibraryIndex::FORMAT)
                      ->size(),
              2);
    EXPECT_FALSE(fs::exists(root / "Physics" / "Mechanics" / DocumentFiles::META_DIR));

    model.setCacheInAppCache(false);
    EXPECT_TRUE(fs::exists(root / "Physics" / "Mechanics" / DocumentFiles::META_DIR / "notes.pack"));
    EXPECT_FALSE(fs::exists(app)) << "nothing left in the app cache";
    model.searchIndex()->waitForDone();
    EXPECT_EQ(model.searchIndex()->documentsRead(), 0);
    model.setLibrary(nullptr);
}

// Where a library without a setting keeps its cache: in its folders on the desktop, in the app cache on Android (sync
// apps would upload the cache folders). A setting of the library wins either way.
TEST_F(LibraryTest, theCacheDefaultsToTheFoldersOnTheDesktopAndToTheAppCacheOnAndroid) {
    makeFolders(root);
    EXPECT_EQ(Library::defaultCacheMode(), CacheLocation::Mode::Folders) << "the desktop";
    EXPECT_FALSE(Library(root).hasCacheSetting());
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::Folders);

    Library::setDefaultCacheMode(CacheLocation::Mode::AppCache);  // Android
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::AppCache);
    {
        LibraryModel model;
        model.setLibrary(std::make_unique<Library>(root));
        EXPECT_TRUE(model.cacheInAppCache());
        model.searchIndex()->flush();
        const fs::path app = Library(root).cacheLocation().appCacheDir();
        EXPECT_TRUE(fs::exists(app / "Physics" / DocumentFiles::META_DIR / "notes.pack"));
        for (const fs::path& f: {root, root / "Physics", root / "Physics" / "Mechanics"}) {
            EXPECT_FALSE(fs::exists(f / DocumentFiles::META_DIR)) << f;
        }
        EXPECT_FALSE(Library(root).hasCacheSetting()) << "nothing to move: no setting written";
        model.setLibrary(nullptr);
    }
    // A setting of the library wins
    Library(root).setCacheMode(CacheLocation::Mode::Folders);
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::Folders);
    Library::setDefaultCacheMode(CacheLocation::Mode::Folders);
    Library(root).setCacheMode(CacheLocation::Mode::AppCache);
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::AppCache);
    fs::remove_all(Library(root).cacheLocation().appCacheDir());
    fs::remove(Library(root).configDir() / "library.json");
}

// The library's settings ("library.json": the cache mode, the "Show" filter) are read once per Library, not on every
// getter (cacheInAppCache is a property QML reads); what it writes is kept too.
TEST_F(LibraryTest, theLibrarySettingsAreReadOnce) {
    const Library lib(root);
    lib.setCacheMode(CacheLocation::Mode::AppCache);
    ShowFilter f;
    f.images = false;
    lib.setShowFilter(f);
    const fs::path file = lib.configDir() / "library.json";
    ASSERT_TRUE(fs::exists(file));
    EXPECT_EQ(Library(root).cacheMode(), CacheLocation::Mode::AppCache) << "on disk for the next one";
    fs::remove(file);
    EXPECT_EQ(lib.cacheMode(), CacheLocation::Mode::AppCache) << "kept, not read again";
    EXPECT_TRUE(lib.hasCacheSetting());
    EXPECT_FALSE(lib.showFilter().images);
    EXPECT_FALSE(Library(root).hasCacheSetting()) << "a new one reads the file";
}

// A library with cache folders of its own (from a desktop, or from before the default) opened where the default is
// the app cache: the folders' caches move there once, nothing is read again.
TEST_F(LibraryTest, cacheFoldersMoveToTheAppCacheWhenThatIsTheDefault) {
    makeFolders(root);
    {
        LibraryModel model;
        model.setLibrary(std::make_unique<Library>(root));
        model.searchIndex()->flush();
        model.setLibrary(nullptr);
    }
    ASSERT_TRUE(fs::exists(root / "Physics" / DocumentFiles::META_DIR / "notes.pack"));
    Library::setDefaultCacheMode(CacheLocation::Mode::AppCache);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    Library::setDefaultCacheMode(CacheLocation::Mode::Folders);
    EXPECT_TRUE(model.cacheInAppCache()) << "the setting is written";
    EXPECT_TRUE(Library(root).hasCacheSetting());
    const fs::path app = Library(root).cacheLocation().appCacheDir();
    for (const fs::path& f: {root, root / "Physics", root / "Physics" / "Mechanics"}) {
        EXPECT_FALSE(fs::exists(f / DocumentFiles::META_DIR)) << f;
    }
    EXPECT_TRUE(fs::exists(app / "Physics" / DocumentFiles::META_DIR / "notes.pack"));
    model.searchIndex()->waitForDone();
    EXPECT_EQ(model.searchIndex()->documentsRead(), 0) << "moved, not made anew";
    EXPECT_EQ(model.searchIndex()->search("zebra").size(), 1u);
    model.setLibrary(nullptr);
    fs::remove_all(app);
    fs::remove(Library(root).configDir() / "library.json");
}

// In the app cache, a folder moved by another program leaves its cache behind: its documents are found again by
// name, size and time, and the old place is cleaned up.
TEST_F(LibraryTest, inTheAppCacheDocumentsMovedByAnotherProgramAreFoundAgain) {
    makeFolders(root);
    Library(root).setCacheMode(CacheLocation::Mode::AppCache);
    const CacheLocation where = Library(root).cacheLocation();
    const fs::path app = where.appCacheDir();
    {
        LibraryIndex index(root, where);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
    }
    ASSERT_TRUE(fs::exists(app / "Physics" / "Mechanics" / DocumentFiles::META_DIR / "notes.pack"));
    EXPECT_FALSE(fs::exists(root / "Physics" / DocumentFiles::META_DIR)) << "nothing in the library's folders";

    fs::rename(root / "Physics", root / "Science");
    LibraryIndex index(root, where);
    index.update(DocumentFiles::scanRecursive(root));
    index.flush();
    EXPECT_EQ(index.documentsRead(), 0) << "found again by name, size and time";
    EXPECT_EQ(index.search("zebra").size(), 1u);
    EXPECT_TRUE(fs::exists(app / "Science" / "Mechanics" / DocumentFiles::META_DIR / "notes.pack"));
    EXPECT_FALSE(fs::exists(app / "Physics")) << "the old place is cleaned up";
    Library(root).setCacheMode(CacheLocation::Mode::Folders);
}

TEST_F(LibraryTest, removingTheCacheLeavesOtherFilesAndTheReadingPositions) {
    makeFolders(root);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->flush();
    DocumentPlaces::setLastPage(root / "Physics" / "sheet.pdf", 1);
    touch(root / "Physics" / DocumentFiles::META_DIR / "mine.txt");
    EXPECT_LT(model.cacheBytes(), 0) << "not counted yet";
    model.measureCache();
    waitFor([&] { return model.cacheBytes() >= 0; });
    EXPECT_GT(model.cacheBytes(), 200);
    EXPECT_EQ(model.cacheFiles(), 6) << "notes and PDF text of three folders (not mine.txt)";

    EXPECT_EQ(model.removeCaches(), static_cast<qint64>(model.cacheBytes()));
    EXPECT_TRUE(model.cacheRemoved());
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR));
    EXPECT_FALSE(fs::exists(root / "Physics" / "Mechanics" / DocumentFiles::META_DIR));
    EXPECT_TRUE(fs::exists(root / "Physics" / DocumentFiles::META_DIR / "mine.txt")) << "not the app's";
    EXPECT_FALSE(fs::exists(root / "Physics" / DocumentFiles::META_DIR / "notes.pack"));
    EXPECT_EQ(model.cacheBytes(), 0);
    EXPECT_EQ(DocumentPlaces::lastPage(root / "Physics" / "sheet.pdf"), 1) << "not cache";

    // Nothing is made again until the library is opened again
    model.refresh();
    ASSERT_FALSE(DocumentCovers::cover(DocumentFiles::itemOf(root / "lecture.pdf")).isNull());
    DocumentCovers::flush();
    processEventsFor(100);
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR));
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->flush();
    EXPECT_TRUE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack"));

    // In the app cache, too
    model.setCacheInAppCache(true);
    const fs::path app = Library(root).cacheLocation().appCacheDir();
    ASSERT_TRUE(fs::exists(app / DocumentFiles::META_DIR / "notes.pack"));
    model.removeCaches();
    EXPECT_FALSE(fs::exists(app));
    model.setLibrary(nullptr);
}

// A cache of another format (an earlier pre-release's) is not read: its documents are read anew and its packs written
// over, once.
TEST_F(LibraryTest, packsOfAnotherFormatAreReadAnew) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        index.flush();
    }
    const fs::path cache = root / DocumentFiles::META_DIR;
    auto notes = Packs::read(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
    ASSERT_TRUE(notes.has_value());
    ASSERT_TRUE(Packs::write(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT - 1, *notes, true));
    ASSERT_FALSE(Packs::read(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT).has_value());
    {
        LibraryIndex again(root);
        again.update(DocumentFiles::scanRecursive(root));
        again.waitForDone();
        EXPECT_EQ(again.documentsRead(), 1) << "read anew";
        EXPECT_EQ(again.search("page 2").size(), 1u);
        again.flush();
    }
    EXPECT_TRUE(Packs::read(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT).has_value()) << "written over";
    LibraryIndex third(root);
    third.update(DocumentFiles::scanRecursive(root));
    third.waitForDone();
    EXPECT_EQ(third.documentsRead(), 0) << "once";
}

// --- the library cache on a generated library -------------------------------------------------------------------
namespace {
/// Deterministic pseudo-words, frequent ones more often (roughly like real text, which compresses about 4:1).
class Words {
public:
    explicit Words(unsigned seed): rng(seed) {
        static const char* syllables[] = {"ka", "lo", "mi", "ne", "tur", "sen", "ra", "vo", "pel", "di", "ag",
                                          "on", "sti", "ber", "ul", "fa", "ze", "tro", "qui", "man"};
        std::mt19937 vocab(7);
        for (int i = 0; i < 3000; ++i) {
            std::string w;
            const int n = 1 + static_cast<int>(vocab() % 4);
            for (int s = 0; s < n; ++s) {
                w += syllables[vocab() % 20];
            }
            vocabulary.push_back(w);
        }
    }
    std::string next() {
        const double u = std::uniform_real_distribution<double>(0, 1)(rng);
        return vocabulary[static_cast<size_t>(u * u * u * static_cast<double>(vocabulary.size() - 1))];
    }
    std::string line(size_t chars) {
        std::string s;
        while (s.size() < chars) {
            s += next() + ' ';
        }
        return s;
    }

private:
    std::mt19937 rng;
    std::vector<std::string> vocabulary;
};

/// A PDF with `pages` pages full of text (about 6 kB of text per page).
void makeTextPdf(const fs::path& p, int pages, unsigned seed) {
    fs::create_directories(p.parent_path());
    Words words(seed);
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 7);
    for (int page = 0; page < pages; ++page) {
        for (int line = 0; line < 70; ++line) {
            cairo_move_to(cr, 30, 40 + line * 11);
            cairo_show_text(cr, words.line(95).c_str());
        }
        cairo_show_page(cr);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

/// ~30 folders with 10 small documents each (lone notes, lone PDFs, pairs), and three long text PDFs.
void makeLibrary(const fs::path& root) {
    unsigned seed = 1;
    std::vector<fs::path> folders;
    for (int s = 1; s <= 6; ++s) {
        const fs::path semester = root / ("Semester " + std::to_string(s));
        folders.push_back(semester);
        for (int c = 1; c <= 4; ++c) {
            folders.push_back(semester / ("Course " + std::to_string(c)));
        }
    }
    for (const fs::path& folder: folders) {
        for (int i = 0; i < 4; ++i) {
            const fs::path xopp = folder / ("notes " + std::to_string(i) + ".xopp");
            fs::create_directories(folder);
            fs::copy_file(fixture(u8"load/pages.xopp"), xopp);
            addText(xopp, 0, Words(++seed).line(300).c_str());
        }
        for (int i = 0; i < 3; ++i) {
            makeTextPdf(folder / ("paper " + std::to_string(i) + ".pdf"), 3, ++seed);
        }
        for (int i = 0; i < 3; ++i) {
            const fs::path pdf = folder / ("lecture " + std::to_string(i) + ".pdf");
            makeTextPdf(pdf, 4, ++seed);
            makeAnnotation(pdf, folder / ("lecture " + std::to_string(i) + ".xopp"));
        }
    }
    makeTextPdf(root / "book.pdf", 250, ++seed);  // over 1 MB of text
    makeTextPdf(folders[1] / "script.pdf", 120, ++seed);
    makeTextPdf(folders[7] / "reader.pdf", 150, ++seed);
}

/// Every file of the library's cache (the dot folders, and the app cache folder `appCache` if given).
std::vector<fs::path> cacheFiles(const fs::path& root, const fs::path& appCache = {}) {
    std::vector<fs::path> files;
    for (const fs::path& base: {root, appCache}) {
        std::error_code ec;
        if (base.empty() || !fs::exists(base, ec)) {
            continue;
        }
        for (auto it = fs::recursive_directory_iterator(base, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            const std::string path = it->path().string();
            if (it->is_regular_file() &&
                (base == appCache || path.find(std::string("/") + DocumentFiles::META_DIR + "/") != std::string::npos)) {
                files.push_back(it->path());
            }
        }
    }
    return files;
}

/// Drop the files from the page cache (fdatasync first), so the next read comes from the disk.
void dropFromPageCache(const std::vector<fs::path>& files) {
    for (const auto& f: files) {
        const int fd = ::open(f.c_str(), O_RDONLY);
        if (fd >= 0) {
            ::fdatasync(fd);
            ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
            ::close(fd);
        }
    }
}

}  // namespace

// Opt-in: XQT_BENCH_LIBRARY=<a folder on disk> (the test's temporary folder is often in memory, where a cold start
// cannot be measured). Generates a library there (~300 small documents in 30 folders and three long PDFs, removed
// afterwards) and measures its cache: opening with a cold and a warm page cache, what is written after one .xopp was
// edited, and the size and number of files.
TEST_F(LibraryTest, benchLibraryCache) {
    if (!qEnvironmentVariableIsSet("XQT_BENCH_LIBRARY")) {
        GTEST_SKIP() << "set XQT_BENCH_LIBRARY=<folder on disk>";
    }
    QTemporaryDir onDisk(qEnvironmentVariable("XQT_BENCH_LIBRARY") + "/xqt-bench-XXXXXX");
    ASSERT_TRUE(onDisk.isValid());
    const fs::path root = fs::path(onDisk.path().toStdString());
    QElapsedTimer t;
    t.start();
    makeLibrary(root);
    const auto items = DocumentFiles::scanRecursive(root);
    std::cout << items.size() << " documents generated in " << t.elapsed() << " ms\n";

    // The cache as the app keeps it: packs in each folder
    const fs::path meta = root / DocumentFiles::META_DIR;
    const fs::path appCache;
    auto openIndex = [&] { return std::make_unique<LibraryIndex>(root); };
    auto flushIndex = [&](LibraryIndex& index) { index.flush(); };
    auto flushPreviews = [&] { DocumentCovers::flush(); };
    DocumentCovers::setLibrary(CacheLocation(root));
    DocumentPlaces::setLibrary(root, Library(root).placesFile());

    {
        auto index = openIndex();
        t.restart();
        index->update(items);
        index->waitForDone();
        flushIndex(*index);
        std::cout << "first indexing: " << t.elapsed() << " ms (" << index->pdfPagesRead() << " PDF pages)\n";
    }
    t.restart();
    for (const auto& item: items) {
        DocumentCovers::cover(item);
    }
    flushPreviews();
    std::cout << "previews: " << t.elapsed() << " ms\n";

    auto open = [&](bool cold) {
        if (cold) {
            dropFromPageCache(cacheFiles(root, appCache));
        }
        auto index = openIndex();
        QElapsedTimer timer;
        timer.start();
        index->update(DocumentFiles::scanRecursive(root));
        index->waitForDone();
        const qint64 ms = timer.elapsed();
        EXPECT_EQ(index->documentsRead(), 0);
        EXPECT_EQ(index->search("book").size(), 1u);
        return ms;
    };
    std::cout << "open, cold page cache: " << open(true) << " ms, again: " << open(true) << " ms\n";
    open(false);
    std::cout << "open, warm: " << open(false) << " ms, " << open(false) << " ms\n";

    auto files = cacheFiles(root, appCache);
    uintmax_t bytes = 0, biggest = 0;
    for (const auto& f: files) {
        bytes += fs::file_size(f);
        biggest = std::max(biggest, fs::file_size(f));
    }
    std::cout << "cache: " << bytes / 1024 << " KiB in " << files.size() << " files (biggest " << biggest / 1024
              << " KiB)\n";

    // One .xopp edited: what is written (the index; then also its new preview)
    const fs::path edited = root / "Semester 3" / "Course 2" / "notes 1.xopp";
    auto index = openIndex();
    index->update(DocumentFiles::scanRecursive(root));
    index->waitForDone();
    const auto before = snapshot(cacheFiles(root, appCache));
    QThread::msleep(20);  // (a new modification time)
    addText(edited, 2, "unicorn");
    index->update(DocumentFiles::scanRecursive(root));
    index->waitForDone();
    flushIndex(*index);
    auto [indexBytes, indexFiles] = written(before, snapshot(cacheFiles(root, appCache)));
    std::cout << "after editing one .xopp, the index wrote " << indexBytes / 1024.0 << " KiB in " << indexFiles
              << " files\n";
    DocumentCovers::cover(DocumentFiles::itemOf(edited));
    flushPreviews();
    auto [allBytes, allFiles] = written(before, snapshot(cacheFiles(root, appCache)));
    std::cout << "with its preview (page 3 edited: drawn again, looks the same): " << allBytes / 1024.0 << " KiB in "
              << allFiles << " files\n";
    EXPECT_EQ(index->search("unicorn").size(), 1u);
    // Page 1 edited: a new preview, the folder's previews.pack is written again
    const auto beforeFirst = snapshot(cacheFiles(root, appCache));
    QThread::msleep(20);
    addText(edited, 0, "pegasus");
    index->update(DocumentFiles::scanRecursive(root));
    index->waitForDone();
    flushIndex(*index);
    DocumentCovers::cover(DocumentFiles::itemOf(edited));
    flushPreviews();
    auto [firstBytes, firstFiles] = written(beforeFirst, snapshot(cacheFiles(root, appCache)));
    std::cout << "page 1 edited, index and preview: " << firstBytes / 1024.0 << " KiB in " << firstFiles
              << " files (previews.pack: " << fs::file_size(edited.parent_path() / DocumentFiles::META_DIR / "previews.pack") / 1024.0
              << " KiB)\n";
    index.reset();

    uintmax_t bookPack = 0;
    for (const auto& f: cacheFiles(root)) {
        if (f.parent_path() == meta && f.filename().string().rfind("pdf-text-", 0) == 0) {
            bookPack = fs::file_size(f);  // (the only entry with a file of its own)
        }
    }
    std::cout << "PDF text of the 250-page PDF: " << bookPack / 1024 << " KiB\n";
    DocumentPlaces::setLibrary({}, {});
}

/*
 * xournal-qt: the files of the library cache (packs), where they are kept, and when they are written.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "shell/DocumentFiles.h"
#include "shell/LibraryCache.h"

#include "../FailingWrites.h"

using namespace xqt;

namespace {
void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}
QByteArray bytesOf(const fs::path& p) {
    QFile f(QString::fromStdString(p.string()));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
/// Text that does not compress to nothing (a counter in every word)
QString text(qsizetype chars) {
    QString s;
    for (int i = 0; s.size() < chars; ++i) {
        s += QStringLiteral("word%1 ").arg(i * 7919 % 100003);
    }
    return s;
}
void waitFor(const std::function<bool()>& cond, int ms = 3000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

class LibraryCacheTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        dir = root / DocumentFiles::META_DIR;
    }
    QTemporaryDir tmp;
    fs::path root, dir;
};
}  // namespace

TEST_F(LibraryCacheTest, aPackIsReadBackOnlyWithItsNameAndFormat) {
    const QCborMap entries{{QStringLiteral("lecture.xopp"), QCborMap{{QStringLiteral("text"), text(5000)}}},
                           {QStringLiteral("paper.pdf"), QCborMap{{QStringLiteral("pages"), 3}}}};
    ASSERT_TRUE(Packs::write(dir, "notes", 1, entries, true));
    EXPECT_TRUE(fs::exists(dir / "notes.pack"));
    EXPECT_TRUE(bytesOf(dir / "notes.pack").startsWith("XQPK"));
    EXPECT_LT(fs::file_size(dir / "notes.pack"), 5000u) << "compressed";

    const auto read = Packs::read(dir, "notes", 1);
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, entries);
    EXPECT_FALSE(Packs::read(dir, "notes", 2).has_value()) << "another content format: read anew";
    EXPECT_FALSE(Packs::read(dir, "previews", 1).has_value()) << "not that pack";
    EXPECT_FALSE(Packs::read(dir, "pdf-text", 1).has_value()) << "missing";

    // Uncompressed (previews: PNG is compressed already)
    ASSERT_TRUE(Packs::write(dir, "previews", 1, {{QStringLiteral("a.pdf"), QByteArray("png bytes")}}, false));
    EXPECT_EQ(Packs::read(dir, "previews", 1)->value(QStringLiteral("a.pdf")).toByteArray(), QByteArray("png bytes"));

    // Damaged: ignored
    {
        QFile f(QString::fromStdString((dir / "notes.pack").string()));
        ASSERT_TRUE(f.open(QIODevice::ReadWrite));
        f.seek(10);
        f.write("garbage");
    }
    EXPECT_FALSE(Packs::read(dir, "notes", 1).has_value());

    // No entries: the pack goes
    ASSERT_TRUE(Packs::write(dir, "notes", 1, {}, true));
    EXPECT_FALSE(fs::exists(dir / "notes.pack"));
}

TEST_F(LibraryCacheTest, bigEntriesHaveAFileOfTheirOwnWrittenOnlyWhenTheyChange) {
    const QString big = text(Packs::OWN_FILE_SIZE + 1000);
    QCborMap entries{{QStringLiteral("book.pdf"), QCborMap{{QStringLiteral("text"), big}}},
                     {QStringLiteral("small.pdf"), QCborMap{{QStringLiteral("text"), QStringLiteral("short")}}}};
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, entries, true));
    const fs::path own = Packs::ownFileOf(dir, "pdf-text", "book.pdf");
    ASSERT_TRUE(fs::exists(own));
    EXPECT_LT(fs::file_size(dir / "pdf-text.pack"), 1000u) << "the big entry is not in the pack";
    EXPECT_EQ(*Packs::read(dir, "pdf-text", 1), entries);

    // A neighbour changed: the big file is not written again
    const auto old = fs::file_time_type::clock::now() - std::chrono::hours(1);
    fs::last_write_time(own, old);
    entries.insert(QStringLiteral("small.pdf"), QCborMap{{QStringLiteral("text"), QStringLiteral("changed")}});
    const std::set<QString> changed{QStringLiteral("small.pdf")};
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, entries, true, &changed));
    EXPECT_EQ(fs::last_write_time(own), old);
    EXPECT_EQ(*Packs::read(dir, "pdf-text", 1), entries);
    // It changed itself
    const std::set<QString> bookChanged{QStringLiteral("book.pdf")};
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, entries, true, &bookChanged));
    EXPECT_NE(fs::last_write_time(own), old);

    // Its file missing: the entry is not there (read again), the others are
    fs::remove(own);
    auto read = Packs::read(dir, "pdf-text", 1);
    ASSERT_TRUE(read.has_value());
    EXPECT_FALSE(read->contains(QStringLiteral("book.pdf")));
    EXPECT_TRUE(read->contains(QStringLiteral("small.pdf")));

    // Gone: its file too
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, entries, true));
    ASSERT_TRUE(fs::exists(own));
    entries.remove(QStringLiteral("book.pdf"));
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, entries, true));
    EXPECT_FALSE(fs::exists(own));
}

TEST_F(LibraryCacheTest, aPackThatCannotBeWrittenStaysAsItWas) {
    const QCborMap first{{QStringLiteral("a.xopp"), 1}};
    ASSERT_TRUE(Packs::write(dir, "notes", 1, first, true));
    {
        test::FileSizeLimit full(1);  // (a full disk: the new pack cannot be written, also not by root)
        EXPECT_FALSE(Packs::write(dir, "notes", 1, {{QStringLiteral("a.xopp"), 2}}, true));
    }
    EXPECT_EQ(*Packs::read(dir, "notes", 1), first);
    if (test::permissionsBind()) {  // (not as root)
        fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
        EXPECT_FALSE(Packs::write(dir, "notes", 1, {{QStringLiteral("a.xopp"), 2}}, true)) << "a read-only folder";
        fs::permissions(dir, fs::perms::owner_all);
        EXPECT_EQ(*Packs::read(dir, "notes", 1), first);
    }
}

TEST_F(LibraryCacheTest, theCacheIsInEachFolderOrMirroredInTheAppCache) {
    const fs::path app = root / "app-cache" / "key";
    fs::create_directories(root / "lib" / "Physics" / "Mechanics");
    const fs::path lib = root / "lib";

    const CacheLocation folders(lib, CacheLocation::Mode::Folders, app);
    EXPECT_EQ(folders.dirOf(lib), lib / ".xournal_library");
    EXPECT_EQ(folders.dirOf(lib / "Physics" / "Mechanics"), lib / "Physics" / "Mechanics" / ".xournal_library");

    const CacheLocation appCache(lib, CacheLocation::Mode::AppCache, app);
    EXPECT_EQ(appCache.dirOf(lib), app / ".xournal_library");
    EXPECT_EQ(appCache.dirOf(lib / "Physics" / "Mechanics"), app / "Physics" / "Mechanics" / ".xournal_library");
    EXPECT_TRUE(appCache.mirrorOf(root / "elsewhere").empty());

    // A folder that cannot be written: the app cache
    if (test::permissionsBind()) {  // (root writes anyway: then it is used)
        fs::permissions(lib / "Physics", fs::perms::owner_read | fs::perms::owner_exec);
        EXPECT_EQ(folders.dirOf(lib / "Physics"), app / "Physics" / ".xournal_library");
        fs::permissions(lib / "Physics", fs::perms::owner_all);
    }
    // ... or where the cache folder cannot be made (a file of that name is in the way)
    std::ofstream(lib / "Physics" / "Mechanics" / ".xournal_library") << "a file";
    EXPECT_EQ(folders.dirOf(lib / "Physics" / "Mechanics"), app / "Physics" / "Mechanics" / ".xournal_library");

    // The default place in the app cache: by the library's key
    EXPECT_NE(CacheLocation(lib).appCacheDir().string().find("libraries"), std::string::npos);
}

TEST_F(LibraryCacheTest, onlyOurFilesAreRemoved) {
    ASSERT_TRUE(Packs::write(dir, "notes", 1, {{QStringLiteral("a.xopp"), 1}}, true));
    ASSERT_TRUE(Packs::write(dir, "pdf-text", 1, {{QStringLiteral("a.xopp"), text(Packs::OWN_FILE_SIZE)}}, true));
    touch(dir / "index" / "0123456789abcdef.json");  // the layout before the packs
    touch(dir / "previews" / "0123.png");
    touch(dir / "pages.json");
    EXPECT_TRUE(Packs::isOurs("notes.pack"));
    EXPECT_TRUE(Packs::isOurs("pdf-text-0123456789abcdef.pack"));
    EXPECT_TRUE(Packs::isOurs("notes.pack.a1B2c3"));
    EXPECT_FALSE(Packs::isOurs("notes.txt"));
    EXPECT_TRUE(Packs::isOldLayout("pages.json"));
    EXPECT_GT(Packs::sizeOf(dir), 1000);

    touch(dir / "mine.txt");  // not ours: the folder stays
    EXPECT_FALSE(Packs::removeIfOnlyOurs(dir));
    EXPECT_TRUE(fs::exists(dir / "notes.pack")) << "nothing removed";
    EXPECT_GT(Packs::removeOurs(dir), 0);
    EXPECT_TRUE(fs::exists(dir / "mine.txt"));
    EXPECT_FALSE(fs::exists(dir / "notes.pack"));
    EXPECT_FALSE(fs::exists(dir / "index"));
    EXPECT_FALSE(fs::exists(dir / "pages.json"));

    fs::remove(dir / "mine.txt");
    ASSERT_TRUE(Packs::write(dir, "notes", 1, {{QStringLiteral("a.xopp"), 1}}, true));
    touch(dir / "pages.json");
    EXPECT_FALSE(Packs::removeIfOnlyOurs(dir)) << "the old layout is not removed on the way (not converted yet)";
    fs::remove(dir / "pages.json");
    EXPECT_TRUE(Packs::removeIfOnlyOurs(dir));
    EXPECT_FALSE(fs::exists(dir));
}

TEST_F(LibraryCacheTest, writesWaitForAPauseButNotForever) {
    int writes = 0;
    WriteScheduler scheduler([&] { ++writes; });
    scheduler.setDelays(60, 250);
    for (int i = 0; i < 3; ++i) {
        scheduler.changed();
    }
    EXPECT_TRUE(scheduler.pending());
    waitFor([&] { return writes > 0; });
    EXPECT_EQ(writes, 1) << "one write for a burst";
    EXPECT_FALSE(scheduler.pending());

    // Changes that keep coming: written at the latest after the longest delay
    QElapsedTimer t;
    t.start();
    while (writes == 1 && t.elapsed() < 2000) {
        scheduler.changed();
        waitFor([] { return false; }, 20);
    }
    EXPECT_EQ(writes, 2);
    EXPECT_LT(t.elapsed(), 600);

    // From another thread
    std::thread([&] { scheduler.changed(); }).join();
    waitFor([&] { return writes > 2; });
    EXPECT_EQ(writes, 3);
    scheduler.changed();
    scheduler.cancel();
    waitFor([] { return false; }, 150);
    EXPECT_EQ(writes, 3) << "cancelled";
}

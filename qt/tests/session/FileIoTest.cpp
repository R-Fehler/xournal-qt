/*
 * xournal-qt: the file helpers (FileIo.h): a file written atomically replaces the old one whole and leaves no
 * temporary file (also when the writer fails), writers of one file wait for each other, the stamp's format (the
 * clean-copy cache names its entries by it), FNV-1a, MD5, extensions and gzip.
 *
 * @license GNU GPLv2 or later
 */
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <zlib.h>

#include "session/FileIo.h"

using namespace xqt;

namespace {
fs::path pathIn(const QTemporaryDir& dir, const char* name) { return fs::path(dir.path().toStdString()) / name; }

size_t filesIn(const fs::path& folder) {
    size_t n = 0;
    for ([[maybe_unused]] const auto& e: fs::directory_iterator(folder)) {
        ++n;
    }
    return n;
}
}  // namespace

TEST(FileIoTest, aFileWrittenAtomicallyReplacesTheOldOneAndLeavesNoTemporaryFile) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "a.txt");
    std::string error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "first", error)) << error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "second, longer", error, fileio::Sync::None)) << error;
    EXPECT_EQ(fileio::readFile(f), "second, longer");
    EXPECT_EQ(filesIn(f.parent_path()), 1u);

    // The Qt overload
    const QString q = dir.filePath(QStringLiteral("b.json"));
    ASSERT_TRUE(fileio::writeFileAtomically(q, QByteArray("{}")));
    EXPECT_EQ(fileio::readFile(pathIn(dir, "b.json")), "{}");
}

TEST(FileIoTest, theReplacedFileKeepsItsPermissionsAndALinkStaysALink) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "mine.md");
    std::string error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "one", error));
    fs::permissions(f, fs::perms::owner_read | fs::perms::owner_write);
    ASSERT_TRUE(fileio::writeFileAtomically(f, "two", error));
    EXPECT_EQ(fs::status(f).permissions() & fs::perms::all, fs::perms::owner_read | fs::perms::owner_write);
#ifndef _WIN32
    const fs::path link = pathIn(dir, "link.md");
    fs::create_symlink(f, link);
    ASSERT_TRUE(fileio::writeFileAtomically(link, "three", error));
    EXPECT_TRUE(fs::is_symlink(link));
    EXPECT_EQ(fileio::readFile(f), "three");
#endif
}

TEST(FileIoTest, aWriterThatFailsLeavesTheOldFileAndNoTemporaryFile) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "keep.pdf");
    std::string error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "old", error));
    try {
        fileio::AtomicFile file(f);
        fileio::writeFileAtomically(file.temp(), "half", error, fileio::Sync::None);  // (a writer that throws then)
        throw std::runtime_error("the writer failed");
    } catch (const std::runtime_error&) {}
    EXPECT_EQ(fileio::readFile(f), "old");
    EXPECT_EQ(filesIn(f.parent_path()), 1u);

    {
        fileio::AtomicFile file(f);  // (nothing written: nothing to commit)
        EXPECT_FALSE(file.commit(error));
    }
    EXPECT_EQ(fileio::readFile(f), "old");

    // A folder that does not exist: an error, nothing written
    EXPECT_FALSE(fileio::writeFileAtomically(pathIn(dir, "no/such/folder.txt"), "x", error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(filesIn(f.parent_path()), 1u);
}

TEST(FileIoTest, temporaryNamesAreUniqueAndHiddenNextToTheTarget) {
    const fs::path target = fs::path("/some/folder") / "doc.pdf";
    const fs::path a = fileio::tempNameFor(target);
    const fs::path b = fileio::tempNameFor(target);
    EXPECT_NE(a, b);
    EXPECT_EQ(a.parent_path(), target.parent_path());
    EXPECT_EQ(a.filename().string().rfind(".doc.pdf.", 0), 0u);  // (IncrementalPdf finds a crash's leftovers by it)
    EXPECT_EQ(a.extension(), ".part");
}

TEST(FileIoTest, aFileAndAFolderCanBeSynced) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "s");
    std::string error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "x", error));
    EXPECT_TRUE(fileio::syncFile(f));
    EXPECT_TRUE(fileio::syncFile(f.parent_path()));
    EXPECT_FALSE(fileio::syncFile(pathIn(dir, "missing")));
}

TEST(FileIoTest, writersOfOneFileWaitForEachOther) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "w.pdf");
    std::promise<void> held;
    std::promise<void> release;
    std::thread first([&] {
        fileio::FileWriteLock lock(f);
        fileio::FileWriteLock again(f);  // (the same thread may take it again)
        held.set_value();
        release.get_future().wait();
    });
    held.get_future().wait();
    EXPECT_TRUE(fileio::FileWriteLock::busy(f));
    EXPECT_TRUE(fileio::FileWriteLock::busy(f.parent_path() / "." / "w.pdf"));  // (the same file by another path)
    EXPECT_FALSE(fileio::FileWriteLock::busy(pathIn(dir, "other.pdf")));
    {
        fileio::FileWriteLock other(pathIn(dir, "other.pdf"));  // (another file: not held up)
    }
    std::atomic<bool> got{false};
    std::thread second([&] {
        fileio::FileWriteLock lock(f);
        got = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));  // (the second writer is not let in meanwhile)
    EXPECT_FALSE(got);
    release.set_value();
    first.join();
    second.join();
    EXPECT_TRUE(got);
    EXPECT_FALSE(fileio::FileWriteLock::busy(f));
}

TEST(FileIoTest, theStampIsSizeAndTimeAndChangesWithTheFile) {
    QTemporaryDir dir;
    const fs::path f = pathIn(dir, "stamped.pdf");
    EXPECT_EQ(fileio::stampOf(f), "");
    EXPECT_FALSE(fileio::fileStamp(f));
    std::string error;
    ASSERT_TRUE(fileio::writeFileAtomically(f, "12345", error));
    // The format the clean-copy cache names its entries by and DocumentSession compares with: "<size>-<time>"
    const auto time = fs::last_write_time(f).time_since_epoch();
    EXPECT_EQ(fileio::stampOf(f), "5-" + std::to_string(static_cast<long long>(
                                                 std::chrono::duration_cast<std::chrono::nanoseconds>(time).count())));
    const auto before = fileio::fileStamp(f);
    ASSERT_TRUE(before);
    EXPECT_EQ(before->size, 5u);
    ASSERT_TRUE(fileio::writeFileAtomically(f, "123456", error));
    EXPECT_NE(*fileio::fileStamp(f), *before);
}

TEST(FileIoTest, fnvMd5HexAndExtensions) {
    // (the values every copy before had: files keep them, FileIo.h)
    EXPECT_EQ(fileio::fnv1a(""), 0x14650fb0739d0383ULL);
    EXPECT_EQ(fileio::fnv1a("a"), 0x44bd8ad473cd9906ULL);
    EXPECT_EQ(fileio::fnv1a("foobar"), 0x88fad7c0a8ff07f2ULL);
    EXPECT_EQ(fileio::fnv1a("bar", fileio::fnv1a("foo")), fileio::fnv1a("foobar"));  // (continued)
    static_assert(fileio::fnv1a("a") == 0x44bd8ad473cd9906ULL);
    EXPECT_EQ(fileio::hex16(0x44bd8ad473cd9906ULL), "44bd8ad473cd9906");
    EXPECT_EQ(fileio::hex16(1), "0000000000000001");
    EXPECT_EQ(QByteArray::fromStdString(fileio::md5Of("")).toHex(), QByteArray("d41d8cd98f00b204e9800998ecf8427e"));
    EXPECT_EQ(fileio::md5Of("abc").size(), 16u);
    EXPECT_TRUE(fileio::hasExtension("a/b.PDF", ".pdf"));
    EXPECT_TRUE(fileio::hasExtension("b.xopp", ".xopp"));
    EXPECT_FALSE(fileio::hasExtension("b.pdf.part", ".pdf"));
    EXPECT_FALSE(fileio::hasExtension("pdf", ".pdf"));
}

TEST(FileIoTest, gzipRoundTripsAndDamagedDataIsToldApart) {
    std::string data;
    for (int i = 0; i < 20000; ++i) {
        data += "<stroke>" + std::to_string(i) + "</stroke>\n";
    }
    const std::string gz = fileio::gzip(data);
    EXPECT_TRUE(fileio::isGzip(gz));
    EXPECT_FALSE(fileio::isGzip(data));
    EXPECT_LT(gz.size(), data.size());
    EXPECT_EQ(fileio::gzip(data), gz);  // (the same bytes for the same data: an embedded .xopp)
    bool ok = false;
    EXPECT_EQ(fileio::gunzip(gz, ok), data);
    EXPECT_TRUE(ok);
    fileio::gunzip(gz.substr(0, gz.size() / 2), ok);
    EXPECT_FALSE(ok);
    fileio::gunzip("not compressed", ok);
    EXPECT_FALSE(ok);
    // zlib data reads too
    uLongf size = compressBound(static_cast<uLong>(data.size()));
    std::string z(size, '\0');
    ASSERT_EQ(compress2(reinterpret_cast<Bytef*>(z.data()), &size, reinterpret_cast<const Bytef*>(data.data()),
                        static_cast<uLong>(data.size()), Z_DEFAULT_COMPRESSION),
              Z_OK);
    z.resize(size);
    EXPECT_EQ(fileio::gunzip(z, ok), data);
    EXPECT_TRUE(ok);
    EXPECT_EQ(fileio::gunzip(fileio::gzip(""), ok), "");
    EXPECT_TRUE(ok);
}

/*
 * xournal-qt: folders opened as a library appear in the Recent grid, among the recent documents; a folder of a library
 * opens as a library of its own.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "shell/Library.h"
#include "shell/RecentFiles.h"
#include "shell/SystemApps.h"
#include "AppController.h"

using namespace xqt;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

/// Records the libraries that would be opened in a window of their own (no process is started).
struct FakeSystemApps: SystemApps {
    QStringList libraries;
    bool startLibraryWindow(const QString& folder) override {
        libraries << folder;
        return true;
    }
    bool openWithSystemApp(const QString&) override { return true; }
    bool showInFileManager(const QString&) override { return true; }
};

class RecentLibrariesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    QString at(const RecentFiles& r, int row, int role) const { return r.data(r.index(row), role).toString(); }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(RecentLibrariesTest, librariesAreListedAmongTheDocumentsByTime) {
    writeFile(root / "a.xopp", "x");
    writeFile(root / "b.md", "# B\n");
    fs::create_directories(root / "Lectures" / "Physics");
    RecentFiles recent(root / "recent.json");
    recent.add(root / "a.xopp");
    recent.addLibrary(root / "Lectures");
    recent.add(root / "b.md");
    ASSERT_EQ(recent.count(), 3);
    EXPECT_EQ(at(recent, 0, RecentFiles::NameRole), "b");
    EXPECT_EQ(at(recent, 1, RecentFiles::NameRole), "Lectures");
    EXPECT_EQ(at(recent, 2, RecentFiles::NameRole), "a");
    EXPECT_TRUE(recent.data(recent.index(1), RecentFiles::IsLibraryRole).toBool());
    EXPECT_FALSE(recent.data(recent.index(0), RecentFiles::IsLibraryRole).toBool());
    EXPECT_EQ(at(recent, 1, RecentFiles::KindRole), "library");
    EXPECT_EQ(at(recent, 1, RecentFiles::PathRole), qstr(root / "Lectures"));
    EXPECT_TRUE(at(recent, 1, RecentFiles::LocationRole).endsWith("/Lectures")) << "the folder's own path";
    EXPECT_EQ(at(recent, 1, RecentFiles::PreviewRole), "");

    // Opened again: first, once
    recent.addLibrary(root / "Lectures" / "");
    ASSERT_EQ(recent.count(), 3);
    EXPECT_EQ(at(recent, 0, RecentFiles::NameRole), "Lectures");
    // Kept by another window's list (read from the file)
    RecentFiles other(root / "recent.json");
    EXPECT_EQ(other.count(), 3);
    EXPECT_TRUE(other.data(other.index(0), RecentFiles::IsLibraryRole).toBool());

    // Not selected with the documents (no copy, move or trash of a whole library from here)
    recent.selectAll();
    EXPECT_EQ(recent.selectionCount(), 2);
    recent.clearSelection();
    recent.toggleSelected(0);
    EXPECT_EQ(recent.selectionCount(), 0);
    EXPECT_FALSE(recent.rename(0, "Other"));
    EXPECT_FALSE(recent.trash(0));
    EXPECT_TRUE(fs::is_directory(root / "Lectures"));

    // Removed from the list, or gone: it drops out
    recent.removePaths({qstr(root / "Lectures")});
    EXPECT_EQ(recent.count(), 2);
    recent.addLibrary(root / "Lectures" / "Physics");
    EXPECT_EQ(recent.count(), 3);
    fs::remove_all(root / "Lectures");
    recent.refresh();
    EXPECT_EQ(recent.count(), 2);
}

TEST_F(RecentLibrariesTest, theStandardFolderIsKnown) {
    // (only paths: nothing is made in the Documents folder)
    EXPECT_TRUE(Library(Library::librariesFolder() / "Default").isInLibrariesFolder());
    EXPECT_TRUE(Library(Library::librariesFolder() / "Physics" / "Week 1").isInLibrariesFolder());
    EXPECT_FALSE(Library(root).isInLibrariesFolder());
    EXPECT_FALSE(Library(Library::librariesFolder().parent_path() / "Xournal_Libraries_old").isInLibrariesFolder());
}

TEST_F(RecentLibrariesTest, aWindowsLibraryBecomesRecentAndOpensInAWindowOfItsOwn) {
    FakeSystemApps fake;
    SystemApps::setInstance(&fake);
    fs::create_directories(root / "Lectures" / "Physics");
    fs::create_directories(root / "Other");
    {
        AppController c;
        auto* recent = qobject_cast<RecentFiles*>(c.recentModel());
        recent->clear();
        c.setLibraryRoot(root / "Lectures");
        ASSERT_GE(recent->count(), 1);
        EXPECT_TRUE(recent->data(recent->index(0), RecentFiles::IsLibraryRole).toBool());
        EXPECT_EQ(at(*recent, 0, RecentFiles::PathRole), qstr(root / "Lectures"));

        // Another library (from Recent, or a folder of this one opened as a library): a window of its own
        c.openLibrary(QUrl::fromLocalFile(qstr(root / "Other")));
        c.openLibrary(QUrl::fromLocalFile(qstr(root / "Lectures" / "Physics")));
        EXPECT_EQ(fake.libraries, (QStringList{qstr(root / "Other"), qstr(root / "Lectures" / "Physics")}));
        // This one: its home screen, no new window
        c.openLibrary(QUrl::fromLocalFile(qstr(root / "Lectures")));
        EXPECT_EQ(fake.libraries.size(), 2);
        recent->clear();
    }
    SystemApps::setInstance(nullptr);
}

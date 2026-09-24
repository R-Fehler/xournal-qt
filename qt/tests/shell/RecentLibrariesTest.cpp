/*
 * xournal-qt: folders opened as a library appear in the Recent grid, among the recent documents; a folder of a library
 * opens as a library of its own.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QGuiApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "shell/Library.h"
#include "shell/LocalUrl.h"
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

TEST_F(RecentLibrariesTest, anEmptyPathIsIgnoredNotACrash) {
    // A document without a file of its own (a text document before the save path knew it) was added with an empty
    // path, and fs::absolute threw "cannot make absolute path []" out of Save as
    std::ofstream(root / "a.xopp") << "x";  // (files that are gone drop out of the list)
    RecentFiles recent(root / "recent.json");
    recent.add(root / "a.xopp");
    EXPECT_NO_THROW(recent.add(fs::path{}));
    EXPECT_NO_THROW(recent.addLibrary(fs::path{}));
    EXPECT_EQ(recent.count(), 1) << "nothing added for an empty path";
}

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

// Windows (2026-09-24): "Downloads folder (quick library)" answered "cannot open c//". The menu made the URL as
// "file://" + path, and with a drive letter that is a host: "file://C:/Users/x/Downloads" is the network path
// //c/Users/x/Downloads to toLocalFile().
TEST_F(RecentLibrariesTest, aWindowsPathOpensItsLibraryHoweverItBecameAUrl) {
    EXPECT_EQ(QUrl("file://C:/Users/x/Downloads").toLocalFile(), "//c/Users/x/Downloads") << "what went wrong";
    FakeSystemApps fake;
    SystemApps::setInstance(&fake);
    {
        AppController c;
        c.openLibrary(QUrl("file://C:/Users/x/Downloads"));  // made by hand
        c.openLibrary(QUrl("C:/Users/x/Other"));             // a path where a URL was expected
        c.openLibraryAt(qstr(root / "Third"));               // a path, as the menu passes it now
        c.openLibrary(QUrl::fromLocalFile(qstr(root / "Fourth")));
        EXPECT_EQ(fake.libraries, (QStringList{"C:/Users/x/Downloads", "C:/Users/x/Other", qstr(root / "Third"),
                                               qstr(root / "Fourth")}));
#ifdef Q_OS_WIN  // (elsewhere "C:/..." is a relative path)
        c.openLibraryAt("C:/Users/x/Fifth");
        EXPECT_EQ(fake.libraries.last(), "C:/Users/x/Fifth");
#endif
    }
    SystemApps::setInstance(nullptr);
}

namespace {
/// Android: one window, and the shared storage needs "All files access" (`granted`), asked for through the system's
/// settings page (`requested`).
struct FakeAndroid: FakeSystemApps {
    QString sharedStorage;
    bool granted = false;
    int requested = 0;
    bool librariesInOwnWindows() override { return false; }
    bool needsAllFilesAccess(const QString& folder) override { return folder.startsWith(sharedStorage); }
    bool hasAllFilesAccess() override { return granted; }
    bool requestAllFilesAccess() override {
        ++requested;
        return true;
    }
};
}  // namespace

// Android: "Open a folder as library" with a folder of the shared storage asks for "All files access" first (the
// window explains it), shows the system's page, and opens the folder when the app is back with it: in this window,
// which switches to that library and remembers it. Without it, nothing opens.
TEST_F(RecentLibrariesTest, onAndroidTheWindowSwitchesToASharedFolderAfterAllFilesAccess) {
    FakeAndroid android;
    android.sharedStorage = qstr(root / "storage");
    SystemApps::setInstance(&android);
    const fs::path own = root / "own" / "Default";
    const fs::path uni = root / "storage" / "Documents" / "Uni";
    writeFile(uni / "Week 1" / "notes.md", "# Notes\n");
    fs::create_directories(own);
    {
        AppController c;
        auto* recent = qobject_cast<RecentFiles*>(c.recentModel());
        recent->clear();
        c.setLibraryRoot(own);
        c.newDocument();
        QStringList asked;
        int picker = 0;
        QObject::connect(&c, &AppController::storageAccessNeeded, [&](const QString& f) { asked << f; });
        QObject::connect(&c, &AppController::pickLibraryFolder, [&] { ++picker; });
        EXPECT_FALSE(c.libraryWindows());
        EXPECT_FALSE(c.storageAccess());

        c.openLibraryAt(qstr(uni));
        EXPECT_EQ(asked, QStringList{qstr(uni)}) << "explained first";
        EXPECT_EQ(c.libraryModel()->property("rootPath").toString(), qstr(own)) << "not opened yet";
        c.requestStorageAccess(qstr(uni));
        EXPECT_EQ(android.requested, 1) << "the system's page";
        // The app goes to the background for the settings page and comes back without it: a message, nothing opens
        QStringList messages;
        QObject::connect(&c, &AppController::message, [&](const QString&, const QString& text) { messages << text; });
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        EXPECT_TRUE(messages.isEmpty()) << "(not back yet: it has not left)";
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        EXPECT_EQ(messages.size(), 1);
        EXPECT_EQ(c.libraryModel()->property("rootPath").toString(), qstr(own));

        // Again, and allowed this time: the folder opens in this window; the tab stays
        c.requestStorageAccess(qstr(uni));
        android.granted = true;
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationInactive);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        EXPECT_TRUE(c.storageAccess());
        EXPECT_EQ(c.libraryModel()->property("rootPath").toString(), qstr(uni));
        EXPECT_TRUE(android.libraries.isEmpty()) << "no window of its own";
        EXPECT_EQ(c.tabCount(), 1);
        EXPECT_TRUE(c.homeVisible());
        EXPECT_EQ(c.rememberedLibrary(), qstr(uni)) << "opened at the next start";
        ASSERT_GE(recent->count(), 1);
        EXPECT_EQ(at(*recent, 0, RecentFiles::PathRole), qstr(uni)) << "in the Recent grid";

        // "Open a folder as library…" with the permission: the picker gives a tree URI of the storage; one of a
        // cloud app's provider cannot be a library
        c.openLibraryAt(qstr(own));
        EXPECT_EQ(c.libraryModel()->property("rootPath").toString(), qstr(own)) << "the app's own folder: no question";
        EXPECT_EQ(asked.size(), 1);
        messages.clear();
        c.openLibrary(QUrl("content://com.google.android.apps.docs.storage/tree/acc%3D1%3Bdoc%3Dabc"));
        EXPECT_EQ(messages.size(), 1);
        EXPECT_EQ(c.libraryModel()->property("rootPath").toString(), qstr(own));
        // Asked for from the picker ("" : the picker opens again when it is given)
        android.granted = false;
        c.requestStorageAccess("");
        android.granted = true;
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        EXPECT_EQ(picker, 1);
        recent->clear();
    }
    SystemApps::setInstance(nullptr);
}

TEST(LocalUrl, localPathsOfFileUrls) {
    using xqt::localPathOf;
    EXPECT_EQ(localPathOf(QUrl()), "");
    EXPECT_EQ(localPathOf(QUrl::fromLocalFile("/home/x/a b.xopp")), "/home/x/a b.xopp");
    EXPECT_EQ(localPathOf(QUrl("file:///home/x/%C3%9Cbung.pdf")), QString::fromUtf8("/home/x/\xc3\x9c" "bung.pdf"));
    EXPECT_EQ(localPathOf(QUrl("file://C:/Users/x/Downloads")), "C:/Users/x/Downloads");
    EXPECT_EQ(localPathOf(QUrl("file://c/Users/x")), "C:/Users/x");
    EXPECT_EQ(localPathOf(QUrl("C:/Users/x/Documents/a.xopp")), "C:/Users/x/Documents/a.xopp");
    EXPECT_EQ(localPathOf(QUrl("file://server/share/a.pdf")), "//server/share/a.pdf") << "a network path stays one";
#ifdef Q_OS_WIN
    EXPECT_EQ(localPathOf(QUrl::fromLocalFile("C:/Users/x/Downloads")), "C:/Users/x/Downloads");
    EXPECT_EQ(localPathOf(QUrl::fromLocalFile("C:\\Users\\x\\Downloads")), "C:/Users/x/Downloads");
#endif
}

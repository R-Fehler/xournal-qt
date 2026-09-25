/*
 * xournal-qt: where the libraries live (Library::Home), and moving them from the app's own folder to the phone's
 * shared Documents folder (LibraryMigration): copy, verify, switch, clean up; failures leave the old place in use; a
 * name that is taken is never merged into; recent files, reading positions, the session journal and the open tabs
 * follow. The platform's folders are injected (Library::setPlatformFolders), so this runs on the desktop as Android.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <sstream>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/DocumentSession.h"
#include "shell/DocumentPlaces.h"
#include "shell/Library.h"
#include "shell/LibraryMigration.h"
#include "shell/LibraryModel.h"
#include "shell/RecentFiles.h"
#include "shell/SessionRecovery.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "util/PathUtil.h"
#include "AppController.h"

using namespace xqt;
namespace LM = xqt::LibraryMigration;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }
RecentFiles* recentOf(const AppController& c) { return static_cast<RecentFiles*>(c.recentModel()); }
LibraryModel* libraryOf(const AppController& c) { return static_cast<LibraryModel*>(c.libraryModel()); }

/// Android without a phone: "All files access" as the test says, one window for all libraries.
struct FakeAndroid: SystemApps {
    bool access = true;
    int asked = 0;
    bool librariesInOwnWindows() override { return false; }
    bool hasAllFilesAccess() override { return access; }
    bool requestAllFilesAccess() override {
        ++asked;
        return true;
    }
    bool startLibraryWindow(const QString&) override { return true; }
    bool openWithSystemApp(const QString&) override { return true; }
    bool showInFileManager(const QString&) override { return true; }
};

class LibraryHomeTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        phone.appDocuments = root / "storage/Android/data/org.xournalqt.app/files/Documents";
        phone.appDownloads = root / "storage/Android/data/org.xournalqt.app/files/Download";
        phone.sharedDocuments = root / "storage/Documents";
        phone.sharedDownloads = root / "storage/Download";
        phone.sharedStorage = root / "storage";
        inApp = phone.appDocuments / "Xournal_Libraries";
        shared = phone.sharedDocuments / "Xournal_Libraries";
        std::error_code ec;
        fs::remove(Util::getConfigFile("settings.xml"), ec);  // (the libraries' home is a setting)
        fs::remove(LM::manifestFile(), ec);
        fs::remove(SessionRecovery::defaultJournalFile(), ec);
    }
    void TearDown() override {
        LM::setCopyHook({});
        Library::setPlatformFolders(nullptr);
        SystemApps::setInstance(nullptr);
        DocumentPlaces::setLibrary({}, {});
        std::error_code ec;
        fs::remove(Util::getConfigFile("settings.xml"), ec);
        fs::remove(LM::manifestFile(), ec);
        fs::remove(SessionRecovery::defaultJournalFile(), ec);
    }
    /// The libraries of the app's own folder: Default with PDFs, a .xopp, a subfolder, the "Opened" folder and a
    /// hidden cache folder; "Uni" with a document; "Empty" without any file.
    void makeLibraries() {
        writeFile(inApp / "Default/paper.pdf", "%PDF-1.4 paper");
        writeFile(inApp / "Default/notes.xopp", "xopp of notes");
        writeFile(inApp / "Default/Lectures/week 1.pdf", "%PDF-1.4 week 1");
        writeFile(inApp / "Default/Lectures/week 1.xopp", "xopp of week 1");
        writeFile(inApp / "Default/Opened/shared.pdf", "%PDF-1.4 from another app");
        writeFile(inApp / "Default/.xournal_library/notes.pack", "cache");
        fs::create_directories(inApp / "Default/Empty folder");
        writeFile(inApp / "Uni/thesis.pdf", std::string(3 * 1024 * 1024, 'x'));  // (more than one chunk)
        fs::create_directories(inApp / "Empty");
    }
    QTemporaryDir tmp;
    fs::path root, inApp, shared;
    PlatformFolders phone;
    FakeAndroid android;
};

}  // namespace

// --- where the libraries are -----------------------------------------------------------------------------------------

TEST_F(LibraryHomeTest, theDesktopHasOneHome) {
    PlatformFolders desktop;
    desktop.appDocuments = root / "home/Documents";
    desktop.appDownloads = root / "home/Downloads";
    Library::setPlatformFolders(&desktop);
    EXPECT_EQ(Library::chooseHome(desktop, true, true), Library::Home::App);
    Library::setHome(Library::Home::Shared);  // (asked for where there is none)
    EXPECT_EQ(Library::home(), Library::Home::App);
    EXPECT_EQ(Library::librariesFolder(), root / "home/Documents/Xournal_Libraries");
    EXPECT_EQ(Library::defaultRoot(), root / "home/Documents/Xournal_Libraries/Default");
    EXPECT_EQ(Library::downloadsFolder(), root / "home/Downloads");
}

TEST_F(LibraryHomeTest, androidUsesThePhonesFoldersWithAccessOnceMoved) {
    Library::setPlatformFolders(&phone);
    EXPECT_EQ(Library::chooseHome(phone, false, false), Library::Home::App);
    EXPECT_EQ(Library::chooseHome(phone, true, false), Library::Home::App) << "not before the libraries were moved";
    EXPECT_EQ(Library::chooseHome(phone, false, true), Library::Home::App) << "not without the access";
    EXPECT_EQ(Library::chooseHome(phone, true, true), Library::Home::Shared);

    EXPECT_EQ(Library::home(), Library::Home::App);
    EXPECT_EQ(Library::librariesFolder(), inApp);
    Library::setHome(Library::Home::Shared);
    EXPECT_EQ(Library::librariesFolder(), shared);
    EXPECT_EQ(Library::defaultRoot(), shared / "Default");
    EXPECT_EQ(Library::librariesFolder(Library::Home::App), inApp);
    // The Downloads quick library is the phone's Download folder, not the app's own (always empty)
    EXPECT_EQ(Library::downloadsFolder(), phone.sharedDownloads);
    fs::create_directories(phone.sharedDownloads / "papers");
    EXPECT_TRUE(Library(phone.sharedDownloads / "papers").isTemporary());
}

TEST_F(LibraryHomeTest, theDownloadsQuickLibraryIsListedAlsoWithoutAccess) {
    Library::setPlatformFolders(&phone);
    struct NoAccess: FakeAndroid {
        bool needsAllFilesAccess(const QString&) override { return true; }
    } noAccess;
    noAccess.access = false;
    SystemApps::setInstance(&noAccess);
    AppController c;
    const QVariantList libs = c.libraries();
    ASSERT_FALSE(libs.isEmpty());
    EXPECT_TRUE(libs.last().toMap().value("downloads").toBool()) << "(not there as a folder the app can see)";
    // Tapping it: the access is explained and asked for first
    QSignalSpy needed(&c, &AppController::storageAccessNeeded);
    c.openLibraryAt(libs.last().toMap().value("path").toString());
    EXPECT_EQ(needed.count(), 1);
}

// --- the move, step by step --------------------------------------------------------------------------------------------

TEST_F(LibraryHomeTest, copyVerifySwitchAndCleanUp) {
    makeLibraries();
    const auto paperTime = fs::last_write_time(inApp / "Default/paper.pdf");
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    ASSERT_EQ(error, "");
    ASSERT_EQ(plan.moves.size(), 3u);  // Default, Empty, Uni
    EXPECT_EQ(plan.files, 7);
    EXPECT_TRUE(LM::hasContent(inApp));

    std::atomic<bool> cancel{false};
    int reports = 0;
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, [&](const LM::Progress&) { ++reports; }, error)) << error;
    EXPECT_GT(reports, 7);
    // Copied into hidden staging folders first: nothing under the libraries' names yet
    EXPECT_FALSE(fs::exists(shared / "Default"));
    EXPECT_TRUE(fs::exists(shared / ".xqt-moving-Default/Lectures/week 1.xopp"));
    ASSERT_TRUE(LM::commit(plan, error)) << error;
    EXPECT_FALSE(fs::exists(shared / ".xqt-moving-Default"));
    EXPECT_EQ(readFile(shared / "Default/Lectures/week 1.xopp"), "xopp of week 1");
    EXPECT_EQ(readFile(shared / "Default/Opened/shared.pdf"), "%PDF-1.4 from another app");
    EXPECT_EQ(readFile(shared / "Default/.xournal_library/notes.pack"), "cache") << "hidden files too";
    EXPECT_TRUE(fs::is_directory(shared / "Default/Empty folder"));
    EXPECT_TRUE(fs::is_directory(shared / "Empty"));
    EXPECT_EQ(fs::file_size(shared / "Uni/thesis.pdf"), 3u * 1024 * 1024);
    EXPECT_EQ(fs::last_write_time(shared / "Default/paper.pdf"), paperTime) << "the library knows files by their time";
    // The old place is whole until the clean-up
    EXPECT_EQ(readFile(inApp / "Default/notes.xopp"), "xopp of notes");

    const LM::Cleanup c = LM::cleanUp(plan);
    EXPECT_EQ(c.removed, 7);
    EXPECT_TRUE(c.kept.empty());
    EXPECT_FALSE(fs::exists(inApp)) << "the app's folder of libraries is gone";
    EXPECT_EQ(readFile(shared / "Default/notes.xopp"), "xopp of notes");
}

TEST_F(LibraryHomeTest, aFailedCopyLeavesTheOldPlaceAsItWas) {
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    int copies = 0;
    LM::setCopyHook([&](const fs::path&, const fs::path&) { return ++copies < 4; });  // (the 4th copy fails)
    std::atomic<bool> cancel{false};
    EXPECT_FALSE(LM::copyAndVerify(plan, cancel, {}, error));
    EXPECT_NE(error, "");
    EXPECT_FALSE(fs::exists(shared / ".xqt-moving-Default")) << "the staging folders are removed";
    EXPECT_FALSE(fs::exists(shared / "Default"));
    EXPECT_FALSE(LM::hasContent(shared));
    EXPECT_EQ(readFile(inApp / "Default/Lectures/week 1.xopp"), "xopp of week 1");
    EXPECT_EQ(readFile(inApp / "Uni/thesis.pdf").size(), 3u * 1024 * 1024);
}

TEST_F(LibraryHomeTest, aDamagedCopyIsFoundByItsHash) {
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    // The same size, one byte different
    LM::setCopyHook([](const fs::path& original, const fs::path& copy) {
        if (original.filename() == "thesis.pdf") {
            std::fstream f(copy, std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(2 * 1024 * 1024);
            f.put('y');
        }
        return true;
    });
    std::atomic<bool> cancel{false};
    EXPECT_FALSE(LM::copyAndVerify(plan, cancel, {}, error));
    EXPECT_NE(error.find("not the same"), std::string::npos) << error;
    EXPECT_FALSE(LM::hasContent(shared));
    EXPECT_TRUE(fs::exists(inApp / "Uni/thesis.pdf"));
}

TEST_F(LibraryHomeTest, anOriginalChangedWhileCopyingStopsTheMove) {
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    LM::setCopyHook([](const fs::path& original, const fs::path&) {
        if (original.filename() == "notes.xopp") {
            std::ofstream(original, std::ios::app) << " and a new stroke";  // (saved by the user meanwhile)
        }
        return true;
    });
    std::atomic<bool> cancel{false};
    EXPECT_FALSE(LM::copyAndVerify(plan, cancel, {}, error));
    EXPECT_NE(error.find("changed while"), std::string::npos) << error;
    EXPECT_EQ(readFile(inApp / "Default/notes.xopp"), "xopp of notes and a new stroke");
    EXPECT_FALSE(LM::hasContent(shared));
}

TEST_F(LibraryHomeTest, cacheFilesWrittenMeanwhileDoNotStopTheMove) {
    // The library's search index and previews finish writing in the background after it was let go
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    LM::setCopyHook([&](const fs::path& original, const fs::path&) {
        if (original.filename() == "notes.pack") {
            writeFile(original, "cache, written again");
            writeFile(inApp / "Default/Lectures/.xournal_library/notes.pack", "a new cache file");
        }
        return true;
    });
    std::atomic<bool> cancel{false};
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, {}, error)) << error;
    ASSERT_TRUE(LM::commit(plan, error));
    const LM::Cleanup c = LM::cleanUp(plan);
    EXPECT_TRUE(c.kept.empty());
    EXPECT_FALSE(fs::exists(inApp)) << "caches go with the old place";
    EXPECT_TRUE(fs::exists(shared / "Default/notes.xopp"));
}

TEST_F(LibraryHomeTest, aTakenNameIsNeverMergedInto) {
    makeLibraries();
    writeFile(shared / "Uni/other.pdf", "the phone's own Uni");
    fs::create_directories(shared / "Empty");
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    ASSERT_EQ(plan.moves.size(), 2u) << "the empty library whose name is taken has nothing to move";
    ASSERT_EQ(plan.dropped.size(), 1u);
    const auto uni = std::find_if(plan.moves.begin(), plan.moves.end(), [](const LM::Move& m) { return m.from.filename() == "Uni"; });
    ASSERT_NE(uni, plan.moves.end());
    EXPECT_EQ(uni->to, shared / "Uni (2)");
    EXPECT_TRUE(uni->renamed);
    std::atomic<bool> cancel{false};
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, {}, error)) << error;
    // Another app makes "Default" meanwhile: the move takes the next free name
    writeFile(shared / "Default/from sync.pdf", "synced");
    ASSERT_TRUE(LM::commit(plan, error)) << error;
    EXPECT_EQ(readFile(shared / "Uni/other.pdf"), "the phone's own Uni");
    EXPECT_FALSE(fs::exists(shared / "Uni/thesis.pdf"));
    EXPECT_TRUE(fs::exists(shared / "Uni (2)/thesis.pdf"));
    EXPECT_TRUE(fs::exists(shared / "Default (2)/notes.xopp"));
    EXPECT_EQ(fs::directory_iterator(shared / "Default") != fs::directory_iterator(), true);
    LM::cleanUp(plan);
    EXPECT_FALSE(fs::exists(inApp));
}

TEST_F(LibraryHomeTest, theCleanUpKeepsFilesChangedSinceTheCopy) {
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    std::atomic<bool> cancel{false};
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, {}, error));
    ASSERT_TRUE(LM::commit(plan, error));
    writeFile(inApp / "Default/notes.xopp", "changed after the copy");
    const LM::Cleanup c = LM::cleanUp(plan);
    ASSERT_EQ(c.kept.size(), 1u);
    EXPECT_EQ(c.kept[0], inApp / "Default/notes.xopp");
    EXPECT_EQ(readFile(inApp / "Default/notes.xopp"), "changed after the copy");
    EXPECT_FALSE(fs::exists(inApp / "Default/paper.pdf"));
}

TEST_F(LibraryHomeTest, theManifestKeepsWhatWasVerified) {
    makeLibraries();
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    std::atomic<bool> cancel{false};
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, {}, error));
    ASSERT_TRUE(LM::commit(plan, error));
    const fs::path file = root / "manifest.json";
    ASSERT_TRUE(LM::writeManifest(plan, file));
    const auto again = LM::readManifest(file);
    ASSERT_TRUE(again);
    ASSERT_EQ(again->moves.size(), plan.moves.size());
    EXPECT_EQ(again->files, plan.files);
    for (size_t i = 0; i < plan.moves.size(); ++i) {
        EXPECT_EQ(again->moves[i].to, plan.moves[i].to);
        ASSERT_EQ(again->moves[i].files.size(), plan.moves[i].files.size());
        for (size_t f = 0; f < plan.moves[i].files.size(); ++f) {
            EXPECT_EQ(again->moves[i].files[f].mtimeNs, plan.moves[i].files[f].mtimeNs);
            EXPECT_EQ(again->moves[i].files[f].hash, plan.moves[i].files[f].hash);
        }
    }
    EXPECT_EQ(LM::cleanUp(*again).removed, 7);
}

// --- the move in the app ----------------------------------------------------------------------------------------------

namespace {
/// Waits for the move (copy, then clean-up) to finish: the controller's message about it.
bool waitForMove(AppController& c, QSignalSpy& messages) {
    return messages.count() > 0 || messages.wait(20000);
}
}  // namespace

TEST_F(LibraryHomeTest, theAppMovesItsLibrariesAndEverythingFollows) {
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    makeLibraries();
    const fs::path oldDoc = inApp / "Default/Lectures/week 1.xopp";
    const fs::path oldUni = inApp / "Uni/thesis.pdf";
    {
        // Journal of a previous run with a tab of the library
        SessionRecovery::Journal j;
        j.clean = true;
        j.tabs.push_back({oldDoc, 0, 0, 2, false});
        ASSERT_TRUE(SessionRecovery::writeJournal(j, SessionRecovery::defaultJournalFile()));
    }
    AppController c;
    c.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::App) << "there is something to move: not before it was moved";
    EXPECT_TRUE(c.librariesInApp());
    EXPECT_TRUE(c.offerLibrariesHome());
    EXPECT_EQ(c.sharedLibrariesName(), "Documents/Xournal_Libraries");
    EXPECT_EQ(c.librariesToMove(), "7 files, 3.0 MB");
    c.setLibraryRoot(inApp / "Default");
    // Recent files, reading positions (of this library, and of one not open), a remembered folder
    recentOf(c)->add(oldDoc);
    recentOf(c)->add(oldUni);
    DocumentPlaces::setLastPage(oldDoc, 3);
    DocumentPlaces::setLastPage(oldUni, 7);  // (another library: kept by its whole path)
    DocumentPlaces::setTitlePage(inApp / "Default/paper.pdf", 1);

    QSignalSpy messages(&c, &AppController::message);
    c.moveLibrariesHome();
    ASSERT_TRUE(waitForMove(c, messages));
    ASSERT_EQ(messages.count(), 1);
    EXPECT_FALSE(messages.at(0).at(2).toBool()) << messages.at(0).at(1).toString().toStdString();
    EXPECT_TRUE(messages.at(0).at(1).toString().contains("Documents/Xournal_Libraries"));

    // The files, in the new place only
    EXPECT_EQ(readFile(shared / "Default/Lectures/week 1.xopp"), "xopp of week 1");
    EXPECT_EQ(fs::file_size(shared / "Uni/thesis.pdf"), 3u * 1024 * 1024);
    EXPECT_FALSE(fs::exists(inApp));
    EXPECT_FALSE(fs::exists(LM::manifestFile())) << "done";
    // The app works there
    EXPECT_EQ(Library::home(), Library::Home::Shared);
    EXPECT_FALSE(c.librariesInApp());
    EXPECT_FALSE(c.offerLibrariesHome());
    ASSERT_NE(libraryOf(c)->library(), nullptr);
    EXPECT_EQ(libraryOf(c)->library()->root(), Library(shared / "Default").root());
    EXPECT_EQ(c.rememberedLibrary(), qstr(Library(shared / "Default").root()));
    // What stores paths followed
    const fs::path newDoc = Library(shared / "Default").root() / "Lectures/week 1.xopp";
    const fs::path newUni = shared / "Uni/thesis.pdf";
    QStringList recentPaths;
    for (int i = 0; i < recentOf(c)->count(); ++i) {
        recentPaths << recentOf(c)->data(recentOf(c)->index(i), RecentFiles::PathRole).toString();
    }
    EXPECT_TRUE(recentPaths.contains(qstr(newDoc))) << recentPaths.join(", ").toStdString();
    EXPECT_TRUE(recentPaths.contains(qstr(newUni))) << recentPaths.join(", ").toStdString();
    EXPECT_EQ(DocumentPlaces::lastPage(newDoc), 3);
    EXPECT_EQ(DocumentPlaces::titlePage(Library(shared / "Default").root() / "paper.pdf"), 1);
    EXPECT_EQ(DocumentPlaces::lastPage(newUni), 7);
    const auto journal = SessionRecovery::readJournal(SessionRecovery::defaultJournalFile());
    ASSERT_TRUE(journal);
    ASSERT_EQ(journal->tabs.size(), 1u);
    EXPECT_EQ(journal->tabs[0].file, Library(shared).root() / "Default/Lectures/week 1.xopp");

    // The next start chooses the phone's folder by itself
    Library::setHome(Library::Home::App);
    AppController next;
    next.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::Shared);
    // ... but not without the access (the app's folder, which is empty now)
    android.access = false;
    next.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::App);
    EXPECT_TRUE(next.librariesInApp());
}

TEST_F(LibraryHomeTest, openTabsFollowTheMove) {
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    writeFile(inApp / "Default/paper.pdf", "%PDF-1.4 paper");
    AppController c;
    c.chooseLibrariesHome();
    c.setLibraryRoot(inApp / "Default");
    QSignalSpy failed(&c, &AppController::message);
    ASSERT_TRUE(c.createDocument("Lecture", true)) << (failed.isEmpty() ? std::string() : failed.at(0).at(1).toString().toStdString());
    ASSERT_EQ(c.tabManager().count(), 1);
    const fs::path before = c.tabManager().session(0)->getFilePath();
    ASSERT_TRUE(fs::exists(before));
    QSignalSpy messages(&c, &AppController::message);
    c.moveLibrariesHome();
    ASSERT_TRUE(waitForMove(c, messages));
    EXPECT_FALSE(messages.at(0).at(2).toBool()) << messages.at(0).at(1).toString().toStdString();
    const fs::path after = c.tabManager().session(0)->getFilePath();
    EXPECT_EQ(after, Library(shared / "Default").root() / "Lecture.xopp");
    EXPECT_TRUE(fs::exists(after));
    EXPECT_FALSE(fs::exists(before));
}

TEST_F(LibraryHomeTest, aFailureKeepsTheAppInTheOldPlaceAndSaysSo) {
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    makeLibraries();
    AppController c;
    c.chooseLibrariesHome();
    c.setLibraryRoot(inApp / "Default");
    recentOf(c)->add(inApp / "Default/notes.xopp");
    LM::setCopyHook([](const fs::path& original, const fs::path&) { return original.filename() != "thesis.pdf"; });
    QSignalSpy messages(&c, &AppController::message);
    c.moveLibrariesHome();
    ASSERT_TRUE(waitForMove(c, messages));
    ASSERT_EQ(messages.count(), 1);
    EXPECT_TRUE(messages.at(0).at(2).toBool()) << "an error";
    EXPECT_TRUE(messages.at(0).at(1).toString().contains("Nothing was changed"));
    EXPECT_EQ(Library::home(), Library::Home::App);
    EXPECT_TRUE(c.librariesInApp());
    ASSERT_NE(libraryOf(c)->library(), nullptr);
    EXPECT_EQ(libraryOf(c)->library()->root(), Library(inApp / "Default").root()) << "in use again";
    EXPECT_EQ(readFile(inApp / "Default/notes.xopp"), "xopp of notes");
    EXPECT_FALSE(LM::hasContent(shared));
    EXPECT_EQ(recentOf(c)->data(recentOf(c)->index(0), RecentFiles::PathRole).toString(),
              qstr(inApp / "Default/notes.xopp"));
    // The next start: still the app's folder, and the offer again
    AppController next;
    next.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::App);
    EXPECT_TRUE(next.offerLibrariesHome());
}

TEST_F(LibraryHomeTest, aNameTakenOnThePhoneBecomesNameTwo) {
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    makeLibraries();
    writeFile(shared / "Default/phone.pdf", "the phone's own");
    AppController c;
    c.chooseLibrariesHome();
    c.setLibraryRoot(inApp / "Default");
    QSignalSpy messages(&c, &AppController::message);
    c.moveLibrariesHome();
    ASSERT_TRUE(waitForMove(c, messages));
    EXPECT_TRUE(messages.at(0).at(1).toString().contains("Default (2)")) << messages.at(0).at(1).toString().toStdString();
    EXPECT_EQ(readFile(shared / "Default/phone.pdf"), "the phone's own");
    EXPECT_FALSE(fs::exists(shared / "Default/notes.xopp"));
    EXPECT_EQ(readFile(shared / "Default (2)/notes.xopp"), "xopp of notes");
    EXPECT_EQ(libraryOf(c)->library()->root(), Library(shared / "Default (2)").root()) << "the library shown before";
}

TEST_F(LibraryHomeTest, withoutAccessItIsAskedForFirstAndDecliningKeepsTheHint) {
    Library::setPlatformFolders(&phone);
    android.access = false;
    SystemApps::setInstance(&android);
    makeLibraries();
    AppController c;
    c.chooseLibrariesHome();
    EXPECT_TRUE(c.offerLibrariesHome());
    c.moveLibrariesHome();
    EXPECT_EQ(android.asked, 1) << "the system's page for the access";
    EXPECT_FALSE(static_cast<LibraryMove*>(c.libraryMoveObject())->running());
    c.declineLibrariesHome();
    EXPECT_FALSE(c.offerLibrariesHome()) << "not at the next start";
    EXPECT_TRUE(c.librariesInApp()) << "the hint stays";
}

TEST_F(LibraryHomeTest, nothingToMoveUsesThePhonesFolderAtOnce) {
    // Installed again after the libraries were moved (they are still on the phone): with the access, they are used
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    writeFile(shared / "Default/notes.xopp", "kept on the phone");
    fs::create_directories(inApp / "Default");  // (made empty at a start before)
    AppController c;
    c.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::Shared);
    EXPECT_FALSE(c.librariesInApp());
    EXPECT_EQ(Library::defaultRoot(), shared / "Default");
    EXPECT_FALSE(fs::exists(inApp / "Default/Default (2)"));
    EXPECT_FALSE(fs::exists(shared / "Default (2)"));
}

TEST_F(LibraryHomeTest, aMoveEndedBeforeItsCleanUpIsFinishedAtTheNextStart) {
    Library::setPlatformFolders(&phone);
    SystemApps::setInstance(&android);
    makeLibraries();
    {
        AppController c;
        recentOf(c)->add(inApp / "Default/notes.xopp");
    }
    // Copied, verified and switched, then the app was ended (no paths followed, nothing cleaned up)
    std::string error;
    LM::Plan plan = LM::plan(inApp, shared, error);
    std::atomic<bool> cancel{false};
    ASSERT_TRUE(LM::copyAndVerify(plan, cancel, {}, error));
    ASSERT_TRUE(LM::commit(plan, error));
    ASSERT_TRUE(LM::writeManifest(plan, LM::manifestFile()));

    AppController c;
    QSignalSpy running(c.libraryMoveObject(), SIGNAL(runningChanged()));
    c.chooseLibrariesHome();
    EXPECT_EQ(Library::home(), Library::Home::Shared);
    EXPECT_EQ(recentOf(c)->data(recentOf(c)->index(0), RecentFiles::PathRole).toString(),
              qstr(Library(shared).root() / "Default/notes.xopp"));
    while (static_cast<LibraryMove*>(c.libraryMoveObject())->running()) {
        ASSERT_TRUE(running.wait(10000));
    }
    EXPECT_FALSE(fs::exists(inApp));
    EXPECT_FALSE(fs::exists(LM::manifestFile()));
    EXPECT_EQ(readFile(shared / "Default/notes.xopp"), "xopp of notes");
}

TEST_F(LibraryHomeTest, accessGivenForAnotherFolderUsesThePhonesLibraries) {
    // Installed again, "Not now", then the Downloads folder is opened: the access asked for there also brings back the
    // libraries kept on the phone (the app's folder has nothing to move)
    Library::setPlatformFolders(&phone);
    struct Phone: FakeAndroid {
        bool needsAllFilesAccess(const QString& folder) override { return !folder.contains("/Android/data/"); }
    } phoneApps;
    phoneApps.access = false;
    SystemApps::setInstance(&phoneApps);
    writeFile(shared / "Default/notes.xopp", "kept on the phone");
    fs::create_directories(phone.sharedDownloads);
    fs::create_directories(inApp / "Default");
    AppController c;
    c.chooseLibrariesHome();
    c.setLibraryRoot(Library::defaultRoot());
    ASSERT_TRUE(c.librariesInApp());
    QSignalSpy needed(&c, &AppController::storageAccessNeeded);
    c.openLibraryAt(qstr(phone.sharedDownloads));
    ASSERT_EQ(needed.count(), 1);
    c.requestStorageAccess(qstr(phone.sharedDownloads));
    EXPECT_EQ(phoneApps.asked, 1);
    phoneApps.access = true;  // (turned on in Android's settings)
    c.applicationStateChanged(Qt::ApplicationInactive);
    c.applicationStateChanged(Qt::ApplicationActive);
    EXPECT_FALSE(c.librariesInApp());
    EXPECT_EQ(Library::home(), Library::Home::Shared);
    ASSERT_NE(libraryOf(c)->library(), nullptr);
    EXPECT_EQ(libraryOf(c)->library()->root(), Library(phone.sharedDownloads).root()) << "then the folder asked for";
    EXPECT_EQ(Library::defaultRoot(), shared / "Default");
}

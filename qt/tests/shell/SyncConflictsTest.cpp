/*
 * xournal-qt: conflict copies of sync apps, told by their names, and shown in the library as conflicts of their
 * document.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/SyncConflicts.h"
#include "shell/SystemApps.h"
#include "shell/TabManager.h"
#include "AppController.h"
#include "config-test.h"

using namespace xqt;

namespace {
std::string originalOf(const std::string& name) {
    const auto c = SyncConflicts::parse(name);
    return c ? c->original : std::string("-");
}
std::string appOf(const std::string& name) {
    const auto c = SyncConflicts::parse(name);
    return c ? c->app : std::string("-");
}
std::string whenOf(const std::string& name) {
    const auto c = SyncConflicts::parse(name);
    return c ? c->when : std::string("-");
}
void touch(const fs::path& p, const std::string& content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}
}  // namespace

// Names as the apps write them (from their sources and documentation, and files seen in synced folders)
TEST(SyncConflicts, namesOfTheSyncApps) {
    // Syncthing (lib/model/folder_sendrecv.go: <name>.sync-conflict-<yyyyMMdd>-<HHmmss>-<short device id><ext>)
    EXPECT_EQ(originalOf("notes.sync-conflict-20240312-101530-ABCDEFG.xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes.sync-conflict-20240312-101530-ABCDEFG.xopp"), "Syncthing");
    EXPECT_EQ(whenOf("notes.sync-conflict-20240312-101530-ABCDEFG.xopp"), "2024-03-12 10:15");
    EXPECT_EQ(originalOf("Lecture 3.v2.sync-conflict-20231101-083000-Q2W3E4R.pdf"), "Lecture 3.v2.pdf");
    EXPECT_EQ(originalOf("Übung (Blatt 2).sync-conflict-20240101-000001-7ZZZZZZ.pdf"), "Übung (Blatt 2).pdf");
    EXPECT_EQ(originalOf("summary.sync-conflict-20240312-101530.md"), "summary.md") << "older versions: no device";
    EXPECT_EQ(originalOf("Makefile.sync-conflict-20240312-101530-ABCDEFG"), "Makefile") << "no extension";
    EXPECT_EQ(originalOf("lecture.pdf.sync-conflict-20240312-101530-ABCDEFG.xopp"), "lecture.pdf.xopp");
    // Dropbox
    EXPECT_EQ(originalOf("notes (conflicted copy).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (Anna's conflicted copy 2024-03-12).xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes (Anna's conflicted copy 2024-03-12).xopp"), "Dropbox");
    EXPECT_EQ(whenOf("notes (Anna's conflicted copy 2024-03-12).xopp"), "2024-03-12");
    EXPECT_EQ(originalOf("notes (Anna’s conflicted copy 2024-03-12).xopp"), "notes.xopp") << "a typographic apostrophe";
    EXPECT_EQ(originalOf("Thesis (Anna Maria Müller's conflicted copy 2024-03-12 (1)).pdf"), "-")
            << "(nested parentheses: not a name Dropbox writes)";
    EXPECT_EQ(originalOf("Paper (Case Conflict).pdf"), "Paper.pdf");
    EXPECT_EQ(originalOf("Paper (Case Conflict 1).pdf"), "Paper.pdf");
    EXPECT_EQ(originalOf("Paper (Selective Sync Conflict).pdf"), "Paper.pdf");
    EXPECT_EQ(originalOf("Paper (Unicode Encoding Conflict).pdf"), "Paper.pdf");
    EXPECT_EQ(appOf("Paper (Selective Sync Conflict).pdf"), "Dropbox");
    // Nextcloud and ownCloud desktop clients (src/libsync/syncengine: "<name> (conflicted copy <date> <time>)<ext>"),
    // also translated
    EXPECT_EQ(originalOf("notes (conflicted copy 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes (conflicted copy 2024-03-12 101530).xopp"), "Nextcloud");
    EXPECT_EQ(whenOf("notes (conflicted copy 2024-03-12 101530).xopp"), "2024-03-12 10:15");
    EXPECT_EQ(originalOf("notes (Konflikt 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (Konfliktkopie 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (in Konflikt stehende Kopie 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (copie en conflit 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (copia in conflitto 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (copia en conflicto 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (cópia em conflito 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (kopia w konflikcie 2024-03-12 101530).xopp"), "-") << "(Polish inflects the word)";
    EXPECT_EQ(originalOf("notes (конфликтующая копия 2024-03-12 101530).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes (conflicted copy 2024-03-12 101530).tar.gz"), "-") << "the mark before the last one";
    EXPECT_EQ(originalOf("notes_conflict-20240312-101530.xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes_conflict-20240312-101530.xopp"), "ownCloud");
    // Seafile
    EXPECT_EQ(originalOf("notes (SFConflict anna@example.org 2024-03-12-10-15-30).xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes (SFConflict anna@example.org 2024-03-12-10-15-30).xopp"), "Seafile");
    EXPECT_EQ(whenOf("notes (SFConflict anna@example.org 2024-03-12-10-15-30).xopp"), "2024-03-12 10:15");
    // OneDrive (the computer's name; Windows' default names)
    EXPECT_EQ(originalOf("notes-DESKTOP-AB12CDE.xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes-LAPTOP-7Q2W3E4.xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes-DESKTOP-AB12CDE-2.xopp"), "notes.xopp");
    EXPECT_EQ(appOf("notes-DESKTOP-AB12CDE.xopp"), "OneDrive");
    // Generic: a word for "conflict" in parentheses (FolderSync, Autosync and others that follow the big apps)
    EXPECT_EQ(originalOf("notes (conflict).xopp"), "notes.xopp");
    EXPECT_EQ(originalOf("notes(Conflict 2).xopp"), "notes.xopp");
}

TEST(SyncConflicts, ordinaryNamesAreNot) {
    for (const char* name: {"notes.xopp", "notes (2).xopp", "notes (copy).xopp", "Conflict resolution.pdf",
                            "sync-conflict.pdf", "notes.sync-conflict.xopp", "notes.sync-conflict-2024-ABCDEFG.xopp",
                            "notes-DESKTOP.xopp", "notes-DESKTOP-abc.xopp",
                            "notes_conflict.xopp", ".notes.sync-conflict-20240312-101530-ABCDEFG.xopp",
                            "Lecture 3 (draft).pdf", ""}) {
        EXPECT_EQ(originalOf(name), "-") << name;
    }
    // A name that only looks like one: it is a conflict copy by its name, the library needs the document too
    EXPECT_EQ(originalOf("Essay (conflict theory).pdf"), "Essay.pdf");
    EXPECT_EQ(originalOf("Game theory (conflicts)"), "Game theory");
}

// The library shows a conflict copy as a conflict of its document (on its card), not as a document of its own, and
// only when that document is in the same folder.
TEST(SyncConflicts, theLibraryFoldsThemIntoTheirDocument) {
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    touch(root / "notes.xopp");
    touch(root / "notes.sync-conflict-20240312-101530-ABCDEFG.xopp");
    touch(root / "notes (conflicted copy 2024-03-12 101530).xopp");
    touch(root / "lecture.xopp");
    touch(root / "lecture.pdf");
    touch(root / "lecture.sync-conflict-20240312-101530-ABCDEFG.pdf");  // (the PDF of a pair)
    touch(root / "summary.md");
    touch(root / "summary (Anna's conflicted copy 2024-03-12).md");
    touch(root / "Essay (conflict theory).pdf");  // (no "Essay.pdf": a document)
    touch(root / "gone.sync-conflict-20240312-101530-ABCDEFG.xopp");  // (its document is gone: shown)
    const auto l = DocumentFiles::scan(root);
    std::map<std::string, std::vector<std::string>> shown;
    for (const auto& item: l.items) {
        auto& conflicts = shown[item.main().filename().string()];
        for (const auto& c: item.conflicts) {
            conflicts.push_back(c.filename().string());
        }
        std::sort(conflicts.begin(), conflicts.end());
    }
    const std::map<std::string, std::vector<std::string>> expected{
            {"notes.xopp",
             {"notes (conflicted copy 2024-03-12 101530).xopp", "notes.sync-conflict-20240312-101530-ABCDEFG.xopp"}},
            {"lecture.xopp", {"lecture.sync-conflict-20240312-101530-ABCDEFG.pdf"}},
            {"summary.md", {"summary (Anna's conflicted copy 2024-03-12).md"}},
            {"Essay (conflict theory).pdf", {}},
            {"gone.sync-conflict-20240312-101530-ABCDEFG.xopp", {}},
    };
    EXPECT_EQ(shown, expected);
    // Searched and indexed like the rest of the folder: not as documents of their own
    const auto all = DocumentFiles::scanRecursive(root);
    EXPECT_EQ(all.size(), 5u);
}

namespace {
/// The trash, without the user's: files are removed (and recorded)
struct FakeTrash: SystemApps {
    QStringList trashed;
    bool moveToTrash(const QString& path) override {
        trashed << path;
        return QFile::remove(path);
    }
};
std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
}  // namespace

// "Keep one": keeping the document moves the copy to the trash; keeping the copy moves the document's file to the
// trash and gives the copy its name. "Compare": both side by side (the copy as the document's reference).
TEST(SyncConflicts, keepOneOrCompare) {
    FakeTrash trash;
    SystemApps::setInstance(&trash);
    QTemporaryDir tmp;
    const fs::path root(tmp.path().toStdString());
    fs::copy_file(GET_TESTFILE(u8"load/strokes.xopp"), root / "notes.xopp");
    const fs::path a = root / "notes.sync-conflict-20240312-101530-ABCDEFG.xopp";
    const fs::path b = root / "notes (conflicted copy 2024-03-13 090000).xopp";
    fs::copy_file(GET_TESTFILE(u8"load/strokes.xopp"), a);
    fs::copy_file(GET_TESTFILE(u8"load/pages.xopp"), b);  // (another version: other bytes)
    {
        AppController c;
        ASSERT_TRUE(c.compareConflict(QString::fromStdString((root / "notes.xopp").string()),
                                      QString::fromStdString(a.string())));
        TabManager& tabs = c.tabManager();
        ASSERT_EQ(tabs.count(), 2);
        EXPECT_EQ(tabs.currentSession()->getFilePath(), root / "notes.xopp");
        const int reference = tabs.referenceOf(tabs.currentIndex());
        ASSERT_GE(reference, 0);
        EXPECT_EQ(tabs.session(reference)->getFilePath(), a);
    }
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.data(model.index(0), LibraryModel::ConflictsRole).toStringList().size(), 2);
    const QVariantList list = model.conflictsOf(QString::fromStdString((root / "notes.xopp").string()));
    ASSERT_EQ(list.size(), 3);
    EXPECT_TRUE(list[0].toMap()["original"].toBool());
    EXPECT_EQ(list[1].toMap()["app"].toString() + list[2].toMap()["app"].toString(), "NextcloudSyncthing");

    const std::string copyBytes = bytesOf(b);
    ASSERT_TRUE(model.resolveConflict(QString::fromStdString(a.string()), false));  // keep the document
    EXPECT_EQ(trash.trashed, QStringList{QString::fromStdString(a.string())});
    EXPECT_TRUE(fs::exists(root / "notes.xopp"));
    ASSERT_TRUE(model.resolveConflict(QString::fromStdString(b.string()), true));  // keep the copy
    EXPECT_EQ(trash.trashed.last(), QString::fromStdString((root / "notes.xopp").string()));
    EXPECT_FALSE(fs::exists(b));
    EXPECT_EQ(bytesOf(root / "notes.xopp"), copyBytes) << "the copy has the document's name";
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::ConflictsRole).toStringList().isEmpty());
    model.setLibrary(nullptr);
    SystemApps::setInstance(nullptr);
}

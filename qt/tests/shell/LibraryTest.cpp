/*
 * xournal-qt: the library (documents as files, pairs of .xopp and PDF, search index, previews, recent files).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <iostream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include <cairo-pdf.h>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/HitPages.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/Previews.h"
#include "shell/RecentFiles.h"

#include "config-test.h"

using namespace xqt;

namespace {
fs::path fixture(const char8_t* rel) { return GET_TESTFILE(rel); }

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

/// A PDF with text ("xournal" on page 1, "Page 2" on page 2), copied from the fixtures.
void makePdf(const fs::path& p) {
    fs::create_directories(p.parent_path());
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), p);
}

/// "<name>.xopp" annotating `pdf` (a relative reference, as upstream saves it).
void makeAnnotation(const fs::path& pdf, const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(pdf);
    ASSERT_TRUE(loaded.document) << loaded.error;
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}

/// The background PDF the .xopp loads, empty if it has none or it is missing.
fs::path backgroundOf(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    EXPECT_TRUE(loaded.document) << loaded.error;
    if (!loaded.document || !loaded.missingPdf.empty() || loaded.attachedPdfMissing) {
        return {};
    }
    return loaded.document->getPdfFilepath();
}

std::vector<std::string> names(const std::vector<DocumentItem>& items) {
    std::vector<std::string> n;
    for (const auto& i: items) {
        n.push_back(i.name());
    }
    return n;
}

void waitFor(const std::function<bool()>& cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

class LibraryTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(LibraryTest, scanShowsPairsLoneFilesAndFoldersOnly) {
    touch(root / "lecture.xopp");
    touch(root / "lecture.pdf");
    touch(root / "notes.xopp");
    touch(root / "paper.pdf");
    touch(root / "attached.xopp");
    touch(root / "attached.xopp.bg.pdf");  // part of attached.xopp
    touch(root / "old.pdf");
    touch(root / "old.pdf.xopp");  // older Xournal++ naming
    touch(root / "lecture.xopp~");  // backup
    touch(root / ".lecture.autosave.xopp");
    touch(root / ".xournal_library" / "previews" / "x.png");
    touch(root / "readme.txt");
    fs::create_directories(root / "Physics");
    touch(root / "Physics" / "sheet.pdf");

    const auto l = DocumentFiles::scan(root);
    ASSERT_EQ(l.folders.size(), 1u);
    EXPECT_EQ(l.folders[0].filename(), "Physics");
    EXPECT_EQ(names(l.items), (std::vector<std::string>{"attached", "lecture", "notes", "old", "paper"}));
    for (const auto& item: l.items) {
        if (item.name() == "lecture") {
            EXPECT_EQ(item.xopp, root / "lecture.xopp");
            EXPECT_EQ(item.pdf, root / "lecture.pdf");
            EXPECT_EQ(item.main(), root / "lecture.xopp");
        } else if (item.name() == "old") {
            EXPECT_EQ(item.pdf, root / "old.pdf");
        } else if (item.name() == "paper") {
            EXPECT_TRUE(item.xopp.empty());
        } else {
            EXPECT_TRUE(item.pdf.empty()) << item.name();
        }
    }
    EXPECT_EQ(DocumentFiles::scanRecursive(root).size(), 6u);
    // The same pairing from any of the two files
    EXPECT_EQ(DocumentFiles::itemOf(root / "lecture.pdf"), (DocumentItem{root / "lecture.xopp", root / "lecture.pdf"}));
    EXPECT_EQ(DocumentFiles::itemOf(root / "lecture.xopp"), (DocumentItem{root / "lecture.xopp", root / "lecture.pdf"}));
    EXPECT_FALSE(DocumentFiles::itemOf(root / "readme.txt").valid());
    EXPECT_FALSE(DocumentFiles::itemOf(root / "missing.pdf").valid());
}

TEST_F(LibraryTest, renameKeepsThePairTogether) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    const auto r = DocumentFiles::rename(DocumentFiles::itemOf(root / "lecture.xopp"), "Week 1");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(fs::exists(root / "lecture.xopp"));
    EXPECT_FALSE(fs::exists(root / "lecture.pdf"));
    EXPECT_EQ(r.item, (DocumentItem{root / "Week 1.xopp", root / "Week 1.pdf"}));
    EXPECT_EQ(backgroundOf(root / "Week 1.xopp"), root / "Week 1.pdf") << "the reference follows";
    EXPECT_EQ(r.moved.size(), 2u);

    // Names that exist or cannot be file names
    touch(root / "taken.pdf");
    EXPECT_FALSE(DocumentFiles::rename(r.item, "taken").ok);
    EXPECT_FALSE(DocumentFiles::rename(r.item, "a/b").ok);
    EXPECT_FALSE(DocumentFiles::rename(r.item, "").ok);
    EXPECT_FALSE(DocumentFiles::rename(r.item, ".hidden").ok);
    EXPECT_TRUE(fs::exists(root / "Week 1.xopp"));

    // A lone PDF
    const auto p = DocumentFiles::rename(DocumentFiles::itemOf(root / "taken.pdf"), "paper");
    ASSERT_TRUE(p.ok) << p.error;
    EXPECT_TRUE(fs::exists(root / "paper.pdf"));
}

TEST_F(LibraryTest, moveTakesBothFilesAndAttachments) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    fs::create_directories(root / "Physics");
    auto r = DocumentFiles::move(DocumentFiles::itemOf(root / "lecture.pdf"), root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(backgroundOf(root / "Physics" / "lecture.xopp"), root / "Physics" / "lecture.pdf");
    EXPECT_FALSE(fs::exists(root / "lecture.xopp"));
    EXPECT_TRUE(DocumentFiles::scan(root).items.empty());

    // The same name in the target: the moved one gets a free name
    makePdf(root / "lecture.pdf");
    r = DocumentFiles::move(DocumentFiles::itemOf(root / "lecture.pdf"), root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.pdf, root / "Physics" / "lecture (2).pdf");

    // An attached PDF ("name.xopp.bg.pdf") goes with its .xopp
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp"), root / "att.xopp");
    fs::copy_file(fixture(u8"packaged_xopp/pdfBackground/old.xopp.bg.pdf"), root / "att.xopp.bg.pdf");
    r = DocumentFiles::move(DocumentFiles::itemOf(root / "att.xopp"), root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Physics" / "att.xopp.bg.pdf"));
    EXPECT_FALSE(fs::exists(root / "att.xopp.bg.pdf"));
    EXPECT_FALSE(backgroundOf(root / "Physics" / "att.xopp").empty());
}

TEST_F(LibraryTest, importCopiesTheDocumentWithThePdfItUses) {
    const fs::path lib = root / "lib", src = root / "src";
    fs::create_directories(lib);
    // An annotation of a PDF that lives elsewhere, under another name
    makePdf(src / "downloads" / "script-v3.pdf");
    makeAnnotation(src / "downloads" / "script-v3.pdf", src / "notes.xopp");

    auto r = DocumentFiles::import(src / "notes.xopp", lib);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item, (DocumentItem{lib / "notes.xopp", lib / "notes.pdf"}));
    EXPECT_EQ(backgroundOf(lib / "notes.xopp"), lib / "notes.pdf") << "the library has its own copy";
    EXPECT_EQ(backgroundOf(src / "notes.xopp"), src / "downloads" / "script-v3.pdf") << "the original is untouched";
    EXPECT_TRUE(r.moved.empty());

    // Again: a free name
    r = DocumentFiles::import(src / "notes.xopp", lib);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.xopp, lib / "notes (2).xopp");
    EXPECT_EQ(backgroundOf(lib / "notes (2).xopp"), lib / "notes (2).pdf");

    // A PDF brings its .xopp along
    makePdf(src / "slides.pdf");
    makeAnnotation(src / "slides.pdf", src / "slides.xopp");
    r = DocumentFiles::import(src / "slides.pdf", lib);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(backgroundOf(lib / "slides.xopp"), lib / "slides.pdf");

    // A folder: its documents, in a new folder
    makePdf(src / "Sheets" / "sheet1.pdf");
    r = DocumentFiles::import(src / "Sheets", lib);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(lib / "Sheets" / "sheet1.pdf"));
    EXPECT_FALSE(DocumentFiles::import(lib, lib / "Sheets").ok) << "not into itself";
    EXPECT_FALSE(DocumentFiles::import(src / "missing.xopp", lib).ok);
}

TEST_F(LibraryTest, foldersCanBeCreatedRenamedAndMoved) {
    auto r = DocumentFiles::createFolder(root, "Physics");
    ASSERT_TRUE(r.ok);
    EXPECT_FALSE(DocumentFiles::createFolder(root, "Physics").ok);
    ASSERT_TRUE(DocumentFiles::createFolder(root, "Semester 1").ok);
    touch(root / "Physics" / "a.pdf");
    r = DocumentFiles::renameFolder(root / "Physics", "Physics I");
    ASSERT_TRUE(r.ok);
    ASSERT_EQ(r.moved.size(), 1u);
    r = DocumentFiles::moveFolder(root / "Physics I", root / "Semester 1");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Semester 1" / "Physics I" / "a.pdf"));
    EXPECT_FALSE(DocumentFiles::moveFolder(root / "Semester 1", root / "Semester 1" / "Physics I").ok);
    // Paths of open documents follow a moved folder
    EXPECT_EQ(DocumentFiles::remap(root / "Physics I" / "a.pdf", root / "Physics I", root / "S" / "Physics I"),
              root / "S" / "Physics I" / "a.pdf");
    EXPECT_EQ(DocumentFiles::remap(root / "Physics II" / "a.pdf", root / "Physics I", root / "X"),
              root / "Physics II" / "a.pdf");
}

TEST_F(LibraryTest, indexFindsTextAndNames) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    fs::copy_file(fixture(u8"load/pages.xopp"), root / "Page 2 notes.xopp");  // text elements "p1".."p10"
    LibraryIndex index(root, root / ".xournal_library" / "index");
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
    LibraryIndex again(root, root / ".xournal_library" / "index");
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.search("p7").size(), 1u);
    // Removed documents are dropped
    fs::remove(root / "Page 2 notes.xopp");
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_TRUE(again.search("p7").empty());
    size_t files = 0;
    for ([[maybe_unused]] const auto& e: fs::directory_iterator(root / ".xournal_library" / "index")) {
        ++files;
    }
    EXPECT_EQ(files, 1u);
}

TEST_F(LibraryTest, previewsAreRenderedOnceAndStoredInTheLibrary) {
    makePdf(root / "lecture.pdf");
    PreviewCache::setLibrary(root, root / ".xournal_library" / "previews");
    const DocumentItem item = DocumentFiles::itemOf(root / "lecture.pdf");
    const QImage img = PreviewCache::preview(item);
    EXPECT_EQ(img.width(), PreviewCache::WIDTH);
    EXPECT_GT(img.height(), 0);
    EXPECT_TRUE(fs::exists(PreviewCache::cacheFile(item)));
    EXPECT_EQ(PreviewCache::cacheFile(item).parent_path(), root / ".xournal_library" / "previews");
    EXPECT_TRUE(PreviewCache::url(item).startsWith("image://preview/"));
    PreviewCache::prune({});
    EXPECT_FALSE(fs::exists(PreviewCache::cacheFile(item)));
    PreviewCache::setLibrary({}, {});
}

TEST_F(LibraryTest, recentFilesShowExistingDocumentsOnce) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    touch(root / "gone.xopp");
    RecentFiles recent(root / "recent.json");
    recent.add(root / "lecture.pdf");
    recent.add(root / "gone.xopp");
    recent.add(root / "lecture.xopp");  // the same document
    EXPECT_EQ(recent.count(), 2);
    fs::remove(root / "gone.xopp");
    recent.refresh();
    ASSERT_EQ(recent.count(), 1);
    EXPECT_EQ(recent.data(recent.index(0), RecentFiles::PathRole).toString().toStdString(),
              (root / "lecture.xopp").string());

    ASSERT_TRUE(recent.rename(0, "renamed"));
    ASSERT_EQ(recent.count(), 1);
    EXPECT_EQ(recent.data(recent.index(0), RecentFiles::NameRole).toString(), "renamed");
    EXPECT_EQ(backgroundOf(root / "renamed.xopp"), root / "renamed.pdf");
    recent.remove(0);
    EXPECT_EQ(recent.count(), 0);
    EXPECT_TRUE(fs::exists(root / "renamed.xopp")) << "only removed from the list";
}

TEST_F(LibraryTest, modelShowsFoldersFlatListAndSearch) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    makePdf(root / "Physics" / "sheet.pdf");
    touch(root / "Physics" / "Mechanics" / "empty.xopp");
    LibraryModel model;
    std::vector<DocumentFiles::Result> changes;
    model.onFilesChanged = [&](const DocumentFiles::Result& r) { changes.push_back(r); };
    model.setLibrary(std::make_unique<Library>(root));
    ASSERT_TRUE(model.available());
    ASSERT_EQ(model.count(), 2);
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::IsFolderRole).toBool());
    EXPECT_EQ(model.data(model.index(0), LibraryModel::ItemCountRole).toInt(), 2);
    EXPECT_EQ(model.data(model.index(1), LibraryModel::NameRole).toString(), "lecture");
    EXPECT_TRUE(model.data(model.index(1), LibraryModel::HasPdfRole).toBool());

    model.setFolder("Physics");
    EXPECT_EQ(model.breadcrumbs().size(), 2);
    EXPECT_EQ(model.count(), 2);  // Mechanics, sheet
    model.setFlat(true);
    EXPECT_EQ(model.count(), 3);
    EXPECT_EQ(model.data(model.index(model.rowOf(QString::fromStdString((root / "Physics" / "sheet.pdf").string()))),
                         LibraryModel::LocationRole)
                      .toString(),
              "Physics");
    model.setFlat(false);

    // Move the lecture into Physics
    model.setFolder("");
    ASSERT_TRUE(model.moveTo(model.rowOf(QString::fromStdString((root / "lecture.xopp").string())), "Physics"));
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].moved.size(), 2u);
    EXPECT_EQ(model.count(), 1);
    EXPECT_TRUE(model.createFolder("New"));
    EXPECT_FALSE(model.createFolder("New"));
    EXPECT_EQ(model.count(), 2);
    EXPECT_FALSE(model.newDocumentPath("New").endsWith("New.xopp")) << "the folder has that name";

    // Search: once the index is ready
    model.searchIndex()->waitForDone();
    model.setSearchQuery("page 2");
    ASSERT_EQ(model.count(), 2) << "lecture and sheet (the same PDF)";
    const int lecture = model.rowOf(QString::fromStdString((root / "Physics" / "lecture.xopp").string()));
    ASSERT_GE(lecture, 0);
    EXPECT_EQ(model.data(model.index(lecture), LibraryModel::FirstHitPageRole).toInt(), 1);
    EXPECT_EQ(model.data(model.index(lecture), LibraryModel::LocationRole).toString(), "Physics");
    model.setSearchQuery("");
    EXPECT_EQ(model.count(), 2);

    // Imports run in the background
    QSignalSpy imported(&model, &LibraryModel::imported);
    makePdf(root.parent_path() / (root.filename().string() + "-outside") / "paper.pdf");
    model.importUrls({QUrl::fromLocalFile(QString::fromStdString(
                             (root.parent_path() / (root.filename().string() + "-outside") / "paper.pdf").string()))},
                     "");
    waitFor([&] { return imported.count() > 0; });
    ASSERT_EQ(imported.count(), 1);
    EXPECT_EQ(imported.first().first().toInt(), 1);
    EXPECT_GE(model.rowOf(QString::fromStdString((root / "paper.pdf").string())), 0);
    fs::remove_all(root.parent_path() / (root.filename().string() + "-outside"));
}

TEST_F(LibraryTest, severalItemsAreSelectedThenMovedOrCopiedTogether) {
    makePdf(root / "a.pdf");
    makePdf(root / "b.pdf");
    makePdf(root / "c.pdf");
    fs::create_directories(root / "Target");
    LibraryModel model;
    int changes = 0;
    model.onFilesChanged = [&](const DocumentFiles::Result&) { ++changes; };
    model.setLibrary(std::make_unique<Library>(root));
    ASSERT_EQ(model.count(), 4);  // Target, a, b, c
    auto rowOf = [&](const char* name) { return model.rowOf(QString::fromStdString((root / name).string())); };

    // Click, Ctrl+click, Shift+click
    model.select(rowOf("a.pdf"), Qt::NoModifier);
    model.select(rowOf("c.pdf"), Qt::ControlModifier);
    EXPECT_EQ(model.selectionCount(), 2);
    EXPECT_TRUE(model.data(model.index(rowOf("c.pdf")), LibraryModel::SelectedRole).toBool());
    EXPECT_FALSE(model.data(model.index(rowOf("b.pdf")), LibraryModel::SelectedRole).toBool());
    model.select(rowOf("c.pdf"), Qt::ShiftModifier);  // range from c (the last clicked) to c
    EXPECT_EQ(model.selectionCount(), 1);
    model.select(rowOf("a.pdf"), Qt::ShiftModifier);
    EXPECT_EQ(model.selectionCount(), 3) << "a to c";
    // An action on a selected row applies to the whole selection, on another row only to that one
    EXPECT_EQ(model.pathsFor(rowOf("b.pdf")).size(), 3);
    EXPECT_EQ(model.pathsFor(rowOf("Target")).size(), 1);
    EXPECT_EQ(model.documentsIn(model.selectedPaths() << model.pathsFor(rowOf("Target"))).size(), 3);

    // Dragging one of them onto a folder moves all
    ASSERT_TRUE(model.moveTo(rowOf("b.pdf"), "Target"));
    EXPECT_EQ(changes, 3);
    EXPECT_EQ(model.selectionCount(), 0);
    EXPECT_EQ(model.count(), 1);
    for (const char* f: {"a.pdf", "b.pdf", "c.pdf"}) {
        EXPECT_TRUE(fs::exists(root / "Target" / f)) << f;
    }

    // Copies (in the background)
    model.setFolder("Target");
    model.selectAll();
    QSignalSpy imported(&model, &LibraryModel::imported);
    ASSERT_TRUE(model.transfer(model.selectedPaths(), "", true));
    waitFor([&] { return imported.count() > 0; });
    for (const char* f: {"a.pdf", "b.pdf", "c.pdf"}) {
        EXPECT_TRUE(fs::exists(root / f)) << f;
        EXPECT_TRUE(fs::exists(root / "Target" / f)) << "copied, not moved: " << f;
    }
    // A folder is moved with its contents; not into itself
    model.setFolder("");
    EXPECT_FALSE(model.transfer({QString::fromStdString((root / "Target").string())}, "Target", false));
    ASSERT_TRUE(DocumentFiles::createFolder(root, "Archive").ok);
    ASSERT_TRUE(model.transfer({QString::fromStdString((root / "Target").string())}, "Archive", false));
    EXPECT_TRUE(fs::exists(root / "Archive" / "Target" / "a.pdf"));
}

TEST_F(LibraryTest, recentFilesSelectionAndRemoval) {
    makePdf(root / "a.pdf");
    makePdf(root / "b.pdf");
    makePdf(root / "c.pdf");
    RecentFiles recent(root / "recent.json");
    for (const char* f: {"a.pdf", "b.pdf", "c.pdf"}) {
        recent.add(root / f);
    }
    ASSERT_EQ(recent.count(), 3);  // c, b, a
    recent.select(0, Qt::NoModifier);
    recent.select(2, Qt::ShiftModifier);
    EXPECT_EQ(recent.selectionCount(), 3);
    recent.toggleSelected(1);
    EXPECT_EQ(recent.selectedPaths().size(), 2);
    recent.removePaths(recent.selectedPaths());
    ASSERT_EQ(recent.count(), 1);
    EXPECT_EQ(recent.selectionCount(), 0);
    EXPECT_EQ(recent.data(recent.index(0), RecentFiles::NameRole).toString(), "b");
    EXPECT_TRUE(fs::exists(root / "a.pdf")) << "only removed from the list";
}

TEST_F(LibraryTest, importCopiesAWholeFolderTree) {
    const fs::path lib = root / "lib", src = root / "src" / "Course";
    fs::create_directories(lib);
    makePdf(src / "Week 1" / "slides.pdf");
    makePdf(src / "Week 2" / "Exercises" / "sheet.pdf");
    makeAnnotation(src / "Week 2" / "Exercises" / "sheet.pdf", src / "Week 2" / "Exercises" / "sheet.xopp");
    fs::create_directories(src / "Week 3");  // empty, kept
    touch(src / "Week 1" / "readme.txt");    // not a document
    makePdf(src / ".git" / "hidden.pdf");    // hidden folders stay behind

    const auto r = DocumentFiles::import(src, lib);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.documents, 2);
    EXPECT_EQ(r.folder, lib / "Course");
    EXPECT_TRUE(fs::exists(lib / "Course" / "Week 1" / "slides.pdf"));
    EXPECT_EQ(backgroundOf(lib / "Course" / "Week 2" / "Exercises" / "sheet.xopp"),
              lib / "Course" / "Week 2" / "Exercises" / "sheet.pdf");
    EXPECT_TRUE(fs::is_directory(lib / "Course" / "Week 3"));
    EXPECT_FALSE(fs::exists(lib / "Course" / "Week 1" / "readme.txt"));
    EXPECT_FALSE(fs::exists(lib / "Course" / ".git"));
    EXPECT_TRUE(fs::exists(src / "Week 2" / "Exercises" / "sheet.xopp")) << "a copy: the original stays";

    // Again: next to the first copy
    EXPECT_EQ(DocumentFiles::import(src, lib).folder, lib / "Course (2)");
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

// The title page and the page a document was left at are kept beside it: in the metadata of its library, by its
// path there; they follow it when it is renamed or moved. The preview shows the title page.
TEST_F(LibraryTest, titleAndLastPagesAreKeptInTheLibrary) {
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
    QFile stored(QString::fromStdString((root / DocumentFiles::META_DIR / "pages.json").string()));
    ASSERT_TRUE(stored.open(QIODevice::ReadOnly)) << "in the library's metadata";
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

    // The preview shows the title page, under a name of its own; the first page keeps the name it always had
    const DocumentItem lecture = DocumentFiles::itemOf(root / "lecture.pdf");
    const fs::path firstName = PreviewCache::cacheFile(lecture);
    const QImage first = PreviewCache::preview(lecture);
    ASSERT_FALSE(first.isNull());
    DocumentPlaces::setTitlePage(lecture.main(), 1);
    EXPECT_NE(PreviewCache::cacheFile(lecture), firstName);
    const QImage second = PreviewCache::preview(lecture);
    ASSERT_FALSE(second.isNull());
    EXPECT_NE(first, second) << "another page";
    DocumentPlaces::setTitlePage(lecture.main(), 0);
    EXPECT_EQ(PreviewCache::cacheFile(lecture), firstName);
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

namespace {
/// A one-page PDF with `word` on it.
void makeWordPdf(const fs::path& p, const char* word) {
    fs::create_directories(p.parent_path());
    cairo_surface_t* s = cairo_pdf_surface_create(p.string().c_str(), 595, 842);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 24);
    cairo_move_to(cr, 72, 100);
    cairo_show_text(cr, word);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
/// Add a text element to a page of a .xopp and save it.
void addText(const fs::path& xopp, size_t page, const char* text) {
    auto loaded = DocumentSession::loadFile(xopp);
    ASSERT_TRUE(loaded.document);
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->move(100, 100);
    loaded.document->getPage(page)->getSelectedLayer()->addElement(std::move(t));
    ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, xopp).ok);
}
}  // namespace

TEST_F(LibraryTest, onlyTheXoppIsReadAgainWhenAnnotationsChange) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    const fs::path dir = root / ".xournal_library" / "index";
    LibraryIndex index(root, dir);
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
    LibraryIndex again(root, dir);
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
    EXPECT_EQ(index->documentsRead(), 4) << "only the .xopp written again by the rename and the move (new PDF path); "
                                            "the folder move reads nothing";
    EXPECT_EQ(index->search("xournal").size(), 2u);

    size_t files = 0;
    for ([[maybe_unused]] const auto& e: fs::directory_iterator(root / ".xournal_library" / "index")) {
        ++files;
    }
    EXPECT_EQ(files, 2u) << "the index files moved along";

    // Renamed by another program: the PDF text is taken over (same file: size and time), only the .xopp is read
    const int pdfPages = index->pdfPagesRead();
    fs::rename(root / "Semester" / "Archive" / "old.pdf", root / "Semester" / "Archive" / "older.pdf");
    model.refresh();
    index->waitForDone();
    EXPECT_EQ(index->pdfPagesRead(), pdfPages);
    EXPECT_EQ(index->search("xournal").size(), 2u);
    EXPECT_EQ(model.searchIndex()->pageCount(root / "Semester" / "Archive" / "older.pdf"), 2);
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

    LibraryIndex index(root, root / ".xournal_library" / "index");
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
    const fs::path dir = root / ".xournal_library" / "index";
    QElapsedTimer t;
    {
        LibraryIndex first(root, dir);
        t.start();
        first.update(DocumentFiles::scanRecursive(root));
        first.waitForDone();
        std::cout << count << " documents, first indexing: " << t.elapsed() << " ms\n";
    }
    // Starting again: the stored index
    LibraryIndex again(root, dir);
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
    LibraryIndex index(root, root / ".xournal_library" / "index");
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

TEST_F(LibraryTest, documentsAndFoldersMoveOrCopyToAnotherLibrary) {
    const fs::path a = root / "A", b = root / "B";
    makePdf(a / "lecture.pdf");
    makeAnnotation(a / "lecture.pdf", a / "lecture.xopp");
    makePdf(a / "Course" / "Week 1" / "sheet.pdf");
    fs::create_directories(b / "Inbox");
    LibraryModel model;
    int changes = 0;
    model.onFilesChanged = [&](const DocumentFiles::Result&) { ++changes; };
    model.setLibrary(std::make_unique<Library>(a));

    // The folders of the other library, as targets
    const QVariantList targets = model.foldersOf(QString::fromStdString(b.string()));
    ASSERT_EQ(targets.size(), 2);
    EXPECT_EQ(targets[0].toMap().value("name").toString(), "B");
    EXPECT_EQ(targets[1].toMap().value("path").toString().toStdString(), (b / "Inbox").string());

    // Move the pair: both files, the reference follows; gone from this library
    const QString lecture = QString::fromStdString((a / "lecture.xopp").string());
    ASSERT_TRUE(model.transferTo({lecture}, QString::fromStdString((b / "Inbox").string()), false));
    EXPECT_EQ(backgroundOf(b / "Inbox" / "lecture.xopp"), b / "Inbox" / "lecture.pdf");
    EXPECT_FALSE(fs::exists(a / "lecture.pdf"));
    EXPECT_EQ(changes, 1) << "open tabs follow";
    EXPECT_LT(model.rowOf(lecture), 0);

    // Copy a folder with its subfolders
    QSignalSpy imported(&model, &LibraryModel::imported);
    ASSERT_TRUE(model.transferTo({QString::fromStdString((a / "Course").string())}, QString::fromStdString(b.string()), true));
    waitFor([&] { return imported.count() > 0; });
    EXPECT_TRUE(fs::exists(b / "Course" / "Week 1" / "sheet.pdf"));
    EXPECT_TRUE(fs::exists(a / "Course" / "Week 1" / "sheet.pdf"));
}

TEST_F(LibraryTest, theDownloadsFolderIsATemporaryLibrary) {
    // The Downloads folder of the XDG user dirs (the tests have their own config folder)
    const fs::path downloads = root / "Downloads";
    fs::create_directories(downloads / "papers");
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    ASSERT_FALSE(config.isEmpty());
    fs::create_directories(config.toStdString());
    {
        QFile dirs(config + "/user-dirs.dirs");
        ASSERT_TRUE(dirs.open(QIODevice::WriteOnly));
        dirs.write(("XDG_DOWNLOAD_DIR=\"" + downloads.string() + "\"\n").c_str());
    }
    EXPECT_EQ(Library(Library::downloadsFolder()).root(), Library(downloads).root());
    EXPECT_TRUE(Library(downloads).isTemporary());
    EXPECT_TRUE(Library(downloads / "papers").isTemporary());
    EXPECT_FALSE(Library(root / "lib").isTemporary());
    // Its index and previews live in the folder, like every library's (fast when it is opened again)
    EXPECT_EQ(Library(downloads).metaDir(), downloads / ".xournal_library");
    EXPECT_TRUE(fs::exists(downloads / ".xournal_library"));
    LibraryModel model;
    EXPECT_TRUE(model.isTemporaryFolder(QString::fromStdString((downloads / "papers").string())));
    QFile::remove(config + "/user-dirs.dirs");
}

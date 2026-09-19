/*
 * xournal-qt: the library (documents as files, pairs of .xopp and PDF, search index, previews, recent files).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
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

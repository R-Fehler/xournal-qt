/*
 * xournal-qt: the library (documents as files, pairs of .xopp and PDF, search index, previews, recent files).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <iostream>
#include <array>
#include <map>
#include <random>

#include <fcntl.h>
#include <unistd.h>

#include <QCoreApplication>
#include <QCborArray>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
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
#include "shell/LibraryCache.h"
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

std::vector<std::string> names(const std::vector<DocumentItem>& items) {
    std::vector<std::string> n;
    for (const auto& i: items) {
        n.push_back(i.name());
    }
    return n;
}

/// The keys of a pack in a folder's cache (sorted).
QStringList packKeys(const fs::path& folder, const QString& pack, int format = LibraryIndex::FORMAT) {
    QStringList keys;
    if (auto entries = Packs::read(folder / DocumentFiles::META_DIR, pack, format)) {
        for (auto it = entries->cbegin(); it != entries->cend(); ++it) {
            keys << it.key().toString();
        }
    }
    keys.sort();
    return keys;
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
    LibraryIndex index(root);
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
    index.flush();
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.search("p7").size(), 1u);
    EXPECT_EQ(again.documentsRead(), 0);
    // Removed documents are dropped
    fs::remove(root / "Page 2 notes.xopp");
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_TRUE(again.search("p7").empty());
    again.flush();
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"lecture.xopp"});
}

TEST_F(LibraryTest, previewsAreRenderedOnceAndStoredInTheLibrary) {
    makePdf(root / "lecture.pdf");
    PreviewCache::setLibrary(CacheLocation(root));
    const DocumentItem item = DocumentFiles::itemOf(root / "lecture.pdf");
    EXPECT_TRUE(PreviewCache::stored(item).isNull()) << "not made yet";
    const QImage img = PreviewCache::preview(item);
    EXPECT_EQ(img.width(), PreviewCache::WIDTH);
    EXPECT_GT(img.height(), 0);
    EXPECT_TRUE(PreviewCache::url(item).startsWith("image://preview/"));
    PreviewCache::flush();
    EXPECT_EQ(packKeys(root, PreviewCache::PACK, PreviewCache::FORMAT), QStringList{"lecture.pdf"})
            << "in the pack of its folder";
    // Stored: read back (not drawn again)
    PreviewCache::setLibrary(CacheLocation(root));
    EXPECT_EQ(PreviewCache::stored(item).size(), img.size());
    PreviewCache::prune({});
    PreviewCache::flush();
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "previews.pack"));
    PreviewCache::setLibrary({});
}

TEST_F(LibraryTest, previewsOfAFolderAreOnePackReadWhenFirstWanted) {
    makePdf(root / "Physics" / "a.pdf");
    makePdf(root / "Physics" / "b.pdf");
    makeWordPdf(root / "Physics" / "c.pdf", "zebra");
    makePdf(root / "top.pdf");
    PreviewCache::setLibrary(CacheLocation(root));
    PreviewCache::setWriteDelays(50, 500);
    for (const auto& item: DocumentFiles::scanRecursive(root)) {
        ASSERT_FALSE(PreviewCache::preview(item).isNull());
    }
    waitFor([&] { return fs::exists(root / "Physics" / DocumentFiles::META_DIR / "previews.pack"); });
    PreviewCache::flush();
    EXPECT_EQ(packKeys(root / "Physics", PreviewCache::PACK, PreviewCache::FORMAT),
              (QStringList{"a.pdf", "b.pdf", "c.pdf"}));
    EXPECT_EQ(packKeys(root, PreviewCache::PACK, PreviewCache::FORMAT), QStringList{"top.pdf"});

    // Read once per folder
    PreviewCache::setLibrary(CacheLocation(root));
    const int reads = PreviewCache::packsRead();
    const QImage a = PreviewCache::stored(DocumentFiles::itemOf(root / "Physics" / "a.pdf"));
    const QImage c = PreviewCache::stored(DocumentFiles::itemOf(root / "Physics" / "c.pdf"));
    EXPECT_FALSE(a.isNull());
    EXPECT_NE(a, c);
    EXPECT_EQ(PreviewCache::packsRead(), reads + 1);

    // A changed document: its new preview takes the place of the old one
    fs::remove(root / "Physics" / "a.pdf");
    makeWordPdf(root / "Physics" / "a.pdf", "giraffe");
    const DocumentItem changed = DocumentFiles::itemOf(root / "Physics" / "a.pdf");
    EXPECT_TRUE(PreviewCache::stored(changed).isNull()) << "the stored one is of the old file";
    EXPECT_NE(PreviewCache::preview(changed), a);
    // Renamed in the app: the preview goes along
    fs::rename(root / "Physics" / "b.pdf", root / "b.pdf");
    PreviewCache::moved({{root / "Physics" / "b.pdf", root / "b.pdf"}});
    EXPECT_FALSE(PreviewCache::stored(DocumentFiles::itemOf(root / "b.pdf")).isNull());
    PreviewCache::flush();
    EXPECT_EQ(packKeys(root / "Physics", PreviewCache::PACK, PreviewCache::FORMAT), (QStringList{"a.pdf", "c.pdf"}));
    EXPECT_EQ(packKeys(root, PreviewCache::PACK, PreviewCache::FORMAT), (QStringList{"b.pdf", "top.pdf"}));
    PreviewCache::setWriteDelays(WriteScheduler::QUIET_MS, WriteScheduler::MAX_DELAY_MS);
    PreviewCache::setLibrary({});
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
    const QString firstUrl = PreviewCache::url(lecture);
    const QImage first = PreviewCache::preview(lecture);
    ASSERT_FALSE(first.isNull());
    DocumentPlaces::setTitlePage(lecture.main(), 1);
    EXPECT_NE(PreviewCache::url(lecture), firstUrl);
    EXPECT_TRUE(PreviewCache::stored(lecture).isNull()) << "the stored one shows another page";
    const QImage second = PreviewCache::preview(lecture);
    ASSERT_FALSE(second.isNull());
    EXPECT_NE(first, second) << "another page";
    DocumentPlaces::setTitlePage(lecture.main(), 0);
    EXPECT_EQ(PreviewCache::url(lecture), firstUrl);
}

// Reading positions are not cache: removing the cache folders keeps them. Those kept in the cache folder before are
// taken over.
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

    // A library whose positions were in its cache folder: taken over
    const fs::path other = root / "Other";
    makePdf(other / "old.pdf");
    touch(other / DocumentFiles::META_DIR / "pages.json");
    {
        QFile old(QString::fromStdString((other / DocumentFiles::META_DIR / "pages.json").string()));
        ASSERT_TRUE(old.open(QIODevice::WriteOnly));
        old.write(R"({"old.pdf":{"last":1,"title":1}})");
    }
    model.setLibrary(std::make_unique<Library>(other));
    EXPECT_EQ(DocumentPlaces::lastPage(other / "old.pdf"), 1);
    EXPECT_EQ(DocumentPlaces::titlePage(other / "old.pdf"), 1);
    EXPECT_TRUE(fs::exists(Library(other).configDir() / "pages.json"));
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


TEST_F(LibraryTest, onlyTheXoppIsReadAgainWhenAnnotationsChange) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    LibraryIndex index(root);
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
    index.flush();
    LibraryIndex again(root);
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

    index->flush();
    EXPECT_EQ(packKeys(root / "Semester" / "Archive", LibraryIndex::NOTES_PACK),
              (QStringList{"Week 1.xopp", "old.pdf"}))
            << "the entries moved along";
    EXPECT_TRUE(packKeys(root / "Semester" / "Archive", LibraryIndex::PDF_TEXT_PACK).contains("Week 1.xopp"));
    EXPECT_FALSE(fs::exists(root / DocumentFiles::META_DIR / "notes.pack")) << "no documents left at the top";

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

    LibraryIndex index(root);
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
    waitFor([] { return false; }, 150);
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
    ASSERT_FALSE(PreviewCache::preview(sheet).isNull());
    PreviewCache::flush();
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
    EXPECT_FALSE(PreviewCache::stored(sheet).isNull());
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
    ASSERT_FALSE(PreviewCache::preview(DocumentFiles::itemOf(root / "lecture.pdf")).isNull());
    PreviewCache::flush();
    waitFor([] { return false; }, 100);
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

// --- the cache of the layout before the packs ---

namespace {
/// An index entry as it was stored before the packs ("index/<hash>.json", format 3, paths relative to the
/// library), with made-up PDF text per page.
void writeOldEntry(const fs::path& root, const DocumentItem& item, const QStringList& pdfText, int number) {
    const auto rel = [&](const fs::path& p) { return QString::fromStdString(p.lexically_relative(root).string()); };
    QJsonObject text;
    QJsonArray pages;
    for (int i = 0; i < pdfText.size(); ++i) {
        text[QString::number(i)] = pdfText[i];
        pages.append(QJsonObject{{"pdf", i}, {"text", ""}, {"aspect", 1.414}});
    }
    const QJsonObject json{{"format", 3},
                           {"file", rel(item.main())},
                           {"name", QString::fromStdString(item.name())},
                           {"xoppStamp", item.xopp.empty() ? QString() : fileStamp(item.xopp)},
                           {"pdf", rel(item.pdf)},
                           {"pdfStamp", fileStamp(item.pdf)},
                           {"pdfText", text},
                           {"pages", pages}};
    const fs::path file = root / DocumentFiles::META_DIR / "index" / (std::to_string(1000000 + number) + "abcdef00.json");
    fs::create_directories(file.parent_path());
    QFile f(QString::fromStdString(file.string()));
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
}
}  // namespace

TEST_F(LibraryTest, anOldCacheIsConvertedWithoutReadingAnythingAgain) {
    makePdf(root / "lecture.pdf");
    makeAnnotation(root / "lecture.pdf", root / "lecture.xopp");
    makePdf(root / "Physics" / "sheet.pdf");
    makePdf(root / "Physics" / "changed.pdf");
    const fs::path old = root / DocumentFiles::META_DIR;
    // The old cache: index entries (one of a document changed since, one of a document that is gone), previews,
    // reading positions, and a file that is not the app's
    const DocumentItem lecture = DocumentFiles::itemOf(root / "lecture.xopp");
    const DocumentItem sheet = DocumentFiles::itemOf(root / "Physics" / "sheet.pdf");
    const DocumentItem changed = DocumentFiles::itemOf(root / "Physics" / "changed.pdf");
    writeOldEntry(root, lecture, {"oldtext alpha", "oldtext beta"}, 1);
    writeOldEntry(root, sheet, {"oldtext gamma", ""}, 2);
    writeOldEntry(root, changed, {"oldtext delta", ""}, 3);
    touch(root / "gone.pdf");
    writeOldEntry(root, DocumentItem{{}, root / "gone.pdf"}, {"oldtext epsilon"}, 4);
    fs::remove(root / "gone.pdf");
    fs::resize_file(root / "Physics" / "changed.pdf", fs::file_size(root / "Physics" / "changed.pdf") + 1);
    QImage red(PreviewCache::WIDTH, 20, QImage::Format_RGB32);
    red.fill(Qt::red);
    fs::create_directories(old / "previews");
    ASSERT_TRUE(red.save(QString::fromStdString((old / "previews" / PreviewCache::outsideFile(sheet).filename()).string())));
    {
        QFile pages(QString::fromStdString((old / "pages.json").string()));
        ASSERT_TRUE(pages.open(QIODevice::WriteOnly));
        pages.write(R"({"Physics/sheet.pdf":{"last":1}})");
    }
    touch(old / "mine.txt");

    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    LibraryIndex* index = model.searchIndex();
    index->waitForDone();
    EXPECT_EQ(index->oldLayoutsConverted(), 1);
    EXPECT_EQ(index->documentsRead(), 1) << "only the document changed since";
    EXPECT_EQ(index->pdfPagesRead(), 2);
    auto hits = index->search("oldtext");
    ASSERT_EQ(hits.size(), 2u) << "the converted text (not read from the PDF)";
    EXPECT_EQ(index->search("oldtext beta").size(), 1u);
    EXPECT_EQ(PreviewCache::stored(sheet), red) << "the old preview";
    EXPECT_EQ(DocumentPlaces::lastPage(root / "Physics" / "sheet.pdf"), 1);

    // The old files are gone, nothing else
    EXPECT_FALSE(fs::exists(old / "index"));
    EXPECT_FALSE(fs::exists(old / "previews"));
    EXPECT_FALSE(fs::exists(old / "pages.json"));
    EXPECT_TRUE(fs::exists(old / "mine.txt"));
    EXPECT_EQ(packKeys(root, LibraryIndex::NOTES_PACK), QStringList{"lecture.xopp"});
    EXPECT_EQ(packKeys(root / "Physics", LibraryIndex::NOTES_PACK), (QStringList{"changed.pdf", "sheet.pdf"}));
    EXPECT_EQ(packKeys(root / "Physics", PreviewCache::PACK, PreviewCache::FORMAT), QStringList{"sheet.pdf"});

    // Opened again: nothing to convert, nothing to read
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    EXPECT_EQ(model.searchIndex()->oldLayoutsConverted(), 0);
    EXPECT_EQ(model.searchIndex()->documentsRead(), 0);
    EXPECT_EQ(model.searchIndex()->search("oldtext").size(), 2u);
    model.setLibrary(nullptr);
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
    const fs::path dir = root / DocumentFiles::META_DIR;
    QElapsedTimer t;
    {
        LibraryIndex first(root);
        t.start();
        first.update(DocumentFiles::scanRecursive(root));
        first.waitForDone();
        std::cout << count << " documents, first indexing: " << t.elapsed() << " ms\n";
    }
    // Starting again: the stored index
    LibraryIndex again(root);
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
    LibraryIndex index(root);
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
    // Its index and previews live in its folders, like every library's (fast when it is opened again)
    EXPECT_EQ(CacheLocation(Library(downloads).root()).dirOf(downloads / "papers"),
              downloads / "papers" / ".xournal_library");
    LibraryModel model;
    EXPECT_TRUE(model.isTemporaryFolder(QString::fromStdString((downloads / "papers").string())));
    QFile::remove(config + "/user-dirs.dirs");
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
    auto flushPreviews = [&] { PreviewCache::flush(); };
    PreviewCache::setLibrary(CacheLocation(root));
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
        PreviewCache::preview(item);
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
    PreviewCache::preview(DocumentFiles::itemOf(edited));
    flushPreviews();
    auto [allBytes, allFiles] = written(before, snapshot(cacheFiles(root, appCache)));
    std::cout << "with its new preview: " << allBytes / 1024.0 << " KiB in " << allFiles << " files\n";
    EXPECT_EQ(index->search("unicorn").size(), 1u);
    index.reset();

    // The same cache in the layout before the packs (as earlier builds wrote it: a JSON file per document, a PNG
    // per preview, all in the root's cache folder), made from the packs
    std::map<fs::path, std::array<QCborMap, 3>> packs;  // notes, PDF text, previews of each folder
    int n = 0;
    uintmax_t bookJson = 0;
    for (const auto& item: DocumentFiles::scanRecursive(root)) {
        const fs::path folder = item.folder();
        if (!packs.count(folder)) {
            const fs::path dir = folder / DocumentFiles::META_DIR;
            packs[folder] = {Packs::read(dir, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT).value_or(QCborMap()),
                             Packs::read(dir, LibraryIndex::PDF_TEXT_PACK, LibraryIndex::FORMAT).value_or(QCborMap()),
                             Packs::read(dir, PreviewCache::PACK, PreviewCache::FORMAT).value_or(QCborMap())};
        }
        const auto& [notes, texts, previews] = packs[folder];
        const QString name = QString::fromStdString(item.main().filename().string());
        const QCborMap e = notes.value(name).toMap();
        const QCborMap pdfPages = texts.value(name).toMap().value(QStringLiteral("pages")).toMap();
        QJsonObject pdfText;
        for (auto it = pdfPages.cbegin(); it != pdfPages.cend(); ++it) {
            pdfText[QString::number(it.key().toInteger())] = it.value().toString();
        }
        QJsonArray pages;
        const QCborArray pdfNr = e.value(QStringLiteral("pdfPages")).toArray();
        for (qsizetype i = 0; i < pdfNr.size(); ++i) {
            pages.append(QJsonObject{{"pdf", pdfNr[i].toInteger()},
                                     {"text", e.value(QStringLiteral("text")).toArray()[i].toString()},
                                     {"aspect", e.value(QStringLiteral("aspects")).toArray()[i].toDouble()}});
        }
        const fs::path pdf = e.value(QStringLiteral("pdf")).toString().isEmpty()
                                     ? fs::path()
                                     : (folder / e.value(QStringLiteral("pdf")).toString().toStdString()).lexically_normal();
        const QJsonObject json{{"format", 3},
                               {"file", QString::fromStdString(item.main().lexically_relative(root).string())},
                               {"name", e.value(QStringLiteral("name")).toString()},
                               {"xoppStamp", e.value(QStringLiteral("xopp")).toString()},
                               {"pdf", QString::fromStdString(pdf.empty() ? "" : pdf.lexically_relative(root).string())},
                               {"pdfStamp", e.value(QStringLiteral("pdfStamp")).toString()},
                               {"pdfText", pdfText},
                               {"pages", pages}};
        const fs::path file = meta / "index" / (std::to_string(++n) + ".json");
        fs::create_directories(file.parent_path());
        QFile out(QString::fromStdString(file.string()));
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(QJsonDocument(json).toJson(QJsonDocument::Compact));
        out.close();
        if (item.main().filename() == "book.pdf") {
            bookJson = fs::file_size(file);
        }
        if (const QByteArray png = previews.value(name).toMap().value(QStringLiteral("png")).toByteArray(); !png.isEmpty()) {
            fs::create_directories(meta / "previews");
            QFile p(QString::fromStdString((meta / "previews" / PreviewCache::outsideFile(item).filename()).string()));
            ASSERT_TRUE(p.open(QIODevice::WriteOnly));
            p.write(png);
        }
    }
    uintmax_t newBytes = 0, oldBytes = 0, bookPack = 0;
    int newFiles = 0, oldFiles = 0;
    for (const auto& f: cacheFiles(root)) {
        const bool old = f.parent_path().filename() == "index" || f.parent_path().filename() == "previews";
        (old ? oldBytes : newBytes) += fs::file_size(f);
        ++(old ? oldFiles : newFiles);
        if (f.parent_path() == meta && f.filename().string().rfind("pdf-text-", 0) == 0) {
            bookPack = fs::file_size(f);  // (the only entry with a file of its own)
        }
    }
    std::cout << "the same cache: old layout " << oldBytes / 1024 << " KiB in " << oldFiles << " files, packs "
              << newBytes / 1024 << " KiB in " << newFiles << " files\n";
    std::cout << "PDF text of the 250-page PDF: JSON " << bookJson / 1024 << " KiB, CBOR + zlib " << bookPack / 1024
              << " KiB\n";
    // Only the old layout left: converted when the library is opened
    for (const auto& f: cacheFiles(root)) {
        if (f.extension() == ".pack") {
            fs::remove(f);
        }
    }
    PreviewCache::setLibrary({});
    dropFromPageCache(cacheFiles(root));
    t.restart();
    {
        LibraryModel model;
        model.setLibrary(std::make_unique<Library>(root));
        model.searchIndex()->waitForDone();
        std::cout << "converting the old layout and opening: " << t.elapsed() << " ms, documents read: "
                  << model.searchIndex()->documentsRead() << "\n";
        EXPECT_EQ(model.searchIndex()->oldLayoutsConverted(), 1);
        EXPECT_EQ(model.searchIndex()->documentsRead(), 0);
        model.setLibrary(nullptr);
    }
    EXPECT_FALSE(fs::exists(meta / "index"));
    DocumentPlaces::setLibrary({}, {});
}

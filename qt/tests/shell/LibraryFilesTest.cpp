/*
 * xournal-qt: Markdown files and images in the library (listing, pairs, file operations, previews, index, search,
 * opening), and the files that belong to a .xopp (attached background images).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>

#include <QColor>
#include <QImage>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"

using namespace xqt;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

std::vector<std::string> names(const std::vector<DocumentItem>& items) {
    std::vector<std::string> n;
    for (const auto& i: items) {
        n.push_back(i.main().filename().string());
    }
    return n;
}

QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

/// An image file (the format from the extension), `w` x `h`, of one color.
void makeImage(const fs::path& p, int w, int h, const QColor& color = QColor(40, 120, 200)) {
    fs::create_directories(p.parent_path());
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(color);
    ASSERT_TRUE(img.save(QString::fromStdString(p.string())));
}

/// A .xopp with one page whose background is an image stored with it ("<xopp>.bg_1.png", as upstream attaches
/// images).
void makeXoppWithAttachedImage(const fs::path& xopp, const fs::path& png) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(400, 300);
    BackgroundImage img;
    GError* error = nullptr;
    img.loadFile(png, &error);
    ASSERT_EQ(error, nullptr);
    img.setAttach(true);
    page->setBackgroundImage(img);
    page->setBackgroundType(PageType(PageTypeFormat::Image));
    doc.addPage(page);
    ASSERT_TRUE(DocumentSession::writeDocument(doc, xopp).ok);
}

/// A .xopp with one blank page.
void makeNotes(const fs::path& xopp) {
    DocumentHandler handler;
    Document doc(&handler);
    doc.addPage(std::make_shared<XojPage>(400, 300));
    fs::create_directories(xopp.parent_path());
    ASSERT_TRUE(DocumentSession::writeDocument(doc, xopp).ok);
}

/// A .xopp annotating an image: one page with the image as background, by its path (as upstream refers to images).
void makeImageAnnotation(const fs::path& image, const fs::path& xopp) {
    DocumentHandler handler;
    Document doc(&handler);
    auto page = std::make_shared<XojPage>(400, 300);
    BackgroundImage img;
    GError* error = nullptr;
    img.loadFile(image, &error);
    ASSERT_EQ(error, nullptr);
    page->setBackgroundImage(img);
    page->setBackgroundType(PageType(PageTypeFormat::Image));
    doc.addPage(page);
    ASSERT_TRUE(DocumentSession::writeDocument(doc, xopp).ok);
}

/// The image the first page of a .xopp shows (empty if none, or it could not be read)
fs::path backgroundImageOf(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    if (!loaded.document || loaded.document->getPageCount() == 0) {
        return {};
    }
    PageRef page = loaded.document->getPage(0);
    if (!page->getBackgroundType().isImagePage() || !page->getBackgroundImage().getPixbuf()) {
        return {};
    }
    return page->getBackgroundImage().getFilepath();
}

/// Whether the .xopp loads with an image as the background of its first page.
bool hasBackgroundImage(const fs::path& xopp) {
    auto loaded = DocumentSession::loadFile(xopp);
    if (!loaded.document || loaded.document->getPageCount() == 0) {
        return false;
    }
    PageRef page = loaded.document->getPage(0);
    return page->getBackgroundType().isImagePage() && page->getBackgroundImage().getPixbuf() != nullptr;
}

class LibraryFilesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(LibraryFilesTest, attachedBackgroundImagesTravelWithTheirXopp) {
    makeImage(root / "src.png", 40, 30);
    makeXoppWithAttachedImage(root / "board.xopp", root / "src.png");
    ASSERT_TRUE(fs::exists(root / "board.xopp.bg_1.png"));
    ASSERT_TRUE(hasBackgroundImage(root / "board.xopp"));

    // Renamed: the image goes along
    auto r = DocumentFiles::rename(DocumentFiles::itemOf(root / "board.xopp"), "Whiteboard");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Whiteboard.xopp.bg_1.png"));
    EXPECT_FALSE(fs::exists(root / "board.xopp.bg_1.png"));
    EXPECT_TRUE(hasBackgroundImage(root / "Whiteboard.xopp"));

    // Moved and copied
    fs::create_directories(root / "Physics");
    r = DocumentFiles::move(r.item, root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(hasBackgroundImage(root / "Physics" / "Whiteboard.xopp"));
    EXPECT_FALSE(fs::exists(root / "Whiteboard.xopp.bg_1.png"));
    r = DocumentFiles::import(root / "Physics" / "Whiteboard.xopp", root);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(hasBackgroundImage(root / "Whiteboard.xopp"));
    EXPECT_TRUE(hasBackgroundImage(root / "Physics" / "Whiteboard.xopp"));

    // Its files: trashed together
    const auto files = DocumentFiles::filesOf(DocumentFiles::itemOf(root / "Whiteboard.xopp"));
    EXPECT_NE(std::find(files.begin(), files.end(), root / "Whiteboard.xopp.bg_1.png"), files.end());
}

TEST_F(LibraryFilesTest, markdownFilesAndImagesAreDocuments) {
    writeFile(root / "notes.md", "# Notes\n");
    makeImage(root / "board.png", 40, 30);
    makeImage(root / "photo.jpg", 40, 30);
    makeImageAnnotation(root / "photo.jpg", root / "photo.xopp");  // one document
    makeImage(root / "scan.JPG", 40, 30);
    makeImage(root / "lecture.png", 40, 30);  // a picture of the lecture: its own document
    writeFile(root / "lecture.pdf", "%PDF");
    writeFile(root / "lecture.xopp", "x");   // belongs to the PDF
    makeImage(root / "src.png", 20, 20);
    makeXoppWithAttachedImage(root / "board2.xopp", root / "src.png");  // board2.xopp.bg_1.png: part of it
    writeFile(root / ".hidden.md", "x");
    writeFile(root / "readme.txt", "x");

    const auto l = DocumentFiles::scan(root);
    EXPECT_EQ(names(l.items), (std::vector<std::string>{"board.png", "board2.xopp", "lecture.xopp", "lecture.png",
                                                        "notes.md", "photo.xopp", "scan.JPG", "src.png"}));
    for (const auto& item: l.items) {
        // The same item from each of its files
        for (const fs::path& f: {item.xopp, item.pdf, item.md, item.image}) {
            if (!f.empty()) {
                EXPECT_EQ(DocumentFiles::itemOf(f), item) << f;
            }
        }
        const std::string file = item.main().filename().string();
        if (file == "photo.xopp") {
            EXPECT_EQ(item.image, root / "photo.jpg");
            EXPECT_EQ(item.kind(), DocumentItem::Kind::Image);
            EXPECT_EQ(item.name(), "photo");
        } else if (file == "notes.md") {
            EXPECT_EQ(item.kind(), DocumentItem::Kind::Markdown);
            EXPECT_EQ(item.name(), "notes");
        } else if (file == "lecture.xopp") {
            EXPECT_EQ(item.pdf, root / "lecture.pdf");
            EXPECT_TRUE(item.image.empty());
            EXPECT_EQ(item.kind(), DocumentItem::Kind::Pdf);
        } else if (file == "board2.xopp") {
            EXPECT_EQ(item.kind(), DocumentItem::Kind::Notes);
        } else {
            EXPECT_EQ(item.kind(), DocumentItem::Kind::Image) << file;
            EXPECT_TRUE(item.xopp.empty()) << file;
        }
    }
    EXPECT_FALSE(DocumentFiles::itemOf(root / "board2.xopp.bg_1.png").valid());
    EXPECT_FALSE(DocumentFiles::itemOf(root / "readme.txt").valid());
    // Reading positions: a pair is known by its image (as by its PDF), so it keeps them when the .xopp comes
    EXPECT_EQ(DocumentPlaces::keyOf(DocumentFiles::itemOf(root / "photo.xopp")), root / "photo.jpg");
    EXPECT_EQ(DocumentPlaces::keyOf(DocumentFiles::itemOf(root / "notes.md")), root / "notes.md");
}

TEST_F(LibraryFilesTest, anImageAndItsXoppAreRenamedMovedCopiedAndTrashedTogether) {
    makeImage(root / "photo.jpg", 40, 30);
    makeImageAnnotation(root / "photo.jpg", root / "photo.xopp");
    ASSERT_EQ(backgroundImageOf(root / "photo.xopp"), root / "photo.jpg");

    auto r = DocumentFiles::rename(DocumentFiles::itemOf(root / "photo.jpg"), "Whiteboard");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item, (DocumentItem{root / "Whiteboard.xopp", {}, {}, root / "Whiteboard.jpg"}));
    EXPECT_FALSE(fs::exists(root / "photo.jpg"));
    EXPECT_FALSE(fs::exists(root / "photo.xopp"));
    EXPECT_EQ(backgroundImageOf(root / "Whiteboard.xopp"), root / "Whiteboard.jpg") << "the reference follows";
    EXPECT_EQ(r.moved.size(), 2u);

    fs::create_directories(root / "Physics");
    r = DocumentFiles::move(r.item, root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(backgroundImageOf(root / "Physics" / "Whiteboard.xopp"), root / "Physics" / "Whiteboard.jpg");

    r = DocumentFiles::import(root / "Physics" / "Whiteboard.jpg", root);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(backgroundImageOf(root / "Whiteboard.xopp"), root / "Whiteboard.jpg") << "the copy has its own";
    EXPECT_EQ(backgroundImageOf(root / "Physics" / "Whiteboard.xopp"), root / "Physics" / "Whiteboard.jpg");

    // A name taken by any document: a lone .xopp moved next to an image of its name would pair with it
    makeNotes(root / "Physics" / "board.xopp");
    makeImage(root / "board.png", 10, 10);
    r = DocumentFiles::move(DocumentFiles::itemOf(root / "Physics" / "board.xopp"), root);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.xopp, root / "board (2).xopp");
    EXPECT_FALSE(DocumentFiles::rename(DocumentFiles::itemOf(root / "board (2).xopp"), "board").ok);

    const auto files = DocumentFiles::filesOf(DocumentFiles::itemOf(root / "Whiteboard.xopp"));
    EXPECT_EQ(files, (std::vector<fs::path>{root / "Whiteboard.xopp", root / "Whiteboard.jpg"}));
}

TEST_F(LibraryFilesTest, markdownFilesAreRenamedMovedAndCopied) {
    writeFile(root / "notes.md", "# Notes\n");
    auto r = DocumentFiles::rename(DocumentFiles::itemOf(root / "notes.md"), "Kalman");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.md, root / "Kalman.md");
    EXPECT_EQ(r.moved, (std::vector<std::pair<fs::path, fs::path>>{{root / "notes.md", root / "Kalman.md"}}));
    fs::create_directories(root / "Physics");
    r = DocumentFiles::move(r.item, root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Physics" / "Kalman.md"));
    r = DocumentFiles::import(root / "Physics" / "Kalman.md", root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.md, root / "Physics" / "Kalman (2).md");
    // A folder is imported with its Markdown files and images
    writeFile(root / "vault" / "a.md", "a");
    makeImage(root / "vault" / "b.png", 10, 10);
    writeFile(root / "vault" / "c.txt", "c");
    r = DocumentFiles::import(root / "vault", root / "Physics");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.documents, 2);
    EXPECT_TRUE(fs::exists(root / "Physics" / "vault" / "a.md"));
    EXPECT_TRUE(fs::exists(root / "Physics" / "vault" / "b.png"));
    EXPECT_FALSE(fs::exists(root / "Physics" / "vault" / "c.txt"));
}

TEST_F(LibraryFilesTest, theLibraryShowsWhatKindEachDocumentIs) {
    writeFile(root / "notes.md", "# Notes\n");
    makeImage(root / "photo.jpg", 40, 30);
    makeImageAnnotation(root / "photo.jpg", root / "photo.xopp");
    makeImage(root / "board.png", 40, 30);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    ASSERT_EQ(model.count(), 3);
    auto kindOf = [&](const fs::path& p) {
        return model.data(model.index(model.rowOf(qstr(p))), LibraryModel::KindRole).toString();
    };
    EXPECT_EQ(kindOf(root / "notes.md"), "md");
    EXPECT_EQ(kindOf(root / "photo.jpg"), "image");
    EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "photo.jpg"))), LibraryModel::PathRole).toString(),
              qstr(root / "photo.xopp"))
            << "opens as its .xopp";
    EXPECT_TRUE(model.data(model.index(model.rowOf(qstr(root / "photo.jpg"))), LibraryModel::HasXoppRole).toBool());
    EXPECT_EQ(kindOf(root / "board.png"), "image");
    // No page count for a Markdown file or an image alone
    model.searchIndex()->waitForDone();
    EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "notes.md"))), LibraryModel::PageCountRole).toInt(), -1);
    EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "board.png"))), LibraryModel::PageCountRole).toInt(), -1);
    // Found by their names
    model.setSearchQuery("board");
    ASSERT_EQ(model.count(), 1);
    EXPECT_EQ(model.data(model.index(0), LibraryModel::PathRole).toString(), qstr(root / "board.png"));
}

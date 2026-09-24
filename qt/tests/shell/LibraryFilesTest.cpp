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

using namespace xqt;

namespace {

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

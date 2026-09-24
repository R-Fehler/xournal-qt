/*
 * xournal-qt: Markdown files and images in the library (listing, pairs, file operations, previews, index, search,
 * opening), and the files that belong to a .xopp (attached background images).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QColor>
#include <QImage>
#include <QBuffer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <gtest/gtest.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "shell/TabManager.h"
#include "AppController.h"
#include "shell/ContentFiles.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
#include "shell/MdSnippets.h"
#include "shell/Previews.h"

#include "MarkdownFile.h"
#include "MdPassages.h"

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

void waitFor(const std::function<bool()>& cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/// A long Markdown text: a heading, then paragraphs; "needle" in paragraphs 5, 40, 41 and 80.
std::string longMarkdown() {
    std::string text = "# Lecture 3\n\n## Kalman filter\n\n";
    for (int i = 0; i < 120; ++i) {
        const bool hit = i == 5 || i == 40 || i == 41 || i == 80;
        text += "Paragraph " + std::to_string(i) + (hit ? " has the needle" : " is about the prediction") +
                " step of the filter.\n\n";
    }
    return text;
}

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

/// A JPEG `w` x `h` taken sideways: its orientation tag (EXIF, "turn by 90°") says how it is shown upright.
void makeSidewaysPhoto(const fs::path& p, int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::darkGreen);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    ASSERT_TRUE(img.save(&buffer, "JPEG"));
    // APP1 "Exif": a big-endian TIFF header and one entry, Orientation (0x0112) = 6
    const char exif[] = "\xFF\xE1\x00\x22" "Exif\x00\x00" "MM\x00\x2A\x00\x00\x00\x08"
                        "\x00\x01" "\x01\x12\x00\x03\x00\x00\x00\x01\x00\x06\x00\x00" "\x00\x00\x00\x00";
    jpeg.insert(2, QByteArray(exif, sizeof(exif) - 1));
    std::ofstream(p, std::ios::binary).write(jpeg.constData(), jpeg.size());
}

/// Pixels marked orange (the current hit) or yellow (the others), also on a grey background (a code block).
int orangePixels(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            n += c.red() > 220 && c.green() > 120 && c.green() < 180 && c.blue() < 80 ? 1 : 0;
        }
    }
    return n;
}
int yellowPixels(const QImage& img) {
    int n = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = img.pixelColor(x, y);
            n += c.red() > 220 && c.green() > 190 && c.blue() < 60 ? 1 : 0;
        }
    }
    return n;
}

/// Dark pixels (text) in a part of an image.
int darkPixels(const QImage& img, const QRect& part) {
    int n = 0;
    for (int y = part.top(); y <= part.bottom(); ++y) {
        for (int x = part.left(); x <= part.right(); ++x) {
            n += qGray(img.pixel(x, y)) < 128 ? 1 : 0;
        }
    }
    return n;
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

// A .xopp kept next to the hybrid PDF of its name ("Keep it as it is" when it was saved as a PDF with notes): the
// card opens the hybrid PDF, as for its export, also without ".name.pages.pdf". Changed after the PDF (edited in
// Xournal++ afterwards), it is not hidden: the two are listed as two documents.
TEST_F(LibraryFilesTest, aXoppKeptNextToItsHybridPdfDoesNotHideIt) {
    makeNotes(root / "notes.xopp");
    {
        DocumentHandler handler;
        Document doc(&handler);
        doc.addPage(std::make_shared<XojPage>(400, 300));
        ASSERT_TRUE(HybridPdf::write(doc, root / "notes.pdf").ok);
    }
    const auto pdfTime = fs::last_write_time(root / "notes.pdf");
    fs::last_write_time(root / "notes.xopp", pdfTime - std::chrono::minutes(5));  // (the old .xopp, kept)
    ASSERT_FALSE(fs::exists(root / ".notes.pages.pdf"));
    auto listed = DocumentFiles::scan(root).items;
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_TRUE(listed[0].hybrid);
    EXPECT_EQ(listed[0].main(), root / "notes.pdf") << "the card opens the hybrid PDF, not the stale .xopp";
    EXPECT_EQ(listed[0].xopp, root / "notes.xopp") << "(which travels with it)";
    EXPECT_EQ(DocumentFiles::itemOf(root / "notes.xopp").main(), root / "notes.pdf");
    EXPECT_EQ(DocumentFiles::itemOf(root / "notes.pdf").main(), root / "notes.pdf");

    // A plain PDF with its .xopp stays the pair that opens the .xopp
    makeNotes(root / "lecture.xopp");
    writeFile(root / "lecture.pdf", "%PDF-1.4 not really\n");
    EXPECT_EQ(DocumentFiles::itemOf(root / "lecture.pdf").main(), root / "lecture.xopp");

    // Edited after the PDF: two documents
    fs::last_write_time(root / "notes.xopp", pdfTime + std::chrono::minutes(5));
    listed = DocumentFiles::scan(root).items;
    std::vector<std::string> mains = names(listed);
    std::sort(mains.begin(), mains.end());
    EXPECT_EQ(mains, (std::vector<std::string>{"lecture.xopp", "notes.pdf", "notes.xopp"}));
    EXPECT_EQ(DocumentFiles::itemOf(root / "notes.xopp").main(), root / "notes.xopp");
    EXPECT_TRUE(DocumentFiles::itemOf(root / "notes.xopp").pdf.empty());
    EXPECT_TRUE(DocumentFiles::itemOf(root / "notes.pdf").xopp.empty());
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

TEST_F(LibraryFilesTest, markdownFilesShowTheirFirstPageAndImagesAThumbnail) {
    writeFile(root / "notes.md", "# Kalman filter\n\nPrediction and **update**.\n\n- one\n- two\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n```cpp\nint x = 1;\n```\n");
    makeImage(root / "wide.png", 800, 400, Qt::black);
    makeSidewaysPhoto(root / "photo.jpg", 80, 40);  // its orientation tag turns it upright
    PreviewCache::setLibrary(CacheLocation(root));

    const QImage md = PreviewCache::preview(DocumentFiles::itemOf(root / "notes.md"));
    ASSERT_EQ(md.width(), PreviewCache::WIDTH);
    EXPECT_NEAR(md.height(), PreviewCache::WIDTH * MarkdownFile::PAGE_HEIGHT / MarkdownFile::PAGE_WIDTH, 2) << "an A4 page";
    EXPECT_GT(darkPixels(md, QRect(0, 0, md.width(), md.height() / 8)), 50) << "the heading at the top";
    EXPECT_EQ(darkPixels(md, QRect(0, md.height() / 2, md.width(), md.height() / 2)), 0);

    const QImage wide = PreviewCache::preview(DocumentFiles::itemOf(root / "wide.png"));
    EXPECT_EQ(wide.size(), QSize(PreviewCache::WIDTH, PreviewCache::WIDTH / 2));
    const QImage photo = PreviewCache::preview(DocumentFiles::itemOf(root / "photo.jpg"));
    EXPECT_EQ(photo.size(), QSize(40, 80)) << "upright (and not made bigger)";

    PreviewCache::flush();
    PreviewCache::setLibrary({});
}

TEST_F(LibraryFilesTest, aMarkdownFileFlowsOverA4Pages) {
    std::string text = "# Lecture 3\n\n## Kalman filter\n\n";
    for (int i = 0; i < 120; ++i) {
        text += "Paragraph " + std::to_string(i) + " about the prediction step of the filter.\n\n";
    }
    auto doc = MarkdownFile::document(text);
    ASSERT_GT(doc->getPageCount(), 2u);
    EXPECT_EQ(doc->getPage(0)->getWidth(), MarkdownFile::PAGE_WIDTH);
    // The page that shows a place of the text
    EXPECT_EQ(MarkdownFile::pageOf(*doc, 0), 0u);
    EXPECT_EQ(MarkdownFile::pageOf(*doc, text.size() - 5), doc->getPageCount() - 1);
    EXPECT_EQ(MarkdownFile::document(text, 1)->getPageCount(), 1u);
    EXPECT_EQ(MarkdownFile::document("")->getPageCount(), 1u) << "an empty file: one empty page";

    // Read: at most so much of a file, cut at a line end
    writeFile(root / "long.md", "\xEF\xBB\xBF" "abc\ndef\nghi\n");
    bool cut = false;
    EXPECT_EQ(MarkdownFile::read(root / "long.md", 11, &cut), "abc\ndef\n");
    EXPECT_TRUE(cut);
    EXPECT_EQ(MarkdownFile::read(root / "long.md", 100, &cut), "abc\ndef\nghi\n");
    EXPECT_FALSE(cut);
}

TEST_F(LibraryFilesTest, markdownTextIsIndexedWithoutItsSyntaxAndFoundWithItsHeadings) {
    writeFile(root / "Uni" / "lecture3.md", "# Lecture 3\n\n"
                                            "Intro with a [link](https://example.org) and [[Kalman filter]].\n\n"
                                            "## Kalman filter\n\n"
                                            "### Prediction\n\n"
                                            "The **prediction** step: predict the state.\n\n"
                                            "- a prediction in a list\n\n"
                                            "## Update\n\n"
                                            "No hit here.\n");
    makeImage(root / "Uni" / "whiteboard.png", 20, 20);
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        EXPECT_EQ(index.documentsRead(), 2);
        EXPECT_EQ(index.pdfPagesRead(), 0);

        auto hits = index.search("prediction");
        ASSERT_EQ(hits.size(), 1u);
        const auto& h = hits[0];
        EXPECT_EQ(h.file, root / "Uni" / "lecture3.md");
        EXPECT_EQ(h.count, 3);
        EXPECT_FALSE(h.snippet.isEmpty());
        EXPECT_TRUE(h.pageHits.empty());
        ASSERT_EQ(h.blockHits.size(), 3u);
        EXPECT_EQ(h.blockHits[0].headings, "Lecture 3 › Kalman filter") << "a heading: the headings above it";
        EXPECT_EQ(h.blockHits[1].headings, "Lecture 3 › Kalman filter › Prediction");
        EXPECT_EQ(h.blockHits[1].count, 1);
        EXPECT_EQ(h.blockHits[2].block, h.blockHits[1].block + 1);
        EXPECT_TRUE(index.search("**prediction").empty()) << "no Markdown syntax";
        EXPECT_TRUE(index.search("example.org").empty()) << "a link's target is not its text";
        ASSERT_EQ(index.search("whiteboard").size(), 1u) << "an image by its name";
        index.flush();
    }
    // In the notes pack of its folder, with its links: read back, nothing is read again
    const auto notes = Packs::read(root / "Uni" / DocumentFiles::META_DIR, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
    ASSERT_TRUE(notes.has_value());
    const QCborMap md = notes->value(QStringLiteral("lecture3.md")).toMap();
    EXPECT_EQ(md.value(QStringLiteral("kind")).toString(), "md");
    EXPECT_EQ(md.value(QStringLiteral("links")).toArray().toVariantList(), QVariantList{"https://example.org"});
    EXPECT_EQ(md.value(QStringLiteral("wikiLinks")).toArray().toVariantList(), QVariantList{"Kalman filter"});
    LibraryIndex again(root);
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0);
    EXPECT_EQ(again.search("prediction").size(), 1u);

    // Changed: read again
    writeFile(root / "Uni" / "lecture3.md", "# Lecture 3\n\nNow about smoothing.\n");
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 1);
    EXPECT_TRUE(again.search("prediction").empty());
    EXPECT_EQ(again.search("smoothing").size(), 1u);
}

TEST_F(LibraryFilesTest, aMarkdownFileOpensForEditingAndAHitAtItsPassage) {
    writeFile(root / "notes.md", longMarkdown());
    const auto before = fs::last_write_time(root / "notes.md");
    AppController c;
    ASSERT_TRUE(c.openPath(qstr(root / "notes.md")));
    DocumentSession* s = c.tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->shownFile(), root / "notes.md");
    EXPECT_FALSE(s->hasFilePath()) << "never a .xopp";
    EXPECT_EQ(c.title(), "notes.md");
    EXPECT_FALSE(c.modified());
    EXPECT_GT(s->getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(c.shownFileNote().isEmpty()) << "edited: no note " << c.shownFileNote().toStdString();
    EXPECT_TRUE(s->isEditableText());
    EXPECT_EQ(s->suggestSavePath(), root / "notes.md") << "saved as itself";
    // Opened again: the same tab
    ASSERT_TRUE(c.openPath(qstr(root / "notes.md")));
    EXPECT_EQ(c.tabManager().count(), 1);

    // A hit in a passage (as the library index numbers them): its page, and there its first hit
    const auto ps = md::passages(md::parse(longMarkdown()));
    int passage = -1;
    for (size_t i = 0; i < ps.size(); ++i) {
        if (ps[i].text.find("Paragraph 41 ") == 0) {
            passage = static_cast<int>(i);
        }
    }
    ASSERT_GE(passage, 0);
    ASSERT_TRUE(c.openSearchHitInPassage(qstr(root / "notes.md"), "needle", passage));
    waitFor([&] { return !s->search().isRunning() && s->search().currentHit() >= 0; });
    const size_t page = MarkdownFile::pageOf(*s->getDocument(), ps[static_cast<size_t>(passage)].begin);
    EXPECT_GT(page, 0u);
    EXPECT_EQ(s->search().currentPage(), page);
    const size_t page40 = MarkdownFile::pageOf(*s->getDocument(), ps[static_cast<size_t>(passage - 1)].begin);
    EXPECT_EQ(s->search().currentOnPage(), page40 == page ? 1 : 0) << "the hit of paragraph 40 comes before it";
    EXPECT_EQ(s->search().hitCount(), 4);
    EXPECT_EQ(fs::last_write_time(root / "notes.md"), before);
}

TEST_F(LibraryFilesTest, editAsNotesMakesNotesFromTheMarkdownFileAndLeavesIt) {
    writeFile(root / "lecture.md", longMarkdown());
    const auto before = fs::last_write_time(root / "lecture.md");
    AppController c;
    c.setLibraryRoot(root);
    ASSERT_TRUE(c.openPath(qstr(root / "lecture.md")));
    DocumentSession* md = c.tabManager().currentSession();
    ASSERT_TRUE(c.editAsNotes());
    ASSERT_EQ(c.tabManager().count(), 2);
    DocumentSession* notes = c.tabManager().currentSession();
    ASSERT_NE(notes, md);
    EXPECT_EQ(notes->textFile(), nullptr) << "notes, not a text file";
    EXPECT_FALSE(notes->hasFilePath());
    EXPECT_TRUE(notes->isModified()) << "its content is nowhere else yet: closing asks";
    EXPECT_FALSE(c.tabManager().isPristine(c.tabManager().currentIndex()));
    EXPECT_EQ(c.title(), "lecture.xopp");
    EXPECT_EQ(notes->suggestSavePath(), root / "lecture.xopp") << "next to the .md";
    EXPECT_EQ(notes->getDocument()->getPageCount(), md->getDocument()->getPageCount());
    EXPECT_EQ(MarkdownFile::pageStarts(*notes->getDocument()), MarkdownFile::pageStarts(*md->getDocument()));
    EXPECT_FALSE(c.editAsNotes()) << "only from a .md";
    // Saved: a .xopp next to the .md, which is left as it was; the library shows two documents
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(qstr(root / "lecture.xopp"))));
    EXPECT_FALSE(notes->isModified());
    EXPECT_TRUE(fs::exists(root / "lecture.xopp"));
    EXPECT_EQ(fs::last_write_time(root / "lecture.md"), before);
    const auto listing = DocumentFiles::scan(root);
    int mds = 0, xopps = 0;
    for (const auto& item: listing.items) {
        mds += !item.md.empty();
        xopps += !item.xopp.empty();
    }
    EXPECT_EQ(mds, 1);
    EXPECT_EQ(xopps, 1);
    EXPECT_EQ(listing.items.size(), 2u) << "two cards: they go their own ways";
    // Opened again, the .xopp has the Markdown text on its pages (drawn formatted) and the .md is still a text file
    auto loaded = DocumentSession::loadFile(root / "lecture.xopp");
    ASSERT_NE(loaded.document, nullptr);
    EXPECT_EQ(MarkdownFile::pageStarts(*loaded.document), MarkdownFile::pageStarts(*md->getDocument()));
}

TEST_F(LibraryFilesTest, anImageOpensAsAPageToWriteOnAndIsSavedAsItsXopp) {
    makeImage(root / "photo.png", 800, 600);
    // A photo taken sideways (turned by its orientation tag), and a WebP: stored with the document
    makeSidewaysPhoto(root / "turned.jpg", 80, 40);
    const bool webp = DocumentFiles::isImageFile(root / "board.webp");
    if (webp) {
        makeImage(root / "board.webp", 60, 30);
    }
    AppController c;
    c.setLibraryRoot(root);
    ASSERT_TRUE(c.openPath(qstr(root / "photo.png")));
    DocumentSession* s = c.tabManager().currentSession();
    ASSERT_EQ(s->getDocument()->getPageCount(), 1u);
    PageRef page = s->getDocument()->getPage(0);
    EXPECT_TRUE(page->getBackgroundType().isImagePage());
    EXPECT_NE(page->getBackgroundImage().getPixbuf(), nullptr);
    EXPECT_NEAR(page->getWidth(), 841.89, 0.01);
    EXPECT_NEAR(page->getHeight(), 841.89 * 0.75, 0.01);
    EXPECT_EQ(c.title(), "photo.png");
    EXPECT_FALSE(c.modified());
    EXPECT_TRUE(c.shownFileNote().contains("photo.xopp")) << c.shownFileNote().toStdString();
    EXPECT_EQ(c.suggestedSaveFile(), QUrl::fromLocalFile(qstr(root / "photo.xopp"))) << "next to it, not elsewhere";
    ASSERT_TRUE(c.openPath(qstr(root / "photo.png")));
    EXPECT_EQ(c.tabManager().count(), 1) << "its tab";

    // Saved next to it: one document with it, which refers to it by its path (nothing copied)
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(qstr(root / "photo.xopp"))));
    EXPECT_EQ(c.shownFileNote(), "");
    EXPECT_EQ(DocumentFiles::itemOf(root / "photo.png").xopp, root / "photo.xopp");
    EXPECT_EQ(backgroundImageOf(root / "photo.xopp"), root / "photo.png");
    EXPECT_TRUE(DocumentFiles::imageAttachmentsOf(root / "photo.xopp").empty());
    // The card opens the .xopp now, also from the image
    ASSERT_TRUE(c.openPath(qstr(root / "photo.png")));
    EXPECT_EQ(c.tabManager().count(), 1);

    // Turned upright, stored with the document
    ASSERT_TRUE(c.openPath(qstr(root / "turned.jpg")));
    s = c.tabManager().currentSession();
    EXPECT_GT(s->getDocument()->getPage(0)->getHeight(), s->getDocument()->getPage(0)->getWidth()) << "upright";
    ASSERT_TRUE(c.saveAs(QUrl::fromLocalFile(qstr(root / "turned.xopp"))));
    EXPECT_EQ(DocumentFiles::imageAttachmentsOf(root / "turned.xopp").size(), 1u);
    EXPECT_TRUE(hasBackgroundImage(root / "turned.xopp"));
    EXPECT_EQ(DocumentFiles::itemOf(root / "turned.jpg").xopp, root / "turned.xopp");
    if (webp) {
        ASSERT_TRUE(c.openPath(qstr(root / "board.webp")));
        EXPECT_TRUE(c.tabManager().currentSession()->getDocument()->getPage(0)->getBackgroundType().isImagePage());
    }
}

TEST_F(LibraryFilesTest, aHitInAMarkdownFileIsASnippetCardOfItsPassage) {
    std::string text = "# Lecture 3\n\n## Kalman filter\n\nThe needle and another needle.\n\n```\n";
    for (int i = 0; i < 100; ++i) {
        text += "line " + std::to_string(i) + (i == 80 ? " needle" : "") + "\n";
    }
    text += "```\n";
    writeFile(root / "kalman.md", text);
    MdSnippetProvider::clearCaches();
    const int parses = MdSnippetProvider::parseCount();

    // The paragraph: its hits marked, the first one (the current one when opened) orange
    const QImage paragraph = MdSnippetProvider::render(root / "kalman.md", 2, "needle", 300, 0);
    ASSERT_EQ(paragraph.width(), 300);
    EXPECT_LT(paragraph.height(), 60) << "one line";
    EXPECT_GT(orangePixels(paragraph), 20) << "the first hit";
    EXPECT_GT(yellowPixels(paragraph), 20) << "the other one";
    // A long code block: cut to the lines around its hit
    const QImage code = MdSnippetProvider::render(root / "kalman.md", 3, "needle", 300, 120);
    EXPECT_LE(code.height(), 120);
    EXPECT_GT(orangePixels(code), 20) << "the hit is in the part shown";
    EXPECT_GT(MdSnippetProvider::render(root / "kalman.md", 3, "needle", 300, 0).height(), 1000) << "all of it";
    EXPECT_EQ(MdSnippetProvider::parseCount(), parses + 1) << "parsed once";
    EXPECT_TRUE(MdSnippetProvider::render(root / "kalman.md", 99, "needle", 300, 0).isNull());

    // The library's search result: a card per passage, with the headings above it
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    model.setSearchQuery("needle");
    ASSERT_EQ(model.count(), 1);
    const QVariantList passages = model.data(model.index(0), LibraryModel::HitPassageListRole).toList();
    ASSERT_EQ(passages.size(), 2);
    EXPECT_EQ(passages[0].toMap()["passage"].toInt(), 2);
    EXPECT_EQ(passages[0].toMap()["count"].toInt(), 2);
    EXPECT_EQ(passages[0].toMap()["headings"].toString(), "Lecture 3 › Kalman filter");
    EXPECT_EQ(passages[1].toMap()["passage"].toInt(), 3);
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::HitPassageBaseRole).toString().startsWith("image://mdsnippet/"));
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::HitPageListRole).toList().isEmpty());
}

// --- files of other apps (Android: "Open with", the share sheet, the file pickers; content:// URIs) ---------------

TEST_F(LibraryFilesTest, aReceivedFileIsCopiedIntoTheOpenedFolderOnceAndOpened) {
    const fs::path library = root / "Library";
    const fs::path outside = root / "Other app";
    fs::create_directories(library);
    makeNotes(outside / "Lecture 1.xopp");
    makeImage(outside / "board.png", 40, 30);
    writeFile(outside / "todo.md", "# To do\n");
    writeFile(outside / "notes.txt", "plain text\n");
    AppController c;
    c.setLibraryRoot(library);
    QSignalSpy note(&c, &AppController::pageActionDone);
    QSignalSpy errors(&c, &AppController::message);

    EXPECT_EQ(c.receiveFiles({qstr(outside / "Lecture 1.xopp"), qstr(outside / "board.png"), qstr(outside / "todo.md"),
                              qstr(outside / "notes.txt")}),
              4);
    const fs::path opened = library / "Opened";
    EXPECT_TRUE(fs::exists(opened / "Lecture 1.xopp"));
    EXPECT_TRUE(fs::exists(opened / "board.png"));
    EXPECT_TRUE(fs::exists(opened / "todo.md"));
    EXPECT_TRUE(fs::exists(opened / "notes.txt"));
    EXPECT_EQ(c.tabCount(), 4);
    EXPECT_TRUE(fs::exists(outside / "Lecture 1.xopp")) << "the other app's file stays";
    ASSERT_EQ(note.count(), 1) << "one note for all of them";
    EXPECT_TRUE(note.first().first().toString().contains("Opened")) << note.first().first().toString().toStdString();
    EXPECT_EQ(errors.count(), 0);

    // The same file again: the copy there is opened, not copied a second time; another file of that name is copied
    // under a free name
    EXPECT_EQ(c.receiveFiles({qstr(outside / "Lecture 1.xopp")}), 1);
    EXPECT_FALSE(fs::exists(opened / "Lecture 1 (2).xopp"));
    writeFile(root / "Elsewhere" / "Lecture 1.xopp", "");  // (the folder)
    DocumentHandler handler;
    Document doc(&handler);
    doc.addPage(std::make_shared<XojPage>(400, 300));
    doc.addPage(std::make_shared<XojPage>(400, 300));
    ASSERT_TRUE(DocumentSession::writeDocument(doc, root / "Elsewhere" / "Lecture 1.xopp").ok);
    EXPECT_EQ(c.receiveFiles({qstr(root / "Elsewhere" / "Lecture 1.xopp")}), 1);
    EXPECT_TRUE(fs::exists(opened / "Lecture 1 (2).xopp"));

    // Something that cannot be read: said so, nothing opened
    errors.clear();
    EXPECT_EQ(c.receiveFiles({qstr(outside / "missing.pdf")}), 0);
    EXPECT_EQ(errors.count(), 1);
    // Text shared without a file
    errors.clear();
    EXPECT_EQ(c.receiveFiles({"xournal-qt:shared-text"}), 0);
    EXPECT_EQ(errors.count(), 1);
}

TEST_F(LibraryFilesTest, pickedFoldersAreCopiedWithTheirStructureAndNamesAreMadeSafe) {
    const fs::path picked = root / "Picked" / "Semester";
    makeNotes(picked / "Week 1" / "notes.xopp");
    writeFile(picked / "Week 2" / "summary.md", "# Summary\n");
    writeFile(picked / ".git" / "config", "x");
    writeFile(picked / "Makefile", "all:\n");  // (in a folder, names stay as they are)
    const fs::path into = root / "Staging";
    fs::create_directories(into);
    std::string error;
    const fs::path copy = ContentFiles::copyInto(qstr(picked), into, error);
    EXPECT_EQ(copy, into / "Semester");
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_TRUE(fs::exists(into / "Semester" / "Week 1" / "notes.xopp"));
    EXPECT_TRUE(fs::exists(into / "Semester" / "Week 2" / "summary.md"));
    EXPECT_FALSE(fs::exists(into / "Semester" / ".git")) << "hidden folders stay behind";
    EXPECT_TRUE(fs::exists(into / "Semester" / "Makefile"));
    // A single file whose name (another app's) has no extension: it is taken from the content
    writeFile(root / "Picked" / "1234", "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n");
    EXPECT_EQ(ContentFiles::copyInto(qstr(root / "Picked" / "1234"), into, error), into / "1234.pdf");

    EXPECT_EQ(ContentFiles::safeName("../../etc/passwd"), "passwd");
    EXPECT_EQ(ContentFiles::safeName(".hidden"), "hidden");
    EXPECT_EQ(ContentFiles::safeName("a:b?.pdf"), "a_b_.pdf");
    EXPECT_EQ(ContentFiles::safeName(""), "Document");
    EXPECT_TRUE(ContentFiles::isForeign(QUrl("content://com.android.providers.downloads.documents/document/12")));
    EXPECT_FALSE(ContentFiles::isForeign(QUrl::fromLocalFile("/tmp/a.pdf")));
    EXPECT_EQ(ContentFiles::sourceOf(QUrl::fromLocalFile("/tmp/a b.pdf")), "/tmp/a b.pdf");
}

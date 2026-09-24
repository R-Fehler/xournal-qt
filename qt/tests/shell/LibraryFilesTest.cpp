/*
 * xournal-qt: Markdown files and images in the library (listing, pairs, file operations, previews, index, search,
 * opening), and the files that belong to a .xopp (attached background images).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QColor>
#include <QImage>
#include <QBuffer>
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
#include "shell/TabManager.h"
#include "AppController.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/Library.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryModel.h"
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

TEST_F(LibraryFilesTest, aMarkdownFileOpensReadOnlyAndAHitAtItsPassage) {
    writeFile(root / "notes.md", longMarkdown());
    const auto before = fs::last_write_time(root / "notes.md");
    AppController c;
    ASSERT_TRUE(c.openPath(qstr(root / "notes.md")));
    DocumentSession* s = c.tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->shownFile(), root / "notes.md");
    EXPECT_FALSE(s->hasFilePath()) << "never written back";
    EXPECT_EQ(c.title(), "notes.md");
    EXPECT_FALSE(c.modified());
    EXPECT_GT(s->getDocument()->getPageCount(), 2u);
    EXPECT_TRUE(c.shownFileNote().startsWith("Read-only")) << c.shownFileNote().toStdString();
    EXPECT_EQ(s->suggestSavePath().extension(), ".xopp");
    EXPECT_NE(s->suggestSavePath().parent_path(), root) << "not a .xopp next to the Markdown file";
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

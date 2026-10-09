/*
 * xournal-qt: sharing a folder or the library as a zip (LibraryShare), receiving one (LibraryUnzip), and cache
 * entries that survive a copy (LibraryIndex adopts an entry whose files have another time but the same size and
 * content hash).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <map>
#include <memory>
#include <set>

#include <QCborArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTimeZone>
#include <cairo-pdf.h>
#include <cairo.h>
#include <gtest/gtest.h>
#include <zip.h>

#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfEncryption.h"
#include "session/PdfHistory.h"
#include "shell/InkTextStore.h"
#include "shell/FileStamps.h"
#include "shell/LibraryIndex.h"
#include "shell/LibraryCache.h"
#include "shell/LibraryShare.h"
#include "shell/LibraryUnzip.h"
#include "shell/DocumentCovers.h"
#include "shell/ZipFile.h"
#include "support/TestSupport.h"

using xqt::test::readFile;
using xqt::test::writeFile;

using xqt::test::makeTextPdf;
using xqt::test::numbered;

using namespace xqt;

namespace {

void addStroke(Document& doc, size_t page, double y = 200) {
    auto s = std::make_unique<Stroke>();
    s->setColor(Color(0xffcc0000U));
    s->setWidth(2);
    s->addPoint(Point(100, y));
    s->addPoint(Point(200, y + 60));
    s->getBoundingBox();
    doc.getPage(page)->getSelectedLayer()->addElement(std::move(s));
}

void setTime(const fs::path& file, qint64 secs) {
    QFile f(QString::fromStdString(file.string()));
    ASSERT_TRUE(f.open(QIODevice::ReadWrite));
    ASSERT_TRUE(f.setFileTime(QDateTime::fromSecsSinceEpoch(secs, QTimeZone::UTC), QFileDevice::FileModificationTime));
}

qint64 secsOf(const fs::path& file) {
    return QFileInfo(QString::fromStdString(file.string())).lastModified().toSecsSinceEpoch();
}

std::string gunzipped(const std::string& bytes) {
    bool ok = false;
    std::string xml = PdfHistory::gunzip(bytes, ok);
    return ok ? xml : bytes;
}

/// The zip's entries by name
std::map<std::string, Zip::Entry> entriesOf(const fs::path& zip) {
    Zip::Reader r(zip);
    std::map<std::string, Zip::Entry> out;
    for (const auto& e: r.entries()) {
        out[e.name] = e;
    }
    return out;
}

std::string entryData(const fs::path& zip, const std::string& name, const std::string& password = {}) {
    Zip::Reader r(zip);
    r.setPassword(password);
    for (const auto& e: r.entries()) {
        if (e.name == name) {
            std::string error;
            return r.read(e, 1u << 30, error);
        }
    }
    return {};
}

std::set<QString> keysOf(const std::optional<QCborMap>& m) {
    std::set<QString> keys;
    if (m) {
        for (auto it = m->cbegin(); it != m->cend(); ++it) {
            keys.insert(it.key().toString());
        }
    }
    return keys;
}

void waitForHashes(LibraryIndex& index) {
    // (the hashes are filled after the documents are indexed, in the same background run)
    index.waitForDone();
    index.flush();
    index.inkText().flush();
}

class LibraryShareTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        lib = fs::canonical(fs::path(tmp.path().toStdString())) / "Studies";
        shared = lib / "Shared";
        // A .xopp with its PDF
        makeTextPdf(shared / "Lectures" / "lecture.pdf", numbered("turbine page ", 3));
        {
            auto loaded = DocumentSession::loadFile(shared / "Lectures" / "lecture.pdf");
            ASSERT_TRUE(loaded.document) << loaded.error;
            addStroke(*loaded.document, 1);
            ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, shared / "Lectures" / "lecture.xopp").ok);
        }
        // A lone .xopp
        {
            Document doc(nullptr);
            doc.addPage(std::make_shared<XojPage>(595, 842));
            addStroke(doc, 0);
            ASSERT_TRUE(DocumentSession::writeDocument(doc, shared / "notes.xopp").ok);
        }
        // A .xopp whose PDF is outside the shared folder
        makeTextPdf(lib / "Elsewhere" / "outside.pdf", numbered("turbine page ", 2));
        {
            auto loaded = DocumentSession::loadFile(lib / "Elsewhere" / "outside.pdf");
            ASSERT_TRUE(loaded.document) << loaded.error;
            addStroke(*loaded.document, 0);
            ASSERT_TRUE(DocumentSession::writeDocument(*loaded.document, shared / "ext.xopp").ok);
        }
        // Markdown with a picture outside and a link outside
        QImage img(8, 8, QImage::Format_RGB32);
        img.fill(Qt::blue);
        fs::create_directories(lib / "Elsewhere");
        ASSERT_TRUE(img.save(QString::fromStdString((lib / "Elsewhere" / "pic.png").string())));
        writeFile(shared / "todo.md", "# To do\n\n![a picture](../Elsewhere/pic.png)\n\nSee [b](../Other/b.md) and "
                                      "[the lecture](Lectures/lecture.xopp#page=2).\n");
        // A PDF with notes that keeps its versions (two)
        makeTextPdf(lib / "base.pdf", numbered("turbine page ", 2));
        {
            auto loaded = DocumentSession::loadFile(lib / "base.pdf");
            ASSERT_TRUE(loaded.document);
            DocumentSession s(*app, std::move(loaded.document));
            s.setKeepsVersions(true);
            addStroke(*s.getDocument(), 0);
            ASSERT_TRUE(s.saveAsHybrid(shared / "hist.pdf").ok);
            PdfHistory::clock = [] { return std::time_t(1700000000) + 3 * 86400; };
            addStroke(*s.getDocument(), 1, 300);
            ASSERT_TRUE(s.save().ok);
            PdfHistory::clock = nullptr;
        }
        fs::remove(lib / "base.pdf");
        ASSERT_TRUE(HybridPdf::markerOf(shared / "hist.pdf").history);
        // A protected PDF
        makeTextPdf(lib / "plain.pdf", numbered("turbine page ", 1));
        {
            PdfEncryption::Protection p;
            p.password = "secret";
            std::string error;
            ASSERT_TRUE(PdfEncryption::rewrite(lib / "plain.pdf", "", shared / "secret.pdf", &p, error)) << error;
        }
        fs::remove(lib / "plain.pdf");
        // An empty folder, and a document in a folder that is not shared
        fs::create_directories(shared / "Empty");
        {
            Document doc(nullptr);
            doc.addPage(std::make_shared<XojPage>(595, 842));
            addStroke(doc, 0);
            fs::create_directories(lib / "Other");
            ASSERT_TRUE(DocumentSession::writeDocument(doc, lib / "Other" / "elsewhere.xopp").ok);
        }
        writeFile(lib / "Other" / "b.md", "# B\n");
        for (auto it = fs::recursive_directory_iterator(lib); it != fs::recursive_directory_iterator(); ++it) {
            if (it->is_regular_file()) {
                setTime(it->path(), 1700000000);  // (whole seconds, a known time)
            }
        }
        out = fs::path(tmp.filePath("out").toStdString());
        fs::create_directories(out);
    }
    void TearDown() override { PdfHistory::clock = nullptr; }

    /// The library indexed, with handwriting read for the notes and its content hashes
    std::unique_ptr<LibraryIndex> indexed() {
        auto index = std::make_unique<LibraryIndex>(lib);
        index->setWriteDelays(10, 50);
        index->update(DocumentFiles::scanRecursive(lib, DocumentFiles::AllFiles));
        index->waitForDone();
        InkDoc ink;
        ink.stamp = fileStamp(shared / "notes.xopp");
        ink.recognizer = QStringLiteral("fake/1");
        ink.complete = true;
        ink.pages.resize(1);
        index->inkText().put(shared / "notes.xopp", ink);
        InkDoc other = ink;
        other.stamp = fileStamp(lib / "Other" / "elsewhere.xopp");
        index->inkText().put(lib / "Other" / "elsewhere.xopp", other);
        waitForHashes(*index);
        return index;
    }

    LibraryShare::Summary share(const LibraryShare::Options& options, LibraryIndex* index, fs::path& zip) {
        zip = out / ("Shared-" + std::to_string(++zips) + ".zip");
        std::string error;
        const auto plan = LibraryShare::plan(shared, lib, "Shared", zip, options, index, error);
        EXPECT_TRUE(error.empty()) << error;
        std::atomic<bool> cancel{false};
        return LibraryShare::run(plan, cancel);
    }

    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    fs::path lib, shared, out;
    int zips = 0;
};

}  // namespace

TEST_F(LibraryShareTest, theAppFormatCarriesFreshReadingsOfExactlyTheSharedDocuments) {
    auto index = indexed();
    ASSERT_GT(index->hashesComputed(), 0);
    std::map<fs::path, std::string> before;
    for (const char* f: {"ext.xopp", "todo.md", "hist.pdf", "notes.xopp"}) {
        before[shared / f] = readFile(shared / f);
    }
    // The preview of the notes, drawn in the library
    DocumentCovers::setLibrary(CacheLocation(lib));
    DocumentItem notesItem;
    notesItem.xopp = shared / "notes.xopp";
    ASSERT_FALSE(DocumentCovers::cover(notesItem).isNull());
    ASSERT_TRUE(DocumentCovers::flush());
    LibraryShare::Options o;
    o.pdfText = true;
    fs::path zip;
    const auto s = share(o, index.get(), zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    EXPECT_FALSE(s.cancelled);
    EXPECT_TRUE(s.failed.empty()) << s.failed.front();
    EXPECT_GE(s.readings, 4);
    for (const auto& [file, bytes]: before) {
        EXPECT_EQ(readFile(file), bytes) << "the user's files are never changed: " << file;
    }

    const auto entries = entriesOf(zip);
    for (const char* name: {"Shared/", "Shared/Empty/", "Shared/Lectures/lecture.pdf", "Shared/Lectures/lecture.xopp",
                            "Shared/notes.xopp", "Shared/ext.xopp", "Shared/todo.md", "Shared/hist.pdf",
                            "Shared/secret.pdf", "Shared/_attached/outside.pdf", "Shared/_attached/pic.png",
                            "Shared/.xournal_library/notes.pack", "Shared/.xournal_library/ink-text.pack",
                            "Shared/.xournal_library/previews.pack",
                            "Shared/.xournal_library/share-manifest.pack",
                            "Shared/Lectures/.xournal_library/notes.pack",
                            "Shared/Lectures/.xournal_library/pdf-text.pack"}) {
        EXPECT_TRUE(entries.count(name)) << name;
    }
    for (const auto& [name, e]: entries) {
        EXPECT_EQ(name.find("Other"), std::string::npos) << "nothing of other folders: " << name;
        EXPECT_TRUE(e.utc) << "the extended timestamp: " << name;
    }
    EXPECT_EQ(entries.at("Shared/notes.xopp").mtime, 1700000000) << "UTC seconds";

    // Paths rewritten in the copies
    const std::string ext = gunzipped(entryData(zip, "Shared/ext.xopp"));
    EXPECT_NE(ext.find("filename=\"_attached/outside.pdf\""), std::string::npos) << ext.substr(0, 600);
    EXPECT_NE(entryData(zip, "Shared/todo.md").find("![a picture](_attached/pic.png)"), std::string::npos);
    ASSERT_EQ(s.attached.size(), 2u);
    ASSERT_EQ(s.outsideLinks.size(), 1u);
    EXPECT_EQ(s.outsideLinks[0], "todo.md → ../Other/b.md");
    // Version history stripped (a compacted copy); the protected PDF as it is
    EXPECT_EQ(entryData(zip, "Shared/secret.pdf"), readFile(shared / "secret.pdf"));
    EXPECT_NE(entryData(zip, "Shared/hist.pdf"), readFile(shared / "hist.pdf"));
    ASSERT_FALSE(s.notes.empty());
    EXPECT_NE(s.notes[0].find("secret.pdf"), std::string::npos);

    // Received: the packs hold exactly the shared documents, stamped as unpacked; nothing is read again
    const fs::path recv = fs::path(tmp.filePath("Recv").toStdString());
    std::atomic<bool> cancel{false};
    const auto r = LibraryUnzip::unpack(zip, recv / "Inbox", CacheLocation(recv), {}, cancel);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.folder, recv / "Inbox" / "Shared");
    EXPECT_TRUE(fs::is_directory(r.folder / "Empty"));
    EXPECT_FALSE(fs::exists(r.folder / ".xournal_library" / "share-manifest.pack")) << "read, not kept";
    EXPECT_FALSE(HybridPdf::markerOf(r.folder / "hist.pdf").history) << "without its versions";
    EXPECT_EQ(secsOf(r.folder / "notes.xopp"), 1700000000);
    const auto notes = Packs::read(r.folder / ".xournal_library", LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT);
    EXPECT_EQ(keysOf(notes), (std::set<QString>{"ext.xopp", "hist.pdf", "notes.xopp", "todo.md"}))
            << "the documents directly in it (the protected one has no readings)";
    const auto ink = Packs::read(r.folder / ".xournal_library", InkTextStore::PACK, InkTextStore::FORMAT);
    EXPECT_EQ(keysOf(ink), std::set<QString>{"notes.xopp"});
    EXPECT_EQ(InkTextStore::decode(ink->value(QStringLiteral("notes.xopp")).toMap())->stamp,
              fileStamp(r.folder / "notes.xopp"));
    EXPECT_FALSE(notes->value(QStringLiteral("hist.pdf")).toMap().contains(QStringLiteral("versions")));

    LibraryIndex received(recv);
    received.update(DocumentFiles::scanRecursive(recv, DocumentFiles::AllFiles));
    received.waitForDone();
    EXPECT_LE(received.documentsRead(), 3) << "only the two attached files and the protected PDF, without entries";
    EXPECT_LE(received.pdfPagesRead(), 2) << "at most the attached PDF's pages; the shared ones came with their text";
    EXPECT_TRUE(received.inkOf(r.folder / "notes.xopp")) << "the handwriting read before";
    EXPECT_EQ(received.search(QStringLiteral("turbine")).size(), 4u) << "lecture, ext, hist and the attached PDF";
    // Its preview is there without drawing it
    DocumentCovers::setLibrary(CacheLocation(recv));
    DocumentItem receivedNotes;
    receivedNotes.xopp = r.folder / "notes.xopp";
    EXPECT_FALSE(DocumentCovers::stored(receivedNotes).isNull()) << "the preview came with the zip, stamped as unpacked";
    DocumentCovers::setLibrary({});
}

TEST_F(LibraryShareTest, withoutPdfTextTheReadingsStillGoAlongAndHistoryOnRequest) {
    auto index = indexed();
    LibraryShare::Options o;
    o.history = true;
    fs::path zip;
    const auto s = share(o, index.get(), zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    const auto entries = entriesOf(zip);
    EXPECT_FALSE(entries.count("Shared/Lectures/.xournal_library/pdf-text.pack"));
    EXPECT_TRUE(entries.count("Shared/.xournal_library/ink-text.pack"));
    EXPECT_EQ(entryData(zip, "Shared/hist.pdf"), readFile(shared / "hist.pdf")) << "with its versions: as it is";

    o = {};
    o.readings = false;
    const auto bare = share(o, index.get(), zip);
    for (const auto& [name, e]: entriesOf(zip)) {
        EXPECT_EQ(name.find("ink-text"), std::string::npos) << name;
        EXPECT_EQ(name.find("notes.pack"), std::string::npos) << name;
    }
    EXPECT_EQ(bare.readings, 0);
}

TEST_F(LibraryShareTest, forXournalAndAsPlainPdfs) {
    LibraryShare::Options o;
    o.format = LibraryShare::Format::Xournal;
    fs::path zip;
    auto s = share(o, nullptr, zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    EXPECT_TRUE(s.failed.empty()) << s.failed.front();
    auto entries = entriesOf(zip);
    for (const char* name: {"Shared/hist.xopp", "Shared/hist.xopp.bg.pdf", "Shared/Lectures/lecture.xopp",
                            "Shared/Lectures/lecture.pdf", "Shared/notes.xopp", "Shared/README.txt",
                            "Shared/secret.pdf"}) {
        EXPECT_TRUE(entries.count(name)) << name;
    }
    EXPECT_FALSE(entries.count("Shared/hist.pdf"));
    for (const auto& [name, e]: entries) {
        EXPECT_EQ(name.find(".xournal_library"), std::string::npos) << "no cache for other apps: " << name;
    }

    o.format = LibraryShare::Format::Pdf;
    s = share(o, nullptr, zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    EXPECT_TRUE(s.failed.empty()) << s.failed.front();
    entries = entriesOf(zip);
    for (const char* name: {"Shared/notes.pdf", "Shared/ext.pdf", "Shared/hist.pdf", "Shared/Lectures/lecture.pdf",
                            "Shared/todo.md", "Shared/secret.pdf"}) {
        EXPECT_TRUE(entries.count(name)) << name;
    }
    EXPECT_FALSE(entries.count("Shared/notes.xopp"));
    const fs::path recv = fs::path(tmp.filePath("Plain").toStdString());
    std::atomic<bool> cancel{false};
    const auto r = LibraryUnzip::unpack(zip, recv, {}, {}, cancel);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(HybridPdf::isHybrid(r.folder / "hist.pdf")) << "the ink drawn into the pages, no notes";
    EXPECT_FALSE(HybridPdf::isHybrid(r.folder / "Lectures" / "lecture.pdf"));
    auto lecture = DocumentSession::loadFile(r.folder / "Lectures" / "lecture.pdf");
    ASSERT_TRUE(lecture.document);
    EXPECT_EQ(lecture.document->getPageCount(), 3u);
    EXPECT_TRUE(PdfEncryption::probe(r.folder / "secret.pdf").isProtected()) << "protected PDFs stay protected";
}

TEST_F(LibraryShareTest, aPasswordEncryptsTheZipWithAes) {
    if (!Zip::aesAvailable()) {
        GTEST_SKIP() << "libzip without AES here";
    }
    LibraryShare::Options o;
    o.format = LibraryShare::Format::Pdf;
    o.password = "open sesame";
    fs::path zip;
    const auto s = share(o, nullptr, zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    for (const auto& [name, e]: entriesOf(zip)) {
        if (!e.folder) {
            EXPECT_TRUE(e.encrypted && e.aes) << name;
        }
    }
    const auto in = LibraryUnzip::inspect(zip);
    EXPECT_TRUE(in.encrypted);
    EXPECT_TRUE(in.supported);
    const fs::path recv = fs::path(tmp.filePath("Locked").toStdString());
    std::atomic<bool> cancel{false};
    auto r = LibraryUnzip::unpack(zip, recv, {}, {}, cancel);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.wrongPassword);
    r = LibraryUnzip::unpack(zip, recv, {}, "wrong", cancel);
    EXPECT_TRUE(r.wrongPassword);
    EXPECT_FALSE(fs::exists(recv / "Shared"));
    r = LibraryUnzip::unpack(zip, recv, {}, "open sesame", cancel);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(r.folder / "notes.pdf"));
}

TEST_F(LibraryShareTest, cancelledNothingIsWritten) {
    LibraryShare::Options o;
    const fs::path zip = out / "cancelled.zip";
    std::string error;
    const auto plan = LibraryShare::plan(shared, lib, "Shared", zip, o, nullptr, error);
    std::atomic<bool> cancel{false};
    int steps = 0;
    const auto s = LibraryShare::run(plan, cancel, [&](int, int, const std::string&) {
        if (++steps == 2) {
            cancel = true;
        }
    });
    EXPECT_TRUE(s.cancelled);
    EXPECT_FALSE(fs::exists(zip));
    for (const auto& e: fs::directory_iterator(out)) {
        ADD_FAILURE() << "left behind: " << e.path();
    }
}

TEST_F(LibraryShareTest, receivingKeepsEverythingInsideTheTarget) {
    EXPECT_EQ(LibraryUnzip::safePath("a/b.txt"), fs::path("a/b.txt"));
    EXPECT_EQ(LibraryUnzip::safePath("./a//b.txt"), fs::path("a/b.txt"));
    EXPECT_EQ(LibraryUnzip::safePath("a\\b.txt"), fs::path("a/b.txt"));
    for (const char* bad: {"../evil.txt", "/etc/evil", "\\\\server\\x", "C:/x.txt", "c:x.txt", "ok/../../evil.txt",
                           "a/../b", "x:stream"}) {
        EXPECT_TRUE(LibraryUnzip::safePath(bad).empty()) << bad;
    }
    // A crafted zip: entries that leave the folder, an absolute one, a link
    const fs::path zip = out / "crafted.zip";
    {
        int err = 0;
        zip_t* z = zip_open(zip.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        ASSERT_TRUE(z);
        auto add = [&](const char* name, const char* data) {
            zip_source_t* src = zip_source_buffer(z, data, strlen(data), 0);
            return zip_file_add(z, name, src, 0);
        };
        add("good/a.txt", "fine");
        add("../evil.txt", "no");
        add("good/../../evil2.txt", "no");
        add("/abs.txt", "no");
        const zip_int64_t link = add("good/link", "/etc/passwd");
        zip_file_set_external_attributes(z, static_cast<zip_uint64_t>(link), 0, ZIP_OPSYS_UNIX, 0120777u << 16);
        ASSERT_EQ(zip_close(z), 0);
    }
    const fs::path recv = fs::path(tmp.filePath("Recv").toStdString());
    fs::create_directories(recv / "Inbox");
    std::atomic<bool> cancel{false};
    const auto r = LibraryUnzip::unpack(zip, recv / "Inbox", {}, {}, cancel);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.folder, recv / "Inbox" / "crafted") << "not all in one folder: named after the zip";
    EXPECT_EQ(readFile(r.folder / "good" / "a.txt"), "fine");
    EXPECT_EQ(r.files, 1);
    EXPECT_EQ(r.skipped.size(), 4u);
    EXPECT_FALSE(fs::exists(r.folder / "good" / "link"));
    std::set<fs::path> all;
    for (auto it = fs::recursive_directory_iterator(fs::path(tmp.path().toStdString()));
         it != fs::recursive_directory_iterator(); ++it) {
        EXPECT_EQ(it->path().filename().string().find("evil"), std::string::npos) << it->path();
        EXPECT_NE(it->path().filename(), "abs.txt");
    }
    // Twice: " (2)"
    const auto again = LibraryUnzip::unpack(zip, recv / "Inbox", {}, {}, cancel);
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.folder, recv / "Inbox" / "crafted (2)");
}

TEST_F(LibraryShareTest, entriesSurviveACopyWithOtherTimesButNotOtherContent) {
    auto index = indexed();
    index.reset();
    // The library copied by hand: every file gets another time (the cache folders come along)
    const fs::path copy = fs::path(tmp.filePath("Copy").toStdString());
    fs::copy(lib, copy, fs::copy_options::recursive);
    for (auto it = fs::recursive_directory_iterator(copy); it != fs::recursive_directory_iterator(); ++it) {
        if (it->is_regular_file() && it->path().parent_path().filename() != ".xournal_library") {
            setTime(it->path(), 1800000000);
        }
    }
    // One Markdown file changed in place, with the same size
    std::string md = readFile(copy / "Shared" / "todo.md");
    md[2] = 'X';
    writeFile(copy / "Shared" / "todo.md", md);
    setTime(copy / "Shared" / "todo.md", 1800000000);

    LibraryIndex adopted(copy);
    adopted.update(DocumentFiles::scanRecursive(copy, DocumentFiles::AllFiles));
    adopted.waitForDone();
    EXPECT_EQ(adopted.documentsRead(), 1) << "only the changed Markdown file is read again";
    EXPECT_GE(adopted.entriesAdopted(), 6);
    EXPECT_EQ(adopted.pdfPagesRead(), 0);
    EXPECT_TRUE(adopted.inkOf(copy / "Shared" / "notes.xopp")) << "its handwriting follows";
    EXPECT_TRUE(adopted.inkOf(copy / "Other" / "elsewhere.xopp"));
}

TEST_F(LibraryShareTest, recordingsGoAlongOrStayBehind) {
    audio::setAppFolder(fs::path(tmp.filePath("app-audio").toStdString()));
    const std::string name = "2026-10-05_10-00-00.ogg";
    // (in its sidecar, next to it; its bytes are not looked at)
    writeFile(shared / "talk.audio" / name, std::string(4096, 'o'));
    {
        Document doc(nullptr);
        doc.addPage(std::make_shared<XojPage>(595, 842));
        auto st = std::make_unique<Stroke>();
        st->setWidth(1.5);
        st->addPoint(Point(100, 100));
        st->addPoint(Point(200, 130));
        audio::stamp(*st, name, 700);
        doc.getPage(0)->getSelectedLayer()->addElement(std::move(st));
        ASSERT_TRUE(DocumentSession::writeDocument(doc, shared / "talk.xopp").ok);
        auto loaded = DocumentSession::loadFile(shared / "talk.xopp");
        ASSERT_TRUE(loaded.document);
        DocumentSession s(*app, std::move(loaded.document));
        ASSERT_TRUE(s.saveAsHybrid(shared / "talk-notes.pdf").ok);
    }
    ASSERT_GT(HybridPdf::recordingBytes(shared / "talk-notes.pdf"), 4000u) << "the PDF with notes carries it";
    const auto survey = LibraryShare::survey(shared);
    EXPECT_EQ(survey.recordings, 2) << "the .xopp's (in its sidecar) and the PDF's";
    EXPECT_GE(survey.recordingBytes, 8192u);

    LibraryShare::Options o;
    fs::path zip;
    auto s = share(o, nullptr, zip);
    ASSERT_TRUE(s.error.empty()) << s.error;
    auto entries = entriesOf(zip);
    EXPECT_TRUE(entries.count("Shared/talk.audio/" + name)) << "next to its notes, where the app finds it";
    EXPECT_EQ(entryData(zip, "Shared/talk-notes.pdf"), readFile(shared / "talk-notes.pdf"));

    o.recordings = false;
    s = share(o, nullptr, zip);
    entries = entriesOf(zip);
    EXPECT_FALSE(entries.count("Shared/talk.audio/" + name));
    const fs::path recv = fs::path(tmp.filePath("NoAudio").toStdString());
    std::atomic<bool> cancel{false};
    const auto r = LibraryUnzip::unpack(zip, recv, {}, {}, cancel);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(HybridPdf::isHybrid(r.folder / "talk-notes.pdf"));
    EXPECT_EQ(HybridPdf::recordingBytes(r.folder / "talk-notes.pdf"), 0u) << "left out of the PDF's copy";
    auto reopened = DocumentSession::loadFile(r.folder / "talk-notes.pdf");
    EXPECT_TRUE(reopened.document) << reopened.error;
    audio::setAppFolder({});
}

/*
 * xournal-qt: the library's "Show" filter (kinds of files shown, per library) and the files that are not documents:
 * text and code files (plain-text cards, read-only, indexed below a size limit) and all other files (a generic card,
 * the system app, the file manager).
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "shell/DocumentFiles.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"

using namespace xqt;

namespace {

void writeFile(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

std::vector<std::string> files(const std::vector<DocumentItem>& items) {
    std::vector<std::string> n;
    for (const auto& i: items) {
        n.push_back(i.main().filename().string());
    }
    return n;
}

QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

/// The names of the rows of a library model, in order
std::vector<std::string> rowNames(const LibraryModel& m) {
    std::vector<std::string> n;
    for (int i = 0; i < m.count(); ++i) {
        n.push_back(m.data(m.index(i), LibraryModel::NameRole).toString().toStdString());
    }
    return n;
}

class LibraryFilterTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
    }
    /// A folder with one file of every kind (the PDF and images are not real ones: only names count here).
    void fillMixedFolder() {
        writeFile(root / "notes.xopp", "x");
        writeFile(root / "paper.pdf", "%PDF");
        writeFile(root / "lecture.pdf", "%PDF");
        writeFile(root / "lecture.xopp", "x");
        writeFile(root / "readme.md", "# Readme\n");
        writeFile(root / "board.png", "png");
        writeFile(root / "script.py", "print('hi')\n");
        writeFile(root / "thesis.tex", "\\documentclass{article}\n");
        writeFile(root / "Makefile", "all:\n");
        writeFile(root / "report.docx", "PK");
        writeFile(root / "data.xlsx", "PK");
        // Never shown
        writeFile(root / ".hidden.txt", "x");
        writeFile(root / "script.py~", "x");
        writeFile(root / "notes.xopp.bg.pdf", "%PDF");
        writeFile(root / "notes.xopp.bg_1.png", "png");
        writeFile(root / ".xournal_library" / "notes.pack", "x");
        writeFile(root / "Thumbs.db", "x");
        fs::create_directories(root / "Sub");
    }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

TEST_F(LibraryFilterTest, textAndOtherFilesAreListedOnlyWhenAsked) {
    fillMixedFolder();
    const auto docs = DocumentFiles::scan(root);
    EXPECT_EQ(files(docs.items),
              (std::vector<std::string>{"board.png", "lecture.xopp", "notes.xopp", "paper.pdf", "readme.md"}))
            << "documents only, as before";

    const auto text = DocumentFiles::scan(root, DocumentFiles::TextFiles);
    EXPECT_EQ(text.items.size(), docs.items.size() + 3) << "script.py, thesis.tex, Makefile";

    const auto all = DocumentFiles::scan(root, DocumentFiles::AllFiles);
    ASSERT_EQ(all.folders.size(), 1u);
    std::map<std::string, DocumentItem> byFile;
    for (const auto& item: all.items) {
        byFile[item.main().filename().string()] = item;
    }
    EXPECT_EQ(all.items.size(), 10u);
    for (const char* never: {".hidden.txt", "script.py~", "notes.xopp.bg.pdf", "notes.xopp.bg_1.png", "Thumbs.db"}) {
        EXPECT_EQ(byFile.count(never), 0u) << never;
    }
    for (const char* text: {"script.py", "thesis.tex", "Makefile"}) {
        ASSERT_EQ(byFile.count(text), 1u) << text;
        EXPECT_EQ(byFile[text].kind(), DocumentItem::Kind::Text) << text;
        EXPECT_STREQ(byFile[text].kindName(), "text");
        EXPECT_EQ(byFile[text].name(), text) << "known by its whole file name";
    }
    for (const char* other: {"report.docx", "data.xlsx"}) {
        ASSERT_EQ(byFile.count(other), 1u) << other;
        EXPECT_EQ(byFile[other].kind(), DocumentItem::Kind::Other) << other;
        EXPECT_STREQ(byFile[other].kindName(), "other");
        EXPECT_EQ(byFile[other].name(), other);
    }
    // The same item from its file, when asked for
    EXPECT_FALSE(DocumentFiles::itemOf(root / "report.docx").valid());
    EXPECT_EQ(DocumentFiles::itemOf(root / "report.docx", DocumentFiles::AllFiles), byFile["report.docx"]);
    EXPECT_FALSE(DocumentFiles::itemOf(root / "report.docx", DocumentFiles::TextFiles).valid());
    EXPECT_EQ(DocumentFiles::itemOf(root / "script.py", DocumentFiles::TextFiles), byFile["script.py"]);
    EXPECT_FALSE(DocumentFiles::itemOf(root / "notes.xopp.bg.pdf", DocumentFiles::AllFiles).valid());
    EXPECT_EQ(DocumentFiles::itemOf(root / "notes.xopp", DocumentFiles::AllFiles).xopp, root / "notes.xopp");
    EXPECT_EQ(DocumentFiles::scanRecursive(root, DocumentFiles::AllFiles).size(), 10u);
}

TEST_F(LibraryFilterTest, theFilterShowsDocumentsByDefaultAndPdfsWithNotesOnRequest) {
    fillMixedFolder();
    const auto all = DocumentFiles::scan(root, DocumentFiles::AllFiles).items;
    auto shown = [&](const ShowFilter& f) {
        std::vector<DocumentItem> s;
        for (const auto& item: all) {
            if (f.shows(item)) {
                s.push_back(item);
            }
        }
        return files(s);
    };
    ShowFilter f;
    EXPECT_TRUE(f.isDefault());
    EXPECT_EQ(f.include(), DocumentFiles::Documents);
    EXPECT_EQ(shown(f),
              (std::vector<std::string>{"board.png", "lecture.xopp", "notes.xopp", "paper.pdf", "readme.md"}));
    f.onlyPdfsWithNotes = true;
    EXPECT_EQ(shown(f), (std::vector<std::string>{"board.png", "lecture.xopp", "notes.xopp", "readme.md"}))
            << "a PDF with its .xopp stays, a PDF alone goes";
    f.pdfs = false;
    EXPECT_EQ(shown(f), (std::vector<std::string>{"board.png", "notes.xopp", "readme.md"}));
    f = ShowFilter();
    f.notes = f.markdown = f.images = false;
    f.text = true;
    EXPECT_EQ(f.include(), DocumentFiles::TextFiles);
    EXPECT_EQ(shown(f), (std::vector<std::string>{"lecture.xopp", "Makefile", "paper.pdf", "script.py", "thesis.tex"}));
    f.pdfs = f.text = false;
    f.other = true;
    EXPECT_EQ(f.include(), DocumentFiles::OtherFiles);
    EXPECT_EQ(shown(f), (std::vector<std::string>{"data.xlsx", "report.docx"}));
}

TEST_F(LibraryFilterTest, otherFilesAreRenamedMovedCopiedAndTrashedLikeDocuments) {
    writeFile(root / "report.docx", "PK report");
    writeFile(root / "report.pdf", "%PDF");  // another file with the same stem: not a pair
    writeFile(root / "Sub" / "report.docx", "PK other");
    const DocumentItem item = DocumentFiles::itemOf(root / "report.docx", DocumentFiles::AllFiles);
    ASSERT_TRUE(item.valid());

    // Renamed by its whole name; a PDF of the same stem does not block it, a file of the name does
    EXPECT_FALSE(DocumentFiles::rename(item, "report.pdf").ok);
    auto r = DocumentFiles::rename(item, "Report 2026.docx");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "Report 2026.docx"));
    EXPECT_FALSE(fs::exists(root / "report.docx"));
    EXPECT_EQ(r.item.other, root / "Report 2026.docx");
    ASSERT_EQ(r.moved.size(), 1u);
    EXPECT_EQ(r.moved[0].second, root / "Report 2026.docx");

    // Moved into a folder where its name is taken: "name (2).ext"
    writeFile(root / "Sub" / "Report 2026.docx", "x");
    r = DocumentFiles::move(r.item, root / "Sub");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.item.other, root / "Sub" / "Report 2026 (2).docx");
    EXPECT_TRUE(fs::exists(root / "Sub" / "Report 2026 (2).docx"));

    // Copied (imported) only when other files are included
    EXPECT_FALSE(DocumentFiles::import(root / "Sub" / "report.docx", root).ok);
    r = DocumentFiles::import(root / "Sub" / "report.docx", root, DocumentFiles::AllFiles);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.documents, 1);
    EXPECT_TRUE(fs::exists(root / "report.docx"));

    // A folder: its text and other files come along only when included
    writeFile(root / "src" / "notes.md", "# N\n");
    writeFile(root / "src" / "main.cpp", "int main() {}\n");
    writeFile(root / "src" / "slides.pptx", "PK");
    fs::create_directories(root / "a");
    fs::create_directories(root / "b");
    r = DocumentFiles::import(root / "src", root / "a");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(fs::exists(root / "a" / "src" / "notes.md"));
    EXPECT_FALSE(fs::exists(root / "a" / "src" / "main.cpp")) << "documents only, as before";
    EXPECT_FALSE(fs::exists(root / "a" / "src" / "slides.pptx"));
    r = DocumentFiles::import(root / "src", root / "b", DocumentFiles::AllFiles);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.documents, 3);
    EXPECT_TRUE(fs::exists(root / "b" / "src" / "main.cpp"));
    EXPECT_TRUE(fs::exists(root / "b" / "src" / "slides.pptx"));

    // Trash
    const DocumentItem copy = DocumentFiles::itemOf(root / "report.docx", DocumentFiles::AllFiles);
    EXPECT_EQ(DocumentFiles::filesOf(copy), std::vector<fs::path>{root / "report.docx"});
    r = DocumentFiles::trash(copy);
    if (r.ok) {  // (no trash in some test environments)
        EXPECT_FALSE(fs::exists(root / "report.docx"));
    }
}

TEST_F(LibraryFilterTest, theLibraryShowsWhatItsFilterSaysAndRemembersIt) {
    fillMixedFolder();
    writeFile(root / "Sub" / "sheet.pdf", "%PDF");
    writeFile(root / "Sub" / "budget.ods", "PK");
    {
        LibraryModel model;
        model.setLibrary(std::make_unique<Library>(root));
        EXPECT_FALSE(model.showFiltered());
        EXPECT_EQ(rowNames(model), (std::vector<std::string>{"Sub", "board", "lecture", "notes", "paper", "readme"}));
        const int sub = model.rowOf(qstr(root / "Sub"));
        EXPECT_EQ(model.data(model.index(sub), LibraryModel::ItemCountRole).toInt(), 1) << "the PDF, not the .ods";

        // Text and code files, and all other files
        model.setShown("text", true);
        model.setShown("other", true);
        EXPECT_TRUE(model.showFiltered());
        EXPECT_EQ(model.count(), 11);
        const QModelIndex docx = model.index(model.rowOf(qstr(root / "report.docx")));
        EXPECT_EQ(model.data(docx, LibraryModel::KindRole).toString(), "other");
        EXPECT_EQ(model.data(docx, LibraryModel::NameRole).toString(), "report.docx");
        EXPECT_EQ(model.data(docx, LibraryModel::SizeRole).toLongLong(), 2);
        EXPECT_EQ(model.data(docx, LibraryModel::FileIconRole).toString(), "xqt-file-doc");
        EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "data.xlsx"))), LibraryModel::FileIconRole).toString(),
                  "xqt-file-spreadsheet");
        EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "script.py"))), LibraryModel::KindRole).toString(),
                  "text");
        EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "Sub"))), LibraryModel::ItemCountRole).toInt(), 2);

        // Only PDFs with notes, no images
        model.setShown("other", false);
        model.setShown("onlyPdfsWithNotes", true);
        model.setShown("images", false);
        EXPECT_EQ(rowNames(model),
                  (std::vector<std::string>{"Sub", "lecture", "Makefile", "notes", "readme", "script.py", "thesis.tex"}));
        EXPECT_EQ(model.data(model.index(model.rowOf(qstr(root / "Sub"))), LibraryModel::ItemCountRole).toInt(), 0);
        // The flat list too
        model.setFlat(true);
        EXPECT_EQ(rowNames(model),
                  (std::vector<std::string>{"lecture", "Makefile", "notes", "readme", "script.py", "thesis.tex"}));
    }
    // Remembered for this library
    LibraryModel again;
    again.setLibrary(std::make_unique<Library>(root));
    const QVariantMap show = again.show();
    EXPECT_TRUE(show["text"].toBool());
    EXPECT_FALSE(show["other"].toBool());
    EXPECT_FALSE(show["images"].toBool());
    EXPECT_TRUE(show["onlyPdfsWithNotes"].toBool());
    EXPECT_EQ(again.count(), 7);
    again.resetShown();
    EXPECT_FALSE(again.showFiltered());
    EXPECT_EQ(again.count(), 6) << "Sub, board, lecture, notes, paper, readme";
    // Another library has its own
    fs::create_directories(root / "Sub" / "x");
    LibraryModel sub;
    sub.setLibrary(std::make_unique<Library>(root / "Sub"));
    EXPECT_FALSE(sub.showFiltered());
}

TEST_F(LibraryFilterTest, searchFindsTextFilesByTheirTextAndOtherFilesByTheirNames) {
    writeFile(root / "notes.md", "# Notes\n\nNothing here.\n");
    writeFile(root / "code" / "kalman.py", "def predict(state):\n    # the needle of the filter\n    return state\n");
    writeFile(root / "big.txt", std::string(LibraryIndex::TEXT_LIMIT + 10, 'a') + " needle");
    writeFile(root / "needle report.docx", "PK");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    model.setSearchQuery("needle");
    EXPECT_EQ(model.count(), 0) << "text and other files are not shown";

    model.setShown("text", true);  // the text files come into the index
    model.searchIndex()->waitForDone();
    model.setSearchQuery("");
    model.setSearchQuery("needle");
    ASSERT_EQ(model.count(), 1) << "a big text file is known by its name only";
    EXPECT_EQ(model.data(model.index(0), LibraryModel::PathRole).toString(), qstr(root / "code" / "kalman.py"));
    EXPECT_EQ(model.data(model.index(0), LibraryModel::HitsRole).toInt(), 1);
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::SnippetRole).toString().contains("needle of the filter"));
    EXPECT_TRUE(model.data(model.index(0), LibraryModel::HitPassageListRole).toList().isEmpty()) << "no snippet cards";
    EXPECT_EQ(model.data(model.index(0), LibraryModel::PageCountRole).toInt(), -1);
    model.setSearchQuery("big");
    ASSERT_EQ(model.count(), 1);
    EXPECT_EQ(model.data(model.index(0), LibraryModel::PathRole).toString(), qstr(root / "big.txt"));

    // Other files: by their names, never in the index
    model.setShown("other", true);
    model.setSearchQuery("needle");
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"needle report.docx", "kalman.py"}))
            << "found by its name, before the hits in the text";
    model.setNamesOnly(true);
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"needle report.docx"}));

    // Hidden again: out of the index
    model.setNamesOnly(false);
    model.setShown("text", false);
    model.searchIndex()->waitForDone();
    model.setSearchQuery("");
    model.setSearchQuery("predict");
    EXPECT_EQ(model.count(), 0);
}

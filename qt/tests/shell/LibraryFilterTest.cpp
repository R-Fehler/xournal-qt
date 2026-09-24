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

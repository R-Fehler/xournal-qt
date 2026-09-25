/*
 * xournal-qt: a text file edited (TextFile): what is written back is the file byte for byte where the text did not
 * change, whatever its line ends, byte order mark or last line; our own saves are told from changes by others.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <string>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "session/TextFile.h"

#include "../FailingWrites.h"

using namespace xqt;

namespace {
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void writeFile(const fs::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}

class TextFileTest: public ::testing::Test {
protected:
    fs::path file(const std::string& name, const std::string& bytes) {
        fs::path p = fs::path(dir.path().toStdString()) / name;
        writeFile(p, bytes);
        return p;
    }
    TextFile load(const fs::path& p) {
        TextFile t;
        std::string error;
        EXPECT_TRUE(t.load(p, TextFile::Kind::Markdown, error)) << error;
        return t;
    }
    /// The text with `from` replaced by `to` (once).
    static std::string edited(std::string text, const std::string& from, const std::string& to) {
        const size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << from;
        return text.replace(at, from.size(), to);
    }
    QTemporaryDir dir;
};
}  // namespace

TEST_F(TextFileTest, theTextIsNormalizedAndUnchangedTextGivesTheSameBytes) {
    for (const std::string bytes: {std::string("# Title\n\nText.\n"), std::string("# Title\r\n\r\nText.\r\n"),
                                   std::string("no newline at the end"), std::string("\xEF\xBB\xBF# BOM\r\nx\r\n"),
                                   std::string("mixed\r\nline\nends\r\n"), std::string(""), std::string("\n\n\n")}) {
        TextFile t = load(file("a.md", bytes));
        EXPECT_EQ(t.text().find('\r'), std::string::npos) << bytes;
        EXPECT_NE(t.text().rfind("\xEF\xBB\xBF", 0), 0u) << "no byte order mark in the text";
        EXPECT_EQ(t.encode(t.text()), bytes) << "saved unchanged: the same bytes";
        EXPECT_TRUE(t.editable());
    }
}

TEST_F(TextFileTest, aWindowsFileKeepsItsLineEndsAndNewLinesGetThem) {
    const std::string bytes = "# Title\r\n\r\nFirst paragraph.\r\n\r\nSecond paragraph.\r\n";
    TextFile t = load(file("win.md", bytes));
    EXPECT_TRUE(t.crlf());
    const std::string text = edited(t.text(), "First paragraph.", "First paragraph, longer.\nA new line.");
    EXPECT_EQ(t.encode(text),
              "# Title\r\n\r\nFirst paragraph, longer.\r\nA new line.\r\n\r\nSecond paragraph.\r\n");
}

TEST_F(TextFileTest, aFileWithoutNewlineAtTheEndStaysSoAndABomStays) {
    TextFile t = load(file("end.md", "\xEF\xBB\xBF" "one\ntwo\nthree"));
    EXPECT_TRUE(t.hasBom());
    EXPECT_EQ(t.text(), "one\ntwo\nthree");
    EXPECT_EQ(t.encode(edited(t.text(), "one", "ONE")), "\xEF\xBB\xBF" "ONE\ntwo\nthree");
    EXPECT_EQ(t.encode(edited(t.text(), "two", "2")), "\xEF\xBB\xBF" "one\n2\nthree");
    EXPECT_EQ(t.encode(t.text() + "!"), "\xEF\xBB\xBF" "one\ntwo\nthree!");
    EXPECT_EQ(t.encode("x" + t.text()), "\xEF\xBB\xBF" "xone\ntwo\nthree");
}

TEST_F(TextFileTest, mixedLineEndsStayWhereTheTextDidNotChange) {
    // Mostly "\n", one "\r\n" line at the start and one at the end
    const std::string bytes = "a\r\nb\nc\nd\ne\r\n";
    TextFile t = load(file("mixed.md", bytes));
    EXPECT_FALSE(t.crlf());
    EXPECT_EQ(t.encode(edited(t.text(), "c", "C\nC2")), "a\r\nb\nC\nC2\nd\ne\r\n");
    // Deleting a line, and inserting at the very start and end
    EXPECT_EQ(t.encode(edited(t.text(), "c\n", "")), "a\r\nb\nd\ne\r\n");
    EXPECT_EQ(t.encode("0\n" + t.text()), "0\na\r\nb\nc\nd\ne\r\n");
    EXPECT_EQ(t.encode(t.text() + "f\n"), "a\r\nb\nc\nd\ne\r\nf\n");
}

TEST_F(TextFileTest, savingWritesAtomicallyAndOwnSavesAreNotChangesOnDisk) {
    const fs::path p = file("s.md", "# A\r\n\r\nB\r\n");
    TextFile t = load(p);
    std::string error;
    ASSERT_TRUE(t.save(edited(t.text(), "B", "B!"), error)) << error;
    EXPECT_EQ(readFile(p), "# A\r\n\r\nB!\r\n");
    EXPECT_FALSE(t.changedOnDisk()) << "our own save";
    EXPECT_EQ(t.text(), "# A\n\nB!\n");
    // Another program writes it
    writeFile(p, "# A\r\n\r\nB?\r\nMore.\r\n");
    std::string now;
    EXPECT_TRUE(t.changedOnDisk(&now));
    EXPECT_EQ(now, "# A\r\n\r\nB?\r\nMore.\r\n");
    // Touched without a change: not a change
    TextFile u = load(p);
    writeFile(p, "# A\r\n\r\nB?\r\nMore.\r\n");
    EXPECT_FALSE(u.changedOnDisk());
    // Nothing is left beside the file (QSaveFile's temporary file is renamed over it)
    size_t files = 0;
    for ([[maybe_unused]] const auto& e: fs::directory_iterator(p.parent_path())) {
        ++files;
    }
    EXPECT_EQ(files, 1u);
}

#ifndef _WIN32
// A save that cannot be written whole (a full disk: here a file size limit) fails and leaves the file as it was. Qt
// 6.7's QSaveFile::commit() alone did not notice the short write and put the cut file in place.
TEST_F(TextFileTest, aSaveThatDoesNotFitLeavesTheFileAsItWas) {
    const fs::path p = file("full.md", "# Kept\n");
    std::string error;
    bool ok = true;
    {
        test::FileSizeLimit full(4);
        ok = TextFile::writeAtomically(p, std::string(1000, 'x'), error);  // (less than the write buffer)
    }
    EXPECT_FALSE(ok);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(readFile(p), "# Kept\n");
    size_t files = 0;
    for ([[maybe_unused]] const auto& e: fs::directory_iterator(p.parent_path())) {
        ++files;
    }
    EXPECT_EQ(files, 1u) << "no temporary file left";
}
#endif

TEST_F(TextFileTest, notUtf8OrTooBigIsNotEditable) {
    TextFile latin = load(file("latin.txt", "caf\xE9\n"));
    EXPECT_FALSE(latin.isUtf8());
    EXPECT_FALSE(latin.editable());
    TextFile big = load(file("big.md", std::string(TextFile::MAX_EDIT_BYTES + 10, 'a')));
    EXPECT_TRUE(big.isTooBig());
    EXPECT_FALSE(big.editable());
    TextFile utf8 = load(file("u.md", "Grüße, 日本, 🙂\n"));
    EXPECT_TRUE(utf8.editable());
}

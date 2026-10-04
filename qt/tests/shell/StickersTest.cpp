/*
 * xournal-qt: the sticker sets on disk and the picker's list (qt/docs/stickers.md): what is a sticker, folders, free
 * names, the own order in its hidden file, last used, and the list's scope, folder, search, sorts and changes.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <set>
#include <string>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "shell/Stickers.h"

using namespace xqt;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& file, const std::string& content = "x") {
    fs::create_directories(file.parent_path());
    std::ofstream(file) << content;
}

std::vector<std::string> names(const StickersModel& m) {
    std::vector<std::string> out;
    for (int i = 0; i < m.rowCount(); ++i) {
        out.push_back(m.data(m.index(i), StickersModel::NameRole).toString().toStdString());
    }
    return out;
}

class StickersTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        library = root / "Library";
        set = stickers::librarySet(library);
        stickers::setAppSet(root / "app-stickers");
        config = root / "config";
    }
    void TearDown() override { stickers::setAppSet({}); }
    QTemporaryDir tmp;
    fs::path root, library, set, config;
};

TEST_F(StickersTest, whatIsASticker) {
    EXPECT_EQ(set, library / "Stickers");
    touch(set / "Arrow.xopp");
    touch(set / "Logo.PNG");
    touch(set / "Lecture 3" / "Formula.xopp");
    touch(set / "Lecture 3" / "Deep" / "Graph.jpg");
    touch(set / "Photo.xopp");
    touch(set / "Photo.jpg");                       // (the background of Photo.xopp: part of it)
    touch(set / ".hidden.xopp");                    // (hidden: an autosave)
    touch(set / "Arrow.xopp~");                     // (a backup)
    touch(set / "notes.txt");
    touch(set / stickers::ORDER_FILE, "{}");
    touch(set / ".xournal_library" / "cache.xopp");  // (a hidden folder)
    auto entries = stickers::list(set);
    std::set<std::string> found;
    for (const auto& e: entries) {
        found.insert(e.folder + "|" + e.name + (e.picture ? "|picture" : ""));
    }
    EXPECT_EQ(found, (std::set<std::string>{"|Arrow", "|Logo|picture", "Lecture 3|Formula", "Lecture 3/Deep|Graph|picture",
                                            "|Photo"}));
    EXPECT_EQ(stickers::folders(set), (QStringList{"Lecture 3", "Lecture 3/Deep"}));
    EXPECT_TRUE(stickers::list(root / "missing").empty());
}

TEST_F(StickersTest, aFreeName) {
    touch(set / "Arrow.xopp");
    touch(set / "Arrow (2).png");
    EXPECT_EQ(stickers::uniqueTarget(set, "Arrow"), set / "Arrow (3).xopp");
    EXPECT_EQ(stickers::uniqueTarget(set, "Box"), set / "Box.xopp");
    EXPECT_EQ(stickers::uniqueTarget(set, ""), set / "Sticker.xopp");
}

TEST_F(StickersTest, theOwnOrderIsAHiddenFileInTheFolder) {
    for (const char* n: {"A.xopp", "B.xopp", "C.xopp", "D.xopp"}) {
        touch(set / n);
    }
    ASSERT_TRUE(stickers::writeOrder(set, {"C.xopp", "gone.xopp", "A.xopp"}));
    EXPECT_TRUE(fs::exists(set / ".sticker-order.json"));
    auto order = stickers::ordered(stickers::list(set));
    std::vector<std::string> got;
    for (const auto& e: order) {
        got.push_back(e.name);
    }
    EXPECT_EQ(got, (std::vector<std::string>{"C", "A", "B", "D"}));  // (the others after them, by name)

    // D one place earlier: the whole order is written
    EXPECT_TRUE(stickers::moveInOrder(set / "D.xopp", -1));
    EXPECT_EQ(stickers::readOrder(set), (std::vector<std::string>{"C.xopp", "A.xopp", "D.xopp", "B.xopp"}));
    EXPECT_FALSE(stickers::moveInOrder(set / "C.xopp", -1));  // (first already)
}

TEST_F(StickersTest, theListOfAScope) {
    touch(set / "Arrow.xopp");
    touch(set / "Box.xopp");
    touch(set / "Lecture 3" / "Formula.xopp");
    touch(stickers::appSet() / "Everywhere.xopp");
    StickersModel m;
    m.setLibrary(library, config);
    m.setSort("name");
    EXPECT_EQ(m.scope(), "library");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Arrow", "Box", "Formula"}));
    EXPECT_EQ(m.folderList(), QStringList{"Lecture 3"});
    m.setFolder("Lecture 3");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Formula"}));
    m.setFolder("");
    m.setSearch("ar");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Arrow"}));
    m.setSearch("lecture");  // (the folder's name too)
    EXPECT_EQ(names(m), (std::vector<std::string>{"Formula"}));
    m.setSearch("");
    m.setScope("app");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Everywhere"}));
    EXPECT_EQ(m.scopeOf(QString::fromStdString((set / "Box.xopp").string())), "library");
    EXPECT_EQ(m.scopeOf(QString::fromStdString((stickers::appSet() / "Everywhere.xopp").string())), "app");
    EXPECT_EQ(m.scopeOf(QString::fromStdString((root / "x.xopp").string())), "");

    // No library: the app-wide set only
    StickersModel alone;
    alone.setLibrary({}, config);
    EXPECT_EQ(alone.scope(), "app");
    alone.setScope("library");
    EXPECT_EQ(alone.scope(), "app");
    EXPECT_FALSE(alone.hasLibrary());
}

TEST_F(StickersTest, lastUsedFirstAndRemembered) {
    touch(set / "Arrow.xopp");
    touch(set / "Box.xopp");
    touch(set / "Circle.xopp");
    {
        StickersModel m;
        m.setLibrary(library, config);
        EXPECT_EQ(m.sort(), "used");
        m.markUsed(QString::fromStdString((set / "Box.xopp").string()));
        EXPECT_EQ(names(m).front(), "Box");
    }
    EXPECT_TRUE(fs::exists(config / "stickers.json"));
    StickersModel again;
    again.setLibrary(library, config);
    EXPECT_EQ(names(again).front(), "Box");
}

TEST_F(StickersTest, changesFollowTheOrderAndTheLastUses) {
    touch(set / "A.xopp");
    touch(set / "B.xopp");
    touch(set / "C.xopp");
    StickersModel m;
    m.setLibrary(library, config);
    const auto path = [&](const char* n) { return QString::fromStdString((set / n).string()); };
    // Move down: the list shows the own order then
    EXPECT_TRUE(m.moveBy(path("A.xopp"), 1));
    EXPECT_EQ(m.sort(), "own");
    EXPECT_EQ(names(m), (std::vector<std::string>{"B", "A", "C"}));
    // A rename keeps its place in the order
    m.markUsed(path("A.xopp"));
    EXPECT_TRUE(m.rename(path("A.xopp"), "Arrow"));
    EXPECT_TRUE(fs::exists(set / "Arrow.xopp"));
    EXPECT_EQ(names(m), (std::vector<std::string>{"B", "Arrow", "C"}));
    EXPECT_FALSE(m.rename(path("B.xopp"), "C"));     // (taken)
    EXPECT_FALSE(m.rename(path("B.xopp"), "a/b"));   // (not a name)
    // Into a folder (made), then a copy in the app-wide set's folder of that name
    EXPECT_TRUE(m.moveToFolder(path("C.xopp"), "Topic"));
    EXPECT_TRUE(fs::exists(set / "Topic" / "C.xopp"));
    EXPECT_EQ(m.folderList(), QStringList{"Topic"});
    EXPECT_TRUE(m.copyToOtherSet(QString::fromStdString((set / "Topic" / "C.xopp").string())));
    EXPECT_TRUE(fs::exists(stickers::appSet() / "Topic" / "C.xopp"));
    // Into another library's Stickers folder
    EXPECT_TRUE(m.copyToLibrary(path("B.xopp"), QString::fromStdString((root / "Other").string())));
    EXPECT_TRUE(fs::exists(root / "Other" / "Stickers" / "B.xopp"));
    // Deleted: gone from the folder and its order
    EXPECT_TRUE(m.remove(path("B.xopp")));
    EXPECT_FALSE(fs::exists(set / "B.xopp"));
    EXPECT_EQ(stickers::readOrder(set), (std::vector<std::string>{"Arrow.xopp"}));
    EXPECT_EQ(names(m), (std::vector<std::string>{"Arrow", "C"}));
}

}  // namespace

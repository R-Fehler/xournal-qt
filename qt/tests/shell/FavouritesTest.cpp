/*
 * xournal-qt: favourites and the library's bookmarks (qt/docs/bookmarks.md): stars kept beside the documents (never in
 * them) that follow renames and moves in the app, the Favourites filter of the library (with the "Show" filter and the
 * search), the bookmarks read into the index's "notes" pack (read back without opening the documents, read again when
 * a file changes, found by the search), and the Bookmarks view.
 *
 * @license GNU GPLv2 or later
 */
#include <fstream>
#include <functional>
#include <iterator>
#include <map>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/DocumentHandler.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/DocumentFiles.h"
#include "shell/DocumentPlaces.h"
#include "shell/Library.h"
#include "shell/LibraryBookmarks.h"
#include "shell/LibraryModel.h"

using namespace xqt;

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void waitFor(const std::function<bool()>& cond, int ms = 5000) {
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

/// A .xopp of `pages` blank pages with these bookmarks (page -> label).
void makeNotes(const fs::path& xopp, size_t pages, const std::map<size_t, std::string>& marks = {}) {
    static DocumentHandler handler;
    fs::create_directories(xopp.parent_path());
    Document doc(&handler);
    for (size_t i = 0; i < pages; ++i) {
        auto p = std::make_shared<XojPage>(595, 842);
        if (auto it = marks.find(i); it != marks.end()) {
            p->setBookmark(it->second);
        }
        doc.addPage(p);
    }
    ASSERT_TRUE(DocumentSession::writeDocument(doc, xopp).ok);
}

std::vector<std::string> rowNames(const LibraryModel& m) {
    std::vector<std::string> n;
    for (int i = 0; i < m.count(); ++i) {
        n.push_back(m.data(m.index(i), LibraryModel::NameRole).toString().toStdString());
    }
    return n;
}

class FavouritesTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString()) / "Library";
        fs::create_directories(root);
        DocumentPlaces::setOutsideFile(fs::path(tmp.path().toStdString()) / "outside-places.json");
    }
    void TearDown() override { DocumentPlaces::setLibrary({}, {}); }
    QTemporaryDir tmp;
    fs::path root;
};
}  // namespace

// A star is kept beside the document (the library's config folder; others by their whole path in the cache), never
// written into it, and follows it when the app renames or moves it
TEST_F(FavouritesTest, aStarIsKeptBesideTheDocumentAndFollowsIt) {
    makeNotes(root / "alpha.xopp", 1);
    makeNotes(root / "beta.xopp", 1);
    fs::create_directories(root / "Sub");
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    const std::string before = bytesOf(root / "alpha.xopp");
    const auto modified = fs::last_write_time(root / "alpha.xopp");
    model.setFavourite(qstr(root / "alpha.xopp"), true);
    EXPECT_TRUE(model.isFavourite(qstr(root / "alpha.xopp")));
    EXPECT_FALSE(model.isFavourite(qstr(root / "beta.xopp")));
    EXPECT_TRUE(model.data(model.index(model.rowOf(qstr(root / "alpha.xopp"))), LibraryModel::FavouriteRole).toBool());
    EXPECT_EQ(bytesOf(root / "alpha.xopp"), before) << "the file is not touched";
    EXPECT_EQ(fs::last_write_time(root / "alpha.xopp"), modified);
    EXPECT_TRUE(fs::exists(model.library()->placesFile()));

    // Renamed and moved in the app: the star follows
    ASSERT_TRUE(model.rename(model.rowOf(qstr(root / "alpha.xopp")), "gamma"));
    EXPECT_TRUE(DocumentPlaces::favourite(root / "gamma.xopp"));
    EXPECT_FALSE(DocumentPlaces::favourite(root / "alpha.xopp"));
    ASSERT_TRUE(model.moveTo(model.rowOf(qstr(root / "gamma.xopp")), "Sub"));
    EXPECT_TRUE(DocumentPlaces::favourite(root / "Sub" / "gamma.xopp"));
    // Moved by another program: lost (documented)
    fs::rename(root / "Sub" / "gamma.xopp", root / "gamma.xopp");
    EXPECT_FALSE(DocumentPlaces::favourite(root / "gamma.xopp"));

    // A document outside the library: by its whole path
    const fs::path outside = fs::path(tmp.path().toStdString()) / "elsewhere" / "paper.xopp";
    makeNotes(outside, 1);
    model.setFavourite(qstr(outside), true);
    EXPECT_TRUE(DocumentPlaces::favourite(outside));
    EXPECT_TRUE(fs::exists(fs::path(tmp.path().toStdString()) / "outside-places.json"));
    model.setFavourite(qstr(outside), false);
    EXPECT_FALSE(DocumentPlaces::favourite(outside));
}

// The Favourites filter: the starred documents of the whole library, without folders; with the kinds shown and the
// search
TEST_F(FavouritesTest, theFavouritesFilterWorksWithTheKindsAndTheSearch) {
    makeNotes(root / "alpha.xopp", 1);
    makeNotes(root / "beta.xopp", 1);
    makeNotes(root / "Sub" / "gamma.xopp", 1);
    makeNotes(root / "Sub" / "delta.xopp", 1);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    model.setFavourite(qstr(root / "beta.xopp"), true);
    model.setFavourite(qstr(root / "Sub" / "gamma.xopp"), true);
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"Sub", "alpha", "beta"}));
    model.setFavouritesOnly(true);
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"beta", "gamma"})) << "all of the library's, no folders";
    model.setFavourite(qstr(root / "beta.xopp"), false);
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"gamma"})) << "unstarred: gone at once";
    model.setFavourite(qstr(root / "beta.xopp"), true);
    model.setSearchQuery("gam");
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"gamma"}));
    model.setSearchQuery("");
    model.setShown("notes", false);
    EXPECT_TRUE(rowNames(model).empty()) << "notes are not shown";
    model.resetShown();
    model.setFavouritesOnly(false);
    EXPECT_EQ(rowNames(model), (std::vector<std::string>{"Sub", "alpha", "beta"}));
}

// The index reads the bookmarks into "notes": another index has them without opening a document; a changed file is
// read again; the search finds a document by a bookmark's label
TEST_F(FavouritesTest, theIndexCachesTheBookmarks) {
    makeNotes(root / "lecture.xopp", 4, {{1, "Exercises"}, {3, ""}});
    makeNotes(root / "Sub" / "plain.xopp", 2);
    {
        LibraryIndex index(root);
        index.update(DocumentFiles::scanRecursive(root));
        index.waitForDone();
        const auto marks = index.bookmarks();
        ASSERT_EQ(marks.size(), 2u);
        EXPECT_EQ(marks[0].file, root / "lecture.xopp");
        EXPECT_EQ(marks[0].page, 1);
        EXPECT_EQ(marks[0].label, "Exercises");
        EXPECT_EQ(marks[1].page, 3);
        EXPECT_EQ(marks[1].label, "");
        EXPECT_GT(marks[0].aspect, 1.0);
        const auto hits = index.search("exercises");
        ASSERT_EQ(hits.size(), 1u);
        EXPECT_EQ(hits[0].firstPage, 1);
        index.flush();
    }
    LibraryIndex again(root);
    const quint64 changes = again.bookmarkChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    EXPECT_EQ(again.documentsRead(), 0) << "from the packs";
    EXPECT_EQ(again.bookmarks().size(), 2u);
    EXPECT_NE(again.bookmarkChanges(), changes);
    // The file changes (saved elsewhere): read again
    makeNotes(root / "lecture.xopp", 4, {{2, "Solutions"}});
    fs::last_write_time(root / "lecture.xopp", fs::last_write_time(root / "lecture.xopp") + std::chrono::seconds(5));
    const quint64 before = again.bookmarkChanges();
    again.update(DocumentFiles::scanRecursive(root));
    again.waitForDone();
    ASSERT_EQ(again.bookmarks().size(), 1u);
    EXPECT_EQ(again.bookmarks()[0].label, "Solutions");
    EXPECT_NE(again.bookmarkChanges(), before);
    EXPECT_TRUE(again.search("exercises").empty());
}

// The Bookmarks view: the bookmarked pages grouped by document (by name), "Page N" for the automatic label; it follows
// the search text (labels and names) and the Favourites filter
TEST_F(FavouritesTest, theBookmarksViewGroupsThemByDocument) {
    makeNotes(root / "zeta.xopp", 3, {{2, "Proof"}, {0, "Start"}});
    makeNotes(root / "Sub" / "alpha.xopp", 2, {{1, ""}});
    makeNotes(root / "none.xopp", 2);
    LibraryModel model;
    model.setLibrary(std::make_unique<Library>(root));
    model.searchIndex()->waitForDone();
    LibraryBookmarksModel view(&model);
    EXPECT_EQ(view.count(), 0) << "made only while shown";
    view.setActive(true);
    ASSERT_EQ(view.count(), 2);
    EXPECT_EQ(view.total(), 3);
    auto marksOf = [&](int row) {
        std::vector<std::pair<int, std::string>> out;
        for (const auto& m: view.data(view.index(row), LibraryBookmarksModel::MarksRole).toList()) {
            out.emplace_back(m.toMap().value("page").toInt(), m.toMap().value("label").toString().toStdString());
        }
        return out;
    };
    EXPECT_EQ(view.data(view.index(0), LibraryBookmarksModel::NameRole).toString(), "alpha");
    EXPECT_EQ(view.data(view.index(0), LibraryBookmarksModel::FolderRole).toString(), "Sub");
    EXPECT_EQ(marksOf(0), (std::vector<std::pair<int, std::string>>{{1, "Page 2"}}));
    EXPECT_EQ(view.data(view.index(1), LibraryBookmarksModel::PathRole).toString(), qstr(root / "zeta.xopp"));
    EXPECT_EQ(marksOf(1), (std::vector<std::pair<int, std::string>>{{0, "Start"}, {2, "Proof"}}));
    EXPECT_TRUE(view.data(view.index(1), LibraryBookmarksModel::PageBaseRole).toString().startsWith("image://hitpage/"));

    model.setSearchQuery("proof");
    ASSERT_EQ(view.count(), 1);
    EXPECT_EQ(marksOf(0), (std::vector<std::pair<int, std::string>>{{2, "Proof"}}));
    model.setSearchQuery("");
    model.setFavourite(qstr(root / "Sub" / "alpha.xopp"), true);
    model.setFavouritesOnly(true);
    ASSERT_EQ(view.count(), 1);
    EXPECT_EQ(view.data(view.index(0), LibraryBookmarksModel::NameRole).toString(), "alpha");
    model.setFavouritesOnly(false);

    // A document saved with a new bookmark: the view follows the index
    makeNotes(root / "none.xopp", 2, {{0, "New"}});
    fs::last_write_time(root / "none.xopp", fs::last_write_time(root / "none.xopp") + std::chrono::seconds(5));
    model.refresh();
    waitFor([&] { return view.count() == 3; });
    EXPECT_EQ(view.count(), 3);
}

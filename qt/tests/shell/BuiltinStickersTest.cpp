/*
 * xournal-qt: the built-in sticker collections (qt/docs/features/stickers.md, "Built in"; qt/resources/stickers): every
 * bundled sticker opens with the app's loader and with upstream's LoadHandler (Xournal++ opens it), is a sticker's page
 * (plain paper, the content at the margin) whose content is pasted as one group, has its English and German names and
 * a sensible size; the picker's "Built in" scope (collections as folders, their titles, the search in both languages,
 * the names in the app's language), read-only, "Copy to my stickers", hidden collections kept in the settings, and the
 * pen's colour.
 *
 * @license GNU GPLv2 or later
 */
#include <set>
#include <string>

#include <QLocale>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "control/xojfile/LoadHandler.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/StickerFile.h"
#include "shell/Stickers.h"

using namespace xqt;
namespace fs = std::filesystem;

namespace {

constexpr double MM = 72.0 / 25.4;

std::vector<std::string> names(const StickersModel& m) {
    std::vector<std::string> out;
    for (int i = 0; i < m.rowCount(); ++i) {
        out.push_back(m.data(m.index(i), StickersModel::NameRole).toString().toStdString());
    }
    return out;
}

bool has(const std::vector<std::string>& list, const std::string& name) {
    return std::find(list.begin(), list.end(), name) != list.end();
}

class BuiltinStickersTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        root = fs::path(tmp.path().toStdString());
        library = root / "Library";
        builtin = fs::path(XQT_BUILD_RESOURCE_DIR) / "stickers";
        stickers::setBuiltinSet(builtin);
        stickers::setAppSet(root / "app-stickers");
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    }
    void TearDown() override {
        stickers::setBuiltinSet({});
        stickers::setAppSet({});
        QLocale::setDefault(QLocale::system());
    }
    QTemporaryDir tmp;
    fs::path root, library, builtin;
};

}  // namespace

// The four collections the author asked for, and the IEC gates beside the distinctive shapes, in their order
TEST_F(BuiltinStickersTest, theCollections) {
    const auto all = stickers::collections(builtin);
    std::vector<std::string> ids;
    std::map<std::string, size_t> counts;
    for (const auto& c: all) {
        ids.push_back(c.id);
        counts[c.id] = c.stickers.size();
        EXPECT_FALSE(c.titleEn.empty());
        EXPECT_FALSE(c.titleDe.empty());
        EXPECT_NE(c.titleEn, c.titleDe) << c.id;
    }
    EXPECT_EQ(ids, (std::vector<std::string>{"circuits-iec", "logic-gates", "logic-gates-iec", "solids", "lab"}));
    EXPECT_GE(counts["circuits-iec"], 20u);
    EXPECT_EQ(counts["logic-gates"], 7u);
    EXPECT_EQ(counts["logic-gates-iec"], 7u);
    EXPECT_GE(counts["solids"], 8u);
    EXPECT_GE(counts["lab"], 10u);
    EXPECT_TRUE(fs::exists(builtin / "LICENCE.md"));
    EXPECT_FALSE(fs::exists(builtin / "generate.py")) << "the generator stays in the sources";
}

// Every .xopp of a collection is in its names.json, with an English and a German name
TEST_F(BuiltinStickersTest, everyStickerHasItsEnglishAndGermanName) {
    for (const auto& c: stickers::collections(builtin)) {
        std::set<std::string> listed;
        for (const auto& s: c.stickers) {
            listed.insert(s.file);
            ASSERT_FALSE(s.en.empty()) << s.file;
            ASSERT_FALSE(s.de.empty()) << s.file;
            EXPECT_FALSE(s.en.front().empty());
            EXPECT_FALSE(s.de.front().empty());
        }
        for (const auto& entry: fs::directory_iterator(builtin / c.id)) {
            if (entry.path().extension() == ".xopp") {
                EXPECT_TRUE(listed.count(entry.path().filename().string())) << c.id << "/" << entry.path().filename();
            }
        }
    }
    // A few by name
    const auto names = [&](const std::string& id, const std::string& file) {
        for (const auto& c: stickers::collections(builtin)) {
            for (const auto& s: c.stickers) {
                if (c.id == id && s.file == file) {
                    return std::make_pair(s.en.front(), s.de.front());
                }
            }
        }
        return std::make_pair(std::string(), std::string());
    };
    EXPECT_EQ(names("circuits-iec", "Resistor.xopp"), std::make_pair(std::string("Resistor"), std::string("Widerstand")));
    EXPECT_EQ(names("solids", "Cube.xopp"), std::make_pair(std::string("Cube"), std::string("Würfel")));
    EXPECT_EQ(names("lab", "Beaker.xopp"), std::make_pair(std::string("Beaker"), std::string("Becherglas")));
    EXPECT_EQ(names("logic-gates", "AND.xopp").second, "UND");
}

// Each one: our loader and upstream's open it without a warning; one page, plain paper, its ink in one layer starting
// at the margin; read as a sticker it is one group; its size is sensible
TEST_F(BuiltinStickersTest, everyStickerOpensInBothLoadersAndIsOneGroup) {
    size_t checked = 0;
    for (const auto& c: stickers::collections(builtin)) {
        for (const auto& s: c.stickers) {
            const fs::path file = builtin / c.id / s.file;
            SCOPED_TRACE(file.string());
            auto ours = DocumentSession::loadFile(file);
            ASSERT_TRUE(ours.document) << ours.error;
            ASSERT_EQ(ours.document->getPageCount(), 1u);
            const PageRef page = ours.document->getPage(0);
            EXPECT_TRUE(page->getBackgroundType().format == PageTypeFormat::Plain);
            ASSERT_EQ(page->getLayers().size(), 1u);
            std::optional<xoj::util::Rectangle<double>> bounds;
            for (const Element* e: page->getLayers()[0]->getElementsView()) {
                EXPECT_EQ(e->getType(), ELEMENT_STROKE);
                EXPECT_EQ(e->getColor(), Colors::black);
                const auto& b = e->getBoundingBox();
                if (bounds) {
                    bounds->unite(b);
                } else {
                    bounds = b;
                }
            }
            ASSERT_TRUE(bounds);
            EXPECT_NEAR(bounds->x, stickers::MARGIN, 0.6);
            EXPECT_NEAR(bounds->y, stickers::MARGIN, 0.6);
            EXPECT_NEAR(page->getWidth(), bounds->width + 2 * stickers::MARGIN, 0.6);
            EXPECT_NEAR(page->getHeight(), bounds->height + 2 * stickers::MARGIN, 0.6);
            // Sizes: a circuit symbol or a gate 6–25 mm long (the ground: 7), a solid 22–40 mm, glassware 12–65 mm (benzene: 14)
            const double longest = std::max(bounds->width, bounds->height) / MM;
            const auto [low, high] = c.id == "solids"                 ? std::make_pair(22.0, 40.0)
                                     : c.id == "lab"                  ? std::make_pair(12.0, 65.0)
                                                                       : std::make_pair(6.0, 25.0);
            EXPECT_GE(longest, low);
            EXPECT_LE(longest, high);

            std::vector<std::string> warnings;
            LoadHandler handler(&warnings);
            auto upstream = handler.loadDocument(file);
            ASSERT_TRUE(upstream) << (warnings.empty() ? std::string() : warnings.front());
            EXPECT_TRUE(warnings.empty()) << warnings.front();
            EXPECT_EQ(upstream->getPageCount(), 1u);

            std::string error;
            auto content = stickers::read(file, &error);
            ASSERT_TRUE(content) << error;
            ASSERT_FALSE(content->elements.empty());
            // (one stroke, as the round-bottom flask, needs no group)
            const auto group = content->elements.front()->getGroup();
            EXPECT_TRUE(group != 0 || content->elements.size() == 1);
            for (const auto& e: content->elements) {
                EXPECT_EQ(e->getGroup(), group) << "one group";
            }
            EXPECT_TRUE(content->notes.empty());
            ++checked;
        }
    }
    EXPECT_GE(checked, 50u);
    // A resistor about 15 mm long (its leads too)
    auto resistor = stickers::read(builtin / "circuits-iec" / "Resistor.xopp");
    ASSERT_TRUE(resistor);
    EXPECT_NEAR(resistor->bounds.width / MM, 15, 1.0);
}

// In the pen's colour: the ink takes it, pictures would not
TEST_F(BuiltinStickersTest, recolour) {
    auto content = stickers::read(builtin / "circuits-iec" / "Ammeter.xopp");
    ASSERT_TRUE(content);
    stickers::recolour(*content, Color(0xff0000U));
    for (const auto& e: content->elements) {
        EXPECT_EQ(e->getColor(), Color(0xff0000U));
    }
}

// --- the picker's list ------------------------------------------------------------------------------------------------

TEST_F(BuiltinStickersTest, theBuiltInScope) {
    StickersModel m;
    m.setLibrary(library, root / "config");
    m.setScope("builtin");
    EXPECT_EQ(m.scope(), "builtin");
    EXPECT_EQ(m.folderList(), (QStringList{"circuits-iec", "logic-gates", "logic-gates-iec", "solids", "lab"}));
    EXPECT_EQ(m.folderTitle("solids"), "3D solids");
    EXPECT_EQ(m.folderTitle("Lecture 3"), "Lecture 3") << "a folder of the user's own is its name";
    EXPECT_FALSE(m.setEmpty());
    // In the set's own order while none was used: the first circuit symbol first
    EXPECT_EQ(names(m).front(), "Resistor");
    m.setFolder("solids");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Cube", "Cuboid", "Cylinder", "Cone", "Sphere", "Square pyramid",
                                                  "Triangular prism", "Tetrahedron"}));
    m.setFolder("");
    EXPECT_TRUE(m.isBuiltin(m.pathAt(0)));
    EXPECT_EQ(m.scopeOf(m.pathAt(0)), "builtin");

    // The search: English and German names, other names, the collection's title
    m.setSearch("Widerstand");
    EXPECT_TRUE(has(names(m), "Resistor"));
    EXPECT_TRUE(has(names(m), "Variable resistor")) << "Einstellbarer Widerstand";
    m.setSearch("würfel");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Cube"}));
    m.setSearch("conical flask");
    EXPECT_EQ(names(m), (std::vector<std::string>{"Erlenmeyer flask"}));
    m.setSearch("Laborgeräte");
    EXPECT_GE(m.rowCount(), 10);
    m.setSearch("");

    // The templates have no built-in set
    StickersModel templates(stickers::Kind::Templates);
    templates.setLibrary(library, root / "config");
    templates.setScope("builtin");
    EXPECT_NE(templates.scope(), "builtin");
}

// The names follow the app's language (QLocale)
TEST_F(BuiltinStickersTest, theNamesInTheAppsLanguage) {
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    StickersModel m;
    m.setLibrary(library, root / "config");
    m.setScope("builtin");
    m.setFolder("circuits-iec");
    EXPECT_EQ(names(m).front(), "Widerstand");
    EXPECT_EQ(m.folderTitle("circuits-iec"), "Schaltzeichen (IEC)");
    m.setSearch("resistor");
    EXPECT_TRUE(has(names(m), "Widerstand")) << "the English name is found too";
}

// Read-only: nothing renames, moves, reorders or deletes a built-in sticker; a copy of one is the user's
TEST_F(BuiltinStickersTest, readOnlyAndCopiedToMine) {
    StickersModel m;
    m.setLibrary(library, root / "config");
    m.setScope("builtin");
    m.setSearch("Resistor");
    m.setSort("name");
    const QString path = m.pathAt(0);
    ASSERT_TRUE(path.endsWith("Resistor.xopp")) << path.toStdString();
    EXPECT_FALSE(m.rename(path, "Mine"));
    EXPECT_FALSE(m.moveBy(path, 1));
    EXPECT_FALSE(m.moveToFolder(path, "Other"));
    EXPECT_FALSE(m.copyToOtherSet(path));
    EXPECT_FALSE(m.remove(path));
    EXPECT_TRUE(fs::exists(path.toStdString()));
    EXPECT_FALSE(fs::exists(builtin / "circuits-iec" / ".sticker-order.json"));

    // Into the library's Stickers folder, named as shown (in German: "Widerstand.xopp")
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    m.refresh();
    const QString copy = m.copyToMine(path);
    ASSERT_FALSE(copy.isEmpty());
    EXPECT_EQ(fs::path(copy.toStdString()), stickers::librarySet(library) / "Widerstand.xopp");
    EXPECT_EQ(m.scopeOf(copy), "library");
    EXPECT_TRUE(stickers::read(copy.toStdString())) << "a sticker";
    EXPECT_NE(fs::status(copy.toStdString()).permissions() & fs::perms::owner_write, fs::perms::none);
    // Again: a name of its own
    EXPECT_EQ(fs::path(m.copyToMine(path).toStdString()).filename(), "Widerstand (2).xopp");

    // Without a library: the app-wide set
    StickersModel noLibrary;
    noLibrary.setLibrary({}, root / "config");
    EXPECT_EQ(fs::path(noLibrary.copyToMine(path).toStdString()), stickers::appSet() / "Resistor.xopp");
}

// Hidden collections: not listed, not searched, kept in the settings; restored
TEST_F(BuiltinStickersTest, hiddenCollectionsAreKeptInTheSettings) {
    const fs::path file = root / "settings.xml";
    {
        Settings settings(file);
        settings.load();
        StickersModel m;
        m.setSettings(&settings);
        m.setLibrary(library, root / "config");
        m.setScope("builtin");
        EXPECT_EQ(m.hiddenCount(), 0);
        m.setFolder("solids");
        m.setCollectionHidden("solids", true);
        m.setCollectionHidden("circuits-iec", true);
        EXPECT_EQ(m.hiddenCount(), 2);
        EXPECT_EQ(m.folder(), "") << "its collection hidden: all of the set";
        EXPECT_EQ(m.folderList(), (QStringList{"logic-gates", "logic-gates-iec", "lab"}));
        m.setSearch("Cube");
        EXPECT_EQ(m.rowCount(), 0) << "not searched";
        m.setSearch("");
        bool listed = false;
        for (const QVariant& v: m.collectionList()) {
            const QVariantMap c = v.toMap();
            if (c.value("id").toString() == "solids") {
                listed = true;
                EXPECT_TRUE(c.value("hidden").toBool());
                EXPECT_EQ(c.value("title").toString(), "3D solids");
            }
        }
        EXPECT_TRUE(listed) << "the settings list every collection";
        m.setPenColour(true);
        EXPECT_TRUE(fs::exists(builtin / "solids" / "Cube.xopp")) << "nothing on disk changes";
    }
    {
        Settings settings(file);
        settings.load();
        EXPECT_EQ(stickers::hiddenCollections(settings), (QStringList{"solids", "circuits-iec"}));
        EXPECT_TRUE(stickers::penColour(settings));
        StickersModel m;
        m.setSettings(&settings);
        m.setLibrary(library, root / "config");
        m.setScope("builtin");
        EXPECT_EQ(m.hiddenCount(), 2);
        EXPECT_TRUE(m.penColour());
        m.restoreCollections();
        EXPECT_EQ(m.hiddenCount(), 0);
        EXPECT_EQ(m.folderList().size(), 5);
    }
    Settings settings(file);
    settings.load();
    EXPECT_TRUE(stickers::hiddenCollections(settings).isEmpty());
}

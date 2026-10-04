/*
 * xournal-qt: the color palettes (ColorPalettes, qt/resources/palettes/palettes.json) and the controller's palette
 * setting and highlighter opacity.
 *
 * @license GNU GPLv2 or later
 */
#include <QFile>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/ColorPalettes.h"
#include "shell/TabManager.h"

#include "AppController.h"

using namespace xqt;
using Kind = ColorPalettes::Kind;

namespace {
QString hex(const std::optional<QColor>& c) { return c ? c->name().toUpper() : QString("none"); }
}  // namespace

TEST(ColorPalettes, theSpecIsCompiledIn) {
    QFile file(":/xqt-palettes/palettes.json");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_TRUE(file.readAll().contains("\"paletteSystem\"")) << "kept as the author wrote it";
    const ColorPalettes& p = ColorPalettes::builtIn();
    QStringList ids;
    for (const ColorPalette& palette: p.palettes()) {
        ids << palette.id;
    }
    EXPECT_EQ(ids, QStringList({"classic", "marker", "pastel", "colorblind-8", "colorblind-6", "dark"}));
    EXPECT_EQ(p.defaultId(), "classic");
    EXPECT_EQ(p.palette("pastel")->name, "Pastel study");
    EXPECT_EQ(p.palette("colorblind-6")->name, "Colorblind-safe (6)");
    EXPECT_EQ(p.rules().size(), 4);
}

TEST(ColorPalettes, rolesHaveNamesForPeople) {
    const ColorPalettes& p = ColorPalettes::builtIn();
    EXPECT_EQ(p.roleKeys(), QStringList({"body", "warnings", "keyTerms", "examples", "definitions", "headings",
                                         "questions", "ideas"}));
    QStringList names;
    for (const QString& key: p.roleKeys()) {
        names << ColorPalettes::roleName(key);
    }
    EXPECT_EQ(names, QStringList({"Body", "Warnings", "Key terms", "Examples", "Definitions", "Headings", "Questions",
                                  "Ideas"}));
    EXPECT_EQ(ColorPalettes::roleName("somethingNew"), "somethingNew");
}

TEST(ColorPalettes, inkAndHighlightPerRole) {
    const ColorPalettes& p = ColorPalettes::builtIn();
    EXPECT_EQ(hex(p.color("classic", "body", Kind::Ink)), "#2B2B2B");
    EXPECT_EQ(hex(p.color("classic", "body", Kind::Highlight)), "#D9D9D9");
    EXPECT_EQ(hex(p.color("marker", "keyTerms", Kind::Highlight)), "#FFE066") << "Open Color's yellow 3";
    EXPECT_EQ(hex(p.color("marker", "ideas", Kind::Ink)), "#9C36B5") << "grape";
    EXPECT_EQ(hex(p.color("colorblind-8", "headings", Kind::Ink)), "#332288") << "Paul Tol's indigo";
    EXPECT_EQ(hex(p.color("colorblind-6", "headings", Kind::Ink)), "#0072B2") << "the Okabe-Ito blue";
    EXPECT_EQ(hex(p.color("dark", "warnings", Kind::Highlight)), "#803026");
    EXPECT_EQ(hex(p.color("nosuch", "body", Kind::Ink)), "none");
    // Every palette defines its roles in the spec's order, each with both colors
    for (const ColorPalette& palette: p.palettes()) {
        int last = -1;
        for (const PaletteRole& r: palette.roles) {
            const int at = static_cast<int>(p.roleKeys().indexOf(r.key));
            EXPECT_GT(at, last) << palette.id.toStdString() << " " << r.key.toStdString();
            last = at;
            EXPECT_TRUE(r.ink.isValid() && r.highlight.isValid());
        }
    }
}

TEST(ColorPalettes, aPaletteMayLeaveRolesOut) {
    const ColorPalettes& p = ColorPalettes::builtIn();
    const ColorPalette* six = p.palette("colorblind-6");
    ASSERT_NE(six, nullptr);
    EXPECT_EQ(six->roles.size(), 6u);
    EXPECT_EQ(six->role("definitions"), nullptr);
    EXPECT_EQ(six->role("ideas"), nullptr);
    EXPECT_FALSE(p.color("colorblind-6", "ideas", Kind::Ink));
    EXPECT_EQ(p.palette("colorblind-8")->roles.size(), 8u);
    // The QML form: only the roles it has, with their names
    for (const QVariant& v: p.toVariant()) {
        const QVariantMap m = v.toMap();
        if (m.value("id") == "colorblind-6") {
            const QVariantList roles = m.value("roles").toList();
            ASSERT_EQ(roles.size(), 6);
            EXPECT_EQ(roles[2].toMap().value("name").toString(), "Key terms");
            EXPECT_EQ(roles[2].toMap().value("ink").value<QColor>(), QColor("#B07A00"));
        }
        if (m.value("id") == "dark") {
            EXPECT_TRUE(m.value("dark").toBool());
        }
    }
}

TEST(ColorPalettes, aColorFollowsItsRoleToAnotherPalette) {
    const ColorPalettes& p = ColorPalettes::builtIn();
    const ColorRef ref = ColorRef::parse("marker:warnings");
    ASSERT_TRUE(ref.valid());
    EXPECT_EQ(ref.toString(), "marker:warnings");
    EXPECT_EQ(hex(p.color(ref, Kind::Ink)), "#E03131");
    EXPECT_EQ(hex(p.follow(ref, "colorblind-6", Kind::Ink)), "#D55E00");
    EXPECT_EQ(hex(p.follow(ref, "pastel", Kind::Highlight)), "#F7C6C0");
    EXPECT_FALSE(p.follow(ColorRef::parse("marker:ideas"), "colorblind-6", Kind::Ink)) << "no ideas there";
    for (const char* bad: {"", "marker", ":warnings", "marker:", "a:b:c"}) {
        EXPECT_FALSE(ColorRef::parse(bad).valid()) << bad;
    }
}

TEST(ColorPalettes, aBrokenSpecIsRefused) {
    ColorPalettes p;
    QString error;
    EXPECT_FALSE(p.load("{", &error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(p.load(R"({"paletteSystem":{"roles":["body"],"palettes":[]}})", &error)) << error.toStdString();
    EXPECT_FALSE(p.load(R"({"paletteSystem":{"roles":["body"],"palettes":[{"id":"x","colors":{"body":{"ink":"#000"}}}]}})",
                        &error))
            << "no highlight";
    EXPECT_FALSE(p.load(R"({"paletteSystem":{"roles":["body"],"palettes":[
        {"id":"x","colors":{"body":{"ink":"#000","highlight":"#ccc"}}},
        {"id":"x","colors":{"body":{"ink":"#111","highlight":"#ddd"}}}]}})",
                        &error))
            << "two with one id";
    EXPECT_TRUE(p.palettes().empty()) << "nothing taken from a broken spec";
    // A role the spec does not list is left out
    ASSERT_TRUE(p.load(R"({"paletteSystem":{"roles":["body"],"palettes":[
        {"id":"x","colors":{"body":{"ink":"#000","highlight":"#ccc"},"extra":{"ink":"#111","highlight":"#ddd"}}}]}})",
                       &error))
            << error.toStdString();
    EXPECT_EQ(p.palettes().front().roles.size(), 1u);
}

TEST(ColorPalettes, theHighlighterIsStrongerOnDarkPaper) {
    for (const char* light: {"#ffffff", "#fdf6e3", "#f1f3f4", "#e8f0fe", "#fef7e0", "#808080"}) {
        EXPECT_FALSE(ColorPalettes::isDarkPaper(QColor(light))) << light;
        EXPECT_DOUBLE_EQ(ColorPalettes::highlighterOpacity(QColor(light)), 0.5) << light;
    }
    for (const char* dark: {"#000000", "#1e1f22", "#333333", "#1a237e", "#5a5a5a"}) {
        EXPECT_TRUE(ColorPalettes::isDarkPaper(QColor(dark))) << dark;
        EXPECT_DOUBLE_EQ(ColorPalettes::highlighterOpacity(QColor(dark)), 0.8) << dark;
    }
    // The palette's mode does not decide: the Dark palette's own background is dark paper, a white page is not
    EXPECT_TRUE(ColorPalettes::isDarkPaper(ColorPalettes::builtIn().palette("dark")->background));
}

TEST(ColorPalettes, theChosenPaletteIsASetting) {
    AppController c;
    c.setColorPalette("classic");
    QSignalSpy changed(&c, &AppController::colorPaletteChanged);
    EXPECT_EQ(c.colorPalette(), "classic");
    EXPECT_EQ(c.colorPalettes().size(), 6);
    c.setColorPalette("marker");
    EXPECT_EQ(c.colorPalette(), "marker");
    EXPECT_EQ(changed.count(), 1);
    c.setColorPalette("no-such-palette");
    EXPECT_EQ(c.colorPalette(), "marker") << "an unknown palette is not taken";
    c.setColorPalette("marker");
    EXPECT_EQ(changed.count(), 1);
    {
        AppController again;  // (the same settings file)
        EXPECT_EQ(again.colorPalette(), "marker") << "kept";
        again.shutdown();
    }
    c.setColorPalette("classic");
    c.shutdown();
}

TEST(ColorPalettes, theHighlighterOpacityFollowsTheCurrentPage) {
    AppController c;
    EXPECT_DOUBLE_EQ(c.highlighterOpacity(), 0.5) << "no document: white paper";
    c.newDocument();
    DocumentSession* s = c.tabManager().currentSession();
    ASSERT_NE(s, nullptr);
    EXPECT_DOUBLE_EQ(c.highlighterOpacity(), 0.5);
    {
        Document* doc = s->getDocument();
        std::unique_lock lock(*doc);
        doc->getPage(0)->setBackgroundColor(Color(0x1e1f22U));
    }
    EXPECT_EQ(c.paperColor(), QColor("#1e1f22"));
    EXPECT_DOUBLE_EQ(c.highlighterOpacity(), 0.8) << "dark paper";
    c.shutdown();
}

TEST(ColorPalettes, aColorTakenFromAPaletteRemembersItsRole) {
    AppController c;
    c.setColorPalette("classic");
    c.newDocument();
    c.selectTool("pen");
    c.setPaletteColor("marker", "ideas");
    EXPECT_EQ(c.color(), QColor("#9C36B5"));
    EXPECT_EQ(c.colorRole(), "marker:ideas");
    EXPECT_EQ(c.colorPalette(), "marker") << "its palette is the chosen one";
    c.selectTool("highlighter");
    EXPECT_EQ(c.colorRole(), "") << "per tool";
    c.setPaletteColor("marker", "keyTerms");
    EXPECT_EQ(c.color(), QColor("#FFE066")) << "the highlighter takes the highlight color";
    EXPECT_EQ(c.colorRoleOf("highlighter"), "marker:keyTerms");
    EXPECT_EQ(c.colorRoleOf("pen"), "marker:ideas");

    // Another palette: the colors follow their roles; a role it leaves out keeps its color (and its role)
    c.setColorPalette("colorblind-6");
    EXPECT_EQ(c.color(), QColor("#F0E442")) << "the key terms highlight there";
    EXPECT_EQ(c.colorRoleOf("highlighter"), "colorblind-6:keyTerms");
    EXPECT_EQ(c.colorRoleOf("pen"), "marker:ideas") << "no ideas in Colorblind-safe (6)";
    c.selectTool("pen");
    EXPECT_EQ(c.color(), QColor("#9C36B5"));
    c.setColorPalette("pastel");
    EXPECT_EQ(c.color(), QColor("#B5578A")) << "follows again where the role is";
    EXPECT_EQ(c.colorRole(), "pastel:ideas");

    // A color of one's own: no role
    c.setColor(QColor("#123456"));
    EXPECT_EQ(c.colorRole(), "");
    c.setColorPalette("classic");
    EXPECT_EQ(c.color(), QColor("#123456")) << "stays";

    // For tool presets (qt/toolbox)
    EXPECT_EQ(c.paletteColor("dark", "warnings", true), QColor("#803026"));
    EXPECT_FALSE(c.paletteColor("colorblind-6", "definitions", false).isValid());
    EXPECT_EQ(c.followPalette("marker:headings", "colorblind-6", false), QColor("#0072B2"));
    EXPECT_FALSE(c.followPalette("marker:ideas", "colorblind-6", false).isValid());
    c.setColorPalette("classic");
    c.shutdown();
}

/*
 * xournal-qt: the toolbox's tools and the arrangement of both bars (ToolboxModel, qt/docs/toolbox.md): the defaults,
 * entries added, changed, moved, replaced and removed, dividers, the app's items (one home each), groups, the colors of
 * roles, what "+" prefills, the most recent entry of a type, the JSON and its debounced writes.
 *
 * @license GNU GPLv2 or later
 */
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "session/AppContext.h"
#include "shell/SettingsModel.h"
#include "shell/ToolboxModel.h"

#include "AppController.h"

using namespace xqt;

namespace {
QStringList typesOf(const QVariantList& l) {
    QStringList t;
    for (const QVariant& v: l) {
        const QVariantMap m = v.toMap();
        t << (m.value("divider").toBool() ? QString("|")
              : m.contains("app")         ? "@" + m.value("app").toString()
              : m.value("group").toBool() ? "(" + typesOf(m.value("members").toList()).join(" ") + ")"
                                          : m.value("type").toString());
    }
    return t;
}
QStringList idsOf(const QVariantList& l) {
    QStringList t;
    for (const QVariant& v: l) {
        t << v.toMap().value("id").toString();
    }
    return t;
}
void waitFor(const std::function<bool()>& done, int ms = 2000) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}
}  // namespace

TEST(ToolboxModel, theDefaultTools) {
    ToolboxModel m(nullptr, nullptr);
    EXPECT_EQ(typesOf(m.entries()),
              QStringList({"pen", "pen", "pen", "|", "highlighter", "highlighter", "|", "eraser", "|", "shape", "text",
                           "sticky", "|", "laser", "|", "@hand", "@select", "@snip", "@pdfText"}))
            << "the user's tools, then the app's tools on the rail";
    const QVariantList tools = m.tools();
    EXPECT_EQ(tools[0].toMap().value("role"), "body");
    EXPECT_EQ(tools[1].toMap().value("role"), "keyTerms");
    EXPECT_EQ(tools[2].toMap().value("role"), "warnings");
    EXPECT_EQ(tools[3].toMap().value("role"), "keyTerms");
    EXPECT_EQ(tools[4].toMap().value("role"), "definitions");
    EXPECT_EQ(m.sections().size(), 6);
    EXPECT_EQ(tools.size(), 10);
    EXPECT_EQ(m.active(), tools[0].toMap().value("id")) << "the first pen is in hand at the first start";
    EXPECT_EQ(m.entry(m.active()).value("width").toDouble(), 1.41);
}

TEST(ToolboxModel, entriesAreNormalized) {
    const QVariantMap e = ToolboxModel::normalized(
            {{"type", "pen"}, {"width", 900}, {"color", "nonsense"}, {"lineStyle", "wiggly"}, {"extra", 1}});
    EXPECT_EQ(e.value("width").toDouble(), 150.0);
    EXPECT_EQ(e.value("color"), "#2b2b2b") << "a broken color: the type's";
    EXPECT_EQ(e.value("lineStyle"), "plain");
    EXPECT_FALSE(e.contains("extra"));
    EXPECT_EQ(e.value("fill").toMap().value("on"), false);
    EXPECT_TRUE(ToolboxModel::normalized({{"type", "spaceship"}}).isEmpty());
    const QVariantMap shape = ToolboxModel::normalized({{"type", "shape"}, {"variant", "arrow"}, {"base", "x"}});
    EXPECT_EQ(shape.value("variant"), "arrow");
    EXPECT_EQ(shape.value("base"), "pen");
    EXPECT_EQ(ToolboxModel::normalized({{"type", "eraser"}, {"variant", "whiteout"}, {"width", 0}}).value("width"), 8.5);
    EXPECT_EQ(ToolboxModel::normalized({{"type", "text"}, {"role", "nonsense"}}).value("role"), "");
    // A snip: its shape only (qt/docs/toolbox.md, "Cycling")
    const QVariantMap snip = ToolboxModel::normalized({{"type", "snip"}, {"variant", "lasso"}, {"color", "#ff0000"}});
    EXPECT_EQ(snip.value("variant"), "lasso");
    EXPECT_FALSE(snip.contains("color"));
    EXPECT_EQ(ToolboxModel::normalized({{"type", "snip"}, {"variant", "circle"}}).value("variant"), "rect");
    EXPECT_TRUE(ToolboxModel::types().contains("snip"));
}

TEST(ToolboxModel, addChangeDuplicateAndRemove) {
    ToolboxModel m(nullptr, nullptr);
    QSignalSpy changed(&m, &ToolboxModel::changed);
    const QString id = m.add({{"type", "pen"}, {"color", "#123456"}, {"role", ""}, {"width", 4}});
    ASSERT_FALSE(id.isEmpty());
    EXPECT_EQ(m.indexOf(id), m.indexOf(m.recentOfType("laser")) + 1) << "after the user's last tool";
    EXPECT_EQ(m.entries().last().toMap().value("app"), "pdfText") << "the app's tools still end the rail";
    EXPECT_EQ(changed.count(), 1);
    EXPECT_TRUE(m.update(id, {{"width", 2.5}, {"fill", QVariantMap{{"on", true}}}}));
    EXPECT_EQ(m.entry(id).value("width").toDouble(), 2.5);
    EXPECT_TRUE(m.entry(id).value("fill").toMap().value("on").toBool());
    EXPECT_EQ(m.entry(id).value("fill").toMap().value("alpha").toInt(), 128) << "the rest of the fill stays";
    // A role, then a color of one's own: the role goes
    m.update(id, {{"role", "warnings"}});
    EXPECT_EQ(m.entry(id).value("role"), "warnings");
    m.update(id, {{"color", "#00ff00"}});
    EXPECT_EQ(m.entry(id).value("role"), "");
    EXPECT_FALSE(m.update(id, {{"type", "eraser"}}) && m.entry(id).value("type") != "pen") << "the type stays";

    const QString copy = m.duplicate(id);
    EXPECT_EQ(m.indexOf(copy), m.indexOf(id) + 1);
    EXPECT_EQ(m.entry(copy).value("color"), "#00ff00");
    EXPECT_NE(copy, id);
    EXPECT_TRUE(m.remove(copy));
    EXPECT_EQ(m.indexOf(copy), -1);

    // After an entry, in its section
    const QString first = m.tools()[0].toMap().value("id").toString();
    const QString after = m.addAfter(first, {{"type", "highlighter"}});
    EXPECT_EQ(m.indexOf(after), m.indexOf(first) + 1);
}

TEST(ToolboxModel, theLastEraserStays) {
    ToolboxModel m(nullptr, nullptr);
    const QString eraser = m.recentOfType("eraser");
    ASSERT_FALSE(eraser.isEmpty());
    EXPECT_FALSE(m.canRemove(eraser));
    EXPECT_FALSE(m.remove(eraser));
    EXPECT_FALSE(m.replace(eraser, {{"type", "pen"}})) << "not replaced by another tool either";
    const QString second = m.add({{"type", "eraser"}, {"variant", "whiteout"}});
    EXPECT_TRUE(m.canRemove(eraser));
    EXPECT_TRUE(m.remove(eraser));
    EXPECT_FALSE(m.remove(second));
}

TEST(ToolboxModel, movingEntriesAndDividers) {
    ToolboxModel m(nullptr, nullptr);
    const QStringList ids = idsOf(m.entries());
    // The third pen one place later: past the divider, into the highlighters' section
    EXPECT_TRUE(m.moveBy(ids[2], 1));
    EXPECT_EQ(typesOf(m.entries()).mid(0, 5), QStringList({"pen", "pen", "|", "pen", "highlighter"}));
    EXPECT_TRUE(m.moveBy(ids[2], -1));
    EXPECT_EQ(idsOf(m.entries()), ids) << "and back";
    EXPECT_FALSE(m.canMoveBy(ids[0], -1)) << "the first cannot go earlier";
    EXPECT_FALSE(m.canMoveBy(ids.last(), 1));
    // Dragged: before the highlighters' first (index 4 counts the places before the move)
    EXPECT_TRUE(m.move(ids[0], 5));
    EXPECT_EQ(idsOf(m.entries()).mid(0, 5), QStringList({ids[1], ids[2], ids[3], ids[4], ids[0]}));
    EXPECT_TRUE(m.move(ids[0], 0));
    EXPECT_EQ(idsOf(m.entries()), ids);
    EXPECT_FALSE(m.move(ids[0], 1)) << "its own place: no change";
    // Dividers
    EXPECT_TRUE(m.hasDividerAfter(ids[2]));
    m.setDividerAfter(ids[2], false);
    EXPECT_FALSE(m.hasDividerAfter(ids[2]));
    EXPECT_EQ(m.sections().size(), 5);
    m.setDividerAfter(ids[0], true);
    EXPECT_EQ(m.sections().size(), 6);
    EXPECT_EQ(m.sections()[0].toList().size(), 1);
    m.setDividerAfter(ids.last(), true);
    EXPECT_FALSE(m.hasDividerAfter(ids.last())) << "none at the end";
    // A divider dragged to the start or next to another one goes
    const QString divider = idsOf(m.entries())[1];
    EXPECT_TRUE(m.move(divider, 0));
    EXPECT_NE(typesOf(m.entries()).first(), "|");
}

TEST(ToolboxModel, replaceKeepsThePlaceAndTheId) {
    ToolboxModel m(nullptr, nullptr);
    const QString id = m.tools()[1].toMap().value("id").toString();
    const int at = m.indexOf(id);
    EXPECT_TRUE(m.replace(id, {{"type", "shape"}, {"variant", "arrow"}}));
    EXPECT_EQ(m.indexOf(id), at);
    EXPECT_EQ(m.entry(id).value("type"), "shape");
    EXPECT_EQ(m.entry(id).value("variant"), "arrow");
}

TEST(ToolboxModel, rolesTakeThePalettesColors) {
    QVariantMap pen = ToolboxModel::defaultOf("pen");
    pen["role"] = "warnings";
    EXPECT_EQ(ToolboxModel::colorIn(pen, "classic").name().toUpper(), "#D6342C");
    QVariantMap hl = ToolboxModel::defaultOf("highlighter");
    hl["role"] = "warnings";
    EXPECT_EQ(ToolboxModel::colorIn(hl, "classic").name().toUpper(), "#FFB4A8") << "a highlighter: the highlight color";
    // A palette that leaves the role out: the entry's own color
    QVariantMap ideas = ToolboxModel::defaultOf("pen");
    ideas["role"] = "ideas";
    ideas["color"] = "#010203";
    EXPECT_EQ(ToolboxModel::colorIn(ideas, "colorblind-6").name(), "#010203");
    ideas["role"] = "";
    EXPECT_EQ(ToolboxModel::colorIn(ideas, "classic").name(), "#010203") << "no role: its color";
    QVariantMap shape = ToolboxModel::defaultOf("shape");
    shape["base"] = "highlighter";
    EXPECT_TRUE(ToolboxModel::highlights(shape));
}

TEST(ToolboxModel, prefillAndTheMostRecentOfAType) {
    ToolboxModel m(nullptr, nullptr);
    const QVariantList tools = m.tools();
    const QString pen2 = tools[1].toMap().value("id").toString();
    const QString pen3 = tools[2].toMap().value("id").toString();
    m.setActive(pen3);
    m.setActive(m.recentOfType("highlighter"));
    EXPECT_EQ(m.recentOfType("pen"), pen3) << "the pen used last";
    m.setActive(pen2);
    EXPECT_EQ(m.recentOfType("pen"), pen2);
    EXPECT_EQ(m.prefill("pen").value("role"), "keyTerms") << "+ prefills from the pen used last";
    EXPECT_FALSE(m.prefill("pen").contains("id"));
    EXPECT_EQ(m.prefill("laser").value("type"), "laser");
    // A sticky note is never in hand
    const QString sticky = m.recentOfType("sticky");
    m.setActive(sticky);
    EXPECT_EQ(m.active(), pen2);
}

TEST(ToolboxModel, storedAsJsonAfterAPause) {
    QString stored;
    int writes = 0;
    {
        ToolboxModel m([&] { return stored; }, [&](const QString& json) { stored = json; ++writes; });
        const QString id = m.tools()[0].toMap().value("id").toString();
        for (int w = 1; w <= 20; ++w) {
            m.update(id, {{"width", w / 4.0}});  // (a slider dragged)
        }
        EXPECT_EQ(writes, 0) << "not at once";
        waitFor([&] { return writes > 0; });
        EXPECT_EQ(writes, 1) << "once after the pause";
        m.update(id, {{"width", 7}});
        m.setActive(m.recentOfType("eraser"));
    }
    EXPECT_EQ(writes, 2) << "a pending change is written when it goes";
    ToolboxModel again([&] { return stored; }, nullptr);
    EXPECT_EQ(again.tools()[0].toMap().value("width").toDouble(), 7.0);
    EXPECT_EQ(again.entry(again.active()).value("type"), "eraser");
    EXPECT_EQ(again.recentOfType("eraser"), again.active());

    // Broken or foreign JSON: the defaults
    ToolboxModel broken([] { return QString("{nonsense"); }, nullptr);
    EXPECT_EQ(broken.tools().size(), ToolboxModel(nullptr, nullptr).tools().size());
    ToolboxModel future([] { return QString(R"({"version":7,"entries":[]})"); }, nullptr);
    EXPECT_EQ(future.tools().size(), 10);
    // Without an eraser: one is added; unknown tools are left out; dividers tidied
    ToolboxModel odd([] {
        return QString(R"({"version":2,"rail":[{"divider":true,"id":"d1"},{"type":"pen","id":"e1"},)"
                       R"({"type":"ufo","id":"e2"},{"divider":true,"id":"d2"},{"divider":true,"id":"d3"},)"
                       R"({"type":"pen","id":"e1"}]})");
    }, nullptr);
    EXPECT_EQ(typesOf(odd.entries()),
              QStringList({"pen", "|", "pen", "eraser"}));
    const QStringList ids = idsOf(odd.entries());
    EXPECT_EQ(QSet<QString>(ids.begin(), ids.end()).size(), ids.size()) << "ids unique";
}

TEST(ToolboxModel, reset) {
    ToolboxModel m(nullptr, nullptr);
    m.remove(m.tools()[0].toMap().value("id").toString());
    m.add({{"type", "laser"}});
    m.reset();
    EXPECT_EQ(m.tools().size(), 10);
}

// --- one arrangement for both bars (qt/rail-scroll) --------------------------------------------------------------------

namespace {
QString firstOf(const ToolboxModel& m, const QString& type, int n = 0) {
    for (const QVariant& v: m.tools()) {
        if (v.toMap().value("type") == type && n-- == 0) {
            return v.toMap().value("id").toString();
        }
    }
    return {};
}
}  // namespace

TEST(ToolboxModel, theTopBarHasItsFirstLayout) {
    ToolboxModel m(nullptr, nullptr);
    EXPECT_EQ(typesOf(m.topItems()),
              QStringList({"@open", "@save", "@milestone", "@share", "@print", "|", "@image", "@sticker", "@addPage",
                           "@write", "|", "@geometry", "@touchDrawing", "@record", "|", "@search", "@read", "@replay",
                           "@present", "@fullScreen", "@zen", "|", "@tags", "@favourite", "@bookmark", "|",
                           "@settings"}));
    EXPECT_EQ(m.items("top"), m.topItems());
    EXPECT_EQ(m.items("rail"), m.entries());
    EXPECT_EQ(m.unplaced(), QStringList({"new"})) << "every other app item has its place";
    EXPECT_EQ(m.barOf(m.idOfApp("hand")), "rail");
    EXPECT_EQ(m.barOf(m.idOfApp("record")), "top");
    EXPECT_EQ(m.kindOf(m.idOfApp("hand")), "app");
    EXPECT_EQ(m.kindOf(firstOf(m, "pen")), "tool");
    EXPECT_TRUE(ToolboxModel::isAppTool("select"));
    EXPECT_FALSE(ToolboxModel::isAppTool("save"));
    const QStringList ids = idsOf(m.entries()) + idsOf(m.topItems());
    EXPECT_EQ(QSet<QString>(ids.begin(), ids.end()).size(), ids.size()) << "ids unique over both bars";
}

// JSON that is broken, without tools, or of another version: the first layout
TEST(ToolboxModel, unknownJsonGivesTheFirstLayout) {
    for (const char* json: {"{nonsense", R"({"version":2})", R"({"version":2,"rail":[{"app":"hand"}],"top":[]})",
                            R"({"version":3,"rail":[{"type":"pen"}]})", R"({"version":1,"entries":[{"type":"pen"}]})"}) {
        ToolboxModel broken([json] { return QString(json); }, nullptr);
        EXPECT_EQ(broken.entries(), ToolboxModel(nullptr, nullptr).entries()) << json;
    }
}

TEST(ToolboxModel, appItemsHaveOneHomeAndGoIntoTheCatalogWhenRemoved) {
    // Unknown names, and a second place of one, are dropped
    ToolboxModel odd([] {
        return QString(R"({"version":2,"rail":[{"id":"e1","type":"pen"},{"id":"a2","app":"hand"},)"
                       R"({"id":"a3","app":"teleporter"},{"id":"a4","app":"hand"}],)"
                       R"("top":[{"id":"a5","app":"hand"},{"id":"a6","app":"save"}]})");
    }, nullptr);
    EXPECT_EQ(typesOf(odd.entries()), QStringList({"pen", "eraser", "@hand"})) << "an eraser after the user's tools";
    EXPECT_EQ(typesOf(odd.topItems()), QStringList({"@save"}));
    EXPECT_TRUE(odd.unplaced().contains("select"));
    EXPECT_FALSE(odd.unplaced().contains("hand"));

    ToolboxModel m(nullptr, nullptr);
    const QString hand = m.idOfApp("hand");
    EXPECT_TRUE(m.canRemove(hand));
    EXPECT_TRUE(m.remove(hand));
    EXPECT_EQ(m.idOfApp("hand"), "");
    EXPECT_TRUE(m.unplaced().contains("hand")) << "in the catalog";
    const QString back = m.place("hand", "rail", 0);
    EXPECT_FALSE(back.isEmpty());
    EXPECT_EQ(m.indexOf(back), 0);
    EXPECT_EQ(m.place("hand", "top"), "") << "placed already";
    EXPECT_EQ(m.place("ufo", "top"), "");
    // Carried to the other bar (one home: it leaves the rail)
    const QString select = m.idOfApp("select");
    EXPECT_TRUE(m.moveTo(select, "top", 0));
    EXPECT_EQ(m.barOf(select), "top");
    EXPECT_EQ(m.indexOf(select), 0);
    EXPECT_FALSE(typesOf(m.entries()).contains("@select"));
    // A divider after an app item, moved by one
    m.setDividerAfter(select, true);
    EXPECT_TRUE(m.hasDividerAfter(select));
    EXPECT_TRUE(m.moveBy(m.idOfApp("snip"), -1));
    // A tool of the user's onto the top bar and back
    const QString pen = firstOf(m, "pen");
    EXPECT_TRUE(m.moveTo(pen, "top", 1));
    EXPECT_EQ(m.barOf(pen), "top");
    EXPECT_EQ(m.tools().size(), 10) << "still one of the tools";
    EXPECT_TRUE(m.moveTo(pen, "rail", 0));
    EXPECT_EQ(m.indexOf(pen), 0);
}

TEST(ToolboxModel, groupsMadeCycledAndUndone) {
    ToolboxModel m(nullptr, nullptr);
    const QString pen1 = firstOf(m, "pen"), pen2 = firstOf(m, "pen", 1), pen3 = firstOf(m, "pen", 2);
    const QString hl = firstOf(m, "highlighter");
    const QString before = m.snapshot();
    const QStringList railBefore = idsOf(m.entries());

    // The highlighter carried onto the first pen: a group in the pen's place, the carried one shown
    const QString g = m.group(hl, pen1);
    ASSERT_FALSE(g.isEmpty());
    EXPECT_EQ(m.kindOf(g), "group");
    EXPECT_EQ(m.indexOf(g), 0);
    EXPECT_EQ(idsOf(m.members(g)), QStringList({pen1, hl}));
    EXPECT_EQ(m.shownOf(g), hl);
    EXPECT_EQ(m.groupOf(hl), g);
    EXPECT_EQ(m.indexOf(hl), 0) << "a member's place: its group's";
    EXPECT_EQ(m.barOf(hl), "rail");
    EXPECT_EQ(m.tools().size(), 10) << "the tools are all still there";
    // Taking a member: the group shows it
    m.setActive(pen1);
    EXPECT_EQ(m.shownOf(g), pen1);
    // More members (an app item too), and one carried onto a member: into that group
    EXPECT_EQ(m.group(pen2, g), g);
    EXPECT_EQ(m.group(m.idOfApp("select"), pen2), g);
    EXPECT_EQ(m.members(g).size(), 4);
    m.use(m.idOfApp("select"));
    EXPECT_EQ(m.shownOf(g), m.idOfApp("select"));
    // Not with itself, a divider, or between two members of one group
    EXPECT_EQ(m.group(pen1, pen1), "");
    EXPECT_EQ(m.group(pen1, hl), "");
    const QString divider = idsOf(m.entries())[typesOf(m.entries()).indexOf("|")];
    EXPECT_EQ(m.group(pen3, divider), "") << "a divider";
    // Stored and read again
    QString stored = m.toJson();
    ToolboxModel again([&] { return stored; }, nullptr);
    EXPECT_EQ(again.members(g), m.members(g));
    EXPECT_EQ(again.shownOf(g), m.idOfApp("select"));

    // Carried out of the group: a member no more, where it was let go
    EXPECT_TRUE(m.moveTo(pen2, "rail", 1));
    EXPECT_EQ(m.groupOf(pen2), "");
    EXPECT_EQ(m.indexOf(pen2), 1);
    EXPECT_EQ(m.members(g).size(), 3);
    // Moved inside the group
    EXPECT_TRUE(m.moveBy(hl, -1));
    EXPECT_EQ(idsOf(m.members(g)).first(), hl);
    // Ungrouped: the members in their order, in its place
    EXPECT_TRUE(m.ungroup(g));
    EXPECT_EQ(m.kindOf(g), "");
    EXPECT_EQ(idsOf(m.entries()).mid(0, 4), QStringList({hl, pen1, m.idOfApp("select"), pen2}));
    // A group of two that loses one is that one again
    const QString g2 = m.group(pen3, pen2);
    EXPECT_TRUE(m.moveTo(pen3, "rail", 0));
    EXPECT_EQ(m.kindOf(g2), "") << "no group of one";
    EXPECT_EQ(m.groupOf(pen2), "");
    // A group carried onto another gives it its members
    const QString ga = m.group(pen1, hl);
    const QString gb = m.group(pen3, pen2);
    EXPECT_EQ(m.group(gb, ga), ga);
    EXPECT_EQ(m.members(ga).size(), 4);
    EXPECT_EQ(m.kindOf(gb), "");

    // Undo: the arrangement of before, as it was
    EXPECT_TRUE(m.restore(before));
    EXPECT_EQ(idsOf(m.entries()), railBefore);
}

TEST(ToolboxModel, aGroupRemovedTakesItsMembersButTheLastEraserStays) {
    ToolboxModel m(nullptr, nullptr);
    const QString eraser = firstOf(m, "eraser"), laser = firstOf(m, "laser");
    const QString hand = m.idOfApp("hand");
    const QString g = m.group(laser, eraser);
    EXPECT_EQ(m.group(hand, g), g);
    EXPECT_TRUE(m.canRemove(g));
    const int at = m.indexOf(g);
    EXPECT_TRUE(m.remove(g));
    EXPECT_EQ(m.kindOf(laser), "");
    EXPECT_TRUE(m.unplaced().contains("hand"));
    EXPECT_EQ(m.indexOf(eraser), at) << "the only eraser stays in the group's place";
}

TEST(ToolboxModel, backToTheFirstLayoutKeepsTheUsersTools) {
    ToolboxModel m(nullptr, nullptr);
    const QString pen1 = firstOf(m, "pen"), laser = firstOf(m, "laser");
    const QString mine = m.add({{"type", "pen"}, {"color", "#336699"}});
    m.group(laser, pen1);
    m.remove(m.idOfApp("hand"));
    m.moveTo(m.idOfApp("save"), "rail", 0);
    m.moveTo(mine, "top", 0);
    m.resetLayout();
    EXPECT_EQ(m.tools().size(), 11) << "the user's tools stay";
    EXPECT_EQ(m.groupOf(laser), "") << "out of their groups";
    EXPECT_EQ(m.barOf(mine), "rail");
    EXPECT_EQ(m.entry(mine).value("color"), "#336699");
    EXPECT_EQ(typesOf(m.entries()).mid(typesOf(m.entries()).size() - 5),
              QStringList({"|", "@hand", "@select", "@snip", "@pdfText"}));
    EXPECT_EQ(typesOf(m.topItems()), typesOf(ToolboxModel(nullptr, nullptr).topItems()));
    EXPECT_EQ(m.unplaced(), QStringList({"new"}));
    const QStringList ids = idsOf(m.entries()) + idsOf(m.topItems());
    EXPECT_EQ(QSet<QString>(ids.begin(), ids.end()).size(), ids.size());
}

// --- taking an entry: the tool in hand gets everything it holds (AppToolbox.cpp) --------------------------------------

namespace {
QString nth(ToolboxModel* m, const QString& type, int n = 0) {
    for (const QVariant& v: m->tools()) {
        if (v.toMap().value("type") == type && n-- == 0) {
            return v.toMap().value("id").toString();
        }
    }
    return {};
}
}  // namespace

TEST(ToolboxApply, anEntryGivesTheToolAllItsSettings) {
    AppController c;
    c.setColorPalette("classic");
    c.newDocument();
    ToolboxModel* m = c.toolboxModel();
    ASSERT_NE(m, nullptr);
    m->reset();

    const QString red = nth(m, "pen", 2);
    ASSERT_TRUE(c.applyToolEntry(red));
    EXPECT_EQ(c.tool(), "pen");
    EXPECT_EQ(c.drawingType(), "default");
    EXPECT_EQ(c.color(), QColor("#D6342C")) << "the warnings role in Classic";
    EXPECT_DOUBLE_EQ(c.customWidth(), 1.41);
    EXPECT_EQ(c.size(), 5) << "its own width";
    EXPECT_EQ(m->active(), red);
    EXPECT_TRUE(c.entryInHand(m->entry(red)));

    const QString arrow = m->add({{"type", "shape"}, {"variant", "arrow"}, {"color", "#123456"}, {"width", 3},
                                  {"lineStyle", "dash"}, {"fill", QVariantMap{{"on", true}, {"alpha", 200}}}});
    ASSERT_TRUE(c.applyToolEntry(arrow));
    EXPECT_EQ(c.tool(), "pen");
    EXPECT_EQ(c.drawingType(), "arrow");
    EXPECT_EQ(c.color(), QColor("#123456"));
    EXPECT_DOUBLE_EQ(c.customWidth(), 3);
    EXPECT_EQ(c.lineStyle(), "dash");
    EXPECT_TRUE(c.fillEnabled());
    EXPECT_EQ(c.fillAlpha(), 200);
    EXPECT_FALSE(c.entryInHand(m->entry(red))) << "the pen draws arrows now";

    ASSERT_TRUE(c.applyToolEntry(red));
    EXPECT_EQ(c.drawingType(), "default") << "back to freehand";
    EXPECT_EQ(c.lineStyle(), "plain");
    EXPECT_FALSE(c.fillEnabled());

    const QString yellow = nth(m, "highlighter");
    ASSERT_TRUE(c.applyToolEntry(yellow));
    EXPECT_EQ(c.tool(), "highlighter");
    EXPECT_EQ(c.color(), QColor("#FFE066")) << "the key terms' highlight color";
    EXPECT_DOUBLE_EQ(c.customWidth(), 8.5);

    const QString eraser = nth(m, "eraser");
    m->update(eraser, {{"variant", "whiteout"}, {"width", 20}});
    ASSERT_TRUE(c.applyToolEntry(eraser));
    EXPECT_EQ(c.tool(), "eraser");
    EXPECT_EQ(qobject_cast<SettingsModel*>(c.settingsModel())->get("eraserMode"), "whiteout");
    EXPECT_DOUBLE_EQ(c.customWidth(), 20);

    const QString text = nth(m, "text");
    m->update(text, {{"font", QVariantMap{{"size", 17}}}});
    ASSERT_TRUE(c.applyToolEntry(text));
    EXPECT_EQ(c.tool(), "text");
    EXPECT_DOUBLE_EQ(c.markdownFontSize(), 17);

    ASSERT_TRUE(c.applyToolEntry(nth(m, "laser")));
    EXPECT_EQ(c.tool(), "laserPointerPen");

    // A sticky note: put on the page in its color; the tool in hand is not an entry
    const QString note = nth(m, "sticky");
    m->update(note, {{"color", "#ffcc80"}});
    ASSERT_TRUE(c.applyToolEntry(note));
    EXPECT_TRUE(c.noteSelected());
    EXPECT_EQ(c.noteColor(), QColor("#ffcc80"));
    EXPECT_NE(m->active(), note);
    c.shutdown();
}

TEST(ToolboxApply, theEntryInHandFollowsAPaletteSwitch) {
    AppController c;
    c.setColorPalette("classic");
    ToolboxModel* m = c.toolboxModel();
    m->reset();
    const QString body = nth(m, "pen");
    ASSERT_TRUE(c.applyToolEntry(body));
    EXPECT_EQ(c.color(), QColor("#2B2B2B"));
    c.setColorPalette("marker");
    EXPECT_EQ(c.color(), c.paletteColor("marker", "body", false)) << "the body ink of Marker";
    EXPECT_EQ(c.toolEntryColor(m->entry(body)), c.color());
    // A color changed since: it stays
    c.setColor(QColor("#00aa00"));
    c.setColorPalette("classic");
    EXPECT_EQ(c.color(), QColor("#00aa00"));
    c.shutdown();
}

TEST(ToolboxApply, theKeysTakeTheEntryOfTheirTypeUsedLast) {
    AppController c;
    c.setColorPalette("classic");
    ToolboxModel* m = c.toolboxModel();
    m->reset();
    const QString second = nth(m, "pen", 1);
    c.applyToolEntry(second);
    c.applyToolEntry(nth(m, "highlighter", 1));
    c.applyToolEntry(nth(m, "eraser"));
    c.takeToolOfType("pen");
    EXPECT_EQ(m->active(), second);
    EXPECT_EQ(c.color(), QColor("#D96B00"));
    c.takeToolOfType("highlighter");
    EXPECT_EQ(m->active(), nth(m, "highlighter", 1));
    c.shutdown();
}

TEST(ToolboxApply, aSnipEntryArmsTheSnipAndTheToolBeforeStaysTheActiveEntry) {
    AppController c;
    c.newDocument();
    ToolboxModel* m = c.toolboxModel();
    m->reset();
    const QString pen = nth(m, "pen");
    c.applyToolEntry(pen);
    const QString snip = m->add({{"type", "snip"}, {"variant", "lasso"}});
    ASSERT_FALSE(snip.isEmpty());
    ASSERT_TRUE(c.applyToolEntry(snip));
    EXPECT_EQ(c.snipShape(), "lasso");
    EXPECT_EQ(c.tool(), "selectRegion");
    EXPECT_TRUE(c.entryInHand(m->entry(snip)));
    EXPECT_EQ(m->active(), pen) << "a snip is one picture: the pen is what comes back";
    m->update(snip, {{"variant", "rect"}});
    EXPECT_FALSE(c.entryInHand(m->entry(snip))) << "armed with the lasso, not the rectangle";
    ASSERT_TRUE(c.applyToolEntry(snip));
    EXPECT_EQ(c.snipShape(), "rect");
    c.cancelSnip();
    EXPECT_EQ(c.snipShape(), "");
    EXPECT_EQ(c.tool(), "pen");
    EXPECT_TRUE(c.entryInHand(m->entry(pen)));
    c.shutdown();
}

// A settings file without a toolbox (a first start) gets the first tools; the tool in hand at the start is the
// toolbox's first pen, and the keys take its entries
TEST(ToolboxApply, aSettingsFileWithoutAToolboxGetsTheFirstTools) {
    {
        AppController before;
        Settings* s = before.context().getSettings();
        before.shutdown();
        s->getCustomElement("xournalQt").setString("toolbox", "");
        s->save();
    }
    AppController after;
    ToolboxModel* m = after.toolboxModel();
    EXPECT_EQ(m->entries(), ToolboxModel(nullptr, nullptr).entries());
    EXPECT_EQ(m->tools().size(), 10);
    EXPECT_EQ(m->active(), nth(m, "pen"));
    EXPECT_EQ(after.tool(), "pen");
    // E and T: the toolbox's eraser and text box
    after.takeToolOfType("eraser");
    EXPECT_EQ(after.tool(), "eraser");
    EXPECT_EQ(m->active(), nth(m, "eraser"));
    after.takeToolOfType("text");
    EXPECT_EQ(after.tool(), "text");
    after.shutdown();
}

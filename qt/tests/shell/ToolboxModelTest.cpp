/*
 * xournal-qt: the toolbox's tools (ToolboxModel, qt/docs/toolbox.md): the defaults, entries added, changed, moved,
 * replaced and removed, dividers, the colors of roles, what "+" prefills, the most recent entry of a type, the JSON and
 * its debounced writes.
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
        t << (m.value("divider").toBool() ? QString("|") : m.value("type").toString());
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
    EXPECT_EQ(typesOf(m.entries()), QStringList({"pen", "pen", "pen", "|", "highlighter", "highlighter", "|", "eraser",
                                                 "|", "shape", "text", "sticky", "|", "laser"}));
    const QVariantList tools = m.tools();
    EXPECT_EQ(tools[0].toMap().value("role"), "body");
    EXPECT_EQ(tools[1].toMap().value("role"), "keyTerms");
    EXPECT_EQ(tools[2].toMap().value("role"), "warnings");
    EXPECT_EQ(tools[3].toMap().value("role"), "keyTerms");
    EXPECT_EQ(tools[4].toMap().value("role"), "definitions");
    EXPECT_EQ(m.sections().size(), 5);
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
    EXPECT_EQ(m.indexOf(id), m.entries().size() - 1) << "at the end";
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
    EXPECT_EQ(m.sections().size(), 4);
    m.setDividerAfter(ids[0], true);
    EXPECT_EQ(m.sections().size(), 5);
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
        return QString(R"({"version":1,"entries":[{"divider":true,"id":"d1"},{"type":"pen","id":"e1"},)"
                       R"({"type":"ufo","id":"e2"},{"divider":true,"id":"d2"},{"divider":true,"id":"d3"},)"
                       R"({"type":"pen","id":"e1"}]})");
    }, nullptr);
    EXPECT_EQ(typesOf(odd.entries()), QStringList({"pen", "|", "pen", "eraser"}));
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
    EXPECT_TRUE(c.textMarkdown());
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

TEST(ToolboxApply, theFirstToolsComeFromTheToolsOfBefore) {
    AppController c;
    c.selectTool("pen");
    c.setColor(QColor("#336699"));
    c.setCustomWidth(2.26);
    qobject_cast<SettingsModel*>(c.settingsModel())->set("eraserMode", "deleteStroke");
    ToolboxModel fromBefore([&c] { return c.migratedToolbox(); }, nullptr);
    const QVariantMap pen = fromBefore.tools()[0].toMap();
    EXPECT_EQ(pen.value("color"), "#336699");
    EXPECT_EQ(pen.value("role"), "") << "a color of one's own";
    EXPECT_DOUBLE_EQ(pen.value("width").toDouble(), 2.26);
    EXPECT_EQ(fromBefore.entry(fromBefore.recentOfType("eraser")).value("variant"), "deleteStroke");
    EXPECT_EQ(fromBefore.tools().size(), 10);
    qobject_cast<SettingsModel*>(c.settingsModel())->set("eraserMode", "default");
    c.shutdown();
}

// 0.8.0 removed the classic tool bar: a settings file of before (toolbarMode "classic", or nothing, and no toolbox yet)
// gets the toolbox, whose first tools carry the pen's color and width, the eraser's kind and the text box's font of
// before; the tool in hand at the start is the toolbox's, and the keys take its entries
TEST(ToolboxApply, aSettingsFileOfTheClassicToolBarGetsTheToolboxWithTheToolsOfBefore) {
    for (const char* mode: {"classic", ""}) {
        {
            AppController before;
            before.toolboxModel()->flush();
            before.selectTool("pen");
            before.setColor(QColor("#336699"));
            before.setCustomWidth(2.26);
            before.setMarkdownFontSize(17);
            qobject_cast<SettingsModel*>(before.settingsModel())->set("eraserMode", "deleteStroke");
            Settings* s = before.context().getSettings();
            before.shutdown();
            // (what 0.7.0 wrote with the classic tool bar: its mode, no toolbox)
            s->getCustomElement("xournalQt").setString("toolbarMode", mode);
            s->getCustomElement("xournalQt").setString("toolbox", "");
            s->save();
        }
        AppController after;
        ToolboxModel* m = after.toolboxModel();
        const QVariantMap pen = m->entry(nth(m, "pen"));
        EXPECT_EQ(pen.value("color"), "#336699") << mode;
        EXPECT_EQ(pen.value("role"), "") << mode;
        EXPECT_DOUBLE_EQ(pen.value("width").toDouble(), 2.26) << mode;
        EXPECT_EQ(m->entry(nth(m, "eraser")).value("variant"), "deleteStroke") << mode;
        EXPECT_DOUBLE_EQ(m->entry(nth(m, "text")).value("font").toMap().value("size").toDouble(), 17) << mode;
        EXPECT_EQ(m->tools().size(), 10) << mode << ": the first tools";
        // The pen in hand is the toolbox's first pen
        EXPECT_EQ(m->active(), nth(m, "pen")) << mode;
        EXPECT_EQ(after.tool(), "pen") << mode;
        EXPECT_EQ(after.color(), QColor("#336699")) << mode;
        EXPECT_DOUBLE_EQ(after.customWidth(), 2.26) << mode;
        // E and T: the toolbox's eraser and text box
        after.takeToolOfType("eraser");
        EXPECT_EQ(after.tool(), "eraser") << mode;
        EXPECT_EQ(m->active(), nth(m, "eraser")) << mode;
        EXPECT_EQ(qobject_cast<SettingsModel*>(after.settingsModel())->get("eraserMode"), "deleteStroke") << mode;
        after.takeToolOfType("text");
        EXPECT_EQ(after.tool(), "text") << mode;
        EXPECT_DOUBLE_EQ(after.markdownFontSize(), 17) << mode;
        // (the next tests of this run start from the first tools)
        qobject_cast<SettingsModel*>(after.settingsModel())->set("eraserMode", "default");
        m->reset();
        after.shutdown();
    }
}

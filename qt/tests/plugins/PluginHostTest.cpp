/*
 * xournal-qt: the plugin host (qt/src/plugins, ADR 0008): manifests, the import check, a command as one undo step,
 * the permission asked on first use, rollback on an exception and by the watchdog, the sandbox (no bridge, no
 * globals the host did not add), the log, dialogs and live dialogs, the settings.
 *
 * @license GNU GPLv2 or later
 */
#include <chrono>

#include <QElapsedTimer>

#include "model/Layer.h"
#include "model/XojPage.h"
#include "undo/UndoRedoHandler.h"

#include "ImportCheck.h"
#include "PluginManifest.h"
#include "PluginTestSupport.h"

using namespace xqt;
using namespace xqt::plugins;
using xqt::test::PluginFixture;

namespace {
const char* const INSERT_TWO = R"(
import { elements } from "xournal";
export function run() {
    elements.insert(0, [{type: "stroke", points: [10, 10, 100, 100], color: "#ff0000"}]);
    elements.insert(0, [{type: "stroke", points: [10, 100, 100, 10], color: "#0000ff"}]);
}
)";
}  // namespace

TEST(PluginManifestTest, aManifestIsCheckedField) {
    QString error;
    auto m = PluginManifest::parse(R"({"id": "org.example.a", "name": "A", "api": "1.0", "permissions": ["edit", "tools"],
        "commands": [{"id": "go", "title": "Go", "shortcut": "Ctrl+Alt+G", "place": "insert", "when": "selection"}]})",
                                   error);
    ASSERT_TRUE(m) << error.toStdString();
    EXPECT_EQ(m->main, "main.mjs");
    EXPECT_EQ(m->permissions, (QStringList{"edit", "tools"}));
    ASSERT_TRUE(m->command("go"));
    EXPECT_EQ(m->command("go")->place, "insert");
    EXPECT_FALSE(m->enabledByDefault);

    EXPECT_FALSE(PluginManifest::parse(R"({"id": "Bad Id", "name": "A", "api": "1.0"})", error));
    EXPECT_FALSE(PluginManifest::parse(R"({"id": "a", "name": "A", "api": "1.0", "permissions": ["network"]})", error));
    EXPECT_TRUE(error.contains("network"));
    EXPECT_FALSE(PluginManifest::parse(R"({"id": "a", "name": "A", "api": "1.0", "main": "../x.mjs"})", error));
    EXPECT_FALSE(PluginManifest::parse(
            R"({"id": "a", "name": "A", "api": "1.0", "commands": [{"id": "my-cmd", "title": "x"}]})", error));
    EXPECT_FALSE(PluginManifest::parse("{\"id\": \"a\",\n \"name\": }", error));
    EXPECT_TRUE(error.startsWith("line 2")) << error.toStdString();
    EXPECT_TRUE(apiSupported("1.0"));
    EXPECT_FALSE(apiSupported("1.1"));
    EXPECT_FALSE(apiSupported("2.0"));
}

TEST_F(PluginFixture, importsOutsideThePluginsFolderAreRefused) {
    EXPECT_EQ(importsOf(R"(import { a } from "./a.mjs"; // import x from "../no.mjs"
        /* import "../no2.mjs" */ const s = "import y from '../no3.mjs'"; export * from './b.mjs';
        const r = /from "x"/g; import "./c.mjs")"),
              (QStringList{"./a.mjs", "./b.mjs", "./c.mjs"}));
    bool dynamic = false;
    importsOf("const m = import ( './x.mjs' )", &dynamic);
    EXPECT_TRUE(dynamic);

    writePlugin("user", "inside", {{"main.mjs", "import { f } from './lib/f.mjs'; export function run() { f(); }"},
                                   {"lib/f.mjs", "import { g } from '../g.mjs'; export function f() { g(); }"},
                                   {"g.mjs", "export function g() {}"}});
    EXPECT_EQ(checkImports(tmp.filePath("user/inside"), "main.mjs"), "");
    writePlugin("user", "outside", {{"main.mjs", "import { s } from '../../secret.mjs'; export function run() {}"}});
    {
        QFile secret(tmp.filePath("secret.mjs"));
        ASSERT_TRUE(secret.open(QIODevice::WriteOnly));
        secret.write("export const s = 1;");
    }
    EXPECT_TRUE(checkImports(tmp.filePath("user/outside"), "main.mjs").contains("outside"));
    writePlugin("user", "dyn", {{"main.mjs", "export function run() { return import('./x.mjs'); }"}});
    EXPECT_TRUE(checkImports(tmp.filePath("user/dyn"), "main.mjs").contains("import()"));
    writePlugin("user", "bare", {{"main.mjs", "import x from 'somewhere'; export function run() {}"}});
    EXPECT_FALSE(checkImports(tmp.filePath("user/bare"), "main.mjs").isEmpty());
    // `import * as api from "xournal"` gives undefined on Qt 6.7 (the CI's KDE neon): refused everywhere, with the forms
    // that work on every Qt; in a comment or a string, and `import *` of the plugin's own files, are fine
    writePlugin("user", "star", {{"main.mjs", "import * as api from \"xournal\"; export function run() {}"}});
    const QString star = checkImports(tmp.filePath("user/star"), "main.mjs");
    EXPECT_TRUE(star.contains("import xournal from")) << star.toStdString();
    writePlugin("user", "starok", {{"main.mjs", "// import * as api from 'xournal'\nimport * as l from './l.mjs';\n"
                                                "import xournal, { doc } from 'xournal'; export function run() {}"},
                                   {"l.mjs", "export const a = 1;"}});
    EXPECT_EQ(checkImports(tmp.filePath("user/starok"), "main.mjs"), "");

    // A plugin that imports outside does not load, and says why
    writePlugin("user", "outside", {{"plugin.json", manifest("org.example.outside")}});
    const auto r = host().run("org.example.outside", "run", env());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains("outside the plugin's folder")) << r.error.toStdString();
}

TEST_F(PluginFixture, aCommandIsOneUndoStepAndThePermissionIsAskedOnce) {
    writePlugin("user", "two", {{"plugin.json", manifest("org.example.two")}, {"main.mjs", INSERT_TWO}});
    auto r = host().run("org.example.two", "run", env());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(elements(), 2u);
    EXPECT_EQ(ui.asked, 1);
    EXPECT_EQ(ui.askedClasses, QStringList{"edit"});
    EXPECT_EQ(ui.undoNotes, QStringList{"Run it"});  // (the toast with Undo)
    UndoRedoHandler& undo = *session->getUndoRedoHandler();
    EXPECT_EQ(undo.undoDescription(), "Undo: Run it");
    undo.undo();
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(undo.canUndo());

    r = host().run("org.example.two", "run", env());
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(ui.asked, 1);  // (remembered)
    EXPECT_EQ(host().grant("org.example.two", "edit"), "allow");
    EXPECT_TRUE(stored.contains("org.example.two"));  // (kept in the settings)
}

TEST_F(PluginFixture, aRefusedPermissionRollsBackAndIsKept) {
    writePlugin("user", "two", {{"plugin.json", manifest("org.example.two")},
                                {"main.mjs", R"(
import { layers, elements } from "xournal";
export function run() { layers.add(0, {name: "first"}); elements.insert(0, [{type: "stroke", points: [1, 1, 9, 9]}]); }
)"}});
    writePlugin("user", "two", {{"plugin.json", manifest("org.example.two", R"(["pages", "edit"])")}});
    host().reload();
    ui.allow = true;
    // pages yes, then edit no: the layer is rolled back
    int calls = 0;
    plugins::Environment e = env();
    // (the first question answered yes, the second no)
    class Mixed final: public PluginUi {
    public:
        int* n;
        explicit Mixed(int* n): n(n) {}
        bool askPermission(const PluginInfo&, const QString&, const QString&) override { return (*n)++ == 0; }
        std::optional<QVariantMap> dialog(const PluginInfo&, const QVariantMap&) override { return std::nullopt; }
        bool openLive(const std::shared_ptr<LiveDialog>&) override { return false; }
        void notify(const QString& t, bool) override { last = t; }
        QString last;
    } mixed(&calls);
    e.ui = &mixed;
    const size_t layers = session->getDocument()->getPage(0)->getLayerCount();
    const auto r = host().run("org.example.two", "run", e);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(calls, 2);
    EXPECT_TRUE(r.error.contains("not allowed")) << r.error.toStdString();
    EXPECT_TRUE(r.error.contains("undone"));
    EXPECT_EQ(mixed.last, r.error);
    EXPECT_EQ(session->getDocument()->getPage(0)->getLayerCount(), layers);
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
    EXPECT_EQ(host().grant("org.example.two", "edit"), "deny");
    // Not asked again: refused at once
    const auto again = host().run("org.example.two", "run", e);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ(calls, 2);
    // A class the manifest does not name is refused without asking
    writePlugin("user", "tools", {{"plugin.json", manifest("org.example.tools", "[]")},
                                  {"main.mjs", R"(import { elements } from "xournal";
export function run() { elements.insert(0, [{type: "stroke", points: [1, 1, 9, 9]}]); })"}});
    host().reload();
    const auto r3 = host().run("org.example.tools", "run", env());
    EXPECT_FALSE(r3.ok);
    EXPECT_TRUE(r3.error.contains("PermissionError")) << r3.error.toStdString();
    EXPECT_EQ(ui.asked, 0);
}

TEST_F(PluginFixture, anExceptionRollsBackAndSaysWhere) {
    writePlugin("user", "throws", {{"plugin.json", manifest("org.example.throws")},
                                   {"main.mjs", R"(import { elements } from "xournal";
export function run() {
    elements.insert(0, [{type: "stroke", points: [10, 10, 100, 100]}]);
    const nothing = null;
    return nothing.length;
}
)"}});
    const auto r = host().run("org.example.throws", "run", env());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains("TypeError")) << r.error.toStdString();
    EXPECT_TRUE(r.error.contains("main.mjs:5")) << r.error.toStdString();
    EXPECT_TRUE(r.error.contains("undone"));
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
    ASSERT_FALSE(host().log("org.example.throws").empty());
    EXPECT_EQ(host().log("org.example.throws").back().level, 2);
    // A bad shape from the API: its own error name
    writePlugin("user", "bad", {{"plugin.json", manifest("org.example.bad")},
                                {"main.mjs", R"(import { elements } from "xournal";
export function run() { elements.insert(0, [{type: "circle"}]); })"}});
    host().reload();
    const auto bad = host().run("org.example.bad", "run", env());
    EXPECT_TRUE(bad.error.contains("TypeError: no shape type")) << "[" << bad.error.toStdString() << "]";
}

TEST_F(PluginFixture, theWatchdogStopsALoopAndRollsBack) {
    writePlugin("user", "loop", {{"plugin.json", manifest("org.example.loop")},
                                 {"main.mjs", R"(import { elements } from "xournal";
export function run() {
    elements.insert(0, [{type: "stroke", points: [10, 10, 100, 100]}]);
    let i = 0;
    while (true) { i++; }
}
)"}});
    host().setTimeLimit(std::chrono::milliseconds(300));
    QElapsedTimer t;
    t.start();
    const auto r = host().run("org.example.loop", "run", env());
    EXPECT_LT(t.elapsed(), 3000);
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.error.contains("stopped")) << r.error.toStdString();
    EXPECT_EQ(elements(), 0u);
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());
    // The engine works again afterwards
    writePlugin("user", "loop", {{"main.mjs", INSERT_TWO}});
    host().reload();
    EXPECT_TRUE(host().run("org.example.loop", "run", env()).ok);
}

TEST_F(PluginFixture, thePluginSeesOnlyTheApi) {
    writePlugin("user", "probe", {{"plugin.json", manifest("org.example.probe", "[]")},
                                  {"main.mjs", R"(import api from "xournal";
const G = (function () { return this; })() || {};
export function run() {
    const found = [];
    ["XMLHttpRequest", "setTimeout", "Qt", "require", "process", "fetch", "print"].forEach(function (n) {
        if (typeof G[n] !== "undefined") found.push(n);
    });
    // The bridge is nowhere: not in the API, not as a property of its functions
    Object.keys(api).forEach(function (k) {
        const v = api[k];
        Object.getOwnPropertyNames(v).forEach(function (p) {
            const f = v[p];
            if (f && (f.deleteLater || f.call === undefined && typeof f === "object" && f.objectName !== undefined)) found.push(k + "." + p);
        });
    });
    try { api.elements.insert = null; found.push("not frozen"); } catch (e) {}
    if (!Object.isFrozen(api.doc)) found.push("doc not frozen");
    console.log("found", found);
    if (found.length > 0) throw new Error("reachable: " + found.join(", "));
    return api.doc.info().pageCount;
}
)"}});
    const auto r = host().run("org.example.probe", "run", env());
    EXPECT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_FALSE(r.changed);
    ASSERT_FALSE(host().log("org.example.probe").empty());
    EXPECT_EQ(host().log("org.example.probe").back().text, "found []");
}

TEST_F(PluginFixture, dialogsAndLiveDialogs) {
    writePlugin("user", "dlg", {{"plugin.json", manifest("org.example.dlg", R"(["edit"])", R"(, {"id": "live", "title": "Live"})")},
                                {"main.mjs", R"(import { ui, elements } from "xournal";
export function run() {
    const v = ui.dialog({title: "Size", fields: [{id: "n", type: "number", value: 3}]});
    if (v === null) return;
    for (let i = 0; i < v.n; ++i) elements.insert(0, [{type: "stroke", points: [i, 0, i, 50]}]);
}
function shapes(values, frame) {
    return [{type: "stroke", points: [frame.x, frame.y, frame.x + frame.width, frame.y + values.k]}];
}
export function live() {
    ui.form({title: "Live", fields: [{id: "k", type: "slider", min: 0, max: 10, value: 5}], values: {k: 5},
             frame: {width: 100, height: 50},
             change: function (values, ctx) {
                 if (ctx.action === "write") elements.insert(0, shapes(values, ctx.frame));
                 return {shapes: shapes(values, ctx.frame)};
             },
             insert: function (values, ctx) { elements.insert(0, shapes(values, ctx.frame), {group: true}); }});
}
)"}});
    ui.dialogAnswer = QVariantMap{{"n", 4}};
    auto r = host().run("org.example.dlg", "run", env());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_EQ(ui.lastDialog.value("title").toString(), "Size");
    EXPECT_EQ(elements(), 4u);
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), "Undo: Run it");
    ui.dialogAnswer.reset();  // (cancelled: nothing)
    r = host().run("org.example.dlg", "run", env());
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.changed);

    session->getUndoRedoHandler()->undo();
    r = host().run("org.example.dlg", "live", env());
    ASSERT_TRUE(r.ok) << r.error.toStdString();
    EXPECT_FALSE(r.changed);
    ASSERT_TRUE(ui.live);
    EXPECT_EQ(ui.live->spec().value("title").toString(), "Live");
    EXPECT_FALSE(ui.live->spec().contains("change"));
    const QVariantMap frame{{"page", 0}, {"x", 50}, {"y", 60}, {"width", 100}, {"height", 50}};
    const QVariantMap preview = ui.live->change({{"k", 7}}, frame, "");
    ASSERT_EQ(preview.value("shapes").toList().size(), 1);
    EXPECT_EQ(preview.value("shapes").toList()[0].toMap().value("points").toList()[3].toDouble(), 67.0);
    EXPECT_EQ(elements(), 0u);
    // A preview may not change the document
    const QVariantMap refused = ui.live->change({{"k", 7}}, frame, "write");
    EXPECT_TRUE(refused.value("error").toString().contains("preview")) << refused.value("error").toString().toStdString();
    EXPECT_EQ(elements(), 0u);
    // Insert: one step, with the toast
    ui.undoNotes.clear();
    ASSERT_TRUE(ui.live->insert({{"k", 7}}, frame));
    EXPECT_EQ(elements(), 1u);
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), "Undo: Live");
    EXPECT_EQ(ui.undoNotes, QStringList{"Live"});
}

TEST_F(PluginFixture, enabledStateUserPluginsAndBrokenOnes) {
    writePlugin("bundled", "b", {{"plugin.json", manifest("org.example.b", R"(["edit"])", "", false)},
                                 {"main.mjs", INSERT_TWO}});
    writePlugin("bundled", "c", {{"plugin.json", manifest("org.example.c")}, {"main.mjs", INSERT_TWO}});
    writePlugin("user", "c-dev", {{"plugin.json", manifest("org.example.c").replace("Test org.example.c", "Dev c")},
                                  {"main.mjs", INSERT_TWO}});
    writePlugin("user", "broken", {{"plugin.json", "{ nope"}});
    writePlugin("user", "newer", {{"plugin.json", manifest("org.example.newer").replace("\"1.0\", \"main\"", "\"1.3\", \"main\"")}});
    ASSERT_EQ(host().plugins().size(), 4u);
    const PluginInfo* b = host().plugin("org.example.b");
    ASSERT_TRUE(b);
    EXPECT_TRUE(b->bundled);
    EXPECT_FALSE(b->enabled);
    EXPECT_FALSE(host().run("org.example.b", "run", env()).ok);  // (off)
    EXPECT_TRUE(host().setEnabled("org.example.b", true));
    EXPECT_TRUE(host().run("org.example.b", "run", env()).ok);
    EXPECT_EQ(host().plugin("org.example.c")->manifest.name, "Dev c");  // (the user's replaces the bundled one)
    EXPECT_FALSE(host().plugin("org.example.c")->bundled);
    EXPECT_TRUE(host().plugin("broken")->error.startsWith("plugin.json"));
    EXPECT_TRUE(host().plugin("org.example.newer")->error.contains("1.3"));
    // The settings survive a new host
    host().setGrant("org.example.c", "edit", "deny");
    hostObject.reset();
    EXPECT_TRUE(host().plugin("org.example.b")->enabled);
    EXPECT_EQ(host().grant("org.example.c", "edit"), "deny");
    host().setGrant("org.example.c", "edit", "");  // (revoked: asked again)
    EXPECT_EQ(host().grant("org.example.c", "edit"), "");
}

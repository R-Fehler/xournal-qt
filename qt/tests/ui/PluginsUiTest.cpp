/*
 * xournal-qt: plugins in the real window (qt/docs/features/plugins.md): a command in ⋮ → Plugins and on its key, the
 * permission question on first use, the note with Undo after it, the live dialog with its preview on the page and its
 * frame, Insert as one undo step, the toolbox's catalog, Settings → Plugins.
 *
 * @license GNU GPLv2 or later
 */
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "shell/ShortcutsModel.h"
#include "shell/TabManager.h"
#include "shell/ToolboxModel.h"
#include "undo/UndoRedoHandler.h"

#include "AppController.h"
#include "AppServices.h"
#include "PluginControl.h"
#include "PluginHost.h"
#include "UiFixture.h"
#include "quick/DocumentCanvasItem.h"

namespace {

const char* const MANIFEST = R"({"id": "org.test.ui", "name": "UI test plugin", "version": "1.0", "api": "1.0",
  "enabled": true, "permissions": ["edit"],
  "commands": [{"id": "lines", "title": "Two lines", "shortcut": "Ctrl+Alt+K"},
               {"id": "live", "title": "Live lines…", "place": "insert", "icon": "xqt-function-plot"}]})";

const char* const MAIN = R"(import { elements, ui } from "xournal";
export function lines() {
    elements.insert(undefined, [{type: "stroke", points: [50, 50, 200, 50], color: "#c01c28", width: 2},
                                {type: "stroke", points: [50, 80, 200, 80], color: "#c01c28", width: 2}]);
}
function shapes(values, f) {
    const out = [];
    for (let i = 0; i < values.n; ++i) {
        const y = f.y + (i + 0.5) * f.height / values.n;
        out.push({type: "stroke", points: [f.x, y, f.x + f.width, y], color: values.color, width: 1.5});
    }
    out.push({type: "markdown", x: f.x + f.width / 2, y: f.y + f.height + 4, text: "$n = " + values.n + "$",
              anchor: "top", color: values.color});
    return out;
}
export function live() {
    ui.form({title: "Lines", insertLabel: "Insert lines",
             fields: [{id: "n", type: "number", label: "How many", value: 3},
                      {id: "color", type: "color", label: "Color", value: "#1a5fb4"}],
             values: {n: 3, color: "#1a5fb4"},
             frame: {width: 200, height: 120},
             change: function (values, ctx) {
                 if (!(values.n >= 1 && values.n <= 50)) return {errors: {n: "1 to 50 lines"}};
                 return {shapes: shapes(values, ctx.frame)};
             },
             insert: function (values, ctx) {
                 elements.insert(ctx.frame.page, shapes(values, ctx.frame), {group: true, data: {n: values.n}});
             }});
}
)";

class PluginsUiTest: public xqt::test::UiFixture {
protected:
    void SetUp() override {
        makeController();
        controller->newDocument();
        const QString folder = controller->pluginControl()->userFolder() + "/ui-test";
        QDir().mkpath(folder);
        write(folder + "/plugin.json", MANIFEST);
        write(folder + "/main.mjs", MAIN);
        controller->pluginControl()->reload();
        // (the settings of the tests before: asked anew, on)
        host().setGrant("org.test.ui", "edit", "");
        host().setEnabled("org.test.ui", true);
        ASSERT_NO_FATAL_FAILURE(loadWindow({QSize(1200, 800), true}));
    }
    void TearDown() override {
        const QString folder = controller ? controller->pluginControl()->userFolder() : QString();
        UiFixture::TearDown();
        if (!folder.isEmpty()) {
            QDir(folder).removeRecursively();
        }
    }
    static void write(const QString& path, const char* text) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(text);
    }
    size_t elements() {
        Document* doc = controller->tabManager().currentSession()->getDocument();
        size_t n = 0;
        for (const Layer* l: doc->getPage(0)->getLayers()) {
            n += l->getElementsView().size();
        }
        return n;
    }
    /// The permission question answered by a click once it is open (the command waits for it in a nested loop)
    void answerWhenAsked(const char* button) {
        auto* timer = new QTimer(window);
        timer->setInterval(30);
        QObject::connect(timer, &QTimer::timeout, window, [this, timer, button] {
            QQuickItem* b = findInScene(button, true);
            QObject* dialog = find<QObject>("pluginPermissionDialog");
            if (b && dialog && dialog->property("opened").toBool()) {
                timer->stop();
                timer->deleteLater();
                QMetaObject::invokeMethod(b, "clicked");
            }
        });
        timer->start();
    }
    xqt::plugins::PluginHost& host() { return controller->services().plugins(); }
};

}  // namespace

TEST_F(PluginsUiTest, aCommandInThePluginsMenuAsksOnceAndIsOneUndoStepWithANote) {
    QObject* menu = find<QObject>("morePluginsMenu");
    ASSERT_NE(menu, nullptr);
    QObject* entry = nullptr;
    ASSERT_TRUE(until([&] { return (entry = entryOf(menu, "pluginCommand_plugin:org.test.ui/lines")) != nullptr; }));
    EXPECT_TRUE(entry->property("text").toString().startsWith("Two lines"));
    answerWhenAsked("pluginAllowButton");
    QMetaObject::invokeMethod(entry, "triggered");
    ASSERT_TRUE(until([&] { return elements() == 2; }));
    EXPECT_EQ(host().grant("org.test.ui", "edit"), "allow");
    // (the question gone: a closing popup still takes the keys)
    ASSERT_TRUE(until([&] { return !find<QObject>("pluginPermissionDialog")->property("visible").toBool(); }));
    // The note with Undo
    QQuickItem* snackbar = findItem("snackbar");
    ASSERT_TRUE(until([&] { return snackbar->isVisible(); }));
    EXPECT_EQ(findItem("snackbarText")->property("text").toString(), "Two lines");
    QQuickItem* undo = findItem("snackbarAction", true);
    ASSERT_NE(undo, nullptr);
    EXPECT_EQ(undo->property("text").toString(), "Undo");
    QMetaObject::invokeMethod(undo, "clicked");
    ASSERT_TRUE(until([&] { return elements() == 0; }));
    EXPECT_FALSE(controller->tabManager().currentSession()->getUndoRedoHandler()->canUndo());

    // Its key (a default no other action has), not asked again
    EXPECT_EQ(controller->services().shortcuts().keys("plugin:org.test.ui/lines"),
              QStringList{"Ctrl+Alt+K"});
    QTest::keyClick(window, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    ASSERT_TRUE(until([&] { return elements() == 2; }));
}

TEST_F(PluginsUiTest, aRefusedPermissionChangesNothingAndSaysSo) {
    answerWhenAsked("pluginDenyButton");
    controller->pluginControl()->run("plugin:org.test.ui/lines");
    EXPECT_EQ(elements(), 0u);
    EXPECT_EQ(host().grant("org.test.ui", "edit"), "deny");
    QQuickItem* text = findItem("snackbarText");
    ASSERT_TRUE(until([&] { return text->property("text").toString().contains("not allowed"); }));
}

TEST_F(PluginsUiTest, theLiveDialogPreviewsOnThePageAndInsertsOneStep) {
    host().setGrant("org.test.ui", "edit", "allow");
    // "Live lines…" is with the commands that insert: in ⋮ → Tools and in the catalog's Insert section
    QObject* tools = find<QObject>("moreToolsMenu");
    ASSERT_TRUE(until([&] { return entryOf(tools, "pluginCommand_plugin:org.test.ui/live") != nullptr; }));
    EXPECT_TRUE(controller->services().toolbox().unplaced().contains("plugin:org.test.ui/live"));
    EXPECT_NE(find<QObject>("pluginButton_plugin:org.test.ui/live"), nullptr);

    ASSERT_TRUE(controller->pluginControl()->run("plugin:org.test.ui/live"));
    auto* dialog = find<QObject>("pluginLiveDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); }));
    auto* canvas = find<DocumentCanvasItem>("canvas");
    ASSERT_NE(canvas, nullptr);
    ASSERT_TRUE(until([&] { return canvas->pluginPreviewShown().shown; }));
    const QRectF before = canvas->pluginPreviewShown().rect;
    EXPECT_EQ(elements(), 0u);  // (a preview is not in the document)
    // The frame is shown around it, in the middle of what is in view
    QQuickItem* frame = findItem("pluginFrame", true);
    ASSERT_NE(frame, nullptr);
    EXPECT_GT(frame->width(), 50);

    // A field typed into: a new preview; a bad value: the message under the field
    QQuickItem* n = findInScene("pluginField_n", true);
    ASSERT_NE(n, nullptr);
    n->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    type("6");
    ASSERT_TRUE(until([&] { return controller->pluginControl()->liveFrame().size() > 0 && canvas->pluginPreviewShown().rect != before; }));
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    type("99");
    QQuickItem* error = nullptr;
    ASSERT_TRUE(until([&] { return (error = findInScene("pluginFieldError_n", true)) != nullptr; }));
    EXPECT_EQ(error->property("text").toString(), "1 to 50 lines");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    type("4");
    ASSERT_TRUE(until([&] { return findInScene("pluginFieldError_n", true) == nullptr; }));

    // The frame moved: the preview goes along
    const QVariantMap f0 = controller->pluginControl()->liveFrame();
    controller->pluginControl()->moveLiveFrame(f0.value("x").toDouble() + 30, f0.value("y").toDouble() + 20, 200, 120);
    EXPECT_NEAR(controller->pluginControl()->liveFrame().value("x").toDouble(), f0.value("x").toDouble() + 30, 0.01);

    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT") + "-plugin-live.png");
    }
    // Insert: the lines and the label, one undo step, the dialog and the preview gone
    QQuickItem* insert = findInScene("pluginInsertButton", true);
    ASSERT_NE(insert, nullptr);
    EXPECT_EQ(insert->property("text").toString(), "Insert lines");
    QMetaObject::invokeMethod(insert, "clicked");
    ASSERT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_EQ(elements(), 5u);
    ASSERT_TRUE(until([&] { return !canvas->pluginPreviewShown().shown; }));
    EXPECT_EQ(controller->tabManager().currentSession()->getUndoRedoHandler()->undoDescription(), "Undo: Live lines…");
    controller->undo();
    EXPECT_EQ(elements(), 0u);
}

TEST_F(PluginsUiTest, settingsListThePluginWithItsPermissionsAndSwitch) {
    host().setGrant("org.test.ui", "edit", "allow");
    auto* settings = find<QObject>("settingsPage");
    ASSERT_NE(settings, nullptr);
    QMetaObject::invokeMethod(settings, "open");
    ASSERT_TRUE(until([&] { return settings->property("opened").toBool(); }));
    QMetaObject::invokeMethod(settings, "showSection", Q_ARG(QVariant, QVariant("plugins")));
    QQuickItem* grant = nullptr;
    ASSERT_TRUE(until([&] { return (grant = findInScene("pluginGrant_org.test.ui_edit", true)) != nullptr; }));
    EXPECT_EQ(grant->property("currentIndex").toInt(), 1);  // (allowed)
    QMetaObject::invokeMethod(grant, "activated", Q_ARG(int, 0));  // (ask first again)
    EXPECT_EQ(host().grant("org.test.ui", "edit"), "");
    QQuickItem* on = findInScene("pluginEnabled_org.test.ui", true);
    ASSERT_NE(on, nullptr);
    EXPECT_TRUE(on->property("checked").toBool());
    QMetaObject::invokeMethod(on, "toggle");
    QMetaObject::invokeMethod(on, "toggled");
    ASSERT_TRUE(until([&] { return !host().plugin("org.test.ui")->enabled; }));
    // Off: its commands are gone from the menus, the keys and the catalog
    for (const QVariant& c: controller->pluginControl()->commands()) {
        EXPECT_NE(c.toMap().value("plugin").toString(), "org.test.ui");
    }
    // (closed before the window goes: a popup's animation must not outlive its engine)
    QMetaObject::invokeMethod(settings, "close");
    ASSERT_TRUE(until([&] { return !settings->property("visible").toBool(); }));
    EXPECT_TRUE(controller->services().shortcuts().keys("plugin:org.test.ui/lines").isEmpty());
    EXPECT_FALSE(controller->services().toolbox().unplaced().contains("plugin:org.test.ui/live"));
}

// The bundled function plotter as the app offers it: "Plot a function…" with the insert commands, its dialog beside
// the page with the preview, Insert (XQT_TEST_SHOT: pictures of the dialog and of the plot inserted)
TEST_F(PluginsUiTest, theFunctionPlotterIsOfferedWithTheInsertCommandsAndInsertsAPlot) {
    const QString key = "plugin:org.xournalqt.function-plotter/plot";
    ASSERT_NE(host().plugin("org.xournalqt.function-plotter"), nullptr);
    host().setGrant("org.xournalqt.function-plotter", "edit", "allow");
    QObject* tools = find<QObject>("moreToolsMenu");
    QObject* entry = nullptr;
    ASSERT_TRUE(until([&] { return (entry = entryOf(tools, "pluginCommand_" + key)) != nullptr; }));
    EXPECT_TRUE(entry->property("text").toString().startsWith("Plot a function…"));
    EXPECT_EQ(controller->services().shortcuts().keys(key), QStringList{"Ctrl+Alt+P"});
    QTest::keyClick(window, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier);
    auto* dialog = find<QObject>("pluginLiveDialog");
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); }));
    auto* canvas = find<DocumentCanvasItem>("canvas");
    ASSERT_TRUE(until([&] { return canvas->pluginPreviewShown().shown; }));
    QQuickItem* expr = findInScene("pluginField_f0_expr", true);
    ASSERT_NE(expr, nullptr);
    expr->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    type("sin(x)+x/2");
    ASSERT_TRUE(until([&] { return controller->pluginControl()->liveState().value("errors").toMap().isEmpty() &&
                                   canvas->pluginPreviewShown().shown; }));
    wait(200);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT") + "-plotter-dialog.png");
    }
    QQuickItem* insert = findInScene("pluginInsertButton", true);
    ASSERT_NE(insert, nullptr);
    QMetaObject::invokeMethod(insert, "clicked");
    ASSERT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_GT(elements(), 20u);
    EXPECT_EQ(controller->tabManager().currentSession()->getUndoRedoHandler()->undoDescription(),
              "Undo: Plot a function…");
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        controller->setZoomPercent(200);
        wait(800);
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT") + "-plotter-inserted.png");
    }
}

// Pictures of plots for looking at them (XQT_PLOT_GALLERY=<folder>; skipped otherwise): poles, π steps, a curve,
// parameters, marks, a legend, the exact scale
TEST_F(PluginsUiTest, plotGallery) {
    const QString folder = qEnvironmentVariable("XQT_PLOT_GALLERY");
    if (folder.isEmpty()) {
        GTEST_SKIP() << "XQT_PLOT_GALLERY not set";
    }
    host().setGrant("org.xournalqt.function-plotter", "edit", "allow");
    ASSERT_TRUE(controller->pluginControl()->run("plugin:org.xournalqt.function-plotter/plot"));
    auto* canvas = find<DocumentCanvasItem>("canvas");
    ASSERT_TRUE(until([&] { return canvas->pluginPreviewShown().shown; }));
    auto* plugins = controller->pluginControl();
    const QVariantMap base = plugins->liveSpec().value("values").toMap();
    struct Case {
        const char* name;
        QVariantMap values;
        const char* action = "";
    };
    const std::vector<Case> cases = {
            {"tan-pi", {{"f0_expr", "tan(x)"}, {"xMin", "-2pi"}, {"xMax", "2pi"}, {"piTicks", true}}},
            {"reciprocal", {{"f0_expr", "1/x"}, {"xMin", "-5"}, {"xMax", "5"}}},
            {"marks", {{"f0_expr", "x^3/4 - x"}, {"roots", true}, {"extrema", true}, {"xMin", "-3"}, {"xMax", "3"}}},
            {"legend", {{"f0_expr", "sqrt(x)"}, {"labels", "legend"}, {"xMin", "-1"}, {"xMax", "6"}}},
            {"exact", {{"f0_expr", "0,5x + 1"}, {"exact", true}, {"unitMm", 10}, {"yAuto", false}, {"yMin", "-2"},
                       {"yMax", "4"}, {"xMin", "-3"}, {"xMax", "4"}}},
    };
    for (const Case& c: cases) {
        QVariantMap v = base;
        for (auto it = c.values.begin(); it != c.values.end(); ++it) {
            v[it.key()] = it.value();
        }
        plugins->liveEdited(v, c.action);
        wait(300);
        nextFrame();
        window->grabWindow().save(folder + "/" + c.name + ".png");
    }
    // A curve and a second function, with parameters
    QVariantMap v = base;
    plugins->liveEdited(v, "addCurve");
    wait(100);
    v = plugins->liveState().value("values").toMap();
    v["f0_expr"] = "a*x^2 + b";
    v["labels"] = "legend";
    plugins->liveEdited(v, "");
    wait(300);
    nextFrame();
    window->grabWindow().save(folder + "/curve-params.png");
}

namespace {
/// The same window at a phone's size (the phone classes: the live dialog is a bottom sheet)
class PluginsPhoneTest: public PluginsUiTest {
protected:
    void SetUp() override {
        makeController();
        controller->newDocument();
        host().setGrant("org.xournalqt.function-plotter", "edit", "allow");
        ASSERT_NO_FATAL_FAILURE(loadWindow({QSize(412, 915), true}));
    }
};
}  // namespace

TEST_F(PluginsPhoneTest, theLiveDialogIsABottomSheetWithThePageAbove) {
    ASSERT_TRUE(controller->pluginControl()->run("plugin:org.xournalqt.function-plotter/plot"));
    auto* dialog = find<QObject>("pluginLiveDialog");
    ASSERT_TRUE(until([&] { return dialog->property("opened").toBool(); }));
    const double y = dialog->property("y").toDouble(), h = dialog->property("height").toDouble();
    EXPECT_EQ(dialog->property("width").toDouble(), window->width());
    EXPECT_GT(y, window->height() * 0.4);
    EXPECT_LE(y + h, window->height() + 0.5);
    auto* canvas = find<DocumentCanvasItem>("canvas");
    ASSERT_TRUE(until([&] { return canvas->pluginPreviewShown().shown; }));
    // The frame starts in the part of the page the sheet leaves free
    QQuickItem* frame = findItem("pluginFrame", true);
    ASSERT_NE(frame, nullptr);
    const QPointF bottom = frame->mapToScene(QPointF(0, frame->height()));
    EXPECT_LT(bottom.y(), y + 1);
    if (qEnvironmentVariableIsSet("XQT_TEST_SHOT")) {
        nextFrame();
        window->grabWindow().save(qEnvironmentVariable("XQT_TEST_SHOT") + "-plotter-phone.png");
    }
    controller->pluginControl()->liveCancel();
    ASSERT_TRUE(until([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_FALSE(canvas->pluginPreviewShown().shown || controller->pluginControl()->liveOpen());
}

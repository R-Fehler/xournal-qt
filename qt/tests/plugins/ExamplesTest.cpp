/*
 * xournal-qt: the example plugins that come with the app (qt/resources/plugins: ports of Xournal++'s ColorCycle,
 * ToggleGrid, LayerActions and Export, off by default), run through the host with the window's operations as a test
 * stands in for them.
 *
 * @license GNU GPLv2 or later
 */
#include <QFile>

#include "model/PageType.h"
#include "undo/UndoRedoHandler.h"

#include "PluginTestSupport.h"

using namespace xqt;

namespace {
class ExamplesTest: public xqt::test::PluginFixture {
protected:
    void SetUp() override {
        PluginFixture::SetUp();
        hostObject = std::make_unique<plugins::PluginHost>(
                QStringList{QStringLiteral(XQT_PLUGINS_SOURCE_DIR), tmp.filePath("user")}, [this] { return stored; },
                [this](const QString& s) { stored = s; });
        // The window's operations as a test has them: the tool's color, a file "chosen" in a dialog
        ops.add("tool.read", "read", false, [this](ops::Context&, const QVariantMap&) -> QVariant {
            return QVariantMap{{"type", "pen"}, {"color", color}, {"width", 1.4}};
        });
        ops.add("tool.color", "tools", false, [this](ops::Context&, const QVariantMap& a) -> QVariant {
            color = a.value("color").toString();
            return true;
        });
        ops.add("file.chooseSave", "files", false, [this](ops::Context&, const QVariantMap& a) -> QVariant {
            proposed = a.value("name").toString();
            return QVariantMap{{"id", "f1"}, {"name", "out"}};
        });
        ops.add("file.write", "files", false, [this](ops::Context&, const QVariantMap& a) -> QVariant {
            written = a.value("text").toString();
            return true;
        });
        ops.add("file.export", "files", false, [this](ops::Context&, const QVariantMap&) -> QVariant {
            ++exported;
            return true;
        });
    }
    void run(const QString& id, const QString& command) {
        ASSERT_TRUE(host().setEnabled(id, true));
        const auto r = host().run(id, command, env());
        ASSERT_TRUE(r.ok) << r.error.toStdString();
    }
    QString color = "#000000";
    QString proposed;
    QString written;
    int exported = 0;
};
}  // namespace

TEST_F(ExamplesTest, theyAreThereAndOff) {
    for (const char* id: {"org.xournalqt.example.color-cycle", "org.xournalqt.example.toggle-grid",
                          "org.xournalqt.example.layer-actions", "org.xournalqt.example.export"}) {
        const plugins::PluginInfo* p = host().plugin(id);
        ASSERT_NE(p, nullptr) << id;
        EXPECT_TRUE(p->error.isEmpty()) << id << ": " << p->error.toStdString();
        EXPECT_FALSE(p->enabled) << id;
    }
    EXPECT_TRUE(host().plugin("org.xournalqt.function-plotter")->enabled);
}

TEST_F(ExamplesTest, colorCycleTakesThePalettesNextColor) {
    run("org.xournalqt.example.color-cycle", "cycle");
    EXPECT_EQ(color, "#1a5fb4");  // (black is the headless palette's first: from it, the next one)
    run("org.xournalqt.example.color-cycle", "cycle");
    EXPECT_EQ(color, "#c01c28");
    EXPECT_EQ(ui.askedClasses, QStringList{"tools"});
    EXPECT_FALSE(session->getUndoRedoHandler()->canUndo());  // (the tool is no change of the document)
}

TEST_F(ExamplesTest, toggleGridIsOneUndoStepForAllPages) {
    session->insertNewPage(1);
    session->insertNewPage(2);
    const size_t n = session->getDocument()->getPageCount();
    run("org.xournalqt.example.toggle-grid", "toggleAll");
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(session->getDocument()->getPage(i)->getBackgroundType().format, PageTypeFormat::Graph);
    }
    EXPECT_EQ(session->getUndoRedoHandler()->undoDescription(), "Undo: Squared paper on or off (all pages)");
    run("org.xournalqt.example.toggle-grid", "toggle");  // (the current page)
    const size_t current = session->getCurrentPageNo();
    EXPECT_EQ(session->getDocument()->getPage(current)->getBackgroundType().format, PageTypeFormat::Lined);
    session->getUndoRedoHandler()->undo();
    session->getUndoRedoHandler()->undo();
    for (size_t i = 0; i < n; ++i) {
        EXPECT_EQ(session->getDocument()->getPage(i)->getBackgroundType().format, PageTypeFormat::Lined);
    }
}

TEST_F(ExamplesTest, layerActionsAddALayerOnEveryPageAsOneStep) {
    session->insertNewPage(1);
    ui.dialogAnswer = QVariantMap{{"name", "Teacher"}};
    run("org.xournalqt.example.layer-actions", "addTopLayer");
    for (size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(session->getDocument()->getPage(i)->getLayers().back()->getName(), "Teacher");
    }
    run("org.xournalqt.example.layer-actions", "onlyFirstLayer");  // (the current page)
    const auto layers = session->getDocument()->getPage(session->getCurrentPageNo())->getLayers();
    EXPECT_TRUE(layers[0]->isVisible());
    EXPECT_FALSE(layers[1]->isVisible());
    session->getUndoRedoHandler()->undo();
    EXPECT_TRUE(layers[1]->isVisible());
    session->getUndoRedoHandler()->undo();  // (both pages' new layers at once)
    EXPECT_EQ(session->getDocument()->getPage(0)->getLayerCount(), 1u);
    EXPECT_EQ(session->getDocument()->getPage(1)->getLayerCount(), 1u);
}

TEST_F(ExamplesTest, exportReachesFilesOnlyThroughTheDialog) {
    run("org.xournalqt.example.export", "exportPdf");
    EXPECT_EQ(exported, 1);
    EXPECT_EQ(proposed, "Document.pdf");
    EXPECT_EQ(ui.askedClasses, QStringList{"files"});
    run("org.xournalqt.example.export", "exportOutline");
    EXPECT_TRUE(written.startsWith("# Document\n")) << written.toStdString();
    EXPECT_TRUE(written.contains("- Page 1: lined, 0 elements"));
}

/*
 * xournal-qt: SettingsModel (the settings screen's access to upstream's Settings).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QColor>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"
#include "shell/SettingsModel.h"

using namespace xqt;

namespace {
class SettingsModelTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
        model = std::make_unique<SettingsModel>(*app);
    }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
    std::unique_ptr<SettingsModel> model;
};
}  // namespace

TEST_F(SettingsModelTest, everyKeyReadsAndWritesBack) {
    for (const QString& key: model->keys()) {
        const QVariant v = model->get(key);
        EXPECT_TRUE(v.isValid()) << key.toStdString();
        EXPECT_TRUE(model->set(key, v)) << key.toStdString();
        EXPECT_EQ(model->get(key), v) << key.toStdString();
    }
    EXPECT_FALSE(model->set("noSuchSetting", 1));
}

TEST_F(SettingsModelTest, valuesReachUpstreamSettingsAndAreClamped) {
    QSignalSpy changed(model.get(), &SettingsModel::changed);
    QSignalSpy appChanged(app.get(), &AppContext::settingsChanged);
    Settings* s = app->getSettings();

    model->set("pressureMultiplier", 2.0);
    EXPECT_DOUBLE_EQ(s->getPressureMultiplier(), 2.0);
    model->set("minimumPressure", 0.0);  // upstream requires >= 0.01
    EXPECT_DOUBLE_EQ(s->getMinimumPressure(), 0.01);
    model->set("stabilizerAveraging", 2);
    EXPECT_EQ(s->getStabilizerAveragingMethod(), StrokeStabilizer::AveragingMethod::VELOCITY_GAUSSIAN);
    model->set("stabilizerAveraging", 7);  // invalid: ignored
    EXPECT_EQ(s->getStabilizerAveragingMethod(), StrokeStabilizer::AveragingMethod::VELOCITY_GAUSSIAN);
    model->set("snapGrid", false);
    EXPECT_FALSE(s->isSnapGrid());
    model->set("snapGrid", true);
    EXPECT_TRUE(s->isSnapGrid());
    model->set("palmRejectionTimeout", 600);
    int timeout = 0;
    s->getCustomElement("touch").getInt("timeout", timeout);
    EXPECT_EQ(timeout, 600);
    model->set("autosaveMinutes", 500);
    EXPECT_EQ(s->getAutosaveTimeout(), 60);
    EXPECT_GE(changed.count(), 5);
    EXPECT_EQ(appChanged.count(), changed.count());
}

TEST_F(SettingsModelTest, penButtonsAndEraser) {
    model->set("eraserButtonTool", "hand");
    EXPECT_EQ(app->getSettings()->getButtonConfig(BUTTON_ERASER)->getAction(), TOOL_HAND);
    EXPECT_EQ(model->get("eraserButtonTool").toString(), "hand");
    model->set("stylusButtonTool", "eraser");
    EXPECT_EQ(app->getSettings()->getButtonConfig(BUTTON_STYLUS_ONE)->getAction(), TOOL_ERASER);
    model->set("eraserMode", "deleteStroke");
    EXPECT_EQ(app->getToolHandler()->getEraserType(), ERASER_TYPE_DELETE_STROKE);
}

TEST_F(SettingsModelTest, newPageTemplate) {
    Settings* s = app->getSettings();
    const QStringList backgrounds = model->pageBackgrounds();
    ASSERT_GE(backgrounds.size(), 4) << "plain, lined, ruled, graph, ...";
    model->set("pageBackground", 3);
    EXPECT_EQ(model->get("pageBackground").toInt(), 3);

    model->set("paperFormat", model->paperFormats().indexOf("Letter"));
    EXPECT_NEAR(s->getPageTemplateSettings().getPageWidth(), 612, 0.01);
    EXPECT_NEAR(s->getPageTemplateSettings().getPageHeight(), 792, 0.01);
    model->set("landscape", true);
    EXPECT_NEAR(s->getPageTemplateSettings().getPageWidth(), 792, 0.01);
    EXPECT_EQ(model->get("paperFormat").toInt(), model->paperFormats().indexOf("Letter"));
    // A 16:9 slide (PowerPoint's 13.33 x 7.5 in) is landscape whatever the page was
    model->set("landscape", false);
    const int slide = model->paperFormats().indexOf("16:9 (presentation)");
    ASSERT_GE(slide, 0);
    EXPECT_TRUE(model->paperIsWide(slide));
    EXPECT_FALSE(model->paperIsWide(model->paperFormats().indexOf("A4")));
    model->set("paperFormat", slide);
    EXPECT_NEAR(s->getPageTemplateSettings().getPageWidth(), 960, 0.01);
    EXPECT_NEAR(s->getPageTemplateSettings().getPageHeight(), 540, 0.01);
    EXPECT_TRUE(model->get("landscape").toBool());
    EXPECT_EQ(model->get("paperFormat").toInt(), slide);
    model->set("paperFormat", model->paperFormats().indexOf("Letter"));
    model->set("landscape", true);
    model->set("pageColor", QColor("#fdf6e3"));
    EXPECT_EQ(model->get("pageColor").value<QColor>(), QColor("#fdf6e3"));

    // New pages of a document use the template.
    model->set("copyLastPageSize", false);
    model->set("copyLastPageSettings", false);
    DocumentSession session(*app);
    session.insertNewPage(1);
    EXPECT_NEAR(session.getDocument()->getPage(1)->getWidth(), 792, 0.01);
}

TEST_F(SettingsModelTest, savedOnceWhenTheScreenCloses) {
    const QString file = tmp.filePath("settings.xml");
    QFile::remove(file);
    model->begin();
    model->set("autosaveMinutes", 9);
    model->set("zoomGestures", false);
    EXPECT_FALSE(QFile::exists(file));
    model->end();
    ASSERT_TRUE(QFile::exists(file));
    Settings reloaded(fs::path(file.toStdString()));
    reloaded.load();
    EXPECT_EQ(reloaded.getAutosaveTimeout(), 9);
    EXPECT_FALSE(reloaded.isZoomGesturesEnabled());
}

// Snapping to the grid is off: for a new setup, and once for settings saved before (where upstream's default had
// switched it on). After that it stays as chosen.
TEST_F(SettingsModelTest, snappingToTheGridIsOffUnlessChosen) {
    Settings* s = app->getSettings();
    EXPECT_FALSE(s->isSnapGrid()) << "a new setup";

    // Settings saved by an earlier version: snapping on, and not yet turned off once
    const std::string file = tmp.filePath("old.xml").toStdString();
    {
        Settings old{fs::path(file)};
        old.load();
        old.setSnapGrid(true);
        old.save();
    }
    auto earlier = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(file), 1);
    EXPECT_FALSE(earlier->getSettings()->isSnapGrid()) << "turned off once";

    // Switched on again by choice: stays on
    earlier->getSettings()->setSnapGrid(true);
    earlier->getSettings()->save();
    earlier.reset();
    auto later = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(file), 1);
    EXPECT_TRUE(later->getSettings()->isSnapGrid()) << "a choice is kept";
}

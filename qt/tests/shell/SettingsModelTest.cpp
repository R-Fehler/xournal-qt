/*
 * xournal-qt: SettingsModel (the settings screen's access to upstream's Settings).
 *
 * @license GNU GPLv2 or later
 */
#include <memory>
#include <vector>

#include <QColor>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/ToolHandler.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/PageTemplateSettings.h"
#include "control/settings/Settings.h"
#include "gui/inputdevices/InputUtils.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "session/AppContext.h"
#include "session/DocumentMode.h"
#include "session/DocumentSession.h"
#include "session/FuzzyQuery.h"
#include "session/TextMatch.h"
#include "shell/ImageMemory.h"
#include "shell/SettingsModel.h"

#include "CurtainLayer.h"
#include "HoverPointer.h"

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

// "Page previews (sidebar, overviews)" in Settings → Documents is the memory for what those show: the sharp
// thumbnails and the sketches (ImageMemory hands them their shares).
TEST_F(SettingsModelTest, thePreviewMemoryIsHandedToThePagePreviews) {
    EXPECT_EQ(model->get("previewMemory").toInt(), ImageMemory::DEFAULT_PREVIEW_MB);
    ASSERT_TRUE(model->set("previewMemory", 128));
    EXPECT_EQ(ImageMemory::previewMemory(), 128 * ImageMemory::MB);
    EXPECT_EQ(ImageMemory::thumbnailShare(ImageMemory::previewMemory()) +
                      ImageMemory::sketchShare(ImageMemory::previewMemory()),
              128 * ImageMemory::MB);
    ASSERT_TRUE(model->set("previewMemory", static_cast<int>(ImageMemory::DEFAULT_PREVIEW_MB)));
    EXPECT_EQ(ImageMemory::previewMemory(), ImageMemory::DEFAULT_PREVIEW_MB * ImageMemory::MB);
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
    // The curtain's darkness (ours): black by default, 20 to 100 %
    EXPECT_EQ(model->get("curtainOpacity").toInt(), 100);
    model->set("curtainOpacity", 60);
    EXPECT_EQ(CurtainLayer::opacitySetting(*s), 60);
    model->set("curtainOpacity", 5);
    EXPECT_EQ(model->get("curtainOpacity").toInt(), CurtainLayer::OPACITY_MIN);
    EXPECT_GE(changed.count(), 5);
    EXPECT_EQ(appChanged.count(), changed.count());
}

// How documents are kept (DocumentMode.h): not chosen yet, it works as Xournal++ files and the window asks; a choice
// is stored in settings.xml; XQT_DOCUMENT_MODE stands in for a choice not stored (tests) and keeps the question away.
TEST_F(SettingsModelTest, documentMode) {
    const QByteArray env = qgetenv("XQT_DOCUMENT_MODE");
    qunsetenv("XQT_DOCUMENT_MODE");
    Settings& s = *app->getSettings();
    EXPECT_EQ(DocumentMode::stored(s), DocumentMode::Mode::Unset) << "a new install, and one from before the question";
    EXPECT_TRUE(DocumentMode::shouldAsk(s));
    EXPECT_FALSE(DocumentMode::pdfOnly(s)) << "as before, until chosen";
    EXPECT_EQ(model->get("documentMode").toString(), "xopp");

    qputenv("XQT_DOCUMENT_MODE", "pdf");
    EXPECT_FALSE(DocumentMode::shouldAsk(s)) << "the environment stands in for the choice";
    EXPECT_TRUE(DocumentMode::pdfOnly(s));
    EXPECT_EQ(DocumentMode::stored(s), DocumentMode::Mode::Unset) << "nothing stored";
    qunsetenv("XQT_DOCUMENT_MODE");

    EXPECT_TRUE(model->set("documentMode", "pdf"));
    EXPECT_EQ(DocumentMode::stored(s), DocumentMode::Mode::Pdf);
    EXPECT_TRUE(DocumentMode::pdfOnly(s));
    EXPECT_FALSE(DocumentMode::shouldAsk(s));
    model->set("documentMode", "something else");
    EXPECT_EQ(model->get("documentMode").toString(), "pdf") << "ignored";
    qputenv("XQT_DOCUMENT_MODE", "xopp");
    EXPECT_TRUE(DocumentMode::pdfOnly(s)) << "a stored choice wins over the environment";
    qunsetenv("XQT_DOCUMENT_MODE");

    // Stored in settings.xml: read back by the next start
    model.reset();
    app.reset();
    app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                       fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    model = std::make_unique<SettingsModel>(*app);
    EXPECT_EQ(DocumentMode::stored(*app->getSettings()), DocumentMode::Mode::Pdf);
    EXPECT_FALSE(DocumentMode::shouldAsk(*app->getSettings()));
    if (!env.isNull()) {
        qputenv("XQT_DOCUMENT_MODE", env);
    }
}

TEST_F(SettingsModelTest, fuzzyTypoToleranceReachesTheSearch) {
    Settings* s = app->getSettings();
    EXPECT_EQ(model->get("fuzzyTypos").toInt(), 1) << "one typo by default";
    for (const int level: {0, 2, 1}) {
        model->set("fuzzyTypos", level);
        int stored = -1;
        s->getCustomElement("xournalQt").getInt("fuzzyTypos", stored);
        EXPECT_EQ(stored, level);
        EXPECT_EQ(FuzzyQuery::typoTolerance(), level) << "the queries parsed from now on take it";
        // A query with one typo (7 letters) and one with two (14 letters)
        const auto count = [](const char* query, const char* text) {
            return textmatch::count(QString::fromUtf8(text), FuzzyQuery::textTerms(QString::fromUtf8(query), true));
        };
        EXPECT_EQ(count("turbnie", "a turbine"), level >= 1 ? 1 : 0) << level;
        EXPECT_EQ(count("trasnfromation", "a transformation"), level >= 2 ? 1 : 0) << level;
    }
    model->set("fuzzyTypos", 9);
    EXPECT_EQ(model->get("fuzzyTypos").toInt(), 2) << "clamped";
    model->set("fuzzyTypos", 1);
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

// The side buttons and the pen's eraser end erase with a size of their own (next to what they do, in Settings → Pen):
// the eraser's width in points; 0 (the default): the tool bar eraser's width and kind, as before. The button's eraser
// follows at once, not only from the next start.
TEST_F(SettingsModelTest, theEraserOfThePenButtonsHasASizeOfItsOwn) {
    ToolHandler* tools = app->getToolHandler();
    Settings* s = app->getSettings();
    // As a press of the button does (CanvasInput::changeTool), and letting it go (the pen's tool again)
    const auto press = [&](Button button) {
        InputUtils::applyButton(tools, s, button);
        return tools->getThickness();
    };
    const auto release = [&] { tools->pointActiveToolToToolbarTool(); };
    for (const char* key: {"eraserButtonWidth", "stylusButtonWidth", "stylusButton2Width"}) {
        EXPECT_DOUBLE_EQ(model->get(key).toDouble(), 0.0) << key << ": the tool bar eraser's, by default";
    }
    tools->selectTool(TOOL_ERASER);
    const double barWidth = tools->getThickness();
    tools->selectTool(TOOL_PEN);

    // A width of its own is the button's tool's while the button is held
    ASSERT_TRUE(model->set("eraserButtonWidth", 20.0));
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(press(BUTTON_ERASER), 20.0) << "the eraser end's own width";
    release();

    // Each button its own; the tool bar eraser's own width stays
    ASSERT_TRUE(model->set("stylusButtonWidth", 30.0));
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 20.0);
    EXPECT_DOUBLE_EQ(model->get("stylusButton2Width").toDouble(), 0.0) << "still the tool bar's";
    EXPECT_DOUBLE_EQ(press(BUTTON_STYLUS_ONE), 30.0) << "the lower side button's own width";
    release();
    EXPECT_DOUBLE_EQ(press(BUTTON_STYLUS_TWO), barWidth) << "the upper side button: the tool bar eraser's";
    release();

    // The tool bar eraser picks another size: a button with a width of its own keeps it, one without follows
    tools->selectTool(TOOL_ERASER);
    tools->setEraserSize(TOOL_SIZE_FINE);
    const double barFine = tools->getThickness();
    tools->selectTool(TOOL_PEN);
    EXPECT_DOUBLE_EQ(press(BUTTON_STYLUS_ONE), 30.0) << "its own width, whatever size the tool bar's eraser takes";
    release();
    EXPECT_DOUBLE_EQ(press(BUTTON_STYLUS_TWO), barFine) << "the tool bar eraser's fine size";
    release();

    // Back to the tool bar eraser's: the button takes the tool bar eraser's width again
    ASSERT_TRUE(model->set("eraserButtonWidth", 0));
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(press(BUTTON_ERASER), barFine) << "as before: the tool bar eraser's";
    release();

    // ... also with that very button's eraser in hand: its tool is made anew, so the tool in hand is the pen's again
    // (never a pointer to the old copy, which goes away there)
    InputUtils::applyButton(tools, s, BUTTON_STYLUS_ONE);
    ASSERT_EQ(tools->getToolType(), TOOL_ERASER) << "that button's eraser in hand";
    ASSERT_TRUE(model->set("stylusButtonWidth", 0));
    EXPECT_EQ(tools->getToolType(), TOOL_PEN) << "no dangling tool: the pen again";
    EXPECT_DOUBLE_EQ(press(BUTTON_STYLUS_ONE), barFine) << "the tool bar eraser's again";
    release();

    // A width out of range is kept within it (the row offers 0.1 to 150 pt)
    ASSERT_TRUE(model->set("eraserButtonWidth", 0.01));
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 0.1);
    ASSERT_TRUE(model->set("eraserButtonWidth", 1000.0));
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 150.0);

    // Kept in the settings file: the next start has it from the first press
    model.reset();
    app.reset();
    app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                       fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    model = std::make_unique<SettingsModel>(*app);
    EXPECT_DOUBLE_EQ(model->get("eraserButtonWidth").toDouble(), 150.0);
    EXPECT_DOUBLE_EQ(model->get("stylusButtonWidth").toDouble(), 0.0);
    ToolHandler* again = app->getToolHandler();
    InputUtils::applyButton(again, app->getSettings(), BUTTON_ERASER);
    EXPECT_DOUBLE_EQ(again->getThickness(), 150.0) << "from the first press, before any eraser was taken";
}

// A button with a width of its own keeps it when it is pressed: "no change" in the settings (a button without a size)
// is what gives the button's tool the tool bar eraser's size and width on every touch, and it must not take the
// button's own width away.
TEST_F(SettingsModelTest, theButtonsOwnWidthSurvivesThePress) {
    ToolHandler* tools = app->getToolHandler();
    Settings* s = app->getSettings();
    tools->selectTool(TOOL_ERASER);
    tools->setCustomThickness(TOOL_ERASER, 23.0, true);  // (a tool bar eraser of its own width)
    tools->selectTool(TOOL_PEN);

    ASSERT_TRUE(model->set("stylusButtonWidth", 4.0));
    for (int press = 0; press < 3; ++press) {
        InputUtils::applyButton(tools, s, BUTTON_STYLUS_ONE);
        EXPECT_DOUBLE_EQ(tools->getThickness(), 4.0) << "press " << press << ": the button's own width, not the 23 pt";
        tools->pointActiveToolToToolbarTool();
    }

    // Without a width of its own it follows the tool bar eraser (the tool bar's width, since 0.12.0)
    ASSERT_TRUE(model->set("stylusButtonWidth", 0));
    InputUtils::applyButton(tools, s, BUTTON_STYLUS_ONE);
    EXPECT_DOUBLE_EQ(tools->getThickness(), 23.0) << "with no width of its own: the tool bar eraser's";
    tools->pointActiveToolToToolbarTool();
    tools->setCustomThickness(TOOL_ERASER, 23.0, false);
}

// The eraser the pointer shows while a side button erases, or at the pen's eraser end before it touches, is the width
// set for that button; the tool bar eraser keeps its own.
TEST_F(SettingsModelTest, thePenButtonsPointerShowsTheirOwnEraserSize) {
    ASSERT_TRUE(model->set("eraserButtonWidth", 40.0));
    ASSERT_TRUE(model->set("stylusButtonWidth", 4.0));
    Settings& s = *app->getSettings();
    ToolHandler& tools = *app->getToolHandler();
    const auto barEraser = [&] { return 2 * tools.getTool(TOOL_ERASER).getThickness(tools.getEraserSize()); };

    // The tool bar eraser in hand: its own width (upstream's square is 2 × the width a side)
    tools.selectTool(TOOL_ERASER);
    const auto bar = hover::eraserMark(tools, s, false, 1.0);
    ASSERT_TRUE(bar);
    EXPECT_DOUBLE_EQ(bar->size, barEraser());

    // The lower side button held: its own width
    InputUtils::applyButton(&tools, &s, BUTTON_STYLUS_ONE);
    const auto button = hover::eraserMark(tools, s, false, 1.0);
    ASSERT_TRUE(button);
    EXPECT_DOUBLE_EQ(button->size, 8.0) << "2 × 4 pt";

    // The pen's eraser end, hovering: the eraser button's own width, before it touches
    tools.selectTool(TOOL_PEN);
    EXPECT_FALSE(hover::eraserMark(tools, s, false, 1.0)) << "the pen: no eraser";
    const auto end = hover::eraserMark(tools, s, true, 1.0);
    ASSERT_TRUE(end);
    EXPECT_DOUBLE_EQ(end->size, 80.0) << "2 × 40 pt";

    // Not an eraser button any more: no eraser shown at the end
    ASSERT_TRUE(model->set("eraserButtonTool", "hand"));
    EXPECT_FALSE(hover::eraserMark(tools, s, true, 1.0));

    // The tool bar's again: as it was
    ASSERT_TRUE(model->set("eraserButtonTool", "eraser"));
    ASSERT_TRUE(model->set("eraserButtonWidth", 0));
    const auto back = hover::eraserMark(tools, s, true, 1.0);
    ASSERT_TRUE(back);
    EXPECT_DOUBLE_EQ(back->size, barEraser());
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

// Posters and flashcards (qt/page-sizes): A0 to A7 first, in order, with their sizes in millimetres as points
// (1 pt = 1/72 in), each portrait and landscape; the other formats after them. A size that is none of them (set in
// Xournal++) stays: -1, shown as text.
TEST_F(SettingsModelTest, isoSizesFromA0ToA7) {
    struct Iso {
        const char* name;
        double w, h;  // mm
    };
    const std::vector<Iso> iso{{"A0", 841, 1189}, {"A1", 594, 841}, {"A2", 420, 594}, {"A3", 297, 420},
                               {"A4", 210, 297},  {"A5", 148, 210}, {"A6", 105, 148}, {"A7", 74, 105}};
    const QStringList names = model->paperFormats();
    ASSERT_GE(names.size(), static_cast<int>(iso.size()) + 3);
    const auto& tpl = [&]() -> const PageTemplateSettings& { return app->getSettings()->getPageTemplateSettings(); };
    for (size_t i = 0; i < iso.size(); ++i) {
        const int index = static_cast<int>(i);
        EXPECT_EQ(names[index].toStdString(), iso[i].name) << "A0 ... A7 first, in order";
        const QSizeF size = SettingsModel::paperSize(index);
        EXPECT_NEAR(size.width(), iso[i].w / 25.4 * 72, 1e-9) << iso[i].name;
        EXPECT_NEAR(size.height(), iso[i].h / 25.4 * 72, 1e-9) << iso[i].name;
        EXPECT_FALSE(model->paperIsWide(index));
        model->set("landscape", false);
        model->set("paperFormat", index);
        EXPECT_DOUBLE_EQ(tpl().getPageWidth(), size.width()) << iso[i].name;
        EXPECT_DOUBLE_EQ(tpl().getPageHeight(), size.height()) << iso[i].name;
        EXPECT_EQ(model->get("paperFormat").toInt(), index);
        model->set("landscape", true);
        EXPECT_DOUBLE_EQ(tpl().getPageWidth(), size.height()) << iso[i].name << " landscape";
        EXPECT_DOUBLE_EQ(tpl().getPageHeight(), size.width()) << iso[i].name << " landscape";
        EXPECT_EQ(model->get("paperFormat").toInt(), index) << iso[i].name << " landscape";
    }
    EXPECT_NEAR(SettingsModel::paperSize(0).width(), 2383.937, 0.001);  // (A0: 841 mm)
    EXPECT_NEAR(SettingsModel::paperSize(7).height(), 297.638, 0.001);  // (A7: 105 mm)
    for (const char* other: {"Letter", "Legal", "16:9 (presentation)"}) {
        EXPECT_GT(names.indexOf(other), 7) << other << " after the A sizes";
    }

    // Another size (Xournal++'s custom size): none of them, and it stays as it is
    PageTemplateSettings custom = tpl();
    custom.setPageWidth(100);
    custom.setPageHeight(200);
    app->getSettings()->setPageTemplateSettings(custom);
    EXPECT_EQ(model->get("paperFormat").toInt(), -1);
    EXPECT_EQ(model->templatePaperSize(), QString("35 × 71 mm"));
    model->set("paperFormat", -1);
    EXPECT_DOUBLE_EQ(tpl().getPageWidth(), 100);
}

// A new A0 poster and an A7 flashcard have their size, and keep it when saved and opened again.
TEST_F(SettingsModelTest, newA0AndA7DocumentsKeepTheirSize) {
    for (const char* name: {"A0", "A7"}) {
        const int index = model->paperFormats().indexOf(name);
        ASSERT_GE(index, 0);
        const QSizeF size = SettingsModel::paperSize(index);
        for (const bool landscape: {false, true}) {
            model->set("paperFormat", index);
            model->set("landscape", landscape);
            const QSizeF want = landscape ? size.transposed() : size;
            const fs::path file = fs::path(tmp.filePath(QString("%1-%2.xopp").arg(name).arg(landscape)).toStdString());
            {
                DocumentSession session(*app);
                session.insertNewPage(1);
                ASSERT_EQ(session.getDocument()->getPageCount(), 2u);
                for (size_t p = 0; p < 2; ++p) {
                    EXPECT_DOUBLE_EQ(session.getDocument()->getPage(p)->getWidth(), want.width()) << name;
                    EXPECT_DOUBLE_EQ(session.getDocument()->getPage(p)->getHeight(), want.height()) << name;
                }
                auto saved = session.saveAs(file);
                ASSERT_TRUE(saved.ok) << saved.error;
            }
            auto loaded = DocumentSession::loadFile(file);
            ASSERT_TRUE(loaded.document) << loaded.error;
            DocumentSession reopened(*app, std::move(loaded.document));
            ASSERT_EQ(reopened.getDocument()->getPageCount(), 2u);
            // (the file keeps points with a few decimals)
            EXPECT_NEAR(reopened.getDocument()->getPage(1)->getWidth(), want.width(), 0.01) << name;
            EXPECT_NEAR(reopened.getDocument()->getPage(1)->getHeight(), want.height(), 0.01) << name;
        }
    }
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

// The web search of selected text (qt/selection-search): Google until another engine or an address is chosen, and
// the choice is in the settings file.
TEST_F(SettingsModelTest, theWebSearchIsKept) {
    EXPECT_EQ(model->get("webSearch").toString(), "google") << "the default";
    const QString file = tmp.filePath("settings.xml");
    for (const QString& choice: {QStringLiteral("duckduckgo"), QStringLiteral("https://example.org/?q={text}")}) {
        model->begin();
        model->set("webSearch", choice);
        model->end();
        Settings reloaded(fs::path(file.toStdString()));
        reloaded.load();
        std::string v;
        EXPECT_TRUE(reloaded.getCustomElement("xournalQt").getString("webSearch", v));
        EXPECT_EQ(QString::fromStdString(v), choice);
        auto again = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(file.toStdString()), 1);
        EXPECT_EQ(SettingsModel(*again).get("webSearch").toString(), choice);
    }
}

// Snapping to the grid is off: for a new setup, and once for settings saved before (where upstream's default had
// switched it on). After that it stays as chosen.
TEST_F(SettingsModelTest, snappingToTheGridIsOffUnlessChosen) {
    Settings* s = app->getSettings();
    EXPECT_FALSE(s->isSnapGrid()) << "a new setup";

    // Switched on by choice: stays on
    const std::string file = tmp.filePath("chosen.xml").toStdString();
    auto first = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(file), 1);
    EXPECT_FALSE(first->getSettings()->isSnapGrid()) << "a new settings file";
    first->getSettings()->setSnapGrid(true);
    first->getSettings()->save();
    first.reset();
    auto later = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR), fs::path(file), 1);
    EXPECT_TRUE(later->getSettings()->isSnapGrid()) << "a choice is kept";
}

// The layout chosen by hand per size class (qt/docs/features/adaptive-layout.md): stored as layout/<class>/<what>,
// saved at once, "" (or "auto") for the automatic choice; the reset removes them all and nothing else.
TEST_F(SettingsModelTest, layoutChoicesPerSizeClass) {
    QSignalSpy changed(model.get(), &SettingsModel::changed);
    EXPECT_FALSE(model->hasLayoutChoices());
    EXPECT_EQ(model->layoutChoice("desktopWide", "sidebar"), "");
    model->setLayoutChoice("desktopWide", "sidebar", "hidden");
    model->setLayoutChoice("tabletPortrait", "sidebar", "shown");
    model->setLayoutChoice("tabletPortrait", "toolbox", "bottom");
    EXPECT_EQ(changed.count(), 3);
    EXPECT_EQ(model->layoutChoice("desktopWide", "sidebar"), "hidden");
    EXPECT_EQ(model->layoutChoice("tabletPortrait", "sidebar"), "shown");
    EXPECT_EQ(model->layoutChoice("phonePortrait", "sidebar"), "") << "each class its own";
    model->setLayoutChoice("tabletPortrait", "sidebar", "shown");
    EXPECT_EQ(changed.count(), 3) << "the same again: no change";
    model->setLayoutChoice("../x", "sidebar", "shown");
    EXPECT_EQ(model->layoutChoice("../x", "sidebar"), "") << "names are plain words";

    // Kept in the settings file
    const int before = model->get("fuzzyTypos").toInt();
    auto again = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                              fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    SettingsModel reread(*again);
    EXPECT_EQ(reread.layoutChoice("tabletPortrait", "toolbox"), "bottom");

    model->setLayoutChoice("desktopWide", "sidebar", "auto");
    EXPECT_EQ(model->layoutChoice("desktopWide", "sidebar"), "");
    model->resetLayoutChoices();
    EXPECT_FALSE(model->hasLayoutChoices());
    EXPECT_EQ(model->layoutChoice("tabletPortrait", "toolbox"), "");
    EXPECT_EQ(model->get("fuzzyTypos").toInt(), before) << "the other settings stay";
}

TEST_F(SettingsModelTest, adaptiveLayoutAndTouchProfile) {
    EXPECT_TRUE(model->get("adaptiveLayout").toBool()) << "on by default";
    EXPECT_EQ(model->get("touchProfile").toString(), "auto");
    model->set("touchProfile", "sometimes");
    EXPECT_EQ(model->get("touchProfile").toString(), "auto") << "only auto, on, off";
    model->set("touchProfile", "on");
    EXPECT_EQ(model->get("touchProfile").toString(), "on");
}

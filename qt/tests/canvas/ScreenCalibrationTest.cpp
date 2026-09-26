/*
 * xournal-qt: screen calibration (ScreenCalibration.h): the zoom of 100 % from a screen's DPI and device pixel ratio
 * (injected, no real screen), per screen in the settings, and a view that follows it.
 *
 * @license GNU GPLv2 or later
 */
#include <memory>

#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "control/settings/Settings.h"
#include "session/AppContext.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "ScreenCalibration.h"

using namespace xqt;
namespace SC = xqt::ScreenCalibration;

namespace {
SC::Display display(const char* key, double reportedDpi, double dpr) {
    SC::Display d;
    d.key = QString::fromUtf8(key);
    d.name = d.key;
    d.reportedDpi = reportedDpi;
    d.dpr = dpr;
    return d;
}

class ScreenCalibrationTest: public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(tmp.isValid());
        app = std::make_unique<AppContext>(fs::path(XQT_BUILD_RESOURCE_DIR),
                                           fs::path(tmp.filePath("settings.xml").toStdString()), 1);
    }
    Settings& settings() { return *app->getSettings(); }
    QTemporaryDir tmp;
    std::unique_ptr<AppContext> app;
};
}  // namespace

TEST_F(ScreenCalibrationTest, hundredPercentIsTheReportedSizeInLogicalPixels) {
    // A monitor at 100 % scaling that says it has 109 dpi (27", 2560 px): 109 logical pixels make an inch
    const auto monitor = display("monitor", 109.0, 1.0);
    EXPECT_DOUBLE_EQ(SC::defaultPpi(monitor), 109.0);
    EXPECT_DOUBLE_EQ(SC::zoom100(settings(), monitor), 109.0 / 72.0);

    // A Surface at 200 %: the panel has 267 ppi, Qt reports 133.5 logical dpi; a page point is 133.5/72 logical
    // pixels (267/72 device pixels)
    const auto surface = display("surface", 133.5, 2.0);
    EXPECT_DOUBLE_EQ(SC::defaultPpi(surface), 267.0);
    EXPECT_DOUBLE_EQ(SC::zoom100(settings(), surface), 133.5 / 72.0);
    EXPECT_DOUBLE_EQ(SC::zoom100(settings(), surface) * surface.dpr, 267.0 / 72.0) << "device pixels per point";

    // KDE/Wayland at 125 % (fractional): 282 ppi panel, reported as 225.6 logical dpi
    const auto laptop = display("laptop", 225.6, 1.25);
    EXPECT_NEAR(SC::defaultPpi(laptop), 282.0, 1e-9);
    EXPECT_NEAR(SC::zoom100(settings(), laptop), 225.6 / 72.0, 1e-12);
}

TEST_F(ScreenCalibrationTest, implausibleReportsFallBackTo96) {
    EXPECT_DOUBLE_EQ(SC::defaultPpi(display("none", 0.0, 1.0)), 96.0) << "no size reported";
    EXPECT_DOUBLE_EQ(SC::defaultPpi(display("qt guess", 100.0, 1.0)), 96.0) << "Qt's own 100 dpi when it knows no size";
    EXPECT_DOUBLE_EQ(SC::defaultPpi(display("tv", 900.0, 1.0)), 96.0) << "a TV that reports 16x9 mm";
    EXPECT_DOUBLE_EQ(SC::defaultPpi(display("tiny", 20.0, 1.0)), 96.0);
    EXPECT_DOUBLE_EQ(SC::defaultPpi(display("hidpi, none", 0.0, 2.0)), 192.0) << "96 logical dpi on a 2x screen";
    EXPECT_DOUBLE_EQ(SC::zoom100(settings(), display("none", 0.0, 1.0)), 96.0 / 72.0) << "the old fixed 100 %";
    EXPECT_TRUE(SC::plausible(96.0));
    EXPECT_TRUE(SC::plausible(160.0)) << "a phone's logical dpi";
    EXPECT_FALSE(SC::plausible(100.0));
}

TEST_F(ScreenCalibrationTest, aCalibrationIsKeptPerScreenAndSurvivesAnotherScaling) {
    auto laptop = display("BOE 0x0bca 344x194mm", 225.6, 1.25);
    const auto monitor = display("DEL U2720Q ABC123 597x336mm", 163.0, 1.5);
    // The ruler shows 1 inch at 230 logical pixels on the laptop at 125 %: the panel has 287.5 ppi
    SC::store(settings(), laptop.key, 230.0 * laptop.dpr);
    ASSERT_TRUE(SC::storedPpi(settings(), laptop.key).has_value());
    EXPECT_NEAR(*SC::storedPpi(settings(), laptop.key), 287.5, 1e-9);
    EXPECT_NEAR(SC::zoom100(settings(), laptop), 230.0 / 72.0, 1e-9);
    EXPECT_FALSE(SC::storedPpi(settings(), monitor.key).has_value()) << "the other screen is not touched";
    EXPECT_NEAR(SC::zoom100(settings(), monitor), 163.0 / 72.0, 1e-9);

    // The system's scaling goes to 150 %: the same inch is 287.5 / 1.5 logical pixels now, without calibrating again
    laptop.dpr = 1.5;
    EXPECT_NEAR(SC::zoom100(settings(), laptop), 287.5 / 1.5 / 72.0, 1e-9);

    // The second screen gets its own; keys with the separators in them are kept apart
    SC::store(settings(), monitor.key, 170.0 * monitor.dpr);
    SC::store(settings(), "odd;key=with%signs", 111.0);
    EXPECT_NEAR(*SC::storedPpi(settings(), laptop.key), 287.5, 1e-9);
    EXPECT_NEAR(*SC::storedPpi(settings(), monitor.key), 255.0, 1e-9);
    EXPECT_NEAR(*SC::storedPpi(settings(), "odd;key=with%signs"), 111.0, 1e-9);

    // Taken back: the screen follows what it reports again
    SC::forget(settings(), laptop.key);
    EXPECT_FALSE(SC::storedPpi(settings(), laptop.key).has_value());
    EXPECT_NEAR(SC::zoom100(settings(), laptop), 225.6 / 72.0, 1e-9);
    EXPECT_TRUE(SC::storedPpi(settings(), monitor.key).has_value());

    // ... and in a settings file written and read again
    settings().save();
    Settings again(fs::path(tmp.filePath("settings.xml").toStdString()));
    again.load();
    EXPECT_NEAR(*SC::storedPpi(again, monitor.key), 255.0, 1e-9);
}

TEST_F(ScreenCalibrationTest, aNewHundredPercentKeepsThePagesAsLargeAsTheyAre) {
    DocumentSession session(*app);
    CanvasView view(session);
    auto& vc = view.getViewController();
    vc.setViewSize(QSizeF(800, 600));
    const double zoom = vc.zoom();
    QSignalSpy hundred(&vc, &ViewController::zoom100Changed);
    QSignalSpy zoomed(&vc, &ViewController::zoomChanged);

    // Shown on a calibrated screen: 100 % is what the calibration says, the zoom itself stays
    const auto screen = display("screen", 120.0, 1.0);
    SC::store(settings(), screen.key, 150.0);
    view.setDisplay(screen);
    EXPECT_DOUBLE_EQ(vc.zoom100(), 150.0 / 72.0);
    EXPECT_DOUBLE_EQ(vc.zoom(), zoom) << "the document is shown as large as before";
    EXPECT_EQ(hundred.count(), 1);
    EXPECT_EQ(zoomed.count(), 0);

    // Calibrated again (Settings -> Display): the view takes it at once
    SC::store(settings(), screen.key, 144.0);
    Q_EMIT app->settingsChanged();
    EXPECT_DOUBLE_EQ(vc.zoom100(), 2.0);

    // Moved to a 2x screen that reports 110 logical dpi: 100 % is 110/72 logical pixels per point there
    view.setDisplay(display("other", 110.0, 2.0));
    EXPECT_DOUBLE_EQ(vc.zoom100(), 110.0 / 72.0);

    // Real size: exactly 100 %
    vc.setZoom(vc.zoom100(), QPointF(400, 300));
    EXPECT_DOUBLE_EQ(vc.zoom() / vc.zoom100(), 1.0);
    EXPECT_DOUBLE_EQ(view.getZoomControl()->getZoomReal(), 1.0) << "upstream's tools see 100 % as well";
}

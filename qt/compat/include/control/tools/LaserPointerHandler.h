/*
 * xournal-qt: shadow of upstream control/tools/LaserPointerHandler.h.
 *
 * The laser pointer: strokes drawn as an overlay of the page, never added to the document nor to the undo stack; they
 * fade out a while (Settings::getLaserPointerFadeOutTime, upstream's setting) after the pen is lifted. Same API as
 * upstream, so upstream's view/overlays/LaserPointerView.cpp compiles unmodified against it. The implementation
 * (qt/src/canvas/LaserPointerHandler.cpp) is a port of upstream's with a QTimer instead of GLib timeouts, which need a
 * GLib main loop (Qt has none on Windows, Android and macOS).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <memory>

#include "model/OverlayBase.h"
#include "model/PageRef.h"

class Control;
class PositionInputData;
class TemporaryStrokeHandler;
class XojPageView;
class QTimer;

namespace xoj::util {
template <class T>
class DispatchPool;
};

namespace xoj::view {
class OverlayView;
class Repaintable;
class LaserPointerView;
};  // namespace xoj::view

class LaserPointerHandler: public OverlayBase {
public:
    LaserPointerHandler(XojPageView* pageView, Control* control, const PageRef& page);
    ~LaserPointerHandler() override;

    void onSequenceCancelEvent();
    bool onMotionNotifyEvent(const PositionInputData& pos, double zoom);
    void onButtonReleaseEvent(const PositionInputData& pos, double zoom);
    void onButtonPressEvent(const PositionInputData& pos, double zoom);

    auto createView(xoj::view::Repaintable* parent) const -> std::unique_ptr<xoj::view::OverlayView>;

    inline auto getViewPool() const -> std::shared_ptr<xoj::util::DispatchPool<xoj::view::LaserPointerView>> {
        return viewPool;
    }

    /// xournal-qt: the alpha of the ink now (255 until it starts to fade)
    uint8_t alpha() const { return fadeoutAlpha; }

private:
    void startFadeoutTimer();
    void fadeoutStep();

    std::unique_ptr<TemporaryStrokeHandler> strokehandler;
    std::shared_ptr<xoj::util::DispatchPool<xoj::view::LaserPointerView>> viewPool;

    // Used only to pass on to (Temporary)StrokeHandler
    Control* ctrl;
    PageRef page;
    XojPageView* pageView;

    std::unique_ptr<QTimer> fadeoutTimer;
    uint8_t fadeoutAlpha = 255;
    const unsigned int fadeoutStartDelay;
    bool hasFinishedStrokes;
};

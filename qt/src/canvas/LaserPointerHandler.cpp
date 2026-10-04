/*
 * xournal-qt: port of upstream control/tools/LaserPointerHandler.cpp (the header shadows upstream's, see
 * qt/compat/include/control/tools/LaserPointerHandler.h). Same steps and timing as upstream; QTimer instead of
 * g_timeout_add.
 *
 * @license GNU GPLv2 or later
 */
#include "control/tools/LaserPointerHandler.h"

#include <QTimer>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/tools/StrokeHandler.h"
#include "gui/PageView.h"
#include "gui/inputdevices/PositionInputData.h"
#include "model/Stroke.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "view/overlays/LaserPointerView.h"

static constexpr int FADEOUT_STEP_DURATION = 50;  ///< in ms
static constexpr uint8_t FADEOUT_ALPHA_STEP = 25;
// The total fadeout duration will be  FADEOUT_STEP_DURATION * 255 / FADEOUT_ALPHA_STEP

class TemporaryStrokeHandler: public StrokeHandler {
public:
    using StrokeHandler::finalizeStroke;
    using StrokeHandler::StrokeHandler;
};

LaserPointerHandler::LaserPointerHandler(XojPageView* pageView, Control* control, const PageRef& page):
        viewPool(std::make_shared<xoj::util::DispatchPool<xoj::view::LaserPointerView>>()),
        ctrl(control),
        page(page),
        pageView(pageView),
        fadeoutStartDelay(control->getSettings()->getLaserPointerFadeOutTime()),
        hasFinishedStrokes(false) {}

LaserPointerHandler::~LaserPointerHandler() = default;

std::unique_ptr<xoj::view::OverlayView> LaserPointerHandler::createView(xoj::view::Repaintable* parent) const {
    auto view = std::make_unique<xoj::view::LaserPointerView>(this, parent);
    if (this->strokehandler) {
        view->on(xoj::view::LaserPointerView::START_NEW_STROKE_REQUEST, this->strokehandler.get());
    }
    return view;
}

void LaserPointerHandler::onButtonPressEvent(const PositionInputData& pos, double zoom) {
    this->strokehandler = std::make_unique<TemporaryStrokeHandler>(ctrl, page);
    this->strokehandler->onButtonPressEvent(pos, zoom);
    this->viewPool->dispatch(xoj::view::LaserPointerView::START_NEW_STROKE_REQUEST, strokehandler.get());

    this->fadeoutTimer.reset();
    this->fadeoutAlpha = 255;
}

void LaserPointerHandler::onButtonReleaseEvent(const PositionInputData& pos, double zoom) {
    if (!strokehandler) {
        return;  // This could happen if the tool changed between button press and release
    }
    this->hasFinishedStrokes = true;
    this->strokehandler->finalizeStroke(pos.pressure);
    this->viewPool->dispatch(xoj::view::LaserPointerView::FINISH_STROKE_REQUEST,
                             Range(this->strokehandler->getStroke()->getBoundingBox()));
    this->strokehandler.reset();
    startFadeoutTimer();
}

bool LaserPointerHandler::onMotionNotifyEvent(const PositionInputData& pos, double zoom) {
    return this->strokehandler && this->strokehandler->onMotionNotifyEvent(pos, zoom);
}

void LaserPointerHandler::onSequenceCancelEvent() {
    auto s = std::move(this->strokehandler);
    if (s && s->getStroke()) {
        Range rg(s->getStroke()->getBoundingBox());
        this->viewPool->dispatch(xoj::view::LaserPointerView::INPUT_CANCELLATION_REQUEST, rg);
    }
    if (this->hasFinishedStrokes) {
        startFadeoutTimer();
    }
}

void LaserPointerHandler::startFadeoutTimer() {
    // Upstream: g_timeout_add(fadeoutStartDelay, triggerFadeoutCallback), which starts the steps of the fade
    this->fadeoutTimer = std::make_unique<QTimer>();
    QTimer* timer = this->fadeoutTimer.get();
    QObject::connect(timer, &QTimer::timeout, [this, timer, waited = false]() mutable {
        if (!waited) {
            waited = true;  // upstream's triggerFadeoutCallback: the delay is over, the steps of the fade follow
            timer->setInterval(FADEOUT_STEP_DURATION);
            return;
        }
        fadeoutStep();
    });
    timer->start(static_cast<int>(this->fadeoutStartDelay));
}

void LaserPointerHandler::fadeoutStep() {
    if (this->fadeoutAlpha <= FADEOUT_ALPHA_STEP) {
        this->fadeoutAlpha = 0;
        this->viewPool->dispatchAndClear(xoj::view::LaserPointerView::FINALIZATION_REQUEST);
        // (the timer is the sender of this call: it goes once its signal is done)
        this->fadeoutTimer->stop();
        this->fadeoutTimer.release()->deleteLater();
        this->pageView->deleteLaserPointerHandler();  // WARNING: deletes *this
        return;
    }
    this->fadeoutAlpha -= FADEOUT_ALPHA_STEP;
    this->viewPool->dispatch(xoj::view::LaserPointerView::SET_ALPHA_REQUEST, this->fadeoutAlpha);
}

#include "CanvasInput.h"

#include "PenHover.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "control/ToolEnums.h"
#include "control/settings/ButtonConfig.h"
#include "control/settings/SettingsEnums.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/CursorSelectionType.h"
#include "control/tools/EditSelection.h"
#include "gui/inputdevices/InputUtils.h"
#include "model/Point.h"
#include "undo/UndoRedoHandler.h"
#include "util/Assert.h"
#include "util/Point.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {
/// How long touch still waits once the pen is away ("touch" / "timeout" setting; upstream's HandRecognition waits a
/// second, here nothing: with proximity, touch is ignored while the pen is near anyway)
constexpr int PALM_TIMEOUT_MS = 0;
/// Up to which height the pen counts as near ("touch" / "nearHeight", percent of what the pen can tell; 100: as far
/// as it is noticed at all)
constexpr int NEAR_HEIGHT_PERCENT = 100;
/// Pens that report proximity: touch works again this soon after the pen left.
constexpr double TAP_MAX_MS = 250.0;
constexpr double DOUBLE_TAP_MS = 350.0;    ///< the second tap comes this soon after the first
constexpr double DOUBLE_TAP_PX = 60.0;     ///< ... and this close to it
constexpr double TAP_SLOP_PX = 16.0;  // Krita's TOUCH_SLOP

double monotonicMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

GdkModifierType toGdkModifiers(Qt::KeyboardModifiers m) {
    int s = 0;
    if (m & Qt::ShiftModifier) s |= GDK_SHIFT_MASK;
    if (m & Qt::ControlModifier) s |= GDK_CONTROL_MASK;
    if (m & Qt::AltModifier) s |= GDK_MOD1_MASK;
    if (m & Qt::MetaModifier) s |= GDK_SUPER_MASK;
    return static_cast<GdkModifierType>(s);
}
}  // namespace

CanvasInput::CanvasInput(CanvasView& view, QObject* parent): QObject(parent), view(view) {
    // A finger held still: the same as a right click (the window then offers paste and the rest)
    longPressTimer.setSingleShot(true);
    longPressTimer.setInterval(500);
    connect(&longPressTimer, &QTimer::timeout, this, [this] {
        longPressFired = true;
        Q_EMIT this->view.contextRequested(touchSessionStartPos);
    });
}

// --- tablet --------------------------------------------------------------------------------------------------------

bool CanvasInput::tabletEvent(QTabletEvent* e, QPointF viewPos) {
    lastPenEventMs = monotonicMs();
    PenHover::instance().record(*e);
    if (penNear()) {
        lastNearMs = lastPenEventMs;
    }

    Event ev;
    ev.deviceClass = e->pointerType() == QPointingDevice::PointerType::Eraser ? DeviceClass::Eraser : DeviceClass::Pen;
    ev.viewPos = viewPos;
    ev.pressure = e->pressure();
    ev.timestamp = static_cast<guint32>(e->timestamp());
    ev.device = e->pointingDevice();
    ev.state = toGdkModifiers(e->modifiers());

    // xournal-qt: some pens switch the tool (e.g. the side button selects the eraser tool) through a proximity
    // change, possibly while the tip is down. Finish the running action of the previous tool first.
    if (inputRunning && runningDeviceClass && *runningDeviceClass != ev.deviceClass && lastEvent) {
        actionEnd(*lastEvent);
        deviceClassPressed = false;
        runningDeviceClass.reset();
    }

    hover = viewPos;
    hoverEraser = ev.deviceClass == DeviceClass::Eraser;
    Q_EMIT hoverChanged();

    switch (e->type()) {
        case QEvent::TabletPress:
            if (e->button() == Qt::LeftButton) {
                cancelTouchGesture();  // the pen always wins over touch
                deviceClassPressed = true;
                runningDeviceClass = ev.deviceClass;
                actionStart(ev);
            } else if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
                // Port of StylusInputHandler: a barrel button press changes the tool; during a stroke, the stroke
                // ends and a new one starts with the button's tool.
                (e->button() == Qt::MiddleButton ? modifier2 : modifier3) = true;
                if (inputRunning) {
                    actionEnd(ev);
                    actionStart(ev);
                } else {
                    changeTool(ev);
                }
            }
            break;
        case QEvent::TabletMove:
            if (deviceClassPressed) {
                actionMotion(ev);
            }
            break;
        case QEvent::TabletRelease:
            if (e->button() == Qt::LeftButton) {
                if (deviceClassPressed) {
                    actionEnd(ev);
                }
                deviceClassPressed = false;
                runningDeviceClass.reset();
            } else if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton) {
                (e->button() == Qt::MiddleButton ? modifier2 : modifier3) = false;
                if (inputRunning) {
                    actionEnd(ev);
                    actionStart(ev);
                } else {
                    changeTool(ev);
                }
            }
            break;
        default:
            break;
    }
    return true;
}

void CanvasInput::proximityEvent(bool entered) {
    proximityEverSeen = true;
    lastPenEventMs = monotonicMs();
    if (penNear()) {
        lastNearMs = lastPenEventMs;  // near until now (leaving) / from now on (coming)
    }
    penInProximity = entered;
    PenHover::instance().setProximity(entered);
    if (entered && penNear()) {
        lastNearMs = lastPenEventMs;
    }
    if (!entered) {
        // Upstream: leaving resets the barrel buttons. A stroke still running (lost release) ends here.
        modifier2 = modifier3 = false;
        if (inputRunning && lastEvent) {
            actionEnd(*lastEvent);
        }
        deviceClassPressed = false;
        runningDeviceClass.reset();
        hover.reset();
        Q_EMIT hoverChanged();
    }
}

// --- mouse ---------------------------------------------------------------------------------------------------------

bool CanvasInput::mouseEvent(QMouseEvent* e, QPointF viewPos) {
    const auto type = e->device() ? e->device()->type() : QInputDevice::DeviceType::Mouse;
    if (type != QInputDevice::DeviceType::Mouse && type != QInputDevice::DeviceType::TouchPad) {
        return true;  // synthesized from touch or tablet: the original event is handled
    }
    Event ev;
    ev.deviceClass = DeviceClass::Mouse;
    ev.viewPos = viewPos;
    ev.pressure = Point::NO_PRESSURE;
    ev.timestamp = static_cast<guint32>(e->timestamp());
    ev.device = e->pointingDevice();
    ev.state = toGdkModifiers(e->modifiers());

    switch (e->type()) {
        case QEvent::MouseButtonPress:
            if (deviceClassPressed) {
                break;  // upstream MouseInputHandler: one button at a time
            }
            // The right button shows what can be done here, unless it was given a tool of its own
            if (e->button() == Qt::RightButton &&
                view.getSession().getSettings()->getButtonConfig(BUTTON_MOUSE_RIGHT)->getAction() == TOOL_NONE) {
                // On the setsquare or the compass it drags the tool instead (with a mouse, what two fingers do)
                if (onGeometryTool(viewPos)) {
                    modifier3 = true;
                    deviceClassPressed = true;
                    runningDeviceClass = DeviceClass::Mouse;
                    draggingGeometryTool = true;
                    lastGeometryPos = onGeometryPage(viewPos);
                    return true;
                }
                Q_EMIT view.contextRequested(viewPos);
                return true;
            }
            modifier2 = e->button() == Qt::MiddleButton;
            modifier3 = e->button() == Qt::RightButton;
            deviceClassPressed = true;
            runningDeviceClass = DeviceClass::Mouse;
            actionStart(ev);
            break;
        case QEvent::MouseMove:
            if (deviceClassPressed && runningDeviceClass == DeviceClass::Mouse) {
                actionMotion(ev);
            }
            break;
        case QEvent::MouseButtonRelease:
            if (deviceClassPressed && runningDeviceClass == DeviceClass::Mouse) {
                actionEnd(ev);
                deviceClassPressed = false;
                runningDeviceClass.reset();
                modifier2 = modifier3 = false;
            }
            break;
        default:
            break;
    }
    return true;
}

// --- port of PenInputHandler -----------------------------------------------------------------------------------------

bool CanvasInput::changeTool(const Event& event) {
    // Port of StylusInputHandler::changeTool / MouseInputHandler::changeTool
    DocumentSession& session = view.getSession();
    Settings* settings = session.getSettings();
    ToolHandler* toolHandler = session.getToolHandler();
    bool toolChanged = false;

    if (event.deviceClass == DeviceClass::Mouse) {
        if (modifier2) {
            toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_MOUSE_MIDDLE);
        } else if (modifier3) {
            toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_MOUSE_RIGHT);
        } else {
            toolChanged = toolHandler->pointActiveToolToToolbarTool();
        }
    } else if (event.deviceClass == DeviceClass::Pen && modifier2) {
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_STYLUS_ONE);
    } else if (event.deviceClass == DeviceClass::Pen && modifier3) {
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_STYLUS_TWO);
    } else if (event.deviceClass == DeviceClass::Eraser) {
        toolChanged = InputUtils::applyButton(toolHandler, settings, Button::BUTTON_ERASER);
    } else {
        toolChanged = toolHandler->pointActiveToolToToolbarTool();
    }

    if (toolChanged) {
        ToolType toolType = toolHandler->getToolType();
        if (toolType == TOOL_TEXT) {
            toolHandler->selectTool(toolType);
        }
        toolHandler->fireToolChanged();
    }
    return true;
}

void CanvasInput::updateLastEvent(const Event& event) {
    lastEvent = event;
    if (view.pageAt(event.viewPos)) {
        lastHitEvent = event;
    }
}

QPointF CanvasInput::onGeometryPage(QPointF viewPos) const {
    CanvasPage* page = view.geometryTool().page();
    return page ? pageCoordinates(*page, viewPos) : QPointF();
}

bool CanvasInput::onGeometryTool(QPointF viewPos) const {
    // Measured on the tool's own page: the same spot of another page is not the tool
    return view.geometryTool().visible() && view.geometryTool().contains(onGeometryPage(viewPos));
}

QPointF CanvasInput::pageCoordinates(CanvasPage& page, QPointF viewPos) const {
    const double zoom = view.getViewController().zoom();
    const QPointF topLeft = page.viewRect().topLeft();
    return QPointF((viewPos.x() - topLeft.x()) / zoom, (viewPos.y() - topLeft.y()) / zoom);
}

PositionInputData CanvasInput::getInputDataRelativeToCurrentPage(CanvasPage* page, const Event& event) const {
    // Port of AbstractInputHandler::getInputDataRelativeToCurrentPage
    xoj_assert(page != nullptr);
    PositionInputData pos = {};
    const QPointF topLeft = page->viewRect().topLeft();
    pos.x = event.viewPos.x() - topLeft.x();
    pos.y = event.viewPos.y() - topLeft.y();
    pos.pressure = Point::NO_PRESSURE;
    if (view.getSession().getSettings()->isPressureSensitivity()) {
        pos.pressure = event.pressure;
    }
    pos.state = event.state;
    pos.timestamp = event.timestamp;
    pos.deviceId = DeviceId(static_cast<const GdkDevice*>(event.device));
    // Drawing while the setsquare or the compass is out: the line follows its nearest edge. Only for the pen and
    // the highlighter - the eraser must reach everything, also what lies under the tool.
    const ToolType drawing = view.getSession().getToolHandler()->getToolType();
    if (view.geometryTool().visible() && page == view.geometryTool().page() &&
        (drawing == TOOL_PEN || drawing == TOOL_HIGHLIGHTER)) {
        const double zoom = view.getViewController().zoom();
        const QPointF snapped = view.geometryTool().snap(QPointF(pos.x / zoom, pos.y / zoom));
        pos.x = snapped.x() * zoom;
        pos.y = snapped.y() * zoom;
    }
    return pos;
}

double CanvasInput::inferPressureValue(const PositionInputData& pos, CanvasPage* page) {
    // Port of PenInputHandler::inferPressureValue
    PositionInputData lastPos = getInputDataRelativeToCurrentPage(page, *this->lastEvent);

    double dt = (pos.timestamp - lastPos.timestamp) / 10.0;
    double distance = xoj::util::Point<double>(pos.x, pos.y).distance(xoj::util::Point<double>(lastPos.x, lastPos.y));
    double inverseSpeed = dt / (distance + 0.001);

    // Arctan is used here for its sigmoid-like shape, so that lim inverseSpeed->infinity (newPressure) is finite.
    double newPressure = 3.142 / 2.0 + std::atan(inverseSpeed * 3.14 - 1.3);
    // This weighted average both smooths abrupt changes and causes an initial increase in pressure.
    newPressure = std::min(newPressure, 2.0) / 5.0 + this->lastPressure * 4.0 / 5.0;
    // Handle the single-point case.
    if (distance == 0) {
        newPressure = std::sqrt(dt / 10.0) - 0.1;
    }
    this->lastPressure = newPressure;
    return (newPressure * 1.1 + 0.8) / 2.0;
}

double CanvasInput::filterPressure(const PositionInputData& pos, CanvasPage* page) {
    // Port of PenInputHandler::filterPressure
    if (pressureMode == PressureMode::NO_PRESSURE) {
        return Point::NO_PRESSURE;
    }
    double filteredPressure;
    if (pressureMode == PressureMode::INFERRED_PRESSURE) {
        filteredPressure = inferPressureValue(pos, page);
    } else {
        // On some devices, the pressure value of some events is missing. Use the last recorded pressure value then.
        if (pos.pressure == Point::NO_PRESSURE) {
            filteredPressure = lastPressure;
        } else {
            filteredPressure = pos.pressure;
            lastPressure = pos.pressure;
        }
    }
    Settings* settings = view.getSession().getSettings();
    xoj_assert(settings->getMinimumPressure() >= 0.01);
    return std::max(settings->getMinimumPressure(), filteredPressure * settings->getPressureMultiplier());
}

void CanvasInput::handleScrollEvent(const Event& event) {
    // Port of PenInputHandler::handleScrollEvent: the hand tool drags the document with the pointer.
    if (lastEvent) {
        view.getViewController().panBy(event.viewPos - lastEvent->viewPos);
    }
}

bool CanvasInput::actionStart(const Event& event) {
    CanvasPage* currentPage = view.pageAt(event.viewPos);
    this->updateLastEvent(event);

    if (!changeTool(event)) {
        return false;
    }
    this->lastPressure = 0.0;  // used for pressure inference

    ToolHandler* toolHandler = view.getSession().getToolHandler();
    const ToolType toolType = toolHandler->getToolType();
    this->inputRunning = toolType != TOOL_IMAGE;
    view.getViewController().stopMomentum();

    this->sequenceStartPage = currentPage;
    this->pressViewPos = event.viewPos;
    this->pressTimeMs = monotonicMs();
    if (toolType == TOOL_HAND) {
        return true;  // the hand tool does not change the selection (scrolling keeps it)
    }

    // Port of PenInputHandler::actionStart (selection part): a press on the selection moves, resizes or rotates it
    // (or deletes it: the × button); a press elsewhere ends it.
    if (EditSelection* selection = view.getSelection()) {
        bool changeSelection = true;
        if ((event.state & GDK_SHIFT_MASK) && isSelectToolTypeSingleLayer(toolType)) {
            changeSelection = false;  // Shift with a select tool adds to the selection
        }
        if (changeSelection) {
            CanvasPage* selectionPage = static_cast<CanvasPage*>(selection->getView());
            PositionInputData selectionPos = this->getInputDataRelativeToCurrentPage(selectionPage, event);
            const CursorSelectionType selType =
                    selection->getSelectionTypeForPos(selectionPos.x, selectionPos.y, view.getZoom());
            if (selType) {
                if (selType == CURSOR_SELECTION_MOVE && modifier3) {
                    selection->copySelection();
                }
                selection->mouseDown(selType, selectionPos.x, selectionPos.y);
                return true;
            }
            view.clearSelection();
            changeTool(event);
            // Stop here: a tap outside the selection only deselects, it does not also draw.
            if (toolHandler->isDrawingTool()) {
                return true;
            }
        }
    }

    // Selected PDF text: a press somewhere else unselects it (a press on a knob never reaches the canvas).
    if (view.hasPdfTextSelection() && !view.pdfTextSelectionContains(event.viewPos)) {
        view.clearPdfTextSelection();
        if (toolHandler->isDrawingTool()) {
            return true;  // that press only unselects, as with a selection of elements
        }
    }

    // The setsquare or the compass: only a press with the right mouse button takes it along (like two fingers on
    // it). Pen and left button draw, and the line follows the nearest edge of the tool.
    if (modifier3 && onGeometryTool(event.viewPos)) {
        draggingGeometryTool = true;
        lastGeometryPos = onGeometryPage(event.viewPos);
        return true;
    }

    if (currentPage) {
        PositionInputData pos = this->getInputDataRelativeToCurrentPage(currentPage, event);
        if (pos.pressure != Point::NO_PRESSURE) {
            pressureMode = PressureMode::DEVICE_PRESSURE;
        } else if (view.getSession().getSettings()->isPressureGuessingEnabled()) {
            pressureMode = PressureMode::INFERRED_PRESSURE;
        } else {
            pressureMode = PressureMode::NO_PRESSURE;
        }
        pos.pressure = this->filterPressure(pos, currentPage);
        return currentPage->onButtonPressEvent(pos);
    }
    return true;
}

bool CanvasInput::actionMotion(const Event& event) {
    ToolHandler* toolHandler = view.getSession().getToolHandler();
    this->changeTool(event);

    if (draggingGeometryTool) {
        // In the coordinates of the tool's page, wherever the pointer is (over another page, between the pages)
        if (view.geometryTool().page()) {
            const QPointF onPage = onGeometryPage(event.viewPos);
            view.geometryTool().moveBy(onPage - lastGeometryPos);
            lastGeometryPos = onPage;
        }
        this->updateLastEvent(event);
        return true;
    }

    if (toolHandler->getToolType() == TOOL_HAND) {
        if (this->deviceClassPressed) {
            this->handleScrollEvent(event);
        }
        this->updateLastEvent(event);
        return true;
    }

    // Port of PenInputHandler::actionMotion (selection part)
    if (EditSelection* selection = view.getSelection()) {
        const bool isShiftDown = event.state & GDK_SHIFT_MASK;
        bool handleSelectionMove = true;
        if (isSelectToolTypeSingleLayer(toolHandler->getToolType()) && !selection->isMoving() &&
            (isShiftDown || this->deviceClassPressed)) {
            handleSelectionMove = false;  // drawing another rectangle/lasso (to add with Shift)
        }
        if (handleSelectionMove) {
            CanvasPage* selectionPage = static_cast<CanvasPage*>(selection->getView());
            PositionInputData pos = this->getInputDataRelativeToCurrentPage(selectionPage, event);
            if (selection->isMoving()) {
                selection->mouseMove(pos.x, pos.y, pos.isAltDown());
            }
            this->updateLastEvent(event);
            return true;
        }
    }

    // Check if page was left / entered
    CanvasPage* lastEventPage = lastEvent ? view.pageAt(lastEvent->viewPos) : nullptr;
    CanvasPage* lastHitEventPage = lastHitEvent ? view.pageAt(lastHitEvent->viewPos) : nullptr;
    CanvasPage* currentPage = view.pageAt(event.viewPos);

    if (!toolHandler->isSinglePageTool()) {
        /*
         * Upstream: when the input sequence moved from one page to another without stopping, fake an end point in the
         * old page and a start point in the new page (only once the new page was entered).
         */
        if (this->deviceClassPressed && currentPage && currentPage != sequenceStartPage && lastHitEventPage) {
            this->actionEnd(*this->lastHitEvent);
            this->updateLastEvent(event);
            bool result = this->actionStart(event);
            this->updateLastEvent(event);
            return result;
        }
        // The input sequence started outside of a page and moved into one: fake a start point in the page.
        if (this->deviceClassPressed && currentPage && !lastEventPage && !lastHitEventPage) {
            bool result = this->actionStart(event);
            this->updateLastEvent(event);
            return result;
        }
    }

    // Single-page tools always work on the page where they started.
    if (this->sequenceStartPage && toolHandler->isSinglePageTool()) {
        PositionInputData pos = getInputDataRelativeToCurrentPage(sequenceStartPage, event);
        if (!toolHandler->acceptsOutOfPageEvents()) {
            const QRectF r = sequenceStartPage->viewRect();
            pos.x = std::clamp(pos.x, 0.0, r.width());
            pos.y = std::clamp(pos.y, 0.0, r.height());
        }
        pos.pressure = this->filterPressure(pos, sequenceStartPage);
        bool result = sequenceStartPage->onMotionNotifyEvent(pos);
        this->updateLastEvent(event);
        return result;
    }

    if (currentPage) {
        PositionInputData pos = getInputDataRelativeToCurrentPage(currentPage, event);
        pos.pressure = this->filterPressure(pos, currentPage);
        bool result = currentPage->onMotionNotifyEvent(pos);
        this->updateLastEvent(event);
        return result;
    }

    this->updateLastEvent(event);
    return false;
}

bool CanvasInput::actionEnd(const Event& event) {
    ToolHandler* toolHandler = view.getSession().getToolHandler();
    if (EditSelection* selection = view.getSelection()) {
        selection->mouseUp();
    }

    if (this->sequenceStartPage && toolHandler->isSinglePageTool()) {
        PositionInputData pos = getInputDataRelativeToCurrentPage(this->sequenceStartPage, event);
        pos.pressure = this->filterPressure(pos, this->sequenceStartPage);
        this->sequenceStartPage->onButtonReleaseEvent(pos);
    } else {
        // Use the last active page if there is no page under the pointer (input left the page and stopped outside).
        CanvasPage* currentPage = view.pageAt(event.viewPos);
        if (!currentPage && this->lastHitEvent) {
            currentPage = view.pageAt(this->lastHitEvent->viewPos);
        }
        if (currentPage) {
            PositionInputData pos = getInputDataRelativeToCurrentPage(currentPage, event);
            pos.pressure = this->filterPressure(pos, currentPage);
            currentPage->onButtonReleaseEvent(pos);
        }
    }

    // A tap with the hand or a select tool that selected nothing: maybe a PDF link.
    const ToolType tt = toolHandler->getToolType();
    const bool tapTool = tt == TOOL_HAND || tt == TOOL_SELECT_RECT || tt == TOOL_SELECT_REGION ||
                         tt == TOOL_SELECT_OBJECT || tt == TOOL_SELECT_PDF_TEXT_LINEAR ||
                         tt == TOOL_SELECT_PDF_TEXT_RECT;
    if (tapTool && !view.getSelection() && monotonicMs() - pressTimeMs <= TAP_MAX_MS * 1.5 &&
        std::hypot(event.viewPos.x() - pressViewPos.x(), event.viewPos.y() - pressViewPos.y()) <= TAP_SLOP_PX / 2) {
        view.tapAt(event.viewPos);
    }

    draggingGeometryTool = false;
    this->sequenceStartPage = nullptr;
    if (toolHandler->pointActiveToolToToolbarTool()) {
        toolHandler->fireToolChanged();
    }
    this->inputRunning = false;
    return false;
}

// --- touch: navigation, gestures, palm rejection ------------------------------------------------------------------

// --- a finger on a selection of elements: the handles are for fingers too, not only for pen and mouse ---

bool CanvasInput::startTouchSelection(QPointF viewPos) {
    EditSelection* selection = view.getSelection();
    if (!selection) {
        return false;
    }
    auto* page = static_cast<CanvasPage*>(selection->getView());
    if (!page) {
        return false;
    }
    Event ev;
    ev.viewPos = viewPos;
    ev.pressure = Point::NO_PRESSURE;
    const PositionInputData pos = getInputDataRelativeToCurrentPage(page, ev);
    const CursorSelectionType type = selection->getSelectionTypeForPos(pos.x, pos.y, view.getZoom());
    if (!type) {
        return false;  // beside the selection: the finger scrolls as usual
    }
    selection->mouseDown(type, pos.x, pos.y);
    return true;
}

void CanvasInput::moveTouchSelection(QPointF viewPos) {
    EditSelection* selection = view.getSelection();
    if (!selection) {
        touchSelection = false;
        return;
    }
    auto* page = static_cast<CanvasPage*>(selection->getView());
    if (!page || !selection->isMoving()) {
        return;
    }
    Event ev;
    ev.viewPos = viewPos;
    ev.pressure = Point::NO_PRESSURE;
    const PositionInputData pos = getInputDataRelativeToCurrentPage(page, ev);
    selection->mouseMove(pos.x, pos.y, false);
}

void CanvasInput::endTouchSelection() {
    if (EditSelection* selection = view.getSelection()) {
        selection->mouseUp();
    }
    touchSelection = false;
    touchSelectionId = -1;
}

bool CanvasInput::penNear() const {
    if (deviceClassPressed && runningDeviceClass != DeviceClass::Mouse) {
        return true;  // writing
    }
    if (!penInProximity) {
        return false;
    }
    // A pen that tells its height is near only up to the height chosen in the settings
    int percent = NEAR_HEIGHT_PERCENT;
    view.getSession().getSettings()->getCustomElement("touch").getInt("nearHeight", percent);
    const PenHover& hover = PenHover::instance();
    return percent >= 100 || !hover.reportsHeight() || hover.height() * 100 <= percent;
}

bool CanvasInput::touchBlocked() const {
    // While the pen is near, touch is ignored (a hand resting on the screen while writing). Once it is away, touch
    // waits the time set in the settings (none by default); pens that never tell whether they are near count from
    // their last event instead.
    if (penNear()) {
        return true;
    }
    int waitMs = PALM_TIMEOUT_MS;
    view.getSession().getSettings()->getCustomElement("touch").getInt("timeout", waitMs);
    const double since = proximityEverSeen ? lastNearMs : lastPenEventMs;
    return monotonicMs() - since < waitMs;
}

void CanvasInput::cancelTouchGesture() {
    if (pinching) {
        view.getViewController().pinchEnd();
    }
    pinching = false;
    panning = false;
    touchSessionIgnored = !touches.empty();  // ignore the rest of the current touch session
    velocitySamples.clear();
    view.getViewController().stopMomentum();
}

void CanvasInput::undo() {
    DocumentSession& s = view.getSession();
    if (s.getUndoRedoHandler()->canUndo()) {
        s.clearSelectionEndText();
        s.getUndoRedoHandler()->undo();
    }
}

void CanvasInput::redo() {
    DocumentSession& s = view.getSession();
    if (s.getUndoRedoHandler()->canRedo()) {
        s.clearSelectionEndText();
        s.getUndoRedoHandler()->redo();
    }
}

bool CanvasInput::touchEvent(QTouchEvent* e, const MapToView& sceneToView) {
    ViewController& vc = view.getViewController();
    const double now = monotonicMs();

    if (e->type() == QEvent::TouchCancel) {
        if (pinching) {
            vc.pinchEnd();
        }
        if (toolGesture) {
            view.geometryTool().endGesture();
            toolGesture = false;
            toolGestureFingers = {-1, -1};
        }
        touches.clear();
        pinching = panning = false;
        touchSessionIgnored = false;
        return true;
    }

    if (touches.empty()) {
        // First finger: a new session. Palm rejection decides for the whole session.
        touchSessionIgnored = touchBlocked();
        if (!touchSessionIgnored) {
            vc.stopMomentum();
        }
        touchSessionMaxPoints = 0;
        touchSessionStartMs = now;
        if (!e->points().isEmpty()) {
            touchSessionStartPos = sceneToView(e->points().first().scenePosition());
            // On a selection of elements (inside it or on one of its handles) the finger works it, it does not scroll
            if (!touchSessionIgnored && startTouchSelection(touchSessionStartPos)) {
                touchSelection = true;
                touchSelectionId = e->points().first().id();
            }
        }
        touchSessionTravel = 0;
        longPressFired = false;
        if (!touchSelection) {
            // (On a selection a finger drags it, held still as well: its actions are in the selection's pill)
            longPressTimer.start();
        }
        velocitySamples.clear();
    }

    std::vector<int> released;
    for (const auto& pt: e->points()) {
        const QPointF pos = sceneToView(pt.scenePosition());
        if (pt.state() == QEventPoint::State::Pressed) {
            touches[pt.id()] = TouchPoint{pos};
            continue;
        }
        if (pt.state() == QEventPoint::State::Released) {
            released.push_back(pt.id());
        }
        if (auto it = touches.find(pt.id()); it != touches.end()) {
            it->second.pos = pos;
        }
    }
    touchSessionMaxPoints = std::max(touchSessionMaxPoints, static_cast<int>(touches.size()));
    if (touchSessionMaxPoints > 1 || touchSessionTravel > TAP_SLOP_PX) {
        longPressTimer.stop();  // moved or a second finger: no long press
    }
    if (touchSessionMaxPoints >= 4 && !touchSessionIgnored) {
        // Four fingers or more: a gesture of the window (the pages, all documents), the canvas keeps still.
        if (pinching) {
            vc.pinchEnd();
            pinching = false;
        }
        panning = false;
        touchSessionIgnored = true;
        velocitySamples.clear();
    }

    if (touchSelection && !touchSessionIgnored) {
        // The finger that started on the selection leads it; more fingers neither zoom nor scroll now
        if (auto it = touches.find(touchSelectionId); it != touches.end()) {
            moveTouchSelection(it->second.pos);
        }
        panning = false;
    } else if (!touchSessionIgnored) {
        std::vector<QPointF> pts;
        std::vector<int> ids;
        QPointF centroid;
        for (const auto& [id, tp]: touches) {
            if (std::find(released.begin(), released.end(), id) == released.end()) {
                pts.push_back(tp.pos);
                ids.push_back(id);
                centroid += tp.pos;
            }
        }
        if (!pts.empty()) {
            centroid /= static_cast<double>(pts.size());
        }
        // Two fingers on the setsquare or the compass: this touch belongs to the tool until the last finger is up.
        // It follows the first two fingers (a third one changes nothing).
        const QPointF pairCentre = pts.size() >= 2 ? (pts[0] + pts[1]) / 2 : centroid;
        if (!toolGesture && !pinching && pts.size() >= 2 && onGeometryTool(pairCentre)) {
            toolGesture = true;
            toolGestureFingers = {-1, -1};
        }
        if (toolGesture) {
            if (pts.size() >= 2 && view.geometryTool().visible()) {
                const double dist = std::hypot(pts[0].x() - pts[1].x(), pts[0].y() - pts[1].y());
                const double angle = std::atan2(pts[1].y() - pts[0].y(), pts[1].x() - pts[0].x());
                const std::pair<int, int> fingers{ids[0], ids[1]};
                if (fingers != toolGestureFingers) {
                    // Other fingers than before (one lifted, one set down): measured anew from here, nothing jumps
                    view.geometryTool().beginGesture(onGeometryPage(pairCentre), angle, dist);
                    toolGestureFingers = fingers;
                } else {
                    view.geometryTool().moveGesture(onGeometryPage(pairCentre), angle, dist);
                }
            } else {
                toolGestureFingers = {-1, -1};  // one finger left: it rests (neither the tool nor the page moves)
            }
            panning = false;
        } else if (pts.size() >= 2) {
            const double dist = std::hypot(pts[0].x() - pts[1].x(), pts[0].y() - pts[1].y());
            if (!pinching) {
                vc.pinchBegin(centroid, dist);
                pinchStartDistance = dist;
                pinching = true;
            } else {
                touchSessionTravel += std::hypot(centroid.x() - lastCentroid.x(), centroid.y() - lastCentroid.y());
                // Upstream's "zoom gestures" setting: without it, two fingers only pan.
                const bool zoom = view.getSession().getSettings()->isZoomGesturesEnabled();
                vc.pinchUpdate(centroid, zoom ? dist : pinchStartDistance);
            }
            lastCentroid = centroid;
            panning = false;
        } else if (pts.size() == 1) {
            if (pinching) {
                // 2 -> 1 fingers: continue as a pan from the remaining finger without a jump.
                vc.pinchEnd();
                pinching = false;
                panning = false;
                velocitySamples.clear();
            }
            if (!panning) {
                panning = true;
                lastCentroid = centroid;
            }
            const QPointF delta = centroid - lastCentroid;
            touchSessionTravel += std::hypot(delta.x(), delta.y());
            if (!delta.isNull()) {
                vc.panBy(delta);
            }
            lastCentroid = centroid;
            velocitySamples.push_back({now, centroid});
            while (velocitySamples.size() > 2 && velocitySamples.front().t < now - 100.0) {
                velocitySamples.erase(velocitySamples.begin());
            }
        }
    }

    for (int id: released) {
        touches.erase(id);
    }
    if (touches.size() != 1) {
        panning = false;
    }
    if (touches.empty() && touchSelection) {
        endTouchSelection();
        longPressTimer.stop();
        longPressFired = false;
        touchSessionIgnored = false;
        velocitySamples.clear();
        return true;  // no tap, no double tap, no fling: that session belonged to the selection
    }
    if (touches.empty() && toolGesture) {
        view.geometryTool().endGesture();
        toolGesture = false;
        toolGestureFingers = {-1, -1};
        longPressTimer.stop();
        longPressFired = false;
        touchSessionIgnored = false;
        velocitySamples.clear();
        return true;  // no tap, no two-finger undo, no double tap, no fling: that touch handled the tool
    }
    if (touches.empty()) {
        longPressTimer.stop();
        if (longPressFired) {
            longPressFired = false;
            touchSessionIgnored = false;
            velocitySamples.clear();
            return true;  // the window took over
        }
        if (!touchSessionIgnored) {
            if (pinching) {
                vc.pinchEnd();
                pinching = false;
            }
            const double duration = now - touchSessionStartMs;
            if (duration <= TAP_MAX_MS && touchSessionTravel <= TAP_SLOP_PX && touchSessionMaxPoints >= 2) {
                if (touchSessionMaxPoints == 2) {
                    undo();
                } else if (touchSessionMaxPoints == 3) {
                    redo();
                }
            } else if (duration <= TAP_MAX_MS && touchSessionTravel <= TAP_SLOP_PX && touchSessionMaxPoints == 1) {
                if (view.hasPdfTextSelection() && !view.pdfTextSelectionContains(touchSessionStartPos)) {
                    view.clearPdfTextSelection();  // a tap beside the selected text unselects it and nothing else
                    lastTapMs = 0;
                } else if (view.tapAt(touchSessionStartPos)) {
                    lastTapMs = 0;  // it was a PDF link: never the first tap of a double tap
                } else if (now - lastTapMs <= DOUBLE_TAP_MS &&
                           std::hypot(touchSessionStartPos.x() - lastTapPos.x(),
                                      touchSessionStartPos.y() - lastTapPos.y()) <= DOUBLE_TAP_PX) {
                    view.doubleTapAt(touchSessionStartPos);
                    lastTapMs = 0;
                } else {
                    lastTapMs = now;
                    lastTapPos = touchSessionStartPos;
                }
            } else if (velocitySamples.size() >= 2) {
                const auto& a = velocitySamples.front();
                const auto& b = velocitySamples.back();
                const double dt = b.t - a.t;
                if (dt > 5.0 && now - b.t < 50.0) {
                    vc.fling((b.pos - a.pos) / dt);
                }
            }
        }
        touchSessionIgnored = false;
        velocitySamples.clear();
    }
    return true;
}

// --- wheel / touchpad ----------------------------------------------------------------------------------------------

bool CanvasInput::wheelEvent(QWheelEvent* e, QPointF viewPos) {
    ViewController& vc = view.getViewController();
    if (e->modifiers() & Qt::ControlModifier) {
        vc.stopMomentum();
        vc.zoomBy(std::pow(1.0015, e->angleDelta().y()), viewPos);
        return true;
    }
    const QPointF delta =
            !e->pixelDelta().isNull() ? QPointF(e->pixelDelta()) : QPointF(e->angleDelta()) / 120.0 * 48.0;
    const double now = monotonicMs();

    switch (e->phase()) {
        case Qt::NoScrollPhase:
            // Mouse wheel (no gesture phases): plain scrolling.
            vc.stopMomentum();
            vc.panBy(delta);
            break;
        case Qt::ScrollBegin:
            // Fingers on the touchpad: stop a running fling.
            vc.stopMomentum();
            wheelSamples.clear();
            if (!delta.isNull()) {
                vc.panBy(delta);
                wheelSamples.push_back({now, delta});
            }
            break;
        case Qt::ScrollUpdate:
            vc.panBy(delta);
            wheelSamples.push_back({now, delta});
            while (wheelSamples.size() > 2 && wheelSamples.front().t < now - 120.0) {
                wheelSamples.erase(wheelSamples.begin());
            }
            break;
        case Qt::ScrollMomentum:
            // The platform generates the momentum itself (macOS): just follow it.
            vc.panBy(delta);
            break;
        case Qt::ScrollEnd: {
            // Fingers lifted: continue with the recent velocity (like GTK's kinetic scrolling on touchpads).
            if (!delta.isNull()) {
                vc.panBy(delta);
                wheelSamples.push_back({now, delta});
            }
            // Only if the fingers were still moving when lifted.
            if (wheelSamples.size() >= 2 && now - wheelSamples.back().t < 60.0) {
                const double dt = wheelSamples.back().t - wheelSamples.front().t;
                QPointF distance;
                for (size_t i = 1; i < wheelSamples.size(); ++i) {
                    distance += wheelSamples[i].delta;  // the first delta happened before the first timestamp
                }
                if (dt > 5.0) {
                    vc.fling(distance / dt);
                }
            }
            wheelSamples.clear();
            break;
        }
    }
    return true;
}

bool CanvasInput::nativeGestureEvent(QNativeGestureEvent* e, QPointF viewPos) {
    if (e->gestureType() == Qt::ZoomNativeGesture) {
        view.getViewController().zoomBy(1.0 + e->value(), viewPos);
    } else if (e->gestureType() == Qt::EndNativeGesture) {
        view.getViewController().zoomGestureEnded();  // (the fingers left the touchpad)
    }
    return true;
}

}  // namespace xqt

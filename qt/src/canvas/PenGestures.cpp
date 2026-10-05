#include "PenGestures.h"

#include <algorithm>
#include <memory>
#include <utility>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/settings/SettingsEnums.h"
#include "control/shaperecognizer/ShapeRecognizer.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/XojPage.h"
#include "gui/inputdevices/PositionInputData.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "view/overlays/StrokeToolView.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "ScratchOut.h"

namespace xqt {

namespace pengestures {
bool holdToStraighten(Settings& s) {
    bool on = true;
    s.getCustomElement("xournalQt").getBool("holdToStraighten", on);
    return on;
}

void setHoldToStraighten(Settings& s, bool on) {
    s.getCustomElement("xournalQt").setBool("holdToStraighten", on);
    s.customSettingsChanged();
}

int holdTime(Settings& s) {
    int ms = DEFAULT_HOLD_MS;
    s.getCustomElement("xournalQt").getInt("holdToStraightenTime", ms);
    return std::clamp(ms, MIN_HOLD_MS, MAX_HOLD_MS);
}

void setHoldTime(Settings& s, int ms) {
    s.getCustomElement("xournalQt").setInt("holdToStraightenTime", std::clamp(ms, MIN_HOLD_MS, MAX_HOLD_MS));
    s.customSettingsChanged();
}
bool scratchOut(Settings& s) {
    bool on = false;
    s.getCustomElement("xournalQt").getBool("scratchOut", on);
    return on;
}

void setScratchOut(Settings& s, bool on) {
    s.getCustomElement("xournalQt").setBool("scratchOut", on);
    s.customSettingsChanged();
}
}  // namespace pengestures

GestureStrokeHandler::GestureStrokeHandler(Control* control, const PageRef& page): StrokeHandler(control, page) {}

bool GestureStrokeHandler::straighten(double restRadius) {
    if (!stroke || done || stroke->getPointCount() < 3) {
        return false;
    }
    // What the recogniser sees: the stroke without the points of the hand shaking while the pen rested
    auto shapeOf = [this, restRadius]() -> std::unique_ptr<Stroke> {
        std::vector<Point> pts = stroke->getPointVector();
        const Point last = pts.back();
        size_t keep = pts.size();
        while (keep > 2 && pts[keep - 2].lineLengthTo(last) <= restRadius) {
            --keep;
        }
        pts.resize(keep);
        auto drawn = stroke->cloneStroke();
        drawn->setPointVector(std::move(pts));
        return ShapeRecognizer().recognizePatterns(drawn.get(), control->getSettings()->getStrokeRecognizerMinSize());
    };
    std::unique_ptr<Stroke> shape = shapeOf();
    if (!shape) {
        return false;  // (nothing recognised: the stroke goes on; the stabilizer is left as it was)
    }
    // The stroke is finished here (the stabilizer's gap filled), and recognised again with its last points
    finalizeStroke(Point::NO_PRESSURE);
    if (auto final = shapeOf()) {
        shape = std::move(final);
    }
    done = true;

    // As upstream's StrokeHandler::onButtonReleaseEvent in the shape recognizer mode: the stroke's insertion, then
    // its replacement (undo: the freehand stroke again, then nothing)
    Layer* layer = page->getSelectedLayer();
    control->getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, stroke.get()));
    if (control->getSettings()->getEmptyLastPageAppend() == EmptyLastPageAppendType::OnDrawOfLastPage) {
        auto* doc = control->getDocument();
        doc->lock_shared();
        const auto pdfPageCount = doc->getPdfPageCount();
        const auto lastPage = doc->getPageCount() - 1;
        doc->unlock_shared();
        if (pdfPageCount == 0 && control->getCurrentPageNo() == lastPage) {
            control->insertNewPage(lastPage + 1, true);
        }
    }
    strokeRecognizerDetected(std::move(shape), layer);  // (the view goes; the stroke is the undo step's now)
    return true;
}

void GestureStrokeHandler::onButtonPressEvent(const PositionInputData& pos, double zoom) {
    StrokeHandler::onButtonPressEvent(pos, zoom);
    pressTime = pos.timestamp;
}

void GestureStrokeHandler::onButtonReleaseEvent(const PositionInputData& pos, double zoom) {
    if (stroke && !done && stroke->getToolType() == StrokeTool::PEN &&
        pengestures::scratchOut(*control->getSettings()) && scratchOut(pos, zoom)) {
        return;
    }
    StrokeHandler::onButtonReleaseEvent(pos, zoom);
}

bool GestureStrokeHandler::scratchOut(const PositionInputData& release, double zoom) {
    const std::vector<Point>& zigzag = stroke->getPointVector();
    const scratchout::Shape shape = scratchout::analyse(zigzag);
    const double durationMs = static_cast<double>(static_cast<gint32>(release.timestamp - pressTime));
    if (!shape.zigzag || !scratchout::quick(shape.length, durationMs, zoom)) {
        return false;
    }
    Layer* layer = page->getSelectedLayer();
    Document* doc = control->getDocument();
    std::vector<std::pair<Element::Index, const Element*>> victims;
    doc->lock_shared();
    for (const Element* e: scratchout::covered(*layer, zigzag, shape, stroke->getWidth())) {
        victims.emplace_back(layer->indexOf(e), e);
    }
    doc->unlock_shared();
    if (victims.empty()) {
        return false;  // (over nothing: a stroke like any other)
    }

    // The strokes go (the last first, so that each keeps its place for the undo), in one undo step
    std::sort(victims.begin(), victims.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    auto undo = std::make_unique<DeleteUndoAction>(page, true);
    Range range(stroke->getBoundingBox());
    doc->lock();
    for (const auto& [index, e]: victims) {
        range = range.unite(Range(e->getBoundingBox()));
        auto [owned, pos] = layer->removeElement(e);
        undo->addElement(layer, std::move(owned), pos);
    }
    doc->unlock();
    // The zigzag is not kept: its view goes without being drawn into the page
    getViewPool()->dispatchAndClear(xoj::view::StrokeToolView::CANCELLATION_REQUEST, range);
    stroke.reset();
    for (const auto& victim: victims) {
        page->fireElementChanged(victim.second);
    }
    control->getUndoRedoHandler()->addUndoAction(std::move(undo));
    return true;
}

}  // namespace xqt

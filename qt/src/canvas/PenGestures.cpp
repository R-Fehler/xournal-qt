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
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

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

}  // namespace xqt

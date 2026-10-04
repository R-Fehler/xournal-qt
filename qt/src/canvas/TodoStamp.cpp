#include "TodoStamp.h"

#include <cmath>
#include <shared_mutex>

#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasView.h"
#include "MdBox.h"
#include "MdTasks.h"

namespace xqt {

namespace todostamp {
namespace {
bool armedNow = false;  // (the UI thread's)
std::function<void()> whenPlaced;
}  // namespace

void arm(std::function<void()> placed) {
    armedNow = true;
    whenPlaced = std::move(placed);
}

void disarm() {
    armedNow = false;
    whenPlaced = nullptr;
}

bool isArmed() { return armedNow; }

void stamped() {
    auto placed = std::move(whenPlaced);
    disarm();
    if (placed) {
        placed();
    }
}
}  // namespace todostamp

bool CanvasView::addTodoStamp(size_t pNr, QPointF onPage) {
    Document* doc = session.getDocument();
    PageRef page;
    Layer* layer = nullptr;
    {
        std::shared_lock lock(*doc);
        if (pNr >= doc->getPageCount()) {
            return false;
        }
        page = doc->getPage(pNr);
        layer = md::markdownLayer(page);
    }
    if (!layer) {
        // The page's Markdown layer (made if needed: at the bottom, the selected layer stays selected)
        Layer::Index selected = 0;
        {
            std::shared_lock lock(*doc);
            selected = page->getSelectedLayerId();
        }
        layer = new Layer();
        layer->setName(std::string(xoj::markdown::LAYER_NAME));
        session.getLayerController()->insertLayer(page, layer, 0);  // (locks the document)
        std::unique_lock lock(*doc);
        page->setSelectedLayerId(selected > 0 ? selected + 1 : 0);
    }
    auto text = std::make_unique<Text>();
    text->setText(std::string(md::tasks::STAMP));
    const std::string family = session.getSettings()->getFont().getName();
    const double size = std::max(8.0, markdownTextSize);
    text->setFont(XojFont(family.empty() ? "Sans" : family, size));
    text->setWrap(std::ceil(2.2 * size));  // (the check box only: the to-do is written beside it)
    // Its check box where the tap was
    double dx = 0.6 * size;
    double dy = 0.6 * size;
    if (const auto box = md::checkBoxRect(*text, md::tasks::find(text->getText()).front().mark)) {
        dx = box->x + box->width / 2;
        dy = box->y + box->height / 2;
    }
    const double x = std::clamp(onPage.x() - dx, 0.0, std::max(0.0, page->getWidth() - 2 * size));
    const double y = std::clamp(onPage.y() - dy, 0.0, std::max(0.0, page->getHeight() - 2 * size));
    text->setTransformation(xoj::util::Matrix::TRANSLATION(x, y));
    const Text* raw = text.get();
    {
        std::unique_lock lock(*doc);
        layer->addElement(std::move(text));
    }
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    page->firePageChanged();
    session.firePageChanged(pNr);
    Q_EMIT updateRequested();
    return true;
}

}  // namespace xqt

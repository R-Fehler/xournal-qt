#include "MarkdownSession.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "control/Control.h"
#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"
#include "view/MarkdownHook.h"

#include "MdBox.h"
#include "TextFlow.h"

namespace xqt {

namespace {
/// The texts of the Markdown layer before / after an edit (the same action undoes and redoes: it swaps).
class MarkdownUndoAction final: public UndoAction {
public:
    MarkdownUndoAction(const PageRef& p, Layer* layer, std::vector<ElementPtr> other):
            UndoAction("MarkdownUndoAction"), layer(layer), other(std::move(other)) {
        this->page = p;
    }
    bool undo(Control* control) override {
        swap(control);
        return true;
    }
    bool redo(Control* control) override {
        swap(control);
        return true;
    }
    std::string getText() override { return "Markdown"; }

private:
    void swap(Control* control) {
        Document* doc = control->getDocument();
        doc->lock();
        auto& elements = layer->getElements();
        std::vector<ElementPtr> texts;
        for (auto it = elements.begin(); it != elements.end();) {
            if ((*it)->getType() == ELEMENT_TEXT) {
                texts.push_back(std::move(*it));
                it = elements.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& e: other) {
            elements.push_back(std::move(e));
        }
        other = std::move(texts);
        doc->unlock();
        page->firePageChanged();
    }
    Layer* layer;
    std::vector<ElementPtr> other;
};
}  // namespace

MarkdownSession::MarkdownSession(DocumentSession& session, QObject* parent): QObject(parent), session(session) {}

MarkdownSession::~MarkdownSession() {
    if (active()) {
        finish();
    }
}

size_t MarkdownSession::pageIndex() const {
    if (!page) {
        return npos;
    }
    std::shared_lock lock(*session.getDocument());
    return session.getDocument()->indexOf(page);
}

std::string MarkdownSession::begin(size_t pageNo, const md::Style& s) {
    if (active()) {
        finish();
    }
    session.clearSelectionEndText();
    Document* doc = session.getDocument();
    style = s;
    std::string source;
    {
        std::shared_lock lock(*doc);
        if (pageNo >= doc->getPageCount()) {
            return {};
        }
        page = doc->getPage(pageNo);
        layer = md::markdownLayer(page);
        createdLayer = !layer;
        selectedBefore = page->getSelectedLayerId();
        // A new box: from the top-left margin (beside the margin line of a ruled page) to the right margin
        const TextFlow::Style margins = TextFlow::styleFor(page, TextFlow::Style{});
        boxX = margins.leftMargin;
        boxY = TextFlow::MARGIN;
        style.width = std::max(50.0, page->getWidth() - margins.leftMargin - margins.rightMargin);
        if (const Text* box = layer ? md::boxOf(*layer) : nullptr) {
            source = box->getText();
            style = md::styleOf(*box);
            boxX = box->getTransformation().shift.x;
            boxY = box->getTransformation().shift.y;
        }
    }
    if (!layer) {
        // At the bottom: writing with the pen goes on top of the box, into the layer it went into before.
        layer = new Layer();
        layer->setName(std::string(xoj::view::MARKDOWN_LAYER_NAME));
        session.getLayerController()->insertLayer(page, layer, 0);  // (locks the document)
        std::unique_lock lock(*doc);
        page->setSelectedLayerId(selectedBefore > 0 ? selectedBefore + 1 : 0);
    }
    {
        std::shared_lock lock(*doc);
        original.clear();
        for (const auto& e: layer->getElementsView()) {
            if (e->getType() == ELEMENT_TEXT) {
                original.push_back(e->clone());
            }
        }
    }
    last = source;
    changed = false;
    return source;
}

std::unique_ptr<Text> MarkdownSession::makeBox(const std::string& source) const {
    auto t = std::make_unique<Text>();
    t->setText(source);
    t->setFont(XojFont(style.family, style.size));
    t->setColor(style.color);
    t->setWrap(style.width);
    t->setTransformation(xoj::util::Matrix::TRANSLATION(boxX, boxY));
    return t;
}

void MarkdownSession::replaceBox(std::vector<ElementPtr> elements) {
    Document* doc = session.getDocument();
    {
        std::unique_lock lock(*doc);
        auto& all = layer->getElements();
        std::erase_if(all, [](const ElementPtr& e) { return e->getType() == ELEMENT_TEXT; });
        for (auto& e: elements) {
            all.push_back(std::move(e));
        }
    }
    page->firePageChanged();
    if (const size_t index = pageIndex(); index != npos) {
        session.firePageChanged(index);           // thumbnails
        Q_EMIT session.pageContentChanged(index);  // chapters (its headings)
    }
}

double MarkdownSession::overflow() const {
    std::shared_lock lock(*session.getDocument());
    const Text* box = md::boxOf(*layer);
    if (!box) {
        return 0;
    }
    const auto rect = md::boxRect(*box);
    return std::max(0.0, rect.y + rect.height - (page->getHeight() - TextFlow::MARGIN));
}

double MarkdownSession::update(const std::string& source) {
    if (!active()) {
        return 0;
    }
    if (source != last) {
        if (!changed) {
            // The undo step at the first change: the document is modified now (saving, autosave, the question
            // when closing). It swaps whatever box the page has then with the box from before.
            std::vector<ElementPtr> before;
            for (const auto& e: original) {
                before.push_back(e->clone());
            }
            session.getUndoRedoHandler()->addUndoAction(
                    std::make_unique<MarkdownUndoAction>(page, layer, std::move(before)));
        }
        changed = true;
        last = source;
        std::vector<ElementPtr> box;
        if (!source.empty()) {
            box.push_back(makeBox(source));
        }
        replaceBox(std::move(box));
    }
    return overflow();
}

void MarkdownSession::finish() {
    if (active()) {
        end();  // (the undo step is there since the first change)
    }
}

void MarkdownSession::cancel() {
    if (!active()) {
        return;
    }
    if (changed) {
        replaceBox(std::move(original));  // (its undo step now swaps the same box: no change)
    }
    end();
}

void MarkdownSession::end() {
    // A Markdown layer made for nothing goes again
    if (createdLayer && layer->getElements().empty()) {
        session.getLayerController()->removeLayer(page, layer);  // (locks the document)
        {
            std::unique_lock lock(*session.getDocument());
            page->setSelectedLayerId(selectedBefore);
        }
        delete layer;
        page->firePageChanged();
    }
    page = nullptr;
    layer = nullptr;
    original.clear();
    last.clear();
    changed = false;
}

}  // namespace xqt

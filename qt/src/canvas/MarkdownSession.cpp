#include "MarkdownSession.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "control/Control.h"
#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "MdBox.h"
#include "TextFlow.h"

namespace xqt {

namespace {
/// The box before / after an edit: the element in the layer and the other version (the same action undoes and
/// redoes: it swaps them). Either may be missing (a box made or emptied away).
class MarkdownUndoAction final: public UndoAction {
public:
    MarkdownUndoAction(const PageRef& p, Layer* layer, Text* current, ElementPtr other):
            UndoAction("MarkdownUndoAction"), layer(layer), current(current), other(std::move(other)) {
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
        ElementPtr removed;
        Element::Index index = Element::InvalidIndex;
        if (current && layer->indexOf(current) != Element::InvalidIndex) {
            auto [e, i] = layer->removeElement(current);
            removed = std::move(e);
            index = i;
        }
        current = static_cast<Text*>(other.get());
        if (other) {
            if (index == Element::InvalidIndex) {
                layer->addElement(std::move(other));
            } else {
                layer->insertElement(std::move(other), index);
            }
        }
        other = std::move(removed);
        doc->unlock();
        page->firePageChanged();
    }
    Layer* layer;
    Text* current;
    ElementPtr other;
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

std::string MarkdownSession::begin(size_t pageNo, const md::Style& s) { return start(pageNo, s, true, 0, 0); }

std::string MarkdownSession::beginBox(size_t pageNo, const md::Style& s, double x, double y) {
    return start(pageNo, s, false, x, y);
}

std::string MarkdownSession::start(size_t pageNo, const md::Style& s, bool isPage, double x, double y) {
    if (active()) {
        finish();
    }
    session.clearSelectionEndText();
    Document* doc = session.getDocument();
    style = s;
    pageText = isPage;
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
        const TextFlow::Style margins = TextFlow::styleFor(page, TextFlow::Style{});
        if (pageText) {
            // The page's box: from the top-left margin (beside the margin line of a ruled page) to the right margin
            boxX = margins.leftMargin;
            boxY = TextFlow::MARGIN;
            style.width = std::max(50.0, page->getWidth() - margins.leftMargin - margins.rightMargin);
            box = layer ? md::pageBoxOf(*layer, boxX, boxY) : nullptr;
        } else {
            // A text box: the one drawn there, or a new one from there to the right margin (its first line around
            // the point, as the text tool places texts)
            box = layer && layer->isVisible() ? md::boxAt(*layer, x, y) : nullptr;
            boxX = x;
            boxY = y - style.size * 0.75;
            style.width = std::max(100.0, page->getWidth() - margins.rightMargin - x);
        }
        original.reset();
        if (box) {
            source = box->getText();
            style = md::styleOf(*box);
            boxX = box->getTransformation().shift.x;
            boxY = box->getTransformation().shift.y;
            original = box->cloneText();
        }
    }
    if (!layer) {
        // At the bottom: writing with the pen goes on top of the box, into the layer it went into before.
        layer = new Layer();
        layer->setName(std::string(xoj::markdown::LAYER_NAME));
        session.getLayerController()->insertLayer(page, layer, 0);  // (locks the document)
        std::unique_lock lock(*doc);
        page->setSelectedLayerId(selectedBefore > 0 ? selectedBefore + 1 : 0);
    }
    last = source;
    changed = false;
    return source;
}

void MarkdownSession::apply(const std::string& source) {
    Document* doc = session.getDocument();
    if (!changed) {
        // The undo step at the first change: the document is modified now (saving, autosave, the question when
        // closing). Its "other" version is the box from before (none: undo takes the box away).
        std::unique_lock lock(*doc);
        if (!box) {
            auto t = std::make_unique<Text>();
            t->setTransformation(xoj::util::Matrix::TRANSLATION(boxX, boxY));
            box = t.get();
            layer->addElement(std::move(t));
        }
        lock.unlock();
        session.getUndoRedoHandler()->addUndoAction(std::make_unique<MarkdownUndoAction>(
                page, layer, box, original ? ElementPtr(original->clone()) : ElementPtr()));
        changed = true;
    }
    {
        // Changed in place (the element stays the same, the undo step refers to it). An empty text is not drawn
        // and not saved (upstream's SaveHandler leaves empty texts out).
        std::unique_lock lock(*doc);
        box->setText(source);
        box->setFont(XojFont(style.family, style.size));
        box->setColor(style.color);
        box->setWrap(style.width);
    }
    changedOnPage();
}

void MarkdownSession::changedOnPage() {
    page->firePageChanged();
    if (const size_t index = pageIndex(); index != npos) {
        session.firePageChanged(index);           // thumbnails
        Q_EMIT session.pageContentChanged(index);  // chapters (its headings)
    }
}

double MarkdownSession::overflow() const {
    std::shared_lock lock(*session.getDocument());
    if (!box || box->getText().empty()) {
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
        last = source;
        apply(source);
    }
    return overflow();
}

double MarkdownSession::setFontSize(double size) {
    if (!active() || size <= 0 || size == style.size) {
        return overflow();
    }
    style.size = size;
    if (box || !last.empty()) {
        apply(last);
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
        // Back to the box from before (its undo step now swaps two equal boxes: no change)
        std::unique_lock lock(*session.getDocument());
        if (original) {
            box->setText(original->getText());
            box->setFont(original->getFont());
            box->setColor(original->getColor());
            box->setWrap(original->getWrap());
        } else {
            box->setText("");
        }
        lock.unlock();
        changedOnPage();
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
    box = nullptr;
    original.reset();
    last.clear();
    changed = false;
}

}  // namespace xqt

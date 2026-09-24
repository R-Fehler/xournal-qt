#include "MarkdownSession.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <shared_mutex>

#include "control/Control.h"
#include "control/layer/LayerController.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/PageType.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "undo/GroupUndoAction.h"
#include "undo/InsertDeletePageUndoAction.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

#include "MarkdownFile.h"
#include "MdBox.h"
#include "MdPaginate.h"
#include "TextFlow.h"

namespace xqt {

namespace {
/// A box before / after an edit: the element in the layer and the other version (the same action undoes and
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

/// The text of the page's own box (at its margins), empty if none.
std::string pageTextOf(const PageRef& page) {
    const Layer* layer = md::markdownLayer(page);
    const Text* box = layer ? md::pageBoxOf(*layer, TextFlow::styleFor(page, TextFlow::Style{}).leftMargin,
                                            TextFlow::MARGIN)
                            : nullptr;
    return box ? box->getText() : std::string();
}

/// Where the page's text goes on a page: its box (width) and how high it may go (a continuous page: no end).
md::Frame frameOf(const PageRef& page, bool continuous = false) {
    const TextFlow::Style m = TextFlow::styleFor(page, TextFlow::Style{});
    return {std::max(50.0, page->getWidth() - m.leftMargin - m.rightMargin),
            continuous ? MarkdownFile::CONTINUOUS_FRAME : std::max(50.0, page->getHeight() - 2 * TextFlow::MARGIN)};
}
}  // namespace

MarkdownSession::MarkdownSession(DocumentSession& session, QObject* parent): QObject(parent), session(session) {}

MarkdownSession::~MarkdownSession() {
    if (active()) {
        finish();
    }
}

size_t MarkdownSession::indexOf(const PageRef& page) const {
    std::shared_lock lock(*session.getDocument());
    return session.getDocument()->indexOf(page);
}

size_t MarkdownSession::pageIndex() const { return active() ? indexOf(chain.front().page) : npos; }
size_t MarkdownSession::lastPageIndex() const { return active() ? indexOf(chain.back().page) : npos; }

std::string MarkdownSession::begin(size_t pageNo, const md::Style& s) { return start(pageNo, s, true, 0, 0); }

std::string MarkdownSession::beginBox(size_t pageNo, const md::Style& s, double x, double y) {
    return start(pageNo, s, false, x, y);
}

MarkdownSession::Page MarkdownSession::pageOf(const PageRef& page, double x, double y) {
    Page p;
    p.page = page;
    p.x = x;
    p.y = y;
    {
        std::shared_lock lock(*session.getDocument());
        p.layer = md::markdownLayer(page);
        p.selectedBefore = page->getSelectedLayerId();
        if (p.layer) {
            p.box = pageText ? md::pageBoxOf(*p.layer, x, y)
                             : (p.layer->isVisible() ? md::boxAt(*p.layer, x, y) : nullptr);
        }
        if (p.box) {
            p.original = p.box->cloneText();
            p.x = p.box->getTransformation().shift.x;
            p.y = p.box->getTransformation().shift.y;
        }
    }
    if (!p.layer) {
        // At the bottom: writing with the pen goes on top of the box, into the layer it went into before.
        p.layer = new Layer();
        p.layer->setName(std::string(xoj::markdown::LAYER_NAME));
        session.getLayerController()->insertLayer(page, p.layer, 0);  // (locks the document)
        std::unique_lock lock(*session.getDocument());
        page->setSelectedLayerId(p.selectedBefore > 0 ? p.selectedBefore + 1 : 0);
        p.createdLayer = true;
    }
    return p;
}

std::string MarkdownSession::start(size_t pageNo, const md::Style& s, bool isPage, double x, double y) {
    if (active()) {
        finish();
    }
    session.clearSelectionEndText();
    Document* doc = session.getDocument();
    style = s;
    pageText = isPage;
    undo = nullptr;
    PageRef page;
    {
        std::shared_lock lock(*doc);
        if (pageNo >= doc->getPageCount()) {
            return {};
        }
        page = doc->getPage(pageNo);
    }
    if (!pageText) {
        // A text box: the one drawn there, or a new one from there to the right margin (its first line around the
        // point, as the text tool places texts)
        Page p = pageOf(page, x, y);
        if (p.box) {
            style = md::styleOf(*p.box);
        } else {
            p.y = y - style.size * 0.75;
            std::shared_lock lock(*doc);
            style.width =
                    std::max(100.0, page->getWidth() - TextFlow::styleFor(page, TextFlow::Style{}).rightMargin - p.x);
        }
        last = p.box ? p.box->getText() : std::string();
        chain.push_back(std::move(p));
        ranges = {{0, last.size(), 0}};
        return last;
    }
    // The page's text, from the first page it flows over (pages whose text continues the page before)
    std::vector<PageRef> pages;
    {
        std::shared_lock lock(*doc);
        size_t first = pageNo;
        while (first > 0 && md::continues(pageTextOf(doc->getPage(first)))) {
            --first;
        }
        pages.push_back(doc->getPage(first));
        for (size_t i = first + 1; i < doc->getPageCount() && md::continues(pageTextOf(doc->getPage(i))); ++i) {
            pages.push_back(doc->getPage(i));
        }
    }
    std::vector<std::string> slices;
    for (const PageRef& p: pages) {
        TextFlow::Style m;
        {
            std::shared_lock lock(*doc);
            m = TextFlow::styleFor(p, TextFlow::Style{});
        }
        chain.push_back(pageOf(p, m.leftMargin, TextFlow::MARGIN));
        slices.push_back(chain.back().box ? chain.back().box->getText() : std::string());
    }
    if (chain.front().box) {
        style = md::styleOf(*chain.front().box);
    }
    last = md::join(slices, &ranges);
    return last;
}

void MarkdownSession::setBox(Page& p, const std::string& text) {
    Document* doc = session.getDocument();
    if (p.box && p.box->getText() == text && p.box->getFontSize() == style.size && p.box->getWrap() == style.width &&
        p.box->getColor() == style.color) {
        return;
    }
    if (!p.recorded) {
        // The first change of this box: into the undo step, which is on the undo stack from the first change of
        // the edit on (the document is modified: saving, autosave, the question when closing). Its other version
        // is the box from before (none: undo takes the box away).
        std::unique_lock lock(*doc);
        if (!p.box) {
            auto t = std::make_unique<Text>();
            t->setTransformation(xoj::util::Matrix::TRANSLATION(p.x, p.y));
            p.box = t.get();
            p.layer->addElement(std::move(t));
        }
        lock.unlock();
        auto action = std::make_unique<MarkdownUndoAction>(p.page, p.layer, p.box,
                                                            p.original ? p.original->clone() : ElementPtr());
        if (!undo) {
            auto group = std::make_unique<GroupUndoAction>();
            undo = group.get();
            group->addAction(std::move(action));
            session.getUndoRedoHandler()->addUndoAction(std::move(group));
        } else {
            undo->addAction(std::move(action));
        }
        p.recorded = true;
    }
    {
        // Changed in place (the element stays the same: the undo step refers to it). An empty text is not drawn
        // and not saved (upstream's SaveHandler leaves empty texts out).
        std::unique_lock lock(*doc);
        p.box->setText(text);
        p.box->setFont(XojFont(style.family, style.size));
        p.box->setColor(style.color);
        p.box->setWrap(style.width);
    }
    changedOnPage(p.page);
}

void MarkdownSession::changedOnPage(const PageRef& page) {
    page->firePageChanged();
    if (const size_t index = indexOf(page); index != npos) {
        session.firePageChanged(index);           // thumbnails
        Q_EMIT session.pageContentChanged(index);  // chapters (its headings)
    }
}

PageRef MarkdownSession::addPageAfter(const PageRef& after) {
    Document* doc = session.getDocument();
    auto page = std::make_shared<XojPage>(after->getWidth(), after->getHeight());
    size_t pos = 0;
    {
        std::unique_lock lock(*doc);
        PageType type = after->getBackgroundType();
        if (type.isPdfPage() || type.isImagePage()) {
            type = PageType(PageTypeFormat::Plain);
        }
        page->setBackgroundType(type);
        page->setBackgroundColor(after->getBackgroundColor());
        pos = doc->indexOf(after) + 1;
        doc->insertPage(page, pos);
    }
    session.firePageInserted(pos);
    return page;
}

void MarkdownSession::removePage(const PageRef& page) {
    const size_t pos = indexOf(page);
    if (pos == npos) {
        return;
    }
    session.firePageDeleted(pos);  // (first the event, then the page goes: as InsertDeletePageUndoAction)
    std::unique_lock lock(*session.getDocument());
    session.getDocument()->deletePage(pos);
}

double MarkdownSession::distribute(const std::string& source) {
    Document* doc = session.getDocument();
    const bool continuous = session.textFile() && session.isTextContinuous();
    const auto frame = [&](size_t i) {
        std::shared_lock lock(*doc);
        return frameOf(i < chain.size() ? chain[i].page : chain.back().page, continuous);  // (new pages: like the last)
    };
    // (a continuous page: all of it, nothing to split or lay out here)
    const md::Pagination pages = continuous ? md::onePage(source, style)
                                            : md::paginate(source, style, frame, split.parts.empty() ? nullptr : &split,
                                                           &splitText);
    ranges = pages.parts;
    // More pages: added after the text's last page
    while (chain.size() < pages.slices.size()) {
        const PageRef page = addPageAfter(chain.back().page);
        TextFlow::Style m;
        {
            std::shared_lock lock(*doc);
            m = TextFlow::styleFor(page, TextFlow::Style{});
        }
        Page p = pageOf(page, m.leftMargin, TextFlow::MARGIN);
        p.createdPage = true;
        chain.push_back(std::move(p));
    }
    for (size_t i = 0; i < pages.slices.size(); ++i) {
        style.width = frame(i).width;
        setBox(chain[i], pages.slices[i]);
    }
    // Fewer: pages added by this edit go again, the others keep an empty box
    while (chain.size() > pages.slices.size() && chain.back().createdPage) {
        removePage(chain.back().page);
        chain.pop_back();
    }
    for (size_t i = pages.slices.size(); i < chain.size(); ++i) {
        if (chain[i].box && !chain[i].box->getText().empty()) {
            setBox(chain[i], "");
        }
    }
    split = pages;
    splitText = source;
    if (continuous) {
        fitContinuousPage();
    }
    return pages.overflow;
}

void MarkdownSession::fitContinuousPage() {
    // A continuous page is as high as its text (at least A4): it grows and shrinks with it
    Page& p = chain.front();
    double height = 0;
    {
        std::shared_lock lock(*session.getDocument());
        height = MarkdownFile::continuousHeight(p.box ? md::contentHeight(*p.box) : 0);
        if (std::abs(height - p.page->getHeight()) < 1) {
            return;
        }
    }
    {
        std::unique_lock lock(*session.getDocument());
        p.page->setSize(p.page->getWidth(), height);
    }
    if (const size_t index = indexOf(p.page); index != npos) {
        session.firePageSizeChanged(index);
    }
}

double MarkdownSession::overflow(const Page& p) const {
    std::shared_lock lock(*session.getDocument());
    if (!p.box || p.box->getText().empty()) {
        return 0;
    }
    const auto rect = md::boxRect(*p.box);
    return std::max(0.0, rect.y + rect.height - (p.page->getHeight() - TextFlow::MARGIN));
}

double MarkdownSession::update(const std::string& source) {
    if (!active()) {
        return 0;
    }
    if (pageText) {
        if (source != last) {
            last = source;
            const double over = distribute(source);
            if (session.textFile()) {
                session.textEdited();  // (a text file: modified or not, by its text)
            }
            return over;
        }
        const bool continuous = session.textFile() && session.isTextContinuous();
        return md::paginate(source, style, [&](size_t i) {
                   std::shared_lock lock(*session.getDocument());
                   return frameOf(chain[std::min(i, chain.size() - 1)].page, continuous);
               }).overflow;
    }
    if (source != last) {
        last = source;
        ranges = {{0, last.size(), 0}};
        setBox(chain[0], source);
    }
    return overflow(chain[0]);
}

std::vector<MarkdownSession::PagePart> MarkdownSession::parts() const {
    std::vector<PagePart> out;
    for (size_t i = 0; i < chain.size() && i < ranges.size(); ++i) {
        out.push_back({chain[i].page, chain[i].box, chain[i].x, chain[i].y, ranges[i]});
    }
    return out;
}

double MarkdownSession::setFontSize(double size) {
    if (!active() || size <= 0 || size == style.size) {
        return update(last);
    }
    style.size = size;
    if (pageText) {
        split = {};  // (other sizes: all pages again)
        return distribute(last);
    }
    if (chain[0].box || !last.empty()) {
        setBox(chain[0], last);
    }
    return overflow(chain[0]);
}

void MarkdownSession::finish() {
    if (!active()) {
        return;
    }
    if (undo && pageText) {
        // Pages at the end that only held a box that is empty now go (not the first page)
        std::vector<std::pair<PageRef, size_t>> removed;
        const auto onlyAnEmptyBox = [&](const Page& p) {
            std::shared_lock lock(*session.getDocument());
            if (p.box && !p.box->getText().empty()) {
                return false;
            }
            if (p.page->getBackgroundType().isPdfPage() || p.page->getBackgroundType().isImagePage()) {
                return false;
            }
            for (const Layer* l: p.page->getLayersView()) {
                for (const Element* e: l->getElementsView()) {
                    if (e != p.box) {
                        return false;
                    }
                }
            }
            return true;
        };
        while (chain.size() > 1 && !chain.back().createdPage && onlyAnEmptyBox(chain.back())) {
            removed.emplace_back(chain.back().page, indexOf(chain.back().page));
            removePage(chain.back().page);
            chain.pop_back();
        }
        // Pages added and removed are in the undo step (after the boxes; undo goes in this order)
        for (const Page& p: chain) {
            if (p.createdPage) {
                undo->addAction(std::make_unique<InsertDeletePageUndoAction>(p.page, indexOf(p.page), true));
            }
        }
        for (auto it = removed.rbegin(); it != removed.rend(); ++it) {  // (put back from the front)
            undo->addAction(std::make_unique<InsertDeletePageUndoAction>(it->first, it->second, false));
        }
    }
    end();
}

void MarkdownSession::cancel() {
    if (!active()) {
        return;
    }
    // Pages added go; the boxes are as they were (the undo step now swaps equal boxes: no change)
    while (!chain.empty() && chain.back().createdPage) {
        removePage(chain.back().page);
        chain.pop_back();
    }
    for (Page& p: chain) {
        if (!p.recorded) {
            continue;
        }
        {
            std::unique_lock lock(*session.getDocument());
            if (p.original) {
                p.box->setText(p.original->getText());
                p.box->setFont(p.original->getFont());
                p.box->setColor(p.original->getColor());
                p.box->setWrap(p.original->getWrap());
            } else {
                p.box->setText("");
            }
        }
        changedOnPage(p.page);
    }
    end();
    if (session.textFile()) {
        session.textEdited();
    }
}

void MarkdownSession::end() {
    for (Page& p: chain) {
        // A Markdown layer made for nothing goes again
        if (p.createdLayer && p.layer->getElements().empty() && indexOf(p.page) != npos) {
            session.getLayerController()->removeLayer(p.page, p.layer);  // (locks the document)
            {
                std::unique_lock lock(*session.getDocument());
                p.page->setSelectedLayerId(p.selectedBefore);
            }
            delete p.layer;
            p.page->firePageChanged();
        }
    }
    chain.clear();
    ranges.clear();
    split = {};
    splitText.clear();
    undo = nullptr;
    last.clear();
}

}  // namespace xqt

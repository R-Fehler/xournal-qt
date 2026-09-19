#include "CanvasView.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <limits>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>

#include "control/PdfCache.h"
#include "config.h"
#include "util/serializing/InputStreamException.h"
#include "util/serializing/ObjectOutputStream.h"
#include "util/serializing/ObjectInputStream.h"
#include "util/serializing/BinObjectEncoding.h"
#include "undo/AddUndoAction.h"
#include "model/XojPage.h"
#include "model/Layer.h"
#include "model/Link.h"
#include "model/TexImage.h"
#include "model/Image.h"
#include "model/Text.h"
#include "gui/XournalppCursor.h"
#include "undo/UndoRedoHandler.h"
#include "undo/DeleteUndoAction.h"
#include "model/Stroke.h"
#include "control/ToolHandler.h"
#include "control/tools/CursorSelectionType.h"
#include "control/tools/EditSelection.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/DocumentChangeType.h"
#include "render/RenderService.h"

#include "CanvasPage.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {
/// Upstream's clipboard target for elements (ClipboardHandler).
constexpr auto XOURNAL_MIME = "application/xournal";
}  // namespace

CanvasView::CanvasView(DocumentSession& session, QObject* parent):
        QObject(parent),
        session(session),
        renderService(*session.getApp().getRenderService()),
        viewController(&layout) {
    pdfCache = std::make_unique<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    registerListener(&session);
    session.setXournalView(this);
    session.setZoomControl(&zoomControl);
    rebuildPages();

    connect(&viewController, &ViewController::zoomChanged, this, [this] {
        // Like upstream: while zooming, show the existing buffers scaled and render sharp only once the zoom is stable.
        renderService.blockRerenderZoom();
        updateRenderParams();
        zoomControl.setZoom(viewController.zoom(), viewController.zoom100());
    });
    connect(&viewController, &ViewController::zoomSettled, this, [this] { updateVisibility(); });
    connect(&viewController, &ViewController::changed, this, [this] {
        updateVisibility();
        Q_EMIT updateRequested();
    });
    connect(&session, &DocumentSession::scrollToPageRequested, this,
            [this](qulonglong page) { viewController.scrollToPage(page); });
    connect(&session, &DocumentSession::scrollToRectRequested, this,
            [this](qulonglong page, QRectF rect) { viewController.scrollToPageRect(page, rect); });
    // Search hits are drawn by the canvas item over the pages.
    connect(&session.search(), &DocumentSearch::changed, this, &CanvasView::updateRequested);
    // Upstream's Control::clearSelectionEndText (before saving, page operations, ...): the elements go back.
    connect(&session, &DocumentSession::clearSelectionRequested, this, [this] {
        if (selection) {
            clearSelection();
        }
    });
    // Column layout changed in the settings: lay out again, keep the current page in view.
    connect(&session.getApp(), &AppContext::settingsChanged, this, [this] {
        if (layoutConfig() != layout.getConfig()) {
            const size_t page = this->session.getCurrentPageNo();
            refreshLayout();
            viewController.fitWidth();
            viewController.scrollToPage(page);
        }
    });

    releaseTimer.setSingleShot(true);
    releaseTimer.setInterval(1000);
    connect(&releaseTimer, &QTimer::timeout, this, &CanvasView::releaseFarBuffers);
    updateRenderParams();
}

CanvasView::~CanvasView() {
    selection.reset();  // the selected elements go back into the document
    session.setXournalView(nullptr);
    session.setZoomControl(nullptr);
    unregisterListener();
    pages.clear();  // detaches and cancels the rasters
}

std::optional<size_t> CanvasView::indexOf(const CanvasPage* page) const {
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].get() == page) {
            return i;
        }
    }
    return std::nullopt;
}

QRectF CanvasView::pageViewRect(size_t index) const {
    const double zoom = viewController.zoom();
    const QRectF r = layout.pageRect(index, zoom);
    return r.translated(viewController.contentOrigin());
}

CanvasPage* CanvasView::pageAt(QPointF viewPos) const {
    if (auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom())) {
        return pages[*idx].get();
    }
    return nullptr;
}

std::pair<size_t, size_t> CanvasView::visiblePages() const {
    return layout.pagesIn(viewController.visibleContentRect(), viewController.zoom());
}

void CanvasView::setDevicePixelRatio(double value) {
    if (value <= 0 || value == dpr) {
        return;
    }
    dpr = value;
    updateRenderParams();
    for (auto& p: pages) {
        if (p->getRaster().withBuffer([](xoj::view::Mask& m) { return m.isInitialized(); })) {
            p->rerenderPage();
        }
    }
}

void CanvasView::updateRenderParams() {
    renderZoom = viewController.zoom();
    renderDpr = dpr;
}

void CanvasView::rebuildPages() {
    pages.clear();
    Document* doc = session.getDocument();
    size_t n = 0;
    {
        std::shared_lock lock(*doc);
        n = doc->getPageCount();
    }
    pages.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        pages.push_back(std::make_unique<CanvasPage>(*this, doc->getPage(i)));
    }
    refreshLayout();
}

DocumentLayout::Config CanvasView::layoutConfig() const {
    // Upstream's view settings (viewColumns, showPairedPages, numPairsOffset).
    const Settings* s = session.getSettings();
    return {static_cast<size_t>(std::max(1, s->getViewColumns())), s->isShowPairedPages(),
            static_cast<size_t>(std::max(0, s->getPairsOffset()))};
}

// --- selection (port of upstream XournalView) ------------------------------------------------------------------

void CanvasView::clearSelection() {
    // Deleting the EditSelection puts the elements back into their layer.
    selection.reset();
    session.getCursor()->setMouseSelectionType(CURSOR_SELECTION_NONE);
    session.getToolHandler()->setSelectionEditTools(false, false, false, false);
    ++selectionRev;
    Q_EMIT selectionChanged(false);
    Q_EMIT updateRequested();
}

void CanvasView::deleteSelection(EditSelection* sel) {
    if (sel == nullptr) {
        sel = selection.get();
    }
    if (sel) {
        auto undo = std::make_unique<DeleteUndoAction>(sel->getSourcePage(), false);
        sel->fillUndoItem(undo.get());
        session.getUndoRedoHandler()->addUndoAction(std::move(undo));
        clearSelection();
        repaintSelection(true);
    }
}

void CanvasView::setSelection(EditSelection* sel) {
    clearSelection();
    selection.reset(sel);
    bool canChangeSize = false, canChangeColor = false, canChangeFill = false, canChangeLineStyle = false;
    for (const Element* e: selection->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            canChangeColor = true;
        } else if (e->getType() == ELEMENT_STROKE) {
            canChangeSize = true;
            const auto* s = dynamic_cast<const Stroke*>(e);
            if (s->getToolType() == StrokeTool::PEN) {
                canChangeColor = canChangeFill = canChangeLineStyle = true;
                break;
            }
            if (s->getToolType() == StrokeTool::HIGHLIGHTER) {
                canChangeColor = canChangeFill = true;
            }
        }
    }
    session.getToolHandler()->setSelectionEditTools(canChangeColor, canChangeSize, canChangeFill, canChangeLineStyle);
    Q_EMIT selectionChanged(true);
    repaintSelection();
}

void CanvasView::repaintSelection(bool) {
    ++selectionRev;
    Q_EMIT updateRequested();
}

bool CanvasView::copySelection() {
    // Port of ClipboardHandler::copy (the Xournal part and the text part)
    if (!selection) {
        return false;
    }
    ObjectOutputStream out(new BinObjectEncoding());
    out.writeString(PROJECT_STRING);
    selection->serialize(out);
    GString* data = out.stealData();
    auto* mime = new QMimeData;
    mime->setData(XOURNAL_MIME, QByteArray(data->str, static_cast<qsizetype>(data->len)));
    g_string_free(data, TRUE);
    QString text;
    for (const Element* e: selection->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            text += (text.isEmpty() ? "" : "\n") + QString::fromStdString(static_cast<const Text*>(e)->getText());
        }
    }
    if (!text.isEmpty()) {
        mime->setText(text);
    }
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}

bool CanvasView::cutSelection() {
    if (!copySelection()) {
        return false;
    }
    deleteSelection();
    return true;
}

bool CanvasView::pasteElements() {
    // Port of Control::clipboardPasteXournal
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(XOURNAL_MIME)) {
        return false;
    }
    const QByteArray bytes = mime->data(XOURNAL_MIME);
    const size_t pNr = session.getCurrentPageNo();
    if (pNr >= pages.size()) {
        return false;
    }
    clearSelection();
    Document* doc = session.getDocument();
    doc->lock();
    PageRef page = doc->getPage(pNr);
    Layer* layer = page->getSelectedLayer();
    auto sel = std::make_unique<EditSelection>(&session, page, layer, pages[pNr].get());
    doc->unlock();
    try {
        ObjectInputStream in;
        if (!in.read(bytes.constData(), static_cast<size_t>(bytes.size()))) {
            return false;
        }
        const std::string version = in.readString();
        if (version != PROJECT_STRING) {
            g_message("Paste from %s to %s", version.c_str(), PROJECT_STRING);
        }
        sel->readSerialized(in);
        const int count = in.readInt();
        auto undo = std::make_unique<AddUndoAction>(page, false);
        for (int i = 0; i < count; i++) {
            const std::string name = in.getNextObjectName();
            ElementPtr element;
            if (name == "Stroke") {
                element = std::make_unique<Stroke>();
            } else if (name == "Image") {
                element = std::make_unique<Image>();
            } else if (name == "TexImage") {
                element = std::make_unique<TexImage>();
            } else if (name == "Text") {
                element = std::make_unique<Text>();
            } else if (name == "Link") {
                element = std::make_unique<Link>();
            } else {
                throw InputStreamException("Unknown object " + name, __FILE__, __LINE__);
            }
            element->readSerialized(in);
            undo->addElement(layer, element.get(), layer->indexOf(element.get()));
            sel->addElement(std::move(element), std::numeric_limits<Element::Index>::max());
        }
        session.getUndoRedoHandler()->addUndoAction(std::move(undo));

        // Paste target: the middle of the visible part of the page (upstream XournalView::getPasteTarget).
        const double zoom = viewController.zoom();
        const QRectF pageRect = layout.pageRect(pNr, zoom);
        QRectF visible = pageRect.intersected(viewController.visibleContentRect());
        if (visible.isEmpty()) {
            visible = pageRect;
        }
        const QPointF target = (visible.center() - pageRect.topLeft()) / zoom;
        const double x = std::max(0.0, target.x() - sel->getWidth() / 2);
        const double y = std::max(0.0, target.y() - sel->getHeight() / 2);
        sel->moveSelection(x - sel->getXOnView(), y - sel->getYOnView());
        sel->mouseUp();
        setSelection(sel.release());
        return true;
    } catch (const std::exception& e) {
        g_warning("could not paste: %s", e.what());
        return false;
    }
}

void CanvasView::selectAllOnPage() {
    // Port of Control::selectAllOnPage
    const size_t pageNr = session.getCurrentPageNo();
    if (pageNr >= pages.size()) {
        return;
    }
    clearSelection();
    Document* doc = session.getDocument();
    doc->lock();
    PageRef page = doc->getPage(pageNr);
    Layer* layer = page->getSelectedLayer();
    auto elements = layer->clearNoFree();
    doc->unlock();
    if (!elements.empty()) {
        InsertionOrder insertionOrder;
        insertionOrder.reserve(elements.size());
        Element::Index n = 0;
        for (auto&& e: elements) {
            insertionOrder.emplace_back(std::move(e), n++);
        }
        auto [sel, rg] = SelectionFactory::createFromFloatingElements(&session, page, layer, pages[pageNr].get(),
                                                                      std::move(insertionOrder));
        page->fireRangeChanged(rg);
        setSelection(sel.release());
    }
}

double CanvasView::getZoom() const { return viewController.zoom(); }
XournalppCursor* CanvasView::getCursor() const { return session.getCursor(); }
Control* CanvasView::getControl() const { return &session; }

void CanvasView::ensureRectIsVisible(int x, int y, int width, int height) {
    // Like gtk_adjustment_clamp_page on both axes (content pixels).
    const QRectF visible = viewController.visibleContentRect();
    QPointF pos = viewController.scrollPosition();
    if (x - 5 < visible.left()) {
        pos.setX(x - 5);
    } else if (x + width + 10 > visible.right()) {
        pos.setX(x + width + 10 - visible.width());
    }
    if (y - 5 < visible.top()) {
        pos.setY(y - 5);
    } else if (y + height + 10 > visible.bottom()) {
        pos.setY(y + height + 10 - visible.height());
    }
    viewController.setScrollPosition(pos);
}

XojPageView* CanvasView::getPageViewAt(int x, int y) const {
    if (auto idx = layout.pageAt(QPointF(x, y), viewController.zoom())) {
        return pages[*idx].get();
    }
    return nullptr;
}

int CanvasView::getTotalPixelWidth() const {
    return static_cast<int>(layout.contentSize(viewController.zoom()).width());
}
int CanvasView::getTotalPixelHeight() const {
    return static_cast<int>(layout.contentSize(viewController.zoom()).height());
}
xoj::util::Rectangle<double> CanvasView::getVisibleRect() {
    const QRectF r = viewController.visibleContentRect();
    return {r.x(), r.y(), r.width(), r.height()};
}
void CanvasView::scrollRelative(double x, double y) {
    viewController.setScrollPosition(viewController.scrollPosition() + QPointF(x, y));
}

void CanvasView::refreshLayout() {
    std::vector<PageRef> refs;
    refs.reserve(pages.size());
    for (const auto& p: pages) {
        refs.push_back(p->getPage());
    }
    layout.update(*session.getDocument(), refs, layoutConfig());
    viewController.layoutChanged();
    Q_EMIT pagesChanged();
}

void CanvasView::updateVisibility() {
    if (pages.empty()) {
        return;
    }
    const auto [first, last] = visiblePages();
    const double zoom = viewController.zoom();
    size_t mostVisible = session.getCurrentPageNo();
    double bestArea = -1;
    const QRectF visible = viewController.visibleContentRect();
    for (size_t i = first; i <= last && i < pages.size(); ++i) {
        CanvasPage* page = pages[i].get();
        const auto info = page->bufferInfo();
        // Render visible pages that have no buffer or a buffer at another zoom/resolution (the render service defers
        // this while a zoom gesture is running).
        if (!info.valid || info.zoom != zoom || info.dpiScale != dpr) {
            page->rerenderPage();
        }
        const QRectF inter = layout.pageRect(i, zoom).intersected(visible);
        if (const double area = inter.width() * inter.height(); area > bestArea) {
            bestArea = area;
            mostVisible = i;
        }
    }
    // Upstream Layout::updateVisibility: the most visible page becomes the current one.
    session.setCurrentPageNo(mostVisible);
    releaseTimer.start();
}

void CanvasView::releaseFarBuffers() {
    // Upstream XournalView::cleanupBufferCache: keep the visible pages plus a preload window.
    const auto [first, last] = visiblePages();
    const size_t before = session.getSettings()->getPreloadPagesBefore();
    const size_t after = session.getSettings()->getPreloadPagesAfter();
    const size_t keepFrom = first > before ? first - before : 0;
    const size_t keepTo = last + after;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (i < keepFrom || i > keepTo) {
            pages[i]->deleteViewBuffer();
        }
    }
}

// --- XournalView ---------------------------------------------------------------------------------------------------

size_t CanvasView::getCurrentPage() const { return session.getCurrentPageNo(); }

void CanvasView::layerChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage();
    }
}

void CanvasView::recreatePdfCache() {
    pdfCache = std::make_unique<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    for (auto& p: pages) {
        p->rerenderPage();
    }
}

// --- RasterHost ------------------------------------------------------------------------------------------------------

Document* CanvasView::rasterDocument() const { return session.getDocument(); }

RasterParams CanvasView::rasterParams() const { return RasterParams{renderZoom.load(), renderDpr.load()}; }

void CanvasView::rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) {
    for (auto& p: pages) {
        if (&p->getRaster() == raster) {
            p->rasterUpdated(area);
            return;
        }
    }
}

// --- DocumentListener ----------------------------------------------------------------------------------------------

void CanvasView::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
        recreatePdfCache();
        rebuildPages();
    }
}

void CanvasView::pageSizeChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage(true);
    }
    refreshLayout();
}

void CanvasView::pageChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage();
    }
}

void CanvasView::pageInserted(size_t page) {
    PageRef ref;
    {
        std::shared_lock lock(*session.getDocument());
        ref = session.getDocument()->getPage(page);
    }
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(std::min(page, pages.size())),
                 std::make_unique<CanvasPage>(*this, std::move(ref)));
    refreshLayout();
}

void CanvasView::pageDeleted(size_t page) {
    if (page < pages.size()) {
        pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(page));
    }
    refreshLayout();
}

void CanvasView::pageSelected(size_t) {}

}  // namespace xqt

#include "CanvasView.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <limits>
#include <QMimeData>
#include <QGuiApplication>
#include <QClipboard>
#include <QBuffer>

#include "control/PdfCache.h"
#include "model/LineStyle.h"
#include "model/Point.h"
#include "undo/GroupUndoAction.h"
#include "view/overlays/PdfElementSelectionView.h"
#include "control/tools/PdfElemSelection.h"
#include "model/LinkDestination.h"
#include "pdf/base/XojPdfAction.h"
#include "view/overlays/OverlayView.h"
#include "config.h"
#include "util/serializing/InputStreamException.h"
#include "util/serializing/ObjectOutputStream.h"
#include "util/serializing/ObjectInputStream.h"
#include "util/serializing/BinObjectEncoding.h"
#include "undo/AddUndoAction.h"
#include "undo/InsertUndoAction.h"
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
#include "util/TextLinks.h"
#include "model/Document.h"
#include "model/DocumentChangeType.h"
#include "render/RenderService.h"

#include "CanvasPage.h"
#include "TextEditor.h"
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
    // Upstream's Control::clearSelectionEndText (before saving, undo, page operations, ...): the elements go back.
    connect(&session, &DocumentSession::clearSelectionRequested, this, [this] {
        endTextEditing();
        clearPdfTextSelection();
        if (selection) {
            clearSelection();
        }
    });
    // Another tool ends the text editing (upstream: ToolHandler listener).
    connect(&session.getApp(), &AppContext::activeToolChanged, this, [this] {
        if (textEditor && this->session.getToolHandler()->getToolType() != TOOL_TEXT) {
            endTextEditing();
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
    endTextEditing();
    pdfSelection.reset();
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
    if (mime && !mime->hasFormat(XOURNAL_MIME) && mime->hasImage()) {
        // An image copied elsewhere (browser, screenshot tool): insert it as an image element.
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        qvariant_cast<QImage>(mime->imageData()).save(&buffer, "PNG");
        return insertImage(png);
    }
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

bool CanvasView::insertImage(const QByteArray& data) {
    const size_t pNr = session.getCurrentPageNo();
    if (pNr >= pages.size() || data.isEmpty()) {
        return false;
    }
    endTextEditing();
    clearSelection();
    auto img = std::make_unique<Image>();
    try {
        img->setImage(std::string(data.constData(), static_cast<size_t>(data.size())));
    } catch (const std::exception& e) {
        g_warning("Not an image: %s", e.what());
        return false;
    }
    const auto [w, h] = img->getNaturalSize();
    if (w <= 0 || h <= 0) {
        return false;
    }
    // Fit into the visible part of the page (and the page), centered there (upstream: automaticScaling).
    const double zoom = viewController.zoom();
    const QRectF pageRect = layout.pageRect(pNr, zoom);
    QRectF visible = pageRect.intersected(viewController.visibleContentRect());
    if (visible.isEmpty()) {
        visible = pageRect;
    }
    const QRectF area((visible.topLeft() - pageRect.topLeft()) / zoom, visible.size() / zoom);
    const double scale = std::min({1.0, area.width() * 0.8 / w, area.height() * 0.8 / h});
    const QPointF origin = area.center() - QPointF(w * scale / 2, h * scale / 2);
    img->setTransformation({scale, 0, 0, scale, {std::max(0.0, origin.x()), std::max(0.0, origin.y())}});

    PageRef page = pages[pNr]->getPage();
    Layer* layer = page->getSelectedLayer();
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, img.get()));
    auto sel = SelectionFactory::createFromFloatingElement(&session, page, layer, pages[pNr].get(), std::move(img));
    setSelection(sel.release());
    return true;
}

std::optional<CanvasView::LinkTarget> CanvasView::linkAt(QPointF viewPos) const {
    // Port of XojPageView::displayLinkPopover (finding the link)
    const auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom());
    if (!idx) {
        return std::nullopt;
    }
    Document* doc = session.getDocument();
    XojPdfPageSPtr pdf;
    {
        std::shared_lock lock(*doc);
        PageRef page = doc->getPage(*idx);
        if (!page->getBackgroundType().isPdfPage()) {
            return std::nullopt;
        }
        pdf = doc->getPdfPage(page->getPdfPageNr());
    }
    if (!pdf) {
        return std::nullopt;
    }
    const double zoom = viewController.zoom();
    const QRectF pageRect = pageViewRect(*idx);
    const QPointF pt = (viewPos - pageRect.topLeft()) / zoom;
    for (auto&& [rect, action]: pdf->getLinks()) {
        if (!(rect.x1 <= pt.x() && pt.x() <= rect.x2 && rect.y1 <= pt.y() && pt.y() <= rect.y2)) {
            continue;
        }
        LinkTarget t;
        t.viewRect = QRectF(pageRect.topLeft() + QPointF(rect.x1, rect.y1) * zoom,
                            QSizeF(rect.x2 - rect.x1, rect.y2 - rect.y1) * zoom);
        auto dest = action->getDestination();
        if (!dest) {
            continue;
        }
        if (auto uri = dest->getURI()) {
            t.uri = QString::fromStdString(*uri);
        } else {
            t.pdfPage = static_cast<int>(dest->getPdfPage());
            std::shared_lock lock(*doc);
            const size_t page = doc->findPdfPage(dest->getPdfPage());
            t.page = page == npos ? -1 : static_cast<int>(page);
        }
        return t;
    }
    return std::nullopt;
}

// The columns of a PDF page: the text lines are grouped by the gaps between them (a gap of at least a twentieth of
// the page width separates two columns). Returns the one around the point.
std::optional<QRectF> CanvasView::textColumnAt(size_t index, QPointF pagePoint) const {
    Document* doc = session.getDocument();
    XojPdfPageSPtr pdf;
    double width = 0;
    double height = 0;
    {
        std::shared_lock lock(*doc);
        if (index >= doc->getPageCount()) {
            return std::nullopt;
        }
        PageRef page = doc->getPage(index);
        if (!page->getBackgroundType().isPdfPage()) {
            return std::nullopt;
        }
        pdf = doc->getPdfPage(page->getPdfPageNr());
        width = page->getWidth();
        height = page->getHeight();
    }
    if (!pdf || width <= 0) {
        return std::nullopt;
    }
    const auto lines = pdf->selectTextLines(XojPdfRectangle(0, 0, width, height), XojPdfPageSelectionStyle::Line);
    if (lines.rects.size() < 4) {
        return std::nullopt;  // hardly a text page
    }
    // Which parts of the width are covered by text at all
    std::vector<std::pair<double, double>> spans;
    for (const XojPdfRectangle& r: lines.rects) {
        const double x1 = std::min(r.x1, r.x2);
        const double x2 = std::max(r.x1, r.x2);
        if (x2 - x1 > 1) {
            spans.emplace_back(x1, x2);
        }
    }
    std::sort(spans.begin(), spans.end());
    const double gap = width / 20;
    std::vector<std::pair<double, double>> columns;
    for (const auto& [x1, x2]: spans) {
        if (!columns.empty() && x1 <= columns.back().second + gap) {
            columns.back().second = std::max(columns.back().second, x2);
        } else {
            columns.emplace_back(x1, x2);
        }
    }
    if (columns.size() < 2) {
        return std::nullopt;  // one block of text: nothing to pick out
    }
    for (const auto& [x1, x2]: columns) {
        if (pagePoint.x() < x1 - gap || pagePoint.x() > x2 + gap) {
            continue;
        }
        // The lines of this column give its top and bottom
        double top = height;
        double bottom = 0;
        for (const XojPdfRectangle& r: lines.rects) {
            const double cx = (r.x1 + r.x2) / 2;
            if (cx >= x1 && cx <= x2) {
                top = std::min({top, r.y1, r.y2});
                bottom = std::max({bottom, r.y1, r.y2});
            }
        }
        if (bottom > top) {
            return QRectF(x1, top, x2 - x1, bottom - top);
        }
    }
    return std::nullopt;
}

void CanvasView::doubleTapAt(QPointF viewPos) {
    const auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom());
    if (!idx) {
        return;
    }
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    const QPointF onPage((viewPos.x() - pageRect.x()) / zoom, (viewPos.y() - pageRect.y()) / zoom);
    // Zoomed in already: back to the whole page
    if (pageRect.width() > viewController.viewSize().width() * 1.05) {
        viewController.fitPage(*idx, true);
        return;
    }
    if (const auto column = textColumnAt(*idx, onPage)) {
        viewController.zoomToPageRect(*idx, QRectF(column->x(), std::max(0.0, onPage.y() - 40), column->width(),
                                                   column->height()));
        return;
    }
    viewController.fitWidth();
}

bool CanvasView::tapAt(QPointF viewPos) {
    // A web address in a text on the page comes first: it lies on top of the PDF
    if (auto text = textLinkAt(viewPos)) {
        Q_EMIT linkTapped(text->uri, -1, text->viewRect);
        return true;
    }
    if (auto link = linkAt(viewPos)) {
        Q_EMIT linkTapped(link->uri, link->page, link->viewRect);
        return true;
    }
    return false;
}

std::optional<CanvasView::LinkTarget> CanvasView::textLinkAt(QPointF viewPos) const {
    const auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom());
    if (!idx) {
        return std::nullopt;
    }
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    const QPointF onPage((viewPos.x() - pageRect.x()) / zoom, (viewPos.y() - pageRect.y()) / zoom);
    Document* doc = session.getDocument();
    std::shared_lock lock(*doc);
    const PageRef page = doc->getPage(*idx);
    if (!page) {
        return std::nullopt;
    }
    for (const Layer* layer: page->getLayersView()) {
        if (!layer->isVisible()) {
            continue;
        }
        for (const Element* element: layer->getElementsView()) {
            if (element->getType() != ELEMENT_TEXT) {
                continue;
            }
            const auto* text = static_cast<const Text*>(element);
            const auto& box = text->getBoundingBox();
            if (onPage.x() < box.x || onPage.x() > box.x + box.width || onPage.y() < box.y ||
                onPage.y() > box.y + box.height) {
                continue;
            }
            const auto links = xoj::util::findLinks(text->getText());
            if (links.empty()) {
                continue;
            }
            // Which line was tapped: the links of that line come first (a text has one font and one size)
            const auto lineCount = static_cast<size_t>(
                    1 + std::count(text->getText().begin(), text->getText().end(), '\n'));
            const double lineHeight = box.height / static_cast<double>(lineCount);
            const auto line = static_cast<size_t>((onPage.y() - box.y) / std::max(1.0, lineHeight));
            size_t newlines = 0;
            for (const auto& link: links) {
                const auto before = static_cast<size_t>(
                        std::count(text->getText().begin(),
                                   text->getText().begin() + static_cast<std::ptrdiff_t>(link.start), '\n'));
                newlines = before;
                if (before == line || links.size() == 1) {
                    LinkTarget target;
                    target.uri = QString::fromStdString(link.uri);
                    target.pdfPage = -1;
                    target.viewRect = QRectF(pageRect.x() + box.x * zoom,
                                             pageRect.y() + (box.y + static_cast<double>(before) * lineHeight) * zoom,
                                             box.width * zoom, lineHeight * zoom);
                    return target;
                }
            }
            (void)newlines;
        }
    }
    return std::nullopt;
}

// --- PDF text ----------------------------------------------------------------------------------------------------

bool CanvasView::hasPdfTextSelection() const { return pdfSelection && pdfSelection->isFinalized(); }

void CanvasView::clearPdfTextSelection() {
    if (!pdfSelection) {
        return;
    }
    if (pdfSelectionPage) {
        pdfSelectionPage->removeOverlayViewsOf(pdfSelection.get());
    }
    pdfSelection.reset();
    pdfSelectionPage = nullptr;
    Q_EMIT pdfTextSelectionCleared();
    Q_EMIT updateRequested();
}

void CanvasView::pdfTextPress(CanvasPage& page, double x, double y) {
    clearPdfTextSelection();
    if (page.getPage()->getPdfPageNr() == npos) {
        return;  // no PDF on this page
    }
    pdfSelection = std::make_unique<PdfElemSelection>(x, y, &session);
    pdfSelectionPage = &page;
    page.addOverlayView(std::make_unique<xoj::view::PdfElementSelectionView>(
            pdfSelection.get(), &page, session.getSettings()->getSelectionColor()));
}

void CanvasView::pdfTextMove(CanvasPage& page, double x, double y) {
    if (pdfSelection && pdfSelectionPage == &page && !pdfSelection->isFinalized()) {
        pdfSelection->currentPos(x, y,
                                 PdfElemSelection::selectionStyleForToolType(session.getToolHandler()->getToolType()));
    }
}

void CanvasView::pdfTextRelease(CanvasPage& page) {
    if (!pdfSelection || pdfSelectionPage != &page || pdfSelection->isFinalized()) {
        return;
    }
    const auto style = PdfElemSelection::selectionStyleForToolType(session.getToolHandler()->getToolType());
    if (!pdfSelection->finalizeSelectionAndRepaint(style)) {
        clearPdfTextSelection();  // no text there
        return;
    }
    // Like upstream: the selected text becomes the primary selection (middle click paste).
    if (QClipboard* cb = QGuiApplication::clipboard(); cb->supportsSelection()) {
        cb->setText(QString::fromStdString(pdfSelection->getSelectedText()), QClipboard::Selection);
    }
    if (pdfTextMode != PdfTextMode::Select) {
        markPdfText(pdfTextMode);  // marking right away: no extra tap
        return;
    }
    // Where to show the actions: around the selected text.
    QRectF box;
    const double zoom = viewController.zoom();
    const QRectF pageRect = pageViewRect(*indexOf(&page));
    for (const auto& r: pdfSelection->getSelectedTextRects()) {
        box |= QRectF(QPointF(std::min(r.x1, r.x2), std::min(r.y1, r.y2)), QPointF(std::max(r.x1, r.x2), std::max(r.y1, r.y2)));
    }
    Q_EMIT pdfTextSelected(QRectF(pageRect.topLeft() + box.topLeft() * zoom, box.size() * zoom));
}

bool CanvasView::markPdfText(PdfTextMode mode) {
    // Port of PdfFloatingToolbox::createStrokes: marker strokes over the selected text lines.
    if (!hasPdfTextSelection() || mode == PdfTextMode::Select) {
        return false;
    }
    const auto textRects = pdfSelection->getSelectedTextRects();
    CanvasPage* page = pdfSelectionPage;
    clearPdfTextSelection();
    if (textRects.empty() || !page) {
        return false;
    }
    ToolHandler* th = session.getToolHandler();
    const bool highlight = mode == PdfTextMode::Highlight;
    // Highlight in the chosen highlight color (else the highlighter's), lines in the pen's color.
    const Color color = highlight && pdfHighlightColor ? *pdfHighlightColor
                                                       : th->getTool(highlight ? TOOL_HIGHLIGHTER : TOOL_PEN).getColor();
    const int opacity = highlight ? th->getSelectPDFTextMarkerOpacity() : 230;
    PageRef pageRef = page->getPage();
    Layer* layer = pageRef->getSelectedLayer();
    Range dirty;
    std::vector<ElementPtr> strokes;
    for (const XojPdfRectangle& rect: textRects) {
        const double top = std::min(rect.y1, rect.y2), bottom = std::max(rect.y1, rect.y2);
        const double h = mode == PdfTextMode::Underline ? bottom : (top + bottom) / 2;
        const double w = highlight ? std::abs(rect.y2 - rect.y1) : 1;
        auto stroke = std::make_unique<Stroke>();
        stroke->setColor(color);
        stroke->setFill(opacity);
        stroke->setToolType(StrokeTool::HIGHLIGHTER);
        stroke->setWidth(w);
        stroke->addPoint(Point(rect.x1, h, -1));
        stroke->addPoint(Point(rect.x2, h, -1));
        stroke->setStrokeCapStyle(StrokeCapStyle::BUTT);
        dirty.addPoint(rect.x1, h - 0.5 * w);
        dirty.addPoint(rect.x2, h + 0.5 * w);
        strokes.push_back(std::move(stroke));
    }
    std::vector<const Element*> ptrs;
    Document* doc = session.getDocument();
    doc->lock();
    for (auto&& st: strokes) {
        ptrs.push_back(st.get());
        layer->addElement(std::move(st));
    }
    doc->unlock();
    pageRef->fireElementsChanged(ptrs, dirty);
    auto undo = std::make_unique<GroupUndoAction>();
    for (const Element* e: ptrs) {
        undo->addAction(std::make_unique<InsertUndoAction>(pageRef, layer, e));
    }
    session.getUndoRedoHandler()->addUndoAction(std::move(undo));
    return true;
}

bool CanvasView::copyPdfText() {
    if (!hasPdfTextSelection()) {
        return false;
    }
    QGuiApplication::clipboard()->setText(QString::fromStdString(pdfSelection->getSelectedText()));
    return true;
}

CanvasView::NavPoint CanvasView::currentPlace() const {
    const double zoom = viewController.zoom();
    const QPointF topLeft = viewController.visibleContentRect().topLeft();
    const QSizeF size = viewController.viewSize();
    const size_t idx = layout.nearestPage(topLeft + QPointF(size.width() / 2, size.height() / 2), zoom);
    if (idx >= pages.size()) {
        return {};
    }
    return {pages[idx]->getPage(), (topLeft - layout.pageRect(idx, zoom).topLeft()) / zoom};
}

bool CanvasView::restorePlace(const NavPoint& place) {
    std::optional<size_t> idx;
    for (size_t i = 0; i < pages.size(); ++i) {
        if (pages[i]->getPage() == place.page) {
            idx = i;
        }
    }
    if (!idx) {
        return false;  // the page was deleted
    }
    const double zoom = viewController.zoom();
    viewController.setScrollPosition(layout.pageRect(*idx, zoom).topLeft() + place.offset * zoom);
    return true;  // (the most visible page becomes the current one, see updateVisibility)
}

void CanvasView::jumpToPage(size_t page) {
    if (page >= pages.size()) {
        return;
    }
    const NavPoint here = currentPlace();
    if (here.page && here.page != pages[page]->getPage()) {
        backStack.push_back(here);
        if (backStack.size() > 50) {
            backStack.erase(backStack.begin());
        }
        forwardStack.clear();
        Q_EMIT navigationChanged();
    }
    session.setCurrentPageNo(page);
    viewController.scrollToPage(page);
}

bool CanvasView::navigateBack() {
    while (!backStack.empty()) {
        NavPoint target = backStack.back();
        backStack.pop_back();
        const NavPoint here = currentPlace();
        if (restorePlace(target)) {
            forwardStack.push_back(here);
            Q_EMIT navigationChanged();
            return true;
        }
    }
    Q_EMIT navigationChanged();
    return false;
}

bool CanvasView::navigateForward() {
    while (!forwardStack.empty()) {
        NavPoint target = forwardStack.back();
        forwardStack.pop_back();
        const NavPoint here = currentPlace();
        if (restorePlace(target)) {
            backStack.push_back(here);
            Q_EMIT navigationChanged();
            return true;
        }
    }
    Q_EMIT navigationChanged();
    return false;
}

void CanvasView::clearNavigation() {
    backStack.clear();
    forwardStack.clear();
    Q_EMIT navigationChanged();
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

void CanvasView::startText(CanvasPage& page, double x, double y) {
    // Port of XojPageView::startText
    if (textEditor) {
        if (&textEditor->getPage() == &page && textEditor->contains(x, y)) {
            textEditor->mousePressed(x, y);
            return;
        }
        endTextEditing();
    }
    textEditor = std::make_unique<TextEditor>(session, page, x, y);
    page.addOverlayView(textEditor->createView());
    Q_EMIT textEditingChanged(true);
}

void CanvasView::endTextEditing() {
    if (!textEditor) {
        return;
    }
    CanvasPage& page = textEditor->getPage();
    page.removeOverlayViewsOf(textEditor.get());
    textEditor.reset();  // finishes (undo action)
    Q_EMIT textEditingChanged(false);
    Q_EMIT updateRequested();
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

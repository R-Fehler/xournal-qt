#include "CanvasView.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>
#include <limits>
#include <QMimeData>
#include <pango/pango.h>
#include <QGuiApplication>
#include <QClipboard>
#include <QBuffer>
#include <QDateTime>
#include <QThreadPool>

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
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "undo/DeleteUndoAction.h"
#include "model/Stroke.h"
#include "control/ToolHandler.h"
#include "control/layer/LayerController.h"
#include "control/tools/CursorSelectionType.h"
#include "control/tools/EditSelection.h"
#include "control/settings/Settings.h"
#include "util/TextLinks.h"
#include "model/Document.h"
#include "model/MarkdownText.h"
#include "model/DocumentChangeType.h"
#include "render/RenderService.h"

#include "pdf/base/XojPdfDocument.h"

#include "CanvasMemory.h"
#include "CanvasPage.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MdBox.h"
#include "Perf.h"
#include "TextEditor.h"
#include "TextFlow.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentLink.h"
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
    pdfCache = std::make_shared<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    // (the rendered pages are kept by CanvasMemory: the PDF cache only serves edits of the visible ones)
    pdfCache->setMaxSize(std::min<size_t>(4, static_cast<size_t>(std::max(1, session.getSettings()->getPdfPageCacheSize()))));
    pdfCachePages = session.getDocument()->getPdfPageCount();
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
    connect(&viewController, &ViewController::zoom100Changed, this,
            [this] { zoomControl.setZoom(viewController.zoom(), viewController.zoom100()); });
    connect(&viewController, &ViewController::zoomSettled, this, [this] {
        renderService.unblockRerenderZoom();  // (a pinch ended: no need to wait longer)
        updateVisibility();
    });
    connect(&viewController, &ViewController::changed, this, [this] {
        Perf::add(Perf::Scrolls);
        viewChanged();
        Q_EMIT updateRequested();
    });
    visibilityTimer.setSingleShot(true);
    connect(&visibilityTimer, &QTimer::timeout, this, [this] {
        sinceVisibility.restart();
        updateVisibility();
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
        if ((textEditor || markdownEditor) && this->session.getToolHandler()->getToolType() != TOOL_TEXT &&
            !textMode()) {  // (a text file is written whatever the tool)
            endTextEditing();
        }
    });
    // Column layout changed in the settings: lay out again, keep the current page in view.
    connect(&session.getApp(), &AppContext::settingsChanged, this, [this] {
        applyZoom100();  // (a screen was calibrated)
        applyScrolling();
        if (layoutConfig() != layout.getConfig()) {
            relayout();
        }
    });
    applyScrolling();

    updateRenderParams();
    CanvasMemory::instance().add(this);
}

CanvasView::~CanvasView() {
    cancelRenders();  // (first: the workers start nothing of this view while it is taken down)
    CanvasMemory::instance().remove(this);
    geometry.hide();  // before its page goes
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

void CanvasView::setDisplay(const ScreenCalibration::Display& display) {
    shownOn = display;
    hasDisplay = true;
    applyZoom100();
}

void CanvasView::applyZoom100() {
    if (hasDisplay) {
        viewController.setZoom100(ScreenCalibration::zoom100(*session.getSettings(), shownOn));
    }
}

void CanvasView::updateRenderParams() {
    renderZoom = viewController.zoom();
    renderDpr = dpr;
}

CanvasPage* CanvasView::canvasPageOf(const XojPage* page) const {
    for (const auto& p: pages) {
        if (p->getPage().get() == page) {
            return p.get();
        }
    }
    return nullptr;
}

void CanvasView::cancelRenders() {
    std::unordered_set<const PageRaster*> rasters;
    for (const auto& p: pages) {
        rasters.insert(&p->getRaster());
    }
    renderService.cancel(rasters);
}

void CanvasView::rebuildPages() {
    geometry.allPagesGoing();
    sharpWanted.clear();
    cancelRenders();
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
    geometry.pagesChanged();
}

DocumentLayout::Config CanvasView::layoutConfig() const {
    DocumentLayout::Config c;
    if (presenting) {
        // One page after the other, each filling the screen
        c.horizontal = true;
        c.noMargins = true;
        return c;
    }
    // Upstream's view settings (viewColumns, showPairedPages, numPairsOffset; sideways: viewFixedRows, viewRows).
    const Settings* s = session.getSettings();
    c.columns = static_cast<size_t>(std::max(1, s->getViewColumns()));
    c.paired = s->isShowPairedPages();
    c.pairsOffset = static_cast<size_t>(std::max(0, s->getPairsOffset()));
    c.horizontal = s->isViewFixedRows();
    c.rows = static_cast<size_t>(std::max(1, s->getViewRows()));
    return c;
}

bool CanvasView::snapSetting(Settings& settings) {
    bool snap = true;
    settings.getCustomElement("xournalQt").getBool("snapPages", snap);
    return snap;
}

void CanvasView::applyScrolling() {
    viewController.setSnapping(presenting || snapSetting(*session.getSettings()), presenting ? 1 : 0);
}

void CanvasView::setPresenting(bool on) {
    if (on == presenting) {
        return;
    }
    const size_t page = session.getCurrentPageNo();
    if (on) {
        zoomBeforePresenting = viewController.zoom();
        fitBeforePresenting = viewController.keptFit();
    }
    presenting = on;
    applyScrolling();
    refreshLayout();
    if (on) {
        viewController.fitPresentedPage(page);
        return;
    }
    if (fitBeforePresenting != ViewController::Fit::None || zoomBeforePresenting <= 0) {
        viewController.fitDefault(page);
    } else {
        const QSizeF size = viewController.viewSize();
        viewController.setZoom(zoomBeforePresenting, QPointF(size.width() / 2, size.height() / 2));
    }
    viewController.scrollToPage(page);
}

void CanvasView::relayout() {
    const size_t page = session.getCurrentPageNo();
    refreshLayout();
    if (presenting) {
        viewController.fitPresentedPage(page);
        return;
    }
    viewController.fitDefault(page);
    viewController.scrollToPage(page);
}

// --- selection (port of upstream XournalView) ------------------------------------------------------------------

void CanvasView::clearSelection() {
    // Deleting the EditSelection puts the elements back into their layer.
    const bool ofMarkdown = selection && markdownSelection && markdownSelection->selection == selection.get();
    selection.reset();
    if (ofMarkdown) {
        endMarkdownSelection();
    }
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

// Text from the clipboard: a text element where the user pasted it (or in the middle of the page)
bool CanvasView::pasteText(const QString& content, std::optional<QPointF> viewPos) {
    const size_t pNr = viewPos ? layout.pageAt(viewController.viewToContent(*viewPos), viewController.zoom())
                                         .value_or(session.getCurrentPageNo())
                               : session.getCurrentPageNo();
    Document* doc = session.getDocument();
    PageRef page;
    Layer* layer = nullptr;
    {
        std::shared_lock lock(*doc);
        if (pNr >= doc->getPageCount()) {
            return false;
        }
        page = doc->getPage(pNr);
        layer = page->getSelectedLayer();
    }
    if (!layer) {
        return false;
    }
    const double zoom = viewController.zoom();
    const QRectF pageRect = layout.pageRect(pNr, zoom);
    QPointF onPage(72, 72);
    if (viewPos) {
        onPage = (viewController.viewToContent(*viewPos) - pageRect.topLeft()) / zoom;
    } else {
        const QRectF visible = pageRect.intersected(viewController.visibleContentRect());
        onPage = ((visible.isEmpty() ? pageRect : visible).center() - pageRect.topLeft()) / zoom;
    }
    auto text = std::make_unique<Text>();
    text->setText(content.toStdString());
    text->setFont(session.getSettings()->getFont());
    text->setColor(session.getToolHandler()->getColor());
    text->move(std::max(0.0, onPage.x()), std::max(0.0, onPage.y()));
    const Text* raw = text.get();
    doc->lock();
    layer->addElement(std::move(text));
    doc->unlock();
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, raw));
    page->firePageChanged();
    session.firePageChanged(pNr);
    Q_EMIT updateRequested();
    return true;
}

bool CanvasView::pasteLinkMarker(std::optional<QPointF> viewPos) {
    const auto copied = links::fromMime(QGuiApplication::clipboard()->mimeData());
    if (!copied) {
        return false;
    }
    Document* doc = session.getDocument();
    size_t pNr = session.getCurrentPageNo();
    QPointF onPage(72, 72);
    PageRef page;
    if (const EditSelection* sel = getSelection()) {
        // Next to the selection: its top right
        page = sel->getSourcePage();
        const auto r = sel->getRect();
        onPage = QPointF(r.x + r.width + 4, r.y);
        std::shared_lock lock(*doc);
        const size_t idx = doc->indexOf(page);
        if (idx == npos) {
            return false;
        }
        pNr = idx;
    } else {
        if (viewPos) {
            pNr = layout.pageAt(viewController.viewToContent(*viewPos), viewController.zoom()).value_or(pNr);
        }
        const double zoom = viewController.zoom();
        const QRectF pageRect = layout.pageRect(pNr, zoom);
        if (viewPos) {
            onPage = (viewController.viewToContent(*viewPos) - pageRect.topLeft()) / zoom;
        } else {
            const QRectF visible = pageRect.intersected(viewController.visibleContentRect());
            onPage = ((visible.isEmpty() ? pageRect : visible).center() - pageRect.topLeft()) / zoom;
        }
        std::shared_lock lock(*doc);
        if (pNr >= doc->getPageCount()) {
            return false;
        }
        page = doc->getPage(pNr);
    }
    // The page's Markdown layer (made if needed: at the bottom, the selected layer stays selected)
    Layer* layer = nullptr;
    {
        std::shared_lock lock(*doc);
        layer = md::markdownLayer(page);
    }
    if (!layer) {
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
    text->setText(links::markerText(*copied, session.documentFile()).toStdString());
    const std::string family = session.getSettings()->getFont().getName();
    text->setFont(XojFont(family.empty() ? "Sans" : family, std::max(6.0, markdownTextSize * 0.85)));
    text->setColor(Color(0x1a, 0x5f, 0xd8));
    // As wide as its text (the box is what is selected and moved)
    text->setWrap(400);
    double width = 20;
    for (const md::Item& item: md::cachedLayout(text->getText(), md::styleOf(*text)).items) {
        if (item.kind == md::Item::Kind::Text && item.layout) {
            PangoRectangle logical;
            pango_layout_get_extents(item.layout.get(), nullptr, &logical);
            width = std::max(width, item.x + static_cast<double>(logical.x + logical.width) / PANGO_SCALE);
        }
    }
    text->setWrap(std::ceil(width) + 2);
    const double pageWidth = page->getWidth();
    const double pageHeight = page->getHeight();
    const double x = std::clamp(onPage.x(), 0.0, std::max(0.0, pageWidth - width - 2));
    const double y = std::clamp(onPage.y(), 0.0, std::max(0.0, pageHeight - 20));
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

bool CanvasView::pasteElements(std::optional<QPointF> viewPos) {
    // Port of Control::clipboardPasteXournal
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    // A copied link ("Copy link"): a link marker (qt/docs/links.md)
    if (mime && mime->hasFormat(links::MIME)) {
        return pasteLinkMarker(viewPos);
    }
    // Plain text from anywhere becomes a text element where it is pasted
    if (mime && !mime->hasFormat(XOURNAL_MIME) && !mime->hasImage() && mime->hasText() &&
        !mime->text().trimmed().isEmpty()) {
        return pasteText(mime->text(), viewPos);
    }
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

        // Paste target: where the user asked for it, else the middle of the visible part of the page (upstream
        // XournalView::getPasteTarget).
        const double zoom = viewController.zoom();
        const QRectF pageRect = layout.pageRect(pNr, zoom);
        QRectF visible = pageRect.intersected(viewController.visibleContentRect());
        if (visible.isEmpty()) {
            visible = pageRect;
        }
        const QPointF target = viewPos ? (viewController.viewToContent(*viewPos) - pageRect.topLeft()) / zoom
                                       : (visible.center() - pageRect.topLeft()) / zoom;
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
    // Zoomed in already: back to the whole page (presenting: filling the screen again)
    if (pageRect.width() > viewController.viewSize().width() * 1.05) {
        if (presenting) {
            viewController.fitPresentedPage(*idx);
        } else {
            viewController.fitPage(*idx, true);
        }
        return;
    }
    if (const auto column = textColumnAt(*idx, onPage)) {
        viewController.zoomToPageRect(*idx, QRectF(column->x(), std::max(0.0, onPage.y() - 40), column->width(),
                                                   column->height()));
        return;
    }
    viewController.fitWidth(*idx);
}

bool CanvasView::toggleMarkdownCheckBox(CanvasPage& page, double x, double y) {
    Document* doc = session.getDocument();
    const PageRef p = page.getPage();
    Layer* layer = nullptr;
    const Text* hit = nullptr;
    std::optional<size_t> mark;
    {
        std::shared_lock lock(*doc);
        layer = md::markdownLayer(p);
        if (!layer || !layer->isVisible()) {
            return false;
        }
        for (const Element* e: layer->getElementsView()) {
            const auto* text = static_cast<const Text*>(e);
            if (e->getType() == ELEMENT_TEXT && !text->isInEditing()) {
                if (const auto m = md::checkBoxAt(*text, x, y)) {
                    hit = text;
                    mark = m;
                }
            }
        }
    }
    if (!hit) {
        return false;
    }
    // The box again with the task switched (the same length: pages and other boxes stay as they are)
    auto switched = hit->cloneText();
    switched->setText(md::toggledTask(hit->getText(), *mark));
    Text* now = switched.get();
    ElementPtr before;
    {
        std::unique_lock lock(*doc);
        auto [old, index] = layer->removeElement(hit);
        before = std::move(old);
        layer->insertElement(std::move(switched), index);
    }
    session.getUndoRedoHandler()->addUndoAction(std::make_unique<TextBoxUndoAction>(p, layer, now, std::move(before)));
    p->firePageChanged();
    if (const auto idx = indexOf(&page)) {
        session.firePageChanged(*idx);
    }
    return true;
}

bool CanvasView::tapAt(QPointF viewPos) {
    // A task's check box in a Markdown text: switched (not in a document shown for reading only)
    if (CanvasPage* page = readingOnly ? nullptr : pageAt(viewPos)) {
        if (const auto idx = indexOf(page)) {
            const QRectF r = pageViewRect(*idx);
            const double zoom = viewController.zoom();
            if (toggleMarkdownCheckBox(*page, (viewPos.x() - r.x()) / zoom, (viewPos.y() - r.y()) / zoom)) {
                return true;
            }
        }
    }
    // A web address in a text on the page comes first: it lies on top of the PDF
    if (auto text = textLinkAt(viewPos)) {
        Q_EMIT linkTapped(text->uri, text->page, text->viewRect);
        return true;
    }
    if (auto link = linkAt(viewPos)) {
        Q_EMIT linkTapped(link->uri, link->page, link->viewRect);
        return true;
    }
    // A text file edited: a tap puts the cursor there
    if (CanvasPage* page = textMode() ? pageAt(viewPos) : nullptr) {
        if (const auto idx = indexOf(page)) {
            const QRectF r = pageViewRect(*idx);
            const double zoom = viewController.zoom();
            textPress(*page, (viewPos.x() - r.x()) / zoom, (viewPos.y() - r.y()) / zoom);
            return true;
        }
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
            if (text->isMarkdown()) {
                // A Markdown box: the links of what is drawn (not of the source)
                if (const auto hit = md::linkAt(*text, onPage.x(), onPage.y())) {
                    LinkTarget target;
                    target.pdfPage = -1;
                    target.page = -1;
                    target.uri = QString::fromStdString(hit->target);
                    if (hit->wiki) {
                        target.uri = QStringLiteral("[[%1]]").arg(target.uri);  // (a name to look for: AppLinks.cpp)
                    }
                    if (target.uri.startsWith(QLatin1String("#Page:"))) {  // a page of this document
                        target.page = target.uri.mid(6).toInt() - 1;
                        target.uri.clear();
                    }
                    target.viewRect = QRectF(pageRect.x() + hit->x * zoom, pageRect.y() + hit->y * zoom,
                                             hit->width * zoom, hit->height * zoom);
                    return target;
                }
                continue;
            }
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
                    target.page = link.page > 0 ? link.page - 1 : -1;  // a page of this document
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

std::optional<CanvasView::MathError> CanvasView::mathErrorAt(QPointF viewPos) const {
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
        if (!layer->isVisible() || !md::isMarkdownLayer(*layer)) {
            continue;
        }
        for (const Element* element: layer->getElementsView()) {
            if (element->getType() != ELEMENT_TEXT) {
                continue;
            }
            const auto hit = md::mathAt(*static_cast<const Text*>(element), onPage.x(), onPage.y());
            if (hit && !hit->span.error.empty()) {
                return MathError{QString::fromStdString(hit->span.error),
                                 QRectF(pageRect.x() + hit->rect.x * zoom, pageRect.y() + hit->rect.y * zoom,
                                        hit->rect.width * zoom, hit->rect.height * zoom)};
            }
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

bool CanvasView::selectPdfTextAt(QPointF viewPos, bool wholeLine) {
    const auto idx = layout.pageAt(viewController.viewToContent(viewPos), viewController.zoom());
    if (!idx || *idx >= pages.size()) {
        return false;
    }
    CanvasPage& page = *pages[*idx];
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    const QPointF onPage((viewPos.x() - pageRect.x()) / zoom, (viewPos.y() - pageRect.y()) / zoom);
    pdfTextPress(page, onPage.x(), onPage.y());
    if (!pdfSelection) {
        return false;
    }
    const auto style = wholeLine ? XojPdfPageSelectionStyle::Line : XojPdfPageSelectionStyle::Word;
    pdfSelection->currentPos(onPage.x(), onPage.y(), style);
    return finishPdfSelection(page, style, false);  // a long press selects, it does not mark
}

bool CanvasView::dragPdfSelection(QPointF viewPos, bool startEnd) {
    if (!pdfSelection || !pdfSelectionPage) {
        return false;
    }
    const auto idx = indexOf(pdfSelectionPage);
    if (!idx) {
        return false;
    }
    const QRectF ends = pdfSelectionEnds();
    if (ends.isNull()) {
        return false;
    }
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    // The end that is not dragged stays where it is, the dragged one follows the finger
    const QPointF anchorView = startEnd ? ends.bottomRight() : ends.topLeft();
    const QPointF anchor((anchorView.x() - pageRect.x()) / zoom, (anchorView.y() - pageRect.y()) / zoom);
    const QPointF head((viewPos.x() - pageRect.x()) / zoom, (viewPos.y() - pageRect.y()) / zoom);
    CanvasPage& page = *pdfSelectionPage;
    pdfTextPress(page, anchor.x(), anchor.y());
    if (!pdfSelection) {
        return false;
    }
    pdfSelection->currentPos(head.x(), head.y(), XojPdfPageSelectionStyle::Linear);
    return finishPdfSelection(page, XojPdfPageSelectionStyle::Linear, false);
}

std::string CanvasView::selectedPdfText() const {
    return pdfSelection && pdfSelection->isFinalized() ? pdfSelection->getSelectedText() : std::string();
}

/// The selected text in the coordinates of its page (points); empty when nothing is selected.
static QRectF selectedTextOnPage(const PdfElemSelection* selection) {
    QRectF box;
    if (!selection) {
        return box;
    }
    for (const XojPdfRectangle& r: selection->getSelectedTextRects()) {
        box |= QRectF(QPointF(std::min(r.x1, r.x2), std::min(r.y1, r.y2)),
                      QPointF(std::max(r.x1, r.x2), std::max(r.y1, r.y2)));
    }
    return box;
}

QRectF CanvasView::pdfSelectionBox() const {
    if (!hasPdfTextSelection() || !pdfSelectionPage) {
        return {};
    }
    const auto idx = indexOf(pdfSelectionPage);
    const QRectF onPage = selectedTextOnPage(pdfSelection.get());
    if (!idx || onPage.isNull()) {
        return {};
    }
    const double zoom = viewController.zoom();
    const QRectF pageRect = pageViewRect(*idx);
    return QRectF(pageRect.topLeft() + onPage.topLeft() * zoom, onPage.size() * zoom);
}

void CanvasView::scrollToPdfSelection() {
    if (!hasPdfTextSelection() || !pdfSelectionPage) {
        return;
    }
    const auto idx = indexOf(pdfSelectionPage);
    const QRectF onPage = selectedTextOnPage(pdfSelection.get());
    if (!idx || onPage.isNull()) {
        return;
    }
    // A little air around it, so it does not sit right at the edge
    viewController.scrollToPageRect(*idx, onPage.adjusted(-20, -40, 20, 40));
}

bool CanvasView::pdfTextSelectionContains(QPointF viewPos) const {
    if (!hasPdfTextSelection() || !pdfSelectionPage) {
        return false;
    }
    const auto idx = indexOf(pdfSelectionPage);
    if (!idx) {
        return false;
    }
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    constexpr double MARGIN = 6;  // a little around the letters, so the edge of a word still counts as on it
    for (const XojPdfRectangle& r: pdfSelection->getSelectedTextRects()) {
        const QRectF onPage(QPointF(std::min(r.x1, r.x2), std::min(r.y1, r.y2)),
                            QPointF(std::max(r.x1, r.x2), std::max(r.y1, r.y2)));
        const QRectF onView(pageRect.topLeft() + onPage.topLeft() * zoom, onPage.size() * zoom);
        if (onView.adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN).contains(viewPos)) {
            return true;
        }
    }
    return false;
}

QRectF CanvasView::pdfSelectionEnds() const {
    if (!pdfSelection || !pdfSelectionPage) {
        return {};
    }
    const auto& rects = pdfSelection->getSelectedTextRects();
    if (rects.empty()) {
        return {};
    }
    const auto idx = indexOf(pdfSelectionPage);
    if (!idx) {
        return {};
    }
    const QRectF pageRect = pageViewRect(*idx);
    const double zoom = viewController.zoom();
    const XojPdfRectangle& first = rects.front();
    const XojPdfRectangle& last = rects.back();
    // topLeft: where the selection begins, bottomRight: where it ends (the handles sit there)
    return QRectF(QPointF(pageRect.x() + std::min(first.x1, first.x2) * zoom,
                          pageRect.y() + std::min(first.y1, first.y2) * zoom),
                  QPointF(pageRect.x() + std::max(last.x1, last.x2) * zoom,
                          pageRect.y() + std::max(last.y1, last.y2) * zoom));
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
    finishPdfSelection(page, PdfElemSelection::selectionStyleForToolType(session.getToolHandler()->getToolType()));
}

bool CanvasView::finishPdfSelection(CanvasPage& page, XojPdfPageSelectionStyle style, bool mark) {
    if (!pdfSelection || pdfSelectionPage != &page) {
        return false;
    }
    {
    if (!pdfSelection->finalizeSelectionAndRepaint(style)) {
        clearPdfTextSelection();  // no text there
        return false;
    }
    }
    // Like upstream: the selected text becomes the primary selection (middle click paste).
    if (QClipboard* cb = QGuiApplication::clipboard(); cb->supportsSelection()) {
        cb->setText(QString::fromStdString(pdfSelection->getSelectedText()), QClipboard::Selection);
    }
    if (mark && pdfTextMode != PdfTextMode::Select && !readingOnly) {
        markPdfText(pdfTextMode);  // the tool marks right away: no extra tap
        return true;
    }
    // Where to show the actions: around the selected text.
    QRectF box;
    const double zoom = viewController.zoom();
    const QRectF pageRect = pageViewRect(*indexOf(&page));
    for (const auto& r: pdfSelection->getSelectedTextRects()) {
        box |= QRectF(QPointF(std::min(r.x1, r.x2), std::min(r.y1, r.y2)), QPointF(std::max(r.x1, r.x2), std::max(r.y1, r.y2)));
    }
    Q_EMIT pdfTextSelected(QRectF(pageRect.topLeft() + box.topLeft() * zoom, box.size() * zoom));
    return true;
}

bool CanvasView::markPdfText(PdfTextMode mode) {
    // Port of PdfFloatingToolbox::createStrokes: marker strokes over the selected text lines.
    if (!hasPdfTextSelection() || mode == PdfTextMode::Select || readingOnly) {
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

bool CanvasView::drawGeometryMarks(double spacingCm) {
    const auto lines = geometry.marks(spacingCm);
    CanvasPage* page = geometry.page();
    if (lines.empty() || !page) {
        return false;
    }
    PageRef pageRef = page->getPage();
    Layer* layer = pageRef->getSelectedLayer();
    ToolHandler* th = session.getToolHandler();
    const Tool& pen = th->getTool(TOOL_PEN);
    const double width = th->isCustomThicknessActive(TOOL_PEN) && th->getCustomThickness(TOOL_PEN) > 0
                                 ? th->getCustomThickness(TOOL_PEN)
                                 : pen.getThickness(pen.getSize());
    Range dirty;
    std::vector<ElementPtr> strokes;
    for (const auto& [from, to]: lines) {
        auto stroke = std::make_unique<Stroke>();
        stroke->setColor(pen.getColor());
        stroke->setToolType(StrokeTool::PEN);
        stroke->setWidth(width);
        stroke->addPoint(Point(from.x(), from.y(), -1));
        stroke->addPoint(Point(to.x(), to.y(), -1));
        dirty.addPoint(from.x() - width, from.y() - width);
        dirty.addPoint(from.x() + width, from.y() + width);
        dirty.addPoint(to.x() - width, to.y() - width);
        dirty.addPoint(to.x() + width, to.y() + width);
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
    updateVisibility();  // a jump: the most visible page becomes the current one right away
    return true;
}

void CanvasView::jumpToPage(size_t page) {
    if (page >= pages.size()) {
        return;
    }
    rememberPlaceBefore(page);
    session.setCurrentPageNo(page);
    viewController.scrollToPage(page);
}

void CanvasView::jumpToRect(size_t page, QRectF rect) {
    if (page >= pages.size()) {
        return;
    }
    rememberPlaceBefore(page);
    session.setCurrentPageNo(page);
    viewController.scrollToPageRect(page, rect.adjusted(-20, -40, 20, 40));
}

void CanvasView::rememberPlaceBefore(size_t page) {
    const NavPoint here = currentPlace();
    if (here.page && here.page != pages[page]->getPage()) {
        backStack.push_back(here);
        if (backStack.size() > 50) {
            backStack.erase(backStack.begin());
        }
        forwardStack.clear();
        Q_EMIT navigationChanged();
    }
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
    if (markdownEditor) {
        // (on the text being written, also on its other pages: a check box, or the cursor goes there)
        if (markdownEditor->toggleCheckBox(page, x, y) || markdownEditor->tap(page, x, y)) {
            return;
        }
        endTextEditing();
    }
    const auto idx = indexOf(&page);
    if (!idx || toggleMarkdownCheckBox(page, x, y)) {
        return;
    }
    // Markdown: the page's text and text boxes, written on the page (formatted while typing) or beside it
    const bool onPageText = markdownBoxAt(page, x, y);
    bool onBox = false;
    bool onText = false;
    {
        std::shared_lock lock(*session.getDocument());
        const PageRef p = page.getPage();
        const Layer* mdLayer = md::markdownLayer(p);
        onBox = mdLayer && mdLayer->isVisible() && md::boxAt(*mdLayer, x, y);
        for (const Element* e: p->getSelectedLayer()->getElementsView()) {
            onText = onText || (e->getType() == ELEMENT_TEXT && e->hasBoundingBoxContaining(x, y));
        }
    }
    if (onPageText || onBox || (markdownText && !onText)) {  // (an ordinary text there is edited as it is)
        if (markdownInPanel) {
            if (onPageText) {
                Q_EMIT markdownRequested(static_cast<int>(*idx));
            } else {
                Q_EMIT markdownBoxRequested(static_cast<int>(*idx), x, y);
            }
        } else {
            startMarkdown(*idx, onPageText, x, y);
        }
        return;
    }
    TextEditor::NewText how;
    how.markdown = markdownText;
    how.markdownSize = markdownTextSize;
    textEditor = std::make_unique<TextEditor>(session, page, x, y, how);
    page.addOverlayView(textEditor->createView());
    Q_EMIT textEditingChanged(true);
}

bool CanvasView::markdownBoxAt(CanvasPage& page, double x, double y) const {
    std::shared_lock lock(*session.getDocument());
    const PageRef p = page.getPage();
    const Layer* layer = md::markdownLayer(p);
    if (!layer || !layer->isVisible()) {
        return false;
    }
    const Text* box = md::pageBoxOf(*layer, TextFlow::styleFor(p, TextFlow::Style{}).leftMargin, TextFlow::MARGIN);
    return box && box == md::boxAt(*layer, x, y);
}

bool CanvasView::textMode() const { return session.isEditableText() && !readingOnly; }

void CanvasView::textPress(CanvasPage& page, double x, double y) {
    if (markdownEditor && (markdownEditor->toggleCheckBox(page, x, y) || markdownEditor->tapAnywhere(page, x, y))) {
        return;
    }
    const auto idx = indexOf(&page);
    if (!idx) {
        return;
    }
    if (!markdownEditor && toggleMarkdownCheckBox(page, x, y)) {
        return;
    }
    startMarkdown(*idx, true, x, y);
}

bool CanvasView::ensureTextEditor() {
    if (markdownEditor) {
        return true;
    }
    if (!textMode()) {
        return false;
    }
    startMarkdown(std::min(session.getCurrentPageNo(), session.getDocument()->getPageCount() - 1), true,
                  TextFlow::MARGIN, TextFlow::MARGIN);
    return markdownEditor != nullptr;
}

void CanvasView::startMarkdown(size_t pageNo, bool pageText, double x, double y) {
    endTextEditing();
    if (textMode()) {
        // A text file: as it was opened (its boxes' style wins where there are boxes)
        markdownEditor = std::make_unique<MarkdownEditor>(*this, session, pageNo, true, x, y,
                                                          MarkdownFile::style(*session.textFile()));
        Q_EMIT textEditingChanged(true);
        Q_EMIT updateRequested();
        return;
    }
    md::Style style;
    // The text tool's font (the family: its name may have a style, e.g. "Sans Bold") and color, the Markdown size
    PangoFontDescription* d = pango_font_description_from_string(session.getSettings()->getFont().getName().c_str());
    if (const char* family = pango_font_description_get_family(d); family && *family) {
        style.family = family;
    }
    pango_font_description_free(d);
    style.size = markdownTextSize;
    style.color = pageText ? Color(0, 0, 0) : session.getToolHandler()->getColor();
    markdownEditor = std::make_unique<MarkdownEditor>(*this, session, pageNo, pageText, x, y, style);
    Q_EMIT textEditingChanged(true);
    Q_EMIT updateRequested();
}

void CanvasView::setMarkdownText(bool markdown, double size, bool inPanel) {
    markdownText = markdown;
    markdownTextSize = size;
    markdownInPanel = inPanel;
}

void CanvasView::endTextEditing() {
    if (markdownEditor) {
        auto editor = std::move(markdownEditor);  // (null while it finishes: finishing may end text editing again)
        editor.reset();                           // finishes (one undo step)
        Q_EMIT textEditingChanged(false);
        Q_EMIT updateRequested();
    }
    if (!textEditor) {
        return;
    }
    CanvasPage& page = textEditor->getPage();
    page.removeOverlayViewsOf(textEditor.get());
    textEditor.reset();  // finishes (undo action)
    Q_EMIT textEditingChanged(false);
    Q_EMIT updateRequested();
}

CanvasTextInput* CanvasView::getTextInput() const {
    if (textEditor) {
        return textEditor.get();
    }
    return markdownEditor.get();
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
    // The part of the pages that is seen. Upstream's layout is never smaller than its window (it centres the pages
    // inside), so its visible part never starts before 0. Ours holds only the pages: where they are narrower (or
    // shorter) than the window, the margins beside them must not count. Otherwise the edge panning of a dragged
    // selection takes the margin for a part of the layout scrolled out of view, and on every tick pushes the
    // selection sideways by the width of the margin.
    const QRectF content(QPointF(0, 0), layout.contentSize(viewController.zoom()));
    const QRectF r = viewController.visibleContentRect().intersected(content);
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
    jumpedPage.reset();  // (pages came or went: another index)
    viewController.layoutChanged();
    Q_EMIT pagesChanged();
}

void CanvasView::viewChanged() {
    const int EVERY_MS = visibilityDelay;  // (about one frame)
    // A jump (to a page, a fit, a new size) right away; plain scrolling and zooming send more changes than there are
    // frames, and looking at the visible pages tells the models and moves the sidebar along.
    const std::optional<size_t> toPage = viewController.takePageJump();
    const bool jumped = viewController.takeJumped();
    if (toPage) {
        jumpedPage = toPage;
    } else if (!jumped) {
        jumpedPage.reset();  // scrolled or zoomed by hand: the most visible page is the current one again
    }
    if (jumped || !sinceVisibility.isValid() || sinceVisibility.elapsed() >= EVERY_MS) {
        sinceVisibility.restart();
        visibilityTimer.stop();
        updateVisibility();
    } else if (!visibilityTimer.isActive()) {
        visibilityTimer.start(EVERY_MS - static_cast<int>(sinceVisibility.elapsed()));
    }
}

void CanvasView::updateVisibility() {
    if (pages.empty()) {
        return;
    }
    ++visibilityCount;
    Perf::add(Perf::Visibility);
    const PerfScope measure(Perf::VisibilityTime);
    const auto [first, last] = visiblePages();
    const double zoom = viewController.zoom();
    size_t mostVisible = session.getCurrentPageNo();
    double bestArea = -1;
    const QRectF visible = viewController.visibleContentRect();
    const auto shownArea = [&](size_t i) {
        const QRectF inter = layout.pageRect(i, zoom).intersected(visible);
        return inter.width() * inter.height();
    };
    // After a jump to a page, that page is the current one while it can be seen: at the end of the document the
    // page before a small last page may show more of itself.
    if (const auto p = jumpedPage; p && *p >= first && *p <= last && *p < pages.size() && shownArea(*p) > 0) {
        mostVisible = *p;
        bestArea = std::numeric_limits<double>::infinity();
    }
    for (size_t i = first; i <= last && i < pages.size(); ++i) {
        if (const double area = shownArea(i); area > bestArea) {
            bestArea = area;
            mostVisible = i;
        }
    }
    // Render visible pages that have no buffer or a buffer at another zoom/resolution (the render service defers
    // this while a zoom gesture is running), the one the reader looks at first.
    auto render = [&](size_t i) {
        CanvasPage* page = pages[i].get();
        if (const auto info = page->bufferInfo(); !info.valid || info.zoom != zoom || info.dpiScale != dpr) {
            page->getRaster().ensureRendered(false);
            if (Perf::on()) {
                sharpWanted[page] = QDateTime::currentMSecsSinceEpoch();  // (the last request counts)
            }
        }
    };
    if (mostVisible >= first && mostVisible <= last && mostVisible < pages.size()) {
        render(mostVisible);
    }
    for (size_t i = first; i <= last && i < pages.size(); ++i) {
        if (i != mostVisible) {
            render(i);
        }
    }
    // Upstream Layout::updateVisibility: the most visible page becomes the current one (this tells the models, and
    // the page sidebar follows).
    {
        const PerfScope measure(Perf::CurrentPageTime);
        session.setCurrentPageNo(mostVisible);
    }
    if (shown) {
        CanvasMemory::instance().used(this);  // (plans what to keep and render in advance once this pauses)
    }
}

// --- memory ----------------------------------------------------------------------------------------------------------

void CanvasView::setReadingOnly(bool on) {
    if (on == readingOnly) {
        return;
    }
    readingOnly = on;
    if (on) {
        endTextEditing();  // (a text being typed when the document became the reference: kept, as when it is left)
    }
}

void CanvasView::setShown(bool value) {
    shown = value;
    if (shown) {
        CanvasMemory::instance().used(this);
    }
}

void CanvasView::pageRendered(const CanvasPage* page) {
    const auto [from, to] = window;
    if (from > to) {
        return;  // (no plan yet)
    }
    const auto index = indexOf(page);
    if (!index || (*index >= from && *index <= to)) {
        return;
    }
    if (const auto [first, last] = visiblePages(); *index >= first && *index <= last) {
        return;  // (in view: the next plan counts it)
    }
    CanvasMemory::instance().replan();
}

qint64 CanvasView::bufferBytes() const {
    qint64 sum = 0;
    for (const auto& p: pages) {
        if (const auto info = p->bufferInfo(); info.valid) {
            sum += static_cast<qint64>(info.pixelSize.width()) * info.pixelSize.height() * 4;
        }
    }
    return sum;
}

qint64 CanvasView::pageBytes(size_t index) const {
    const QRectF r = layout.pageRect(index, viewController.zoom());
    const qint64 atZoom = static_cast<qint64>(std::ceil(r.width() * dpr)) * static_cast<qint64>(std::ceil(r.height() * dpr)) * 4;
    const auto info = pages[index]->bufferInfo();
    return info.valid ? std::max(atZoom, static_cast<qint64>(info.pixelSize.width()) * info.pixelSize.height() * 4)
                      : atZoom;
}

qint64 CanvasView::planCache(qint64 share) {
    const size_t n = pages.size();
    if (n == 0) {
        window = {1, 0};
        return 0;
    }
    auto [first, last] = visiblePages();
    if (first > last || last >= n) {
        first = last = std::min(session.getCurrentPageNo(), n - 1);
    }
    qint64 planned = 0;
    for (size_t i = first; i <= last; ++i) {
        planned += pageBytes(i);  // (the visible pages always stay)
    }
    // Of the rest: 35 % before, 65 % after; where one side has no more pages, the other gets the rest
    const qint64 rest = std::max<qint64>(0, share - planned);
    qint64 beforeBudget = static_cast<qint64>(static_cast<double>(rest) * CanvasMemory::BEFORE_SHARE);
    qint64 afterBudget = rest - beforeBudget;
    size_t from = first, to = last;
    qint64 before = 0, after = 0;
    auto growAfter = [&] {
        while (to + 1 < n && after + pageBytes(to + 1) <= afterBudget) {
            after += pageBytes(++to);
        }
    };
    auto growBefore = [&] {
        while (from > 0 && before + pageBytes(from - 1) <= beforeBudget) {
            before += pageBytes(--from);
        }
    };
    growAfter();
    growBefore();
    if (to + 1 == n) {
        beforeBudget = rest - after;
        growBefore();
    } else if (from == 0) {
        afterBudget = rest - before;
        growAfter();
    }
    window = {from, to};
    for (size_t i = 0; i < n; ++i) {
        if (i < from || i > to) {
            pages[i]->deleteViewBuffer();
        }
    }
    // The PDF cache: the visible pages (edits re-render parts of them)
    std::unordered_set<size_t> visiblePdf;
    for (size_t i = first; i <= last; ++i) {
        if (pages[i]->getPage()->getBackgroundType().isPdfPage()) {
            visiblePdf.insert(pages[i]->getPage()->getPdfPageNr());
        }
    }
    evictPdfCache(std::move(visiblePdf));

    // In advance: nearest first, the two sides in the proportion of their parts
    renderService.dropQueued(RenderService::Priority::Preload);
    const double zoom = viewController.zoom();
    const auto current = static_cast<std::ptrdiff_t>(session.getCurrentPageNo());
    size_t a = last + 1;
    auto b = static_cast<std::ptrdiff_t>(first) - 1;
    qint64 doneAfter = 0, doneBefore = 0;
    const auto start = static_cast<std::ptrdiff_t>(from);
    while (a <= to || b >= start) {
        const bool takeAfter =
                b < start || (a <= to && static_cast<double>(doneAfter) * CanvasMemory::BEFORE_SHARE <=
                                                 static_cast<double>(doneBefore) * (1 - CanvasMemory::BEFORE_SHARE));
        const size_t i = takeAfter ? a++ : static_cast<size_t>(b--);
        (takeAfter ? doneAfter : doneBefore) += pageBytes(i);
        const auto info = pages[i]->bufferInfo();
        const bool near = std::abs(static_cast<std::ptrdiff_t>(i) - current) <= CanvasMemory::NEAR_PAGES;
        if (!info.valid || (near && (info.zoom != zoom || info.dpiScale != dpr))) {
            pages[i]->getRaster().ensureRendered(true);
        }
    }
    return planned + before + after;
}

void CanvasView::evictPdfCache(std::unordered_set<size_t> keep) {
    QThreadPool::globalInstance()->start([cache = pdfCache, keep = std::move(keep)] { cache->evictAllExcept(keep); });
}

qint64 CanvasView::trimTo(qint64 allowed) {
    evictPdfCache({});
    const auto [first, last] = visiblePages();
    const auto current = static_cast<std::ptrdiff_t>(session.getCurrentPageNo());
    qint64 held = 0;
    std::vector<std::pair<std::ptrdiff_t, size_t>> farthestFirst;  // distance, page
    for (size_t i = 0; i < pages.size(); ++i) {
        if (const auto info = pages[i]->bufferInfo(); info.valid) {
            held += static_cast<qint64>(info.pixelSize.width()) * info.pixelSize.height() * 4;
            if (!(shown && i >= first && i <= last)) {  // (shown in another window: its visible pages stay)
                farthestFirst.emplace_back(std::abs(static_cast<std::ptrdiff_t>(i) - current), i);
            }
        }
    }
    std::sort(farthestFirst.begin(), farthestFirst.end(), std::greater<>());
    for (const auto& [distance, i]: farthestFirst) {
        if (held <= allowed) {
            break;
        }
        const auto info = pages[i]->bufferInfo();
        held -= static_cast<qint64>(info.pixelSize.width()) * info.pixelSize.height() * 4;
        pages[i]->deleteViewBuffer();
    }
    return held;
}

// --- XournalView ---------------------------------------------------------------------------------------------------

size_t CanvasView::getCurrentPage() const { return session.getCurrentPageNo(); }

void CanvasView::layerChanged(size_t page) {
    if (page < pages.size()) {
        pages[page]->rerenderPage();
    }
}

PdfCache* CanvasView::rasterPdfCache(bool background) const {
    if (!background) {
        return pdfCache.get();
    }
    std::lock_guard lock(backgroundPdfMutex);
    if (!backgroundPdfLoaded) {
        backgroundPdfLoaded = true;
        fs::path path;
        size_t count = 0;
        {
            Document* doc = session.getDocument();
            std::shared_lock docLock(*doc);
            count = doc->getPdfPageCount();
            if (count > 0) {
                path = doc->getPdfFilepath();
            }
        }
        if (!path.empty()) {
            XojPdfDocument own;
            GError* error = nullptr;
            // (not loadable, e.g. with a password, or changed on disk: the document's own instance)
            if (own.load(path, "", &error) && own.getPageCount() == count) {
                backgroundPdfCache = std::make_unique<PdfCache>(own, nullptr);  // (keeps nothing: size 0)
            }
            if (error) {
                g_error_free(error);
            }
        }
    }
    return backgroundPdfCache ? backgroundPdfCache.get() : pdfCache.get();
}

XojPdfPageSPtr CanvasView::rasterPendingPdfPage(size_t number) const { return session.pendingPdfPage(number); }

void CanvasView::recreatePdfCache() { replacePdfCache(true); }

void CanvasView::replacePdfCache(bool rerender) {
    // The old ones may still be in use by a render: they go with the view (empty)
    evictPdfCache({});
    retiredPdfCaches.push_back(std::move(pdfCache));
    {
        std::lock_guard lock(backgroundPdfMutex);
        if (backgroundPdfCache) {
            retiredPdfCaches.push_back(std::move(backgroundPdfCache));
        }
        backgroundPdfLoaded = false;
    }
    pdfCache = std::make_shared<PdfCache>(session.getDocument()->getPdfDocument(), session.getSettings());
    pdfCache->setMaxSize(std::min<size_t>(4, static_cast<size_t>(std::max(1, session.getSettings()->getPdfPageCacheSize()))));
    const size_t before = std::exchange(pdfCachePages, session.getDocument()->getPdfPageCount());
    for (auto& p: pages) {
        // (a page drawn again goes to the queue of the pages in view: all of them there would make the pages the
        // reader waits for wait behind every page of the document)
        const PageRef& page = p->getPage();
        if (rerender || (page->getBackgroundType().isPdfPage() && page->getPdfPageNr() >= before)) {
            p->rerenderPage();
        }
    }
}

// --- RasterHost ------------------------------------------------------------------------------------------------------

Document* CanvasView::rasterDocument() const { return session.getDocument(); }

RasterParams CanvasView::rasterParams() const { return RasterParams{renderZoom.load(), renderDpr.load()}; }

void CanvasView::rasterUpdated(PageRaster* raster, std::optional<xoj::util::Rectangle<double>> area) {
    for (auto& p: pages) {
        if (&p->getRaster() == raster) {
            p->rasterUpdated(area);
            if (auto it = sharpWanted.find(p.get()); it != sharpWanted.end() && !area) {
                // XQT_PERF: how long a page in view waited for its render at the zoom it is shown at
                if (const auto info = p->bufferInfo(); info.valid && info.zoom == viewController.zoom()) {
                    Perf::took(Perf::SharpTime, (QDateTime::currentMSecsSinceEpoch() - it->second) * 1000);
                    sharpWanted.erase(it);
                }
            }
            return;
        }
    }
}

// --- DocumentListener ----------------------------------------------------------------------------------------------

void CanvasView::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
        recreatePdfCache();
        rebuildPages();
    } else if (type == DOCUMENT_CHANGE_PDF_BOOKMARKS) {
        // Another background PDF was loaded: the caches hold the old one. When pasted PDF pages joined the merged
        // PDF, the pages keep their pictures (their PDF pages are the same, with the same numbers).
        replacePdfCache(!session.pdfKeepsPictures());
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
    geometry.pagesChanged();  // a page moved elsewhere comes back as a new one
}

void CanvasView::pageDeleted(size_t page) {
    if (page < pages.size()) {
        geometry.pageGoing(pages[page].get());
        sharpWanted.erase(pages[page].get());
        pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(page));
    }
    refreshLayout();
    geometry.pagesChanged();
}

void CanvasView::pageSelected(size_t pageNo) {
    // A selection of Markdown texts moved to another page is dropped into that page's Markdown layer (made if
    // needed): it is its selected layer for now.
    if (!selection || !markdownSelection || markdownSelection->selection != selection.get()) {
        return;
    }
    Document* doc = session.getDocument();
    PageRef page;
    {
        std::shared_lock lock(*doc);
        page = doc->getPage(pageNo);
    }
    if (!page || std::any_of(markdownSelection->pages.begin(), markdownSelection->pages.end(),
                             [&](const auto& p) { return p.page == page; })) {
        return;
    }
    MarkdownSelection::Page entry;
    entry.page = page;
    Layer* layer = nullptr;
    {
        std::shared_lock lock(*doc);
        layer = md::markdownLayer(page);
        entry.before = page->getSelectedLayerId();
    }
    if (!layer) {
        layer = new Layer();
        layer->setName(std::string(xoj::markdown::LAYER_NAME));
        session.getLayerController()->insertLayer(page, layer, 0);  // (locks the document)
        entry.created = layer;
        entry.before = entry.before > 0 ? entry.before + 1 : 0;
    }
    {
        std::unique_lock lock(*doc);
        const auto& layers = page->getLayers();
        page->setSelectedLayerId(static_cast<Layer::Index>(
                std::distance(layers.begin(), std::find(layers.begin(), layers.end(), layer)) + 1));
    }
    markdownSelection->pages.push_back(entry);
}

std::optional<Layer::Index> CanvasView::selectMarkdownLayer(const PageRef& page) {
    std::unique_lock lock(*session.getDocument());
    const Layer* layer = md::markdownLayer(page);
    if (!layer || !layer->isVisible() || page->getSelectedLayer() == layer) {
        return std::nullopt;
    }
    const Layer::Index before = page->getSelectedLayerId();
    const auto& layers = page->getLayers();
    page->setSelectedLayerId(static_cast<Layer::Index>(
            std::distance(layers.begin(), std::find(layers.begin(), layers.end(), layer)) + 1));
    return before;
}

void CanvasView::restoreSelectedLayer(const PageRef& page, Layer::Index layer) {
    {
        std::unique_lock lock(*session.getDocument());
        page->setSelectedLayerId(layer);
    }
    session.getLayerController()->fireRebuildLayerMenu();  // (the layer list shows the selected layer)
}

bool CanvasView::isMarkdownLayer(const PageRef& page, Layer::Index layer) const {
    std::shared_lock lock(*session.getDocument());
    const auto& layers = page->getLayers();
    return layer >= 1 && layer <= layers.size() && md::isMarkdownLayer(*layers[layer - 1]);
}

void CanvasView::markdownSelectionMade(const PageRef& page, Layer::Index before) {
    if (!selection || isMarkdownLayer(page, before)) {
        return;
    }
    markdownSelection = MarkdownSelection{selection.get(), {{page, before, nullptr}}};
}

void CanvasView::endMarkdownSelection() {
    auto ended = std::move(markdownSelection);
    markdownSelection.reset();
    if (!ended) {
        return;
    }
    for (const auto& p: ended->pages) {
        Layer::Index before = p.before;
        if (p.created && p.created->getElements().empty()) {  // made for a move that went elsewhere
            session.getLayerController()->removeLayer(p.page, p.created);  // (locks the document)
            delete p.created;
            before = before > 0 ? before - 1 : 0;
        }
        std::unique_lock lock(*session.getDocument());
        p.page->setSelectedLayerId(std::min<Layer::Index>(before, p.page->getLayerCount()));
    }
    session.getLayerController()->fireRebuildLayerMenu();  // (the layer list shows the selected layer)
}

}  // namespace xqt

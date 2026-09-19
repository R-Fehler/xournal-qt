#include "DocumentCanvasItem.h"
#include "TextFlowEditor.h"
#include "TouchGestures.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include <cairo.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QQmlEngine>
#include <QNativeGestureEvent>
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "control/tools/EditSelection.h"
#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"
#include "TextEditor.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

namespace {
constexpr int TILE = 256;

class TileNode final: public QSGSimpleTextureNode {
public:
    ~TileNode() override { delete texture(); }
};

/// Scene graph of one page: a transform (position, and buffer zoom -> current zoom) with texture tiles.
class PageNode final: public QSGTransformNode {
public:
    PageNode() {
        shadow = new QSGSimpleRectNode(QRectF(), QColor(0, 0, 0, 60));
        appendChildNode(shadow);
        placeholder = new QSGSimpleRectNode(QRectF(), Qt::white);
        appendChildNode(placeholder);
        // Search hits over the tiles (tiles are inserted before this node).
        searchRoot = new QSGTransformNode;
        appendChildNode(searchRoot);
    }
    void clearTiles() {
        for (auto* t: tiles) {
            removeChildNode(t);
            delete t;
        }
        tiles.clear();
        cols = rows = 0;
    }
    QSGSimpleRectNode* shadow;
    QSGSimpleRectNode* placeholder;
    QSGTransformNode* searchRoot;
    quint64 searchRevision = ~quint64(0);
    double searchScale = 0;
    std::vector<TileNode*> tiles;
    int cols = 0, rows = 0;
    double bufferZoom = 0;
    double dpiScale = 0;
    QSize pixelSize;
};

// Containers are (identity) transform nodes, not plain QSGNodes: Qt Quick's software backend re-resolves a changed
// node's transform and clip from its parent, and only records them for transform/clip/opacity nodes. Under a plain
// parent a re-positioned page would lose the item's position.
class CanvasRootNode final: public QSGTransformNode {
public:
    CanvasRootNode() {
        pagesRoot = new QSGTransformNode;
        appendChildNode(pagesRoot);
        selectionRoot = new QSGTransformNode;
        appendChildNode(selectionRoot);
        hover = new QSGSimpleRectNode(QRectF(), QColor(0x1d, 0x2b, 0x8f));
        appendChildNode(hover);
    }
    QSGTransformNode* pagesRoot;
    QSGTransformNode* selectionRoot;  ///< the selection (EditSelection::paint), above the pages
    TileNode* selection = nullptr;
    quint64 selectionRevision = ~quint64(0);
    double selectionZoom = 0;
    QRectF selectionRegion;  ///< in the selection page's view pixels
    QSGSimpleRectNode* hover;
    std::unordered_map<const xqt::CanvasPage*, PageNode*> pages;
};

QRect tileRect(int index, int cols, QSize pixelSize) {
    const int cx = index % cols, cy = index / cols;
    return QRect(cx * TILE, cy * TILE, TILE, TILE).intersected(QRect(QPoint(0, 0), pixelSize));
}

double snap(double v, double dpr) { return std::round(v * dpr) / dpr; }
}  // namespace

void xqt::registerQuickTypes() {
    qmlRegisterType<DocumentCanvasItem>("XournalQt.Canvas", 1, 0, "DocumentCanvas");
    qmlRegisterType<TextFlowEditor>("XournalQt.Canvas", 1, 0, "TextFlowEditor");
    qmlRegisterType<TouchGestures>("XournalQt.Canvas", 1, 0, "TouchGestures");
}

DocumentCanvasItem::DocumentCanvasItem(QQuickItem* parent): QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptTouchEvents(true);
    setCursor(Qt::CrossCursor);
    // Proximity events are only delivered to the application object.
    qApp->installEventFilter(this);
}

DocumentCanvasItem::~DocumentCanvasItem() {
    qApp->removeEventFilter(this);
    if (filteredWindow) {
        filteredWindow->removeEventFilter(this);
    }
}

QObject* DocumentCanvasItem::view() const { return canvasView.data(); }

void DocumentCanvasItem::setView(QObject* object) {
    auto* v = qobject_cast<xqt::CanvasView*>(object);
    if (v == canvasView) {
        return;
    }
    if (canvasView) {
        disconnect(canvasView, nullptr, this, nullptr);
    }
    input.reset();
    canvasView = v;
    viewReplaced = true;
    if (canvasView) {
        input = std::make_unique<xqt::CanvasInput>(*canvasView);
        connect(canvasView, &xqt::CanvasView::updateRequested, this, &QQuickItem::update);
        connect(canvasView, &xqt::CanvasView::pagesChanged, this, &QQuickItem::update);
        connect(canvasView, &xqt::CanvasView::pagesChanged, this, &DocumentCanvasItem::viewportChanged);
        connect(&canvasView->getViewController(), &xqt::ViewController::changed, this,
                &DocumentCanvasItem::viewportChanged);
        connect(canvasView, &QObject::destroyed, this, [this] {
            input.reset();
            viewReplaced = true;
            update();
        });
        connect(input.get(), &xqt::CanvasInput::hoverChanged, this, &QQuickItem::update);
        connect(canvasView, &xqt::CanvasView::textEditingChanged, this, [this](bool editing) {
            setFlag(ItemAcceptsInputMethod, editing);
            if (editing) {
                forceActiveFocus(Qt::OtherFocusReason);
                QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
                QGuiApplication::inputMethod()->show();  // tablets: the on-screen keyboard
            } else {
                QGuiApplication::inputMethod()->hide();
                QGuiApplication::inputMethod()->update(Qt::ImEnabled);
            }
        });
        updateViewGeometry();
    }
    Q_EMIT viewChanged();
    Q_EMIT viewportChanged();
    update();
}

qreal DocumentCanvasItem::contentWidth() const {
    return canvasView ? canvasView->documentLayout().contentSize(canvasView->getViewController().zoom()).width() : 0;
}

qreal DocumentCanvasItem::contentHeight() const {
    return canvasView ? canvasView->documentLayout().contentSize(canvasView->getViewController().zoom()).height() : 0;
}

qreal DocumentCanvasItem::contentX() const {
    return canvasView ? canvasView->getViewController().scrollPosition().x() : 0;
}

qreal DocumentCanvasItem::contentY() const {
    return canvasView ? canvasView->getViewController().scrollPosition().y() : 0;
}

void DocumentCanvasItem::scrollTo(qreal x, qreal y) {
    if (canvasView) {
        canvasView->getViewController().setScrollPosition(QPointF(x, y));
    }
}

void DocumentCanvasItem::updateViewGeometry() {
    if (!canvasView || width() <= 0 || height() <= 0) {
        return;
    }
    if (window()) {
        canvasView->setDevicePixelRatio(window()->effectiveDevicePixelRatio());
    }
    canvasView->getViewController().setViewSize(size());
}

void DocumentCanvasItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateViewGeometry();
    }
}

void DocumentCanvasItem::itemChange(ItemChange change, const ItemChangeData& value) {
    if (change == ItemSceneChange) {
        if (filteredWindow) {
            filteredWindow->removeEventFilter(this);
        }
        filteredWindow = value.window;
        if (filteredWindow) {
            // Runs before QQuickWindow::event() and thus before Qt Quick's delivery agent.
            filteredWindow->installEventFilter(this);
            updateViewGeometry();
        }
    } else if (change == ItemDevicePixelRatioHasChanged) {
        updateViewGeometry();
    }
    QQuickItem::itemChange(change, value);
}

namespace {
/// Deepest visible item at a scene position, following the stacking order (topmost first), like Qt Quick's
/// delivery. Popups, dialogs and their modal dimmer live in the window's overlay, which is above the content.
QQuickItem* topmostItemAt(QQuickItem* item, QPointF scenePos) {
    // Paint order: by z, then declaration order. (QQuickItem::childAt ignores z, and would e.g. find an
    // ApplicationWindow's background (z -1) on top of the content.)
    QList<QQuickItem*> children = item->childItems();
    std::stable_sort(children.begin(), children.end(), [](QQuickItem* a, QQuickItem* b) { return a->z() < b->z(); });
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if ((*it)->isVisible() && (*it)->contains((*it)->mapFromScene(scenePos))) {
            QQuickItem* top = topmostItemAt(*it, scenePos);
            // The popup overlay covers the window while any popup is open; only its popups (and the dimmer of a
            // modal one) are on top, not the overlay itself (e.g. a tool tip somewhere else).
            if (top == *it && (*it)->inherits("QQuickOverlay")) {
                continue;
            }
            return top;
        }
    }
    return item;
}
}  // namespace

bool DocumentCanvasItem::claims(QPointF scenePos) const {
    if (!isVisible() || !isEnabled() || !window() ||
        !QRectF(0, 0, width(), height()).contains(mapFromScene(scenePos))) {
        return false;
    }
    // Only take events the canvas would get anyway: not those for a dialog, popup or control on top of it.
    const QQuickItem* top = topmostItemAt(window()->contentItem(), scenePos);
    return top == this || (top && isAncestorOf(top));
}

void DocumentCanvasItem::takeKeyboardFocus() {
    // Working on the canvas: the keys (Ctrl+Z, Ctrl+C, Delete, ...) are for the document again, not for the page
    // sidebar or grid that may have had the focus.
    if (!hasActiveFocus()) {
        forceActiveFocus(Qt::MouseFocusReason);
    }
}

bool DocumentCanvasItem::eventFilter(QObject* watched, QEvent* e) {
    if (!input) {
        return false;
    }
    if (watched == qApp) {
        if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
            input->proximityEvent(e->type() == QEvent::TabletEnterProximity);
        }
        return false;
    }
    if (watched != filteredWindow) {
        return false;
    }
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto* t = static_cast<QTabletEvent*>(e);
            if (!penGrab && !claims(t->position())) {
                return false;  // unaccepted: Qt synthesizes mouse events for the QML controls
            }
            if (e->type() == QEvent::TabletPress && t->button() == Qt::LeftButton) {
                penGrab = true;
                takeKeyboardFocus();
            } else if (e->type() == QEvent::TabletRelease && t->button() == Qt::LeftButton) {
                penGrab = false;
            }
            input->tabletEvent(t, mapFromScene(t->position()));
            t->accept();
            return true;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto* t = static_cast<QTouchEvent*>(e);
            if (e->type() == QEvent::TouchBegin) {
                touchSessionOwned = !t->points().isEmpty() && claims(t->points().first().scenePosition());
                if (touchSessionOwned) {
                    takeKeyboardFocus();
                }
            }
            if (!touchSessionOwned) {
                return false;
            }
            input->touchEvent(t, [this](QPointF scenePos) { return mapFromScene(scenePos); });
            if (e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
                touchSessionOwned = false;
            }
            t->accept();
            return true;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove: {
            auto* m = static_cast<QMouseEvent*>(e);
            const bool inside = claims(m->scenePosition());
            if (!mouseGrab && (!inside || (e->type() == QEvent::MouseMove && m->buttons() == Qt::NoButton))) {
                return false;
            }
            if (e->type() == QEvent::MouseButtonPress) {
                mouseGrab = true;
                takeKeyboardFocus();
            } else if (e->type() == QEvent::MouseButtonRelease && m->buttons() == Qt::NoButton) {
                mouseGrab = false;
            }
            input->mouseEvent(m, mapFromScene(m->scenePosition()));
            m->accept();
            return true;
        }
        case QEvent::Wheel: {
            auto* w = static_cast<QWheelEvent*>(e);
            if (!claims(w->scenePosition())) {
                return false;
            }
            input->wheelEvent(w, mapFromScene(w->scenePosition()));
            w->accept();
            return true;
        }
        case QEvent::NativeGesture: {
            auto* g = static_cast<QNativeGestureEvent*>(e);
            if (!claims(g->scenePosition())) {
                return false;
            }
            input->nativeGestureEvent(g, mapFromScene(g->scenePosition()));
            g->accept();
            return true;
        }
        default:
            return false;
    }
}

void DocumentCanvasItem::releaseResources() { viewReplaced = true; }

bool DocumentCanvasItem::event(QEvent* e) {
    // While editing text, typing keys belong to the editor, not to the window's shortcuts (Ctrl+C, Delete, ...).
    if (e->type() == QEvent::ShortcutOverride && canvasView && canvasView->getTextEditor() &&
        xqt::TextEditor::wantsKey(static_cast<QKeyEvent*>(e))) {
        e->accept();
        return true;
    }
    return QQuickItem::event(e);
}

void DocumentCanvasItem::keyPressEvent(QKeyEvent* e) {
    xqt::TextEditor* editor = canvasView ? canvasView->getTextEditor() : nullptr;
    bool finish = false;
    if (editor && editor->keyPressed(e, finish)) {
        if (finish) {
            canvasView->endTextEditing();
        } else {
            QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImSurroundingText |
                                                   Qt::ImCursorPosition | Qt::ImAnchorPosition);
        }
        e->accept();
        return;
    }
    QQuickItem::keyPressEvent(e);
}

void DocumentCanvasItem::inputMethodEvent(QInputMethodEvent* e) {
    if (xqt::TextEditor* editor = canvasView ? canvasView->getTextEditor() : nullptr) {
        editor->inputMethodEvent(e);
        e->accept();
        return;
    }
    QQuickItem::inputMethodEvent(e);
}

QVariant DocumentCanvasItem::inputMethodQuery(Qt::InputMethodQuery query) const {
    xqt::TextEditor* editor = canvasView ? canvasView->getTextEditor() : nullptr;
    if (!editor) {
        return query == Qt::ImEnabled ? QVariant(false) : QQuickItem::inputMethodQuery(query);
    }
    if (query == Qt::ImCursorRectangle) {
        auto idx = canvasView->indexOf(&editor->getPage());
        if (!idx) {
            return QRectF();
        }
        const double zoom = canvasView->getViewController().zoom();
        const QRectF r = editor->cursorRectOnPage();
        const QPointF origin = canvasView->pageViewRect(*idx).topLeft();
        return QRectF(origin + r.topLeft() * zoom, r.size() * zoom);
    }
    return editor->inputMethodQuery(query);
}

void DocumentCanvasItem::updateSelectionNode(QSGNode* rootNode, double zoom, double dpr) {
    auto* root = static_cast<CanvasRootNode*>(rootNode);
    EditSelection* sel = canvasView->getSelection();
    auto idx = sel ? canvasView->indexOf(static_cast<xqt::CanvasPage*>(sel->getView())) : std::nullopt;
    if (!sel || !idx) {
        if (root->selection) {
            root->selectionRoot->removeChildNode(root->selection);
            delete root->selection;
            root->selection = nullptr;
        }
        root->selectionRevision = ~quint64(0);
        return;
    }
    const QPointF pageOrigin = canvasView->pageViewRect(*idx).topLeft();
    if (!root->selection || root->selectionRevision != canvasView->selectionRevision() || root->selectionZoom != zoom) {
        // Upstream's XournalWidget draws the selection in its page's pixel coordinates: selection->paint(cr, zoom).
        // Render the part around it (handles, rotation) into a texture.
        const double cx = (sel->getXOnView() + sel->getWidth() / 2) * zoom;
        const double cy = (sel->getYOnView() + sel->getHeight() / 2) * zoom;
        const double r = std::hypot(sel->getWidth(), sel->getHeight()) / 2 * zoom + 60;
        const QRectF region(std::floor(cx - r), std::floor(cy - r), std::ceil(2 * r), std::ceil(2 * r));
        QImage img(QSize(std::max(1, static_cast<int>(region.width() * dpr)),
                         std::max(1, static_cast<int>(region.height() * dpr))),
                   QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
                img.bits(), CAIRO_FORMAT_ARGB32, img.width(), img.height(), static_cast<int>(img.bytesPerLine()));
        cairo_t* cr = cairo_create(surface);
        cairo_scale(cr, dpr, dpr);
        cairo_translate(cr, -region.x(), -region.y());
        sel->paint(cr, zoom);
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        const bool fresh = !root->selection;
        if (fresh) {
            root->selection = new TileNode;
            root->selection->setFiltering(QSGTexture::Linear);
        }
        QSGTexture* previous = root->selection->texture();
        root->selection->setTexture(window()->createTextureFromImage(img));
        delete previous;
        if (fresh) {  // (only with a texture: the software renderer crashes on texture nodes without one)
            root->selectionRoot->appendChildNode(root->selection);
        }
        root->selectionRevision = canvasView->selectionRevision();
        root->selectionZoom = zoom;
        root->selectionRegion = region;
    }
    root->selection->setRect(root->selectionRegion.translated(pageOrigin));
}

void DocumentCanvasItem::updateSearchHits(QSGNode* pageNode, size_t pageIndex, double scale) {
    auto* node = static_cast<PageNode*>(pageNode);
    const xqt::DocumentSearch& search = canvasView->getSession().search();
    if (node->searchRevision == search.revision() && node->searchScale == scale) {
        return;
    }
    node->searchRevision = search.revision();
    node->searchScale = scale;
    while (QSGNode* child = node->searchRoot->firstChild()) {
        node->searchRoot->removeChildNode(child);
        delete child;
    }
    const auto& hits = search.hits();
    auto it = std::lower_bound(hits.begin(), hits.end(), pageIndex,
                               [](const xqt::DocumentSearch::Hit& h, size_t p) { return h.page < p; });
    for (; it != hits.end() && it->page == pageIndex; ++it) {
        const bool current = static_cast<int>(it - hits.begin()) == search.currentHit();
        const QRectF r(it->rect.x() * scale, it->rect.y() * scale, it->rect.width() * scale, it->rect.height() * scale);
        node->searchRoot->appendChildNode(new QSGSimpleRectNode(
                r.adjusted(-1, -1, 1, 1), current ? QColor(255, 120, 0, 150) : QColor(255, 210, 0, 110)));
    }
}

QSGNode* DocumentCanvasItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* root = static_cast<CanvasRootNode*>(old);
    if (!root) {
        root = new CanvasRootNode;
    }
    if (viewReplaced) {
        // Another document: drop all page nodes (textures) of the previous one.
        for (auto& [page, node]: root->pages) {
            root->pagesRoot->removeChildNode(node);
            delete node;
        }
        root->pages.clear();
        viewReplaced = false;
    }
    if (!canvasView) {
        root->hover->setRect(QRectF());
        return root;
    }

    const double zoom = canvasView->getViewController().zoom();
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const auto [first, last] = canvasView->visiblePages();

    std::unordered_map<const xqt::CanvasPage*, PageNode*> keep;
    for (size_t i = first; i <= last && i < canvasView->pageCount(); ++i) {
        xqt::CanvasPage* page = canvasView->getPage(i);
        PageNode* node = nullptr;
        bool fresh = false;
        if (auto it = root->pages.find(page); it != root->pages.end()) {
            node = it->second;
            root->pages.erase(it);
        } else {
            node = new PageNode;
            root->pagesRoot->appendChildNode(node);
            fresh = true;
        }
        keep[page] = node;

        const QRectF r = canvasView->pageViewRect(i);
        const auto info = page->bufferInfo();
        bool all = false;
        std::vector<QRect> dirty;
        if (info.valid) {
            dirty = page->takeDirty(info, all);
        }

        QMatrix4x4 m;
        m.translate(static_cast<float>(snap(r.x(), dpr)), static_cast<float>(snap(r.y(), dpr)));
        // Children are in view pixels (no buffer yet) or buffer pixels (scaled by the node's transform).
        updateSearchHits(node, i, info.valid ? info.zoom : zoom);
        if (!info.valid) {
            node->clearTiles();
            node->setMatrix(m);
            node->placeholder->setRect(QRectF(QPointF(0, 0), r.size()));
            node->shadow->setRect(QRectF(QPointF(2, 2), r.size()));
            continue;
        }
        const double scale = zoom / info.zoom;
        m.scale(static_cast<float>(scale), static_cast<float>(scale));
        node->setMatrix(m);
        // Placeholder and shadow in the buffer's logical coordinates (the transform scales them).
        const QSizeF bufferLogical(r.width() / scale, r.height() / scale);
        node->placeholder->setRect(QRectF(QPointF(0, 0), bufferLogical));
        node->shadow->setRect(QRectF(QPointF(2 / scale, 2 / scale), bufferLogical));

        const bool rebuild = fresh || node->bufferZoom != info.zoom || node->dpiScale != info.dpiScale ||
                             node->pixelSize != info.pixelSize;
        std::vector<int> toCompose;
        if (rebuild) {
            node->clearTiles();
            node->cols = (info.pixelSize.width() + TILE - 1) / TILE;
            node->rows = (info.pixelSize.height() + TILE - 1) / TILE;
            node->bufferZoom = info.zoom;
            node->dpiScale = info.dpiScale;
            node->pixelSize = info.pixelSize;
            for (int t = 0; t < node->cols * node->rows; ++t) {
                auto* tile = new TileNode;
                tile->setFiltering(QSGTexture::Linear);
                const QRect px = tileRect(t, node->cols, info.pixelSize);
                tile->setRect(QRectF(px.x() / info.dpiScale, px.y() / info.dpiScale, px.width() / info.dpiScale,
                                     px.height() / info.dpiScale));
                node->tiles.push_back(tile);
                toCompose.push_back(t);
            }
        } else if (all) {
            for (int t = 0; t < node->cols * node->rows; ++t) {
                toCompose.push_back(t);
            }
        } else {
            for (const QRect& d: dirty) {
                const int x0 = d.left() / TILE, x1 = d.right() / TILE, y0 = d.top() / TILE, y1 = d.bottom() / TILE;
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        const int t = y * node->cols + x;
                        if (t >= 0 && t < static_cast<int>(node->tiles.size()) &&
                            std::find(toCompose.begin(), toCompose.end(), t) == toCompose.end()) {
                            toCompose.push_back(t);
                        }
                    }
                }
            }
        }
        for (int t: toCompose) {
            TileNode* tile = node->tiles[static_cast<size_t>(t)];
            const QImage img = page->composeTile(tileRect(t, node->cols, info.pixelSize));
            QSGTexture* previous = tile->texture();
            tile->setTexture(window()->createTextureFromImage(img, QQuickWindow::TextureIsOpaque));
            delete previous;
            if (!tile->parent()) {
                node->insertChildNodeBefore(tile, node->searchRoot);
            }
        }
    }
    // Pages that scrolled out of view: free their textures.
    for (auto& [page, node]: root->pages) {
        root->pagesRoot->removeChildNode(node);
        delete node;
    }
    root->pages = std::move(keep);
    updateSelectionNode(root, zoom, dpr);

    if (auto h = input ? input->hoverPosition() : std::nullopt) {
        root->hover->setRect(QRectF(h->x() - 3, h->y() - 3, 6, 6));
        root->hover->setColor(input->hoverIsEraser() ? QColor(0xd0, 0x30, 0x30) : QColor(0x1d, 0x2b, 0x8f));
    } else {
        root->hover->setRect(QRectF());
    }
    return root;
}

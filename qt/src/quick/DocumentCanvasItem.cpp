#include "DocumentCanvasItem.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include <QCoreApplication>
#include <QMatrix4x4>
#include <QNativeGestureEvent>
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "CanvasInput.h"
#include "CanvasPage.h"
#include "CanvasView.h"

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
    std::vector<TileNode*> tiles;
    int cols = 0, rows = 0;
    double bufferZoom = 0;
    double dpiScale = 0;
    QSize pixelSize;
};

class CanvasRootNode final: public QSGNode {
public:
    CanvasRootNode() {
        pagesRoot = new QSGNode;
        appendChildNode(pagesRoot);
        hover = new QSGSimpleRectNode(QRectF(), QColor(0x1d, 0x2b, 0x8f));
        appendChildNode(hover);
    }
    QSGNode* pagesRoot;
    QSGSimpleRectNode* hover;
    std::unordered_map<const xqt::CanvasPage*, PageNode*> pages;
};

QRect tileRect(int index, int cols, QSize pixelSize) {
    const int cx = index % cols, cy = index / cols;
    return QRect(cx * TILE, cy * TILE, TILE, TILE).intersected(QRect(QPoint(0, 0), pixelSize));
}

double snap(double v, double dpr) { return std::round(v * dpr) / dpr; }
}  // namespace

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
        connect(canvasView, &QObject::destroyed, this, [this] {
            input.reset();
            viewReplaced = true;
            update();
        });
        connect(input.get(), &xqt::CanvasInput::hoverChanged, this, &QQuickItem::update);
        updateViewGeometry();
    }
    Q_EMIT viewChanged();
    update();
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

bool DocumentCanvasItem::claims(QPointF scenePos) const {
    return isVisible() && QRectF(0, 0, width(), height()).contains(mapFromScene(scenePos));
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
                node->appendChildNode(tile);
            }
        }
    }
    // Pages that scrolled out of view: free their textures.
    for (auto& [page, node]: root->pages) {
        root->pagesRoot->removeChildNode(node);
        delete node;
    }
    root->pages = std::move(keep);

    if (auto h = input ? input->hoverPosition() : std::nullopt) {
        root->hover->setRect(QRectF(h->x() - 3, h->y() - 3, 6, 6));
        root->hover->setColor(input->hoverIsEraser() ? QColor(0xd0, 0x30, 0x30) : QColor(0x1d, 0x2b, 0x8f));
    } else {
        root->hover->setRect(QRectF());
    }
    return root;
}

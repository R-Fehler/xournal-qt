#include "QuickCanvas.h"

#include <cmath>
#include <vector>

#include <QMatrix4x4>
#include <QNativeGestureEvent>
#include <QQuickWindow>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "SpikeContext.h"

namespace {
constexpr int TILE = 256;

class TileNode: public QSGSimpleTextureNode {
public:
    ~TileNode() override { delete texture(); }
};

class CanvasRootNode: public QSGNode {
public:
    CanvasRootNode() {
        xform = new QSGTransformNode;
        appendChildNode(xform);
        shadow = new QSGSimpleRectNode(QRectF(), QColor(0, 0, 0, 90));
        xform->appendChildNode(shadow);
        hover = new QSGSimpleRectNode(QRectF(), QColor(0x1d, 0x2b, 0x8f));
        appendChildNode(hover);
    }
    QSGTransformNode* xform;
    QSGSimpleRectNode* shadow;
    QSGSimpleRectNode* hover;
    std::vector<TileNode*> tiles;
    int cols = 0, rows = 0;
    quint64 generation = ~0ull;
};
}  // namespace

QuickCanvas::QuickCanvas(QQuickItem* parent): QQuickItem(parent), ctx(SpikeContext::instance()) {
    setFlag(ItemHasContents, true);
    Q_ASSERT(ctx);
    connect(ctx->model(), &InkModel::dirty, this, &QuickCanvas::markDirty);
    connect(ctx->model(), &InkModel::fullyDirty, this, &QQuickItem::update);
    connect(ctx->viewport(), &Viewport::changed, this, &QQuickItem::update);
    connect(ctx->router(), &InputRouter::hoverChanged, this, &QQuickItem::update);
}

void QuickCanvas::markDirty(QRect r) {
    const int x0 = r.left() / TILE, x1 = r.right() / TILE;
    const int y0 = r.top() / TILE, y1 = r.bottom() / TILE;
    const int cols = (ctx->model()->bufferSize().width() + TILE - 1) / TILE;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            dirtyTiles.insert(y * cols + x);
        }
    }
    update();
}

void QuickCanvas::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size() && !newGeometry.isEmpty()) {
        ctx->viewport()->setViewSize(newGeometry.size());
    }
}

void QuickCanvas::itemChange(ItemChange change, const ItemChangeData& value) {
    if (change == ItemSceneChange) {
        if (filteredWindow) {
            filteredWindow->removeEventFilter(this);
            disconnect(filteredWindow, nullptr, this, nullptr);
        }
        filteredWindow = value.window;
        if (filteredWindow) {
            // Runs before QQuickWindow::event() and thus before Qt Quick's delivery agent.
            filteredWindow->installEventFilter(this);
            // Emitted on the render thread in the threaded render loop.
            connect(
                    filteredWindow, &QQuickWindow::frameSwapped, this, [r = ctx->router()] { r->framePresented(); },
                    Qt::DirectConnection);
        }
    }
    QQuickItem::itemChange(change, value);
}

bool QuickCanvas::claimsScenePoint(QPointF scenePos) const {
    return QRectF(0, 0, width(), height()).contains(mapFromScene(scenePos));
}

bool QuickCanvas::eventFilter(QObject* watched, QEvent* e) {
    if (watched != filteredWindow) {
        return false;
    }
    InputRouter* router = ctx->router();
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto* t = static_cast<QTabletEvent*>(e);
            if (!penGrab && !claimsScenePoint(t->position())) {
                return false;  // unaccepted -> Qt synthesizes mouse events for the QML controls
            }
            if (e->type() == QEvent::TabletPress) {
                penGrab = true;
            } else if (e->type() == QEvent::TabletRelease) {
                penGrab = false;
            }
            router->tablet(t, mapFromScene(t->position()));
            t->accept();
            return true;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto* t = static_cast<QTouchEvent*>(e);
            if (e->type() == QEvent::TouchBegin) {
                touchSessionOwned = !t->points().isEmpty() && claimsScenePoint(t->points().first().scenePosition());
            }
            if (!touchSessionOwned) {
                return false;
            }
            router->touch(t, [this](QPointF scenePos) { return mapFromScene(scenePos); });
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
            const bool inside = claimsScenePoint(m->scenePosition());
            if (!mouseGrab && (!inside || (e->type() == QEvent::MouseMove && m->buttons() == Qt::NoButton))) {
                return false;
            }
            if (e->type() == QEvent::MouseButtonPress) {
                mouseGrab = true;
            } else if (e->type() == QEvent::MouseButtonRelease && m->buttons() == Qt::NoButton) {
                mouseGrab = false;
            }
            router->mouse(m, mapFromScene(m->scenePosition()));
            m->accept();
            return true;
        }
        case QEvent::Wheel: {
            auto* w = static_cast<QWheelEvent*>(e);
            if (!claimsScenePoint(w->scenePosition())) {
                return false;
            }
            router->wheel(w, mapFromScene(w->scenePosition()));
            w->accept();
            return true;
        }
        case QEvent::NativeGesture: {
            auto* g = static_cast<QNativeGestureEvent*>(e);
            if (!claimsScenePoint(g->scenePosition())) {
                return false;
            }
            router->nativeGesture(g, mapFromScene(g->scenePosition()));
            g->accept();
            return true;
        }
        default:
            return false;
    }
}

QSGNode* QuickCanvas::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* root = static_cast<CanvasRootNode*>(old);
    if (!root) {
        root = new CanvasRootNode;
    }
    // The GUI thread is blocked here, so reading the CPU buffer is safe.
    ctx->router()->syncPoint();
    InkModel* model = ctx->model();
    const QSize bs = model->bufferSize();
    const double rs = model->renderScale();
    const QImage img = model->bufferImage();

    const auto makeTexture = [&](const QRect& r) {
        return window()->createTextureFromImage(img.copy(r), QQuickWindow::TextureIsOpaque);
    };
    const auto tileRect = [&](int idx) {
        const int cx = idx % root->cols, cy = idx / root->cols;
        return QRect(cx * TILE, cy * TILE, TILE, TILE).intersected(QRect(QPoint(0, 0), bs));
    };

    if (root->generation != model->generation()) {
        // New buffer: rebuild all tiles. Nodes get their texture before entering the tree.
        for (TileNode* t: root->tiles) {
            root->xform->removeChildNode(t);
            delete t;
        }
        root->tiles.clear();
        root->cols = (bs.width() + TILE - 1) / TILE;
        root->rows = (bs.height() + TILE - 1) / TILE;
        for (int i = 0; i < root->cols * root->rows; ++i) {
            const QRect r = tileRect(i);
            auto* t = new TileNode;
            t->setFiltering(QSGTexture::Linear);
            t->setTexture(makeTexture(r));
            t->setRect(QRectF(r.x() / rs, r.y() / rs, r.width() / rs, r.height() / rs));
            root->xform->appendChildNode(t);
            root->tiles.push_back(t);
        }
        root->generation = model->generation();
    } else {
        for (int idx: dirtyTiles) {
            if (idx < 0 || idx >= static_cast<int>(root->tiles.size())) {
                continue;
            }
            TileNode* node = root->tiles[static_cast<size_t>(idx)];
            QSGTexture* previous = node->texture();
            node->setTexture(makeTexture(tileRect(idx)));
            delete previous;
        }
    }
    dirtyTiles.clear();

    Viewport* vp = ctx->viewport();
    QMatrix4x4 m;
    m.translate(static_cast<float>(vp->offset().x()), static_cast<float>(vp->offset().y()));
    m.scale(static_cast<float>(vp->scale()), static_cast<float>(vp->scale()));
    root->xform->setMatrix(m);
    root->shadow->setRect(QRectF(3.0 / vp->scale(), 3.0 / vp->scale(), InkModel::PAGE_W, InkModel::PAGE_H));

    if (auto hover = ctx->router()->hoverPos()) {
        root->hover->setRect(QRectF(hover->x() - 3, hover->y() - 3, 6, 6));
        root->hover->setColor(ctx->router()->isEraserHover() ? QColor(0xd0, 0x30, 0x30) : QColor(0x1d, 0x2b, 0x8f));
    } else {
        root->hover->setRect(QRectF());
    }
    return root;
}

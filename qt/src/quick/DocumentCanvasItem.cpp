#include "DocumentCanvasItem.h"
#include "EmojiNames.h"
#include "AdaptiveLayout.h"
#include "TouchGestures.h"
#include "DarkTileMaterial.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

#include <cairo.h>

#include <QKeySequence>
#include <QLoggingCategory>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QQmlEngine>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QPixmap>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGRendererInterface>
#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGSimpleRectNode>
#include <QSGSimpleTextureNode>
#include <QSGTransformNode>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>

#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "CanvasInput.h"
#include "InputLog.h"
#include "CanvasPage.h"
#include "DarkPages.h"
#include "Perf.h"
#include "MarkdownBoxResize.h"
#include "CanvasView.h"
#include "StickyNotes.h"
#include "CurtainLayer.h"
#include "GeometryToolLayer.h"
#include "GeometryToolPicture.h"
#include "HoverPointer.h"
#include "TextEditor.h"
#include "session/AppContext.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

namespace {
constexpr int TILE = 256;
/// Tiles composed and uploaded in one frame (about 0.26 MB each)
constexpr int TILES_WHILE_MOVING = 12;
constexpr int TILES_WHEN_STILL = 64;
/// The picture of the setsquare or compass: at most this many pixels a side (64 MB); a bigger one is drawn smaller,
/// with the part in view drawn sharp once it rests
constexpr double MAX_PICTURE = 4096;
/// A new size or zoom of the setsquare or compass is drawn anew when it has not changed for this long (ms)
constexpr int GEOMETRY_SETTLES_MS = 150;
/// How long the mouse rests on a formula that cannot be drawn before its error is shown.
constexpr int HOVER_RESTS_MS = 400;

class TileNode final: public QSGSimpleTextureNode {
public:
    ~TileNode() override { delete texture(); }
    /// Shown dark on the GPU (dark pages: DarkTileMaterial) with `table`, or as it is (nullptr). Call again after
    /// setTexture.
    void setDark(QSGTexture* table) {
        if (!light) {
            light = material();
            lightOpaque = opaqueMaterial();
        }
        if (!table) {
            if (material() != light) {
                setMaterial(light);
                setOpaqueMaterial(lightOpaque);
                markDirty(QSGNode::DirtyMaterial);
            }
            return;
        }
        if (material() != &dark || dark.tile != texture() || dark.table != table) {
            dark.tile = texture();
            dark.table = table;
            setMaterial(&dark);
            setOpaqueMaterial(&dark);
            markDirty(QSGNode::DirtyMaterial);
        }
    }
    bool shownDark() const { return material() == &dark; }
    DarkTileMaterial dark;

private:
    QSGMaterial* light = nullptr;
    QSGMaterial* lightOpaque = nullptr;
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
            // Only the composed tiles are in the scene graph. Removing a node that is not a child is not checked in a
            // release build of Qt and empties this node's list of children (the page, its tiles and preview vanish).
            if (t->parent() == this) {
                removeChildNode(t);
            }
            delete t;
        }
        tiles.clear();
        composed.clear();
        cols = rows = 0;
    }
    /// The page's preview (drawn in advance) over the whole page, until it is rendered; null image: none
    /// `cpuDark`: the preview turned dark on the CPU first (dark pages on the software renderer; on the GPU the
    /// caller sets the preview's material)
    void showPreview(QQuickWindow* window, const QImage& image, QSizeF size, bool cpuDark = false) {
        if (image.isNull()) {
            hidePreview();
            return;
        }
        if (!preview) {
            preview = new TileNode;
            preview->setFiltering(QSGTexture::Linear);
        }

        if (previewKey != image.cacheKey() || previewDark != cpuDark) {
            xqt::Perf::add(xqt::Perf::Previews);
            QSGTexture* previous = preview->texture();
            QImage shown = image;
            if (cpuDark) {
                xqt::dark::apply(shown, tone.paper);
            }
            preview->setTexture(window->createTextureFromImage(shown, QQuickWindow::TextureIsOpaque));
            delete previous;
            previewKey = image.cacheKey();
            previewDark = cpuDark;
        }
        preview->setRect(QRectF(QPointF(0, 0), size));
        if (!preview->parent()) {
            // With its texture (the software renderer needs one), and under the tiles
            insertChildNodeAfter(preview, placeholder);
        }
    }
    void hidePreview() {
        if (preview) {
            removeChildNode(preview);
            delete preview;
            preview = nullptr;
            previewKey = 0;
        }
    }
    std::vector<bool> composed;  ///< tiles that have their picture (the others are not in the scene graph)
    QSGSimpleRectNode* shadow;
    QSGSimpleRectNode* placeholder;
    QSGTransformNode* searchRoot;
    quint64 searchRevision = ~quint64(0);
    double searchScale = 0;
    std::vector<TileNode*> tiles;
    TileNode* preview = nullptr;
    qint64 previewKey = 0;
    bool previewDark = false;
    /// Dark pages: how the page is shown (CanvasPage::darkTone; not dark: as it is), and whether its tiles are turned
    /// dark on the GPU (else on the CPU when composed)
    xqt::CanvasPage::DarkTone tone;
    bool toneOnGpu = false;
    QSGTexture* darkTable = nullptr;  ///< (the root's, while toneOnGpu)
    int cols = 0, rows = 0;
    double bufferZoom = 0;
    double dpiScale = 0;
    QSize pixelSize;
    QPoint origin;  ///< the buffer's top left (a big page drawn in part), logical pixels of the buffer's zoom
};

/// A rectangular clip (the page of the setsquare or compass: it is cut at the page's edges, as when it was drawn into
/// the page).
class PageClipNode final: public QSGClipNode {
public:
    PageClipNode(): geometry(QSGGeometry::defaultAttributes_Point2D(), 4) {
        setGeometry(&geometry);
        setIsRectangular(true);
    }
    void setRect(const QRectF& r) {
        if (r == clipRect()) {
            return;
        }
        setClipRect(r);
        QSGGeometry::updateRectGeometry(&geometry, r);
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry geometry;
};

/// A point on the nearest whole device pixel
QPointF snapPoint(QPointF p, double dpr) {
    return QPointF(std::round(p.x() * dpr) / dpr, std::round(p.y() * dpr) / dpr);
}

/// The setsquare or compass over its page: pictures of it under transforms (see GeometryToolPicture). Moving, turning
/// and sizing it change the transforms; the pictures are drawn anew only for a new size or zoom, once that is stable.
class GeometryNode final: public QSGTransformNode {
public:
    GeometryNode() {
        clip = new PageClipNode;
        appendChildNode(clip);
        body = new QSGTransformNode;
        clip->appendChildNode(body);
        displayAt = new QSGTransformNode;
        clip->appendChildNode(displayAt);
    }
    /// Show the picture of an image (a new texture), in its rect; null image: none
    void show(QQuickWindow* window, QSGTransformNode* parent, TileNode*& node, xqt::GeometryToolPicture::Image image,
              std::atomic<qint64>& pixels) {
        if (image.image.isNull()) {
            drop(parent, node);
            return;
        }
        const bool fresh = !node;
        if (fresh) {
            node = new TileNode;
            node->setFiltering(QSGTexture::Linear);
        }
        QSGTexture* previous = node->texture();
        node->setTexture(window->createTextureFromImage(image.image));
        delete previous;
        node->setRect(image.rect);
        pixels += static_cast<qint64>(image.image.width()) * image.image.height();
        if (fresh) {  // (only with a texture: the software renderer crashes on texture nodes without one)
            if (parent == body && node == patch && base) {
                parent->insertChildNodeAfter(node, base);  // the sharp part over the whole
            } else if (parent == body && node == base && patch) {
                parent->insertChildNodeBefore(node, patch);
            } else {
                parent->appendChildNode(node);
            }
        }
    }
    void drop(QSGTransformNode* parent, TileNode*& node) {
        if (node) {
            parent->removeChildNode(node);
            delete node;
            node = nullptr;
        }
    }
    void clear() {
        drop(body, base);
        drop(body, patch);
        drop(displayAt, display);
        of = 0;
    }
    PageClipNode* clip;
    QSGTransformNode* body;       ///< the tool's own coordinates (points), at the height its pictures were drawn for
    QSGTransformNode* displayAt;  ///< the middle of the angle display, upright
    TileNode* base = nullptr;     ///< the whole tool (at most MAX_PICTURE pixels a side)
    TileNode* patch = nullptr;    ///< a big tool at a high zoom: the part in view, sharp (once it rests)
    TileNode* display = nullptr;
    quint64 of = 0;  ///< whose pictures these are (GeometryToolPicture::serial)
    double baseHeight = 0;
    double baseScale = 0;
    QRectF patchRect;  ///< own coordinates
    double patchScale = 0;
    std::string displayText;
    double displayScale = 0;
};

/// The curtain (CurtainLayer): black over part of the page under a transform of its own (moving, turning and sizing
/// it change only that and the size of the black), cut at the edges of the canvas; its handles over it. The spotlight:
/// four black rectangles around its hole, reaching beyond the canvas, and its rounded corners as four small pictures
/// (made once).
class CurtainNode final: public QSGTransformNode {
public:
    static constexpr int HANDLES = 8;
    CurtainNode() {
        clip = new PageClipNode;
        appendChildNode(clip);
        body = new QSGTransformNode;
        clip->appendChildNode(body);
        sheet = new QSGSimpleRectNode(QRectF(), Qt::black);
        body->appendChildNode(sheet);
        for (auto*& f: around) {
            f = new QSGSimpleRectNode(QRectF(), Qt::black);
            body->appendChildNode(f);
        }
        for (int i = 0; i < HANDLES; ++i) {
            frames[i] = new QSGSimpleRectNode(QRectF(), HANDLE_COLOR);
            clip->appendChildNode(frames[i]);
            fills[i] = new QSGSimpleRectNode(QRectF(), Qt::white);
            clip->appendChildNode(fills[i]);
        }
    }
    void clear() {
        sheet->setRect(QRectF());
        hideHole();
        hideHandles();
    }
    void hideHole() {
        for (auto* f: around) {
            f->setRect(QRectF());
        }
        for (TileNode* c: corners) {
            if (c && c->parent()) {
                body->removeChildNode(c);
            }
        }
    }
    /// The spotlight: black around a hole of `w` x `h` (points, around the origin) with corners of radius `r`, as far
    /// as `reach` from its middle
    void showHole(QQuickWindow* window, double w, double h, double r, double reach) {
        const double x = w / 2, y = h / 2;
        around[0]->setRect(QRectF(QPointF(-reach, -reach), QPointF(reach, -y)));  // above
        around[1]->setRect(QRectF(QPointF(-reach, y), QPointF(reach, reach)));    // below
        around[2]->setRect(QRectF(QPointF(-reach, -y), QPointF(-x, y)));          // left
        around[3]->setRect(QRectF(QPointF(x, -y), QPointF(reach, y)));            // right
        const QRectF at[4] = {QRectF(-x, -y, r, r), QRectF(x - r, -y, r, r), QRectF(-x, y - r, r, r),
                              QRectF(x - r, y - r, r, r)};
        for (int i = 0; i < 4; ++i) {
            if (!corners[i]) {
                // Black with a quarter of a disc left out: its middle at the corner of the picture towards the hole
                constexpr int N = 128;
                QImage image(N, N, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::black);
                QPainter painter(&image);
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setCompositionMode(QPainter::CompositionMode_Clear);
                painter.setPen(Qt::NoPen);
                painter.setBrush(Qt::black);
                const QPointF centre(i % 2 == 0 ? N : 0, i < 2 ? N : 0);
                painter.drawEllipse(centre, N, N);
                painter.end();
                corners[i] = new TileNode;
                corners[i]->setFiltering(QSGTexture::Linear);
                corners[i]->setTexture(window->createTextureFromImage(image));
            }
            corners[i]->setRect(at[i]);
            if (!corners[i]->parent()) {
                body->appendChildNode(corners[i]);
            }
        }
    }
    void hideHandles() {
        for (int i = 0; i < HANDLES; ++i) {
            frames[i]->setRect(QRectF());
            fills[i]->setRect(QRectF());
        }
        if (knob && knob->parent()) {
            clip->removeChildNode(knob);
        }
    }
    /// The knob that turns it: a white disc in a ring (a picture of its own, made once for each pixel ratio), on
    /// whole device pixels
    void showKnob(QQuickWindow* window, QPointF at, double dpr) {
        const int size = static_cast<int>(std::ceil(KNOB * dpr));
        if (knob && knobDpr != dpr) {
            if (knob->parent()) {
                clip->removeChildNode(knob);
            }
            delete knob;
            knob = nullptr;
        }
        if (!knob) {
            QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            const double ring = size / 7.0;
            painter.setPen(QPen(HANDLE_COLOR, ring));
            painter.setBrush(Qt::white);
            painter.drawEllipse(QRectF(ring / 2, ring / 2, size - ring, size - ring));
            painter.end();
            knob = new TileNode;
            knob->setFiltering(QSGTexture::Linear);
            knob->setTexture(window->createTextureFromImage(image));
            knobDpr = dpr;
        }
        const double side = size / dpr;
        knob->setRect(QRectF(snapPoint(at - QPointF(side / 2, side / 2), dpr), QSizeF(side, side)));
        if (!knob->parent()) {
            clip->appendChildNode(knob);
        }
    }
    ~CurtainNode() override {
        // (in the tree they go with it)
        if (knob && !knob->parent()) {
            delete knob;
        }
        for (TileNode* c: corners) {
            if (c && !c->parent()) {
                delete c;
            }
        }
    }
    static inline const QColor HANDLE_COLOR{0x3f, 0x51, 0xb5};  // (the window's accent, Material Indigo)
    static constexpr double HANDLE = 12;  ///< a handle's size (item pixels)
    static constexpr double KNOB = 20;
    PageClipNode* clip;
    QSGTransformNode* body;  ///< its own coordinates (points from its middle, unturned)
    QSGSimpleRectNode* sheet;                    ///< the curtain
    std::array<QSGSimpleRectNode*, 4> around{};  ///< the spotlight: black above, below, left and right of its hole
    std::array<TileNode*, 4> corners{};          ///< ... and in its corners (top left, top right, bottom left, right)
    std::array<QSGSimpleRectNode*, HANDLES> frames{};
    std::array<QSGSimpleRectNode*, HANDLES> fills{};
    TileNode* knob = nullptr;
    double knobDpr = 0;  ///< the pixel ratio its picture was made for
};

// Containers are (identity) transform nodes, not plain QSGNodes: Qt Quick's software backend re-resolves a changed
// node's transform and clip from its parent, and only records them for transform/clip/opacity nodes. Under a plain
// parent a re-positioned page would lose the item's position.
class CanvasRootNode final: public QSGTransformNode {
public:
    CanvasRootNode() {
        pagesRoot = new QSGTransformNode;
        appendChildNode(pagesRoot);
        geometry = new GeometryNode;
        appendChildNode(geometry);
        selectionRoot = new QSGTransformNode;
        appendChildNode(selectionRoot);
        curtain = new CurtainNode;
        appendChildNode(curtain);
    }
    QSGTransformNode* pagesRoot;
    GeometryNode* geometry;  ///< the setsquare or compass, over the pages and under the selection
    QSGTransformNode* selectionRoot;  ///< the selection (EditSelection::paint), above the pages
    CurtainNode* curtain;             ///< over everything of the document (only the pen's hover dot is above it)
    TileNode* selection = nullptr;
    quint64 selectionRevision = ~quint64(0);
    double selectionZoom = 0;
    double selectionDpr = 0;  ///< the pixel ratio it was drawn for (a window moved to another screen: drawn anew)
    QRectF selectionRegion;   ///< in the selection page's view pixels (whole device pixels)
    bool selectionDark = false;  ///< drawn dark (dark pages)
    std::unordered_map<const xqt::CanvasPage*, PageNode*> pages;
    /// Dark pages on the GPU: the table (DarkPages.h) as a texture, made anew when it changes
    QSGTexture* darkTable = nullptr;
    quint64 darkTableGeneration = ~quint64(0);
    ~CanvasRootNode() override { delete darkTable; }
};

QRect tileRect(int index, int cols, QSize pixelSize) {
    const int cx = index % cols, cy = index / cols;
    return QRect(cx * TILE, cy * TILE, TILE, TILE).intersected(QRect(QPoint(0, 0), pixelSize));
}

double snap(double v, double dpr) { return std::round(v * dpr) / dpr; }

/// All canvas items (a window may have two: the document of the tab and its reference beside it)
std::vector<DocumentCanvasItem*>& allCanvases() {
    static std::vector<DocumentCanvasItem*> items;
    return items;
}
}  // namespace

namespace {
constexpr double PI = 3.14159265358979323846;

/// Triangles of a line `width` wide along the closed polygon `points`; dashed when `dash` > 0 (that long on, that
/// long off, measured along it)
void addStroke(std::vector<QPointF>& triangles, const std::vector<QPointF>& points, double width, double dash) {
    const auto quad = [&](QPointF a, QPointF b, QPointF normal) {
        triangles.insert(triangles.end(), {a + normal, a - normal, b + normal, b + normal, a - normal, b - normal});
    };
    double along = 0;
    for (size_t i = 0; i < points.size(); ++i) {
        const QPointF a = points[i], b = points[(i + 1) % points.size()];
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (length <= 0) {
            continue;
        }
        const QPointF dir = (b - a) / length;
        const QPointF normal(-dir.y() * width / 2, dir.x() * width / 2);
        if (dash <= 0) {
            quad(a - dir * (width / 2), b + dir * (width / 2), normal);  // (longer by half the width: closed corners)
            continue;
        }
        for (double t = 0; t < length;) {
            const double phase = std::fmod(along, 2 * dash);
            const double step = std::min(phase < dash ? dash - phase : 2 * dash - phase, length - t);
            if (phase < dash) {
                quad(a + dir * t, a + dir * (t + step), normal);
            }
            t += step;
            along += step;
        }
    }
}

QSGGeometryNode* trianglesNode(const std::vector<QPointF>& triangles, const QColor& color) {
    auto* geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), static_cast<int>(triangles.size()));
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    QSGGeometry::Point2D* v = geometry->vertexDataAsPoint2D();
    for (size_t i = 0; i < triangles.size(); ++i) {
        v[i].set(static_cast<float>(triangles[i].x()), static_cast<float>(triangles[i].y()));
    }
    auto* material = new QSGFlatColorMaterial;
    material->setColor(color);
    auto* node = new QSGGeometryNode;
    node->setGeometry(geometry);
    node->setMaterial(material);
    node->setFlags(QSGNode::OwnsGeometry | QSGNode::OwnsMaterial);
    return node;
}
}  // namespace

/// The pointer drawn by the canvas (qt/docs/hover-cursors.md): an item of its own over the pages, so that following
/// the pen moves only it (a new position in the scene graph) and draws no page anew. Its nodes are made again only
/// when what it shows changes. The dot is a small texture; the eraser's outline is geometry (no texture however big
/// it is: a big eraser at a high zoom is thousands of pixels wide).
class HoverMarkItem final: public QQuickItem {
public:
    explicit HoverMarkItem(QQuickItem* parent): QQuickItem(parent) {
        setFlag(ItemHasContents, true);
        setAcceptedMouseButtons(Qt::NoButton);
        setSize(QSizeF(xqt::hover::dotSide(), xqt::hover::dotSide()));
    }
    /// The eraser's outline, or (none) the dot
    void show(const std::optional<xqt::hover::EraserMark>& mark) {
        if (mark == eraser) {
            return;
        }
        eraser = mark;
        const int side = eraser ? xqt::hover::eraserSide(*eraser) : xqt::hover::dotSide();
        setSize(QSizeF(side, side));
        update();
    }
    std::optional<xqt::hover::EraserMark> eraser;
    QRectF dotRect;  ///< the dot's picture in the last frame (tests)
    QSize dotPixels;

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override {
        if (change == ItemDevicePixelRatioHasChanged) {
            update();  // (the dot's picture for the new pixel ratio)
        }
        QQuickItem::itemChange(change, value);
    }
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override {
        auto* root = static_cast<QSGTransformNode*>(old);
        if (!root) {
            root = new QSGTransformNode;  // (a transform node: see CanvasRootNode)
        }
        const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
        if (root->firstChild() && builtFor == eraser && builtDpr == dpr) {
            return root;
        }
        builtFor = eraser;
        builtDpr = dpr;
        while (QSGNode* child = root->firstChild()) {
            root->removeChildNode(child);
            delete child;
        }
        const QPointF center(width() / 2, height() / 2);
        if (!eraser) {
            auto* dot = new QSGSimpleTextureNode;
            const QImage picture = xqt::hover::dotImage(dpr);
            dot->setTexture(window()->createTextureFromImage(picture));
            dot->setOwnsTexture(true);
            dot->setFiltering(QSGTexture::Linear);
            // One pixel of it for each of the screen, from the item's top left (on a whole device pixel): its
            // ceil(side * dpr) pixels are a little more than the side at a ratio such as 1.1 or 1.33
            dot->setRect(QRectF(0, 0, picture.width() / dpr, picture.height() / dpr));
            dotRect = dot->rect();
            dotPixels = dot->texture()->textureSize();
            root->appendChildNode(dot);
            return root;
        }
        // As hover::paintEraser draws it: a faint gray inside, a light halo, the gray outline (dashed: whole strokes)
        const double r = eraser->size / 2;
        std::vector<QPointF> outline;
        if (eraser->round) {
            const int n = std::clamp(static_cast<int>(std::ceil(PI * eraser->size / 3)), 24, 720);
            for (int i = 0; i < n; ++i) {
                const double a = 2 * PI * i / n;
                outline.push_back(center + QPointF(r * std::cos(a), r * std::sin(a)));
            }
        } else {
            outline = {center + QPointF(-r, -r), center + QPointF(r, -r), center + QPointF(r, r),
                       center + QPointF(-r, r)};
        }
        std::vector<QPointF> fill;
        for (size_t i = 0; i < outline.size(); ++i) {
            fill.insert(fill.end(), {center, outline[i], outline[(i + 1) % outline.size()]});
        }
        root->appendChildNode(trianglesNode(fill, xqt::hover::ERASER_FILL));
        std::vector<QPointF> halo, line;
        addStroke(halo, outline, xqt::hover::HALO_WIDTH, 0);
        addStroke(line, outline, xqt::hover::LINE_WIDTH, eraser->wholeStrokes ? xqt::hover::DASH : 0);
        root->appendChildNode(trianglesNode(halo, xqt::hover::HALO));
        root->appendChildNode(trianglesNode(line, xqt::hover::ERASER_LINE));
        return root;
    }

private:
    std::optional<xqt::hover::EraserMark> builtFor;
    double builtDpr = 0;
};

void xqt::registerQuickTypes() {
    qmlRegisterType<DocumentCanvasItem>("XournalQt.Canvas", 1, 0, "DocumentCanvas");
    qmlRegisterType<TouchGestures>("XournalQt.Canvas", 1, 0, "TouchGestures");
    qmlRegisterType<AdaptiveLayout>("XournalQt.Canvas", 1, 0, "AdaptiveLayout");
    qmlRegisterSingletonType<EmojiNames>("XournalQt.Canvas", 1, 0, "Emoji",
                                         [](QQmlEngine*, QJSEngine*) -> QObject* { return new EmojiNames; });
}

DocumentCanvasItem::DocumentCanvasItem(QQuickItem* parent): QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setAcceptTouchEvents(true);
    refreshPointer();
    // Proximity events are only delivered to the application object.
    qApp->installEventFilter(this);
    xqt::AdaptiveLayout::watchBeforeCanvases();  // (it sees the strokes on this canvas too: they hold the size class)
    allCanvases().push_back(this);
    geometryTimer.setSingleShot(true);
    geometryTimer.setInterval(GEOMETRY_SETTLES_MS);
    connect(&geometryTimer, &QTimer::timeout, this, [this] {
        geometrySettled = true;
        update();
    });
    linkTimer.setSingleShot(true);
    linkTimer.setInterval(LINK_HOVER_MS);
    connect(&linkTimer, &QTimer::timeout, this, [this] {
        if (linkId.first && !linkCovered && linkShown != linkPending) {
            linkShown = linkPending;
            Q_EMIT hoveredLinkChanged();
        }
    });
    hoverTimer.setSingleShot(true);
    hoverTimer.setInterval(HOVER_RESTS_MS);
    connect(&hoverTimer, &QTimer::timeout, this, [this] {
        // (only once the mouse rests: the hit test of the Markdown texts is not for every move)
        if (auto* v = canvasView.data(); v && !mouseGrab && claims(hoverScenePos)) {
            if (const auto hit = v->mathErrorAt(toView(mapFromScene(hoverScenePos)))) {
                setMathError(hit->error, toItem(hit->viewRect));
                return;
            }
        }
        setMathError({}, {});
    });
}

void DocumentCanvasItem::linkHovers(QPointF itemPos, Qt::KeyboardModifiers modifiers, bool mouse) {
    linkHoverAt = itemPos;
    linkHoverByMouse = mouse;
    std::optional<xqt::CanvasView::LinkHover> link;
    const QPointF viewPos = toView(itemPos);
    if (canvasView && input && isVisible() && QRectF(0, 0, width(), height()).contains(itemPos) &&
        !canvasView->curtain().covers(viewPos)) {  // (under the curtain nothing is shown, not even where links go)
        link = canvasView->hoverLinkAt(viewPos);  // (the links each page keeps: cheap for every move)
    }
    if (!link) {
        const bool width = mouse && canvasView && input && canvasView->boxResize().onHandle(viewPos);
        endLinkHover(width);  // (the handle that sets a box's width)
        linkHoverAt = itemPos;
        return;
    }
    if (link->id != linkId) {
        // Another link: is it the canvas that is under the pointer there, not a control over it? (Only asked when
        // the link changes: the hit test of the items is not for every move.)
        linkId = link->id;
        linkCovered = !claims(mapToScene(itemPos));
        linkPending = QVariantMap{{QStringLiteral("uri"), link->target.uri},
                                  {QStringLiteral("page"), link->target.page},
                                  {QStringLiteral("pdfPage"), link->target.pdfPage}};
        if (linkCovered) {
            linkTimer.stop();
            if (!linkShown.isEmpty()) {
                linkShown.clear();
                Q_EMIT hoveredLinkChanged();
            }
        } else if (!linkShown.isEmpty()) {
            linkShown = linkPending;  // from one link to the next: at once, as browsers do
            Q_EMIT hoveredLinkChanged();
        } else {
            linkTimer.start();
        }
    } else if (linkCovered && !claims(mapToScene(itemPos))) {
        // (still under a control, or a menu is open: asked again while so)
    } else if (linkCovered) {
        linkCovered = false;  // the control or menu went
        linkTimer.start();
    }
    if (linkPointer != itemPos) {
        linkPointer = itemPos;
        Q_EMIT hoveredLinkPointerChanged();
    }
    // The mouse's cursor: a pointing hand where a click follows the link
    const bool hand = mouse && !linkCovered && input->clickFollowsLink(link->editing, modifiers);
    const bool width = mouse && !hand && canvasView->boxResize().onHandle(viewPos);
    setPointerKind(hand ? PointerKind::Link : width ? PointerKind::WidthHandle : PointerKind::Tool);
}

void DocumentCanvasItem::endLinkHover(bool widthHandle) {
    linkHoverAt.reset();
    linkId = {nullptr, 0};
    linkCovered = false;
    linkTimer.stop();
    if (!linkShown.isEmpty()) {
        linkShown.clear();
        Q_EMIT hoveredLinkChanged();
    }
    setPointerKind(widthHandle ? PointerKind::WidthHandle : PointerKind::Tool);
}

void DocumentCanvasItem::setPointerKind(PointerKind kind) {
    pointerKind = kind;
    applyCursor();
}

void DocumentCanvasItem::refreshPointer() {
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const auto pointer = canvasView ? xqt::hover::pointerSetting(*canvasView->getSession().getSettings())
                                    : xqt::hover::Pointer::Dot;
    // With the eraser (also the pen's eraser end, a side button that erases): the eraser itself, its size at this zoom.
    // Not where nothing is erased: a document shown for reading, a text file (pen and mouse put the cursor there).
    std::optional<xqt::hover::EraserMark> eraser;
    if (canvasView && !canvasView->isReadingOnly() && !canvasView->textMode()) {
        auto& session = canvasView->getSession();
        eraser = xqt::hover::eraserMark(*session.getToolHandler(), *session.getSettings(),
                                        pointerSource == PointerSource::Pen && pointerEraserEnd,
                                        canvasView->getViewController().zoom());
    }
    const bool eraserCursor = eraser && xqt::hover::eraserFitsCursor(*eraser, dpr);
    // The tool's pointer is a cursor of the platform: the compositor moves it with the pointer, before the app sees
    // the move (a picture drawn by the app is a frame or more behind)
    const QString key = eraserCursor ? QStringLiteral("eraser:%1:%2:%3@%4")
                                               .arg(eraser->size)
                                               .arg(eraser->round)
                                               .arg(eraser->wholeStrokes)
                                               .arg(dpr)
                        : pointer == xqt::hover::Pointer::Crosshair ? QStringLiteral("cross")
                                                                    : QStringLiteral("dot@%1").arg(dpr);
    if (key != toolCursorKey) {
        toolCursorKey = key;
        if (eraserCursor) {
            const int middle = xqt::hover::eraserSide(*eraser) / 2;
            toolCursor = QCursor(QPixmap::fromImage(xqt::hover::eraserImage(*eraser, dpr)), middle, middle);
        } else if (pointer == xqt::hover::Pointer::Crosshair) {
            toolCursor = QCursor(Qt::CrossCursor);
        } else {
            const int middle = xqt::hover::dotSide() / 2;
            toolCursor = QCursor(QPixmap::fromImage(xqt::hover::dotImage(dpr)), middle, middle);
        }
        applyCursor();
    }
    // A pen the platform shows no cursor for (Android, iOS): the canvas draws the dot (or the eraser) where the pen is.
    // An eraser too big for a cursor: drawn as well, around the dot or crosshair.
    const bool penWithoutCursor = pointerSource == PointerSource::Pen && !xqt::hover::platformShowsPenCursor();
    markWanted = penWithoutCursor || (eraser && !eraserCursor);
    markEraser = markWanted ? eraser : std::nullopt;
    if (hoverMark) {
        hoverMark->show(markEraser);
    }
    placeHoverMark();
}

void DocumentCanvasItem::applyCursor() {
    const QString key = pointerKind == PointerKind::Link          ? QStringLiteral("hand")
                        : pointerKind == PointerKind::WidthHandle ? QStringLiteral("width")
                                                                  : toolCursorKey;
    if (key == appliedCursorKey) {
        return;
    }
    appliedCursorKey = key;
    const QCursor c = pointerKind == PointerKind::Link          ? QCursor(Qt::PointingHandCursor)
                      : pointerKind == PointerKind::WidthHandle ? QCursor(Qt::SizeHorCursor)
                                                                : toolCursor;
    setCursor(c);
    if (windowCursorBeforePen && window()) {
        window()->setCursor(c);  // (the pen shows it: the window's cursor)
    }
}

void DocumentCanvasItem::pointerMoved(QPointF itemPos, PointerSource source, bool eraserEnd) {
    pointerPos = itemPos;
    if (source != pointerSource || eraserEnd != pointerEraserEnd) {
        pointerSource = source;
        pointerEraserEnd = eraserEnd;
        refreshPointer();
    } else {
        placeHoverMark();
    }
}

void DocumentCanvasItem::pointerGone(PointerSource source) {
    if (source == pointerSource && pointerPos) {
        pointerPos.reset();
        placeHoverMark();
    }
}

void DocumentCanvasItem::placeHoverMark() {
    if (!markWanted || !pointerPos || !isVisible()) {
        if (hoverMark) {
            hoverMark->setVisible(false);
        }
        return;
    }
    if (!hoverMark) {
        hoverMark = new HoverMarkItem(this);
        hoverMark->setZ(1);  // (over nothing else of the canvas's own: it has no other children)
        hoverMark->show(markEraser);
    }
    // Whole device pixels: the picture stays sharp
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const QPointF topLeft = *pointerPos - QPointF(hoverMark->width() / 2, hoverMark->height() / 2);
    hoverMark->setPosition(QPointF(std::round(topLeft.x() * dpr) / dpr, std::round(topLeft.y() * dpr) / dpr));
    hoverMark->setVisible(true);
}

DocumentCanvasItem::HoverMarkShown DocumentCanvasItem::hoverMarkShown() const {
    HoverMarkShown shown;
    if (hoverMark && hoverMark->isVisible()) {
        shown.visible = true;
        shown.side = hoverMark->width();
        shown.center = hoverMark->position() + QPointF(hoverMark->width() / 2, hoverMark->height() / 2);
        shown.eraser = hoverMark->eraser;
        shown.dot = hoverMark->dotRect;
        shown.dotPixels = hoverMark->dotPixels;
    }
    return shown;
}

void DocumentCanvasItem::showCursorForPen() {
    QWindow* w = window();
    if (!w || !xqt::hover::platformShowsPenCursor()) {
        return;
    }
    // Qt Quick sets the window's cursor from the item under the mouse; the pen's events go to the canvas before Qt
    // Quick sees them, so the window may still have the cursor of a control the mouse was last over
    const QCursor want = cursor();
    const QCursor have = w->cursor();
    if (have.shape() == want.shape() &&
        (want.shape() != Qt::BitmapCursor || have.pixmap().cacheKey() == want.pixmap().cacheKey())) {
        return;
    }
    if (!windowCursorBeforePen) {
        windowCursorBeforePen = have;
    }
    w->setCursor(want);
}

void DocumentCanvasItem::giveBackWindowCursor() {
    if (!windowCursorBeforePen) {
        return;
    }
    // (what Qt Quick last set: it sets it again only when the item under the mouse changes)
    if (window()) {
        window()->setCursor(*windowCursorBeforePen);
    }
    windowCursorBeforePen.reset();
}

void DocumentCanvasItem::mouseHovers(QPointF scenePos) {
    hoverScenePos = scenePos;
    mouseOverWindow = true;
    if (!mathErrorText.isEmpty() && !mathErrorArea.contains(mapFromScene(scenePos))) {
        setMathError({}, {});
    }
    hoverTimer.start();
}

void DocumentCanvasItem::setMathError(const QString& error, const QRectF& rect) {
    if (error == mathErrorText && rect == mathErrorArea) {
        return;
    }
    mathErrorText = error;
    mathErrorArea = rect;
    Q_EMIT mathErrorChanged();
}

DocumentCanvasItem::~DocumentCanvasItem() {
    if (canvasView) {
        canvasView->setMousePointerSource({});
    }
    auto& items = allCanvases();
    items.erase(std::remove(items.begin(), items.end(), this), items.end());
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
        canvasView->setMousePointerSource({});
        // (two canvases that swap their views: the other one may show it already)
        if (!shownByAnother(canvasView)) {
            canvasView->setShown(false);
            canvasView->setReadingOnly(false);
            canvasView->setEdgeTapWidth(0);
            canvasView->setSnapVertically(false);
            canvasView->setRotatable(true);
        }
    }
    endLinkHover();
    if (pointerApp) {
        disconnect(pointerApp, nullptr, this, nullptr);
        pointerApp = nullptr;
    }
    input.reset();
    canvasView = v;
    viewReplaced = true;
    if (canvasView) {
        canvasView->setShown(true);
        canvasView->setReadingOnly(reading);
        canvasView->setEdgeTapWidth(edgeWidth);
        canvasView->setSnapVertically(verticalSnap);
        canvasView->setRotatable(turnable);
        // (a paste with the keys asks where the mouse rests: over a sticky note it goes into the note)
        canvasView->setMousePointerSource([this]() -> std::optional<QPointF> {
            if (!mouseOverWindow || !claims(hoverScenePos)) {
                return std::nullopt;
            }
            return toView(mapFromScene(hoverScenePos));
        });
        input = std::make_unique<xqt::CanvasInput>(*canvasView);
        connect(canvasView, &xqt::CanvasView::updateRequested, this, &QQuickItem::update);
        connect(canvasView, &xqt::CanvasView::edgeTapped, this, &DocumentCanvasItem::edgeTapped);
        connect(canvasView, &xqt::CanvasView::middleTapped, this, [this](QPointF viewPos, int count) {
            Q_EMIT middleTapped(canvasView->getViewController().viewToScreen(viewPos), count);
        });
        connect(canvasView, &xqt::CanvasView::writingRefused, this, [this](QPointF viewPos) {
            Q_EMIT writingRefused(canvasView->getViewController().viewToScreen(viewPos));
        });
        connect(canvasView, &xqt::CanvasView::pagesChanged, this, &QQuickItem::update);
        connect(canvasView, &xqt::CanvasView::pagesChanged, this, &DocumentCanvasItem::viewportChanged);
        connect(&canvasView->getViewController(), &xqt::ViewController::changed, this,
                &DocumentCanvasItem::viewportChanged);
        // The pages moved under a resting pointer (the wheel, a jump): the link there now
        connect(&canvasView->getViewController(), &xqt::ViewController::changed, this, [this] {
            if (linkHoverAt && !mouseGrab && !penGrab) {
                linkHovers(*linkHoverAt, QGuiApplication::keyboardModifiers(), linkHoverByMouse);
            }
        });
        connect(canvasView, &QObject::destroyed, this, [this] {
            input.reset();
            viewReplaced = true;
            update();
        });
        // The pointer follows the settings (dot or crosshair)
        pointerApp = &canvasView->getSession().getApp();
        connect(pointerApp, &xqt::AppContext::settingsChanged, this, &DocumentCanvasItem::refreshPointer);
        // ... the tool (the eraser's size and kind; a side button that erases while it is held), and the zoom
        connect(pointerApp, &xqt::AppContext::activeToolChanged, this, &DocumentCanvasItem::refreshPointer);
        connect(pointerApp, &xqt::AppContext::toolPropertiesChanged, this, &DocumentCanvasItem::refreshPointer);
        connect(&canvasView->getViewController(), &xqt::ViewController::changed, this,
                &DocumentCanvasItem::refreshPointer);
        connect(canvasView, &xqt::CanvasView::emojiCompletionChanged, this, &DocumentCanvasItem::emojiCompletionChanged);
        // (the hint that a note's text goes on below the note: with the cursor, and where the note is shown)
        connect(canvasView, &xqt::CanvasView::markdownCursorChanged, this, &DocumentCanvasItem::noteTextHintChanged);
        connect(canvasView, &xqt::CanvasView::textEditingChanged, this, &DocumentCanvasItem::noteTextHintChanged);
        connect(&canvasView->getViewController(), &xqt::ViewController::changed, this,
                &DocumentCanvasItem::noteTextHintChanged);
        connect(canvasView, &xqt::CanvasView::textEditingChanged, this, [this](bool editing) {
            Q_EMIT textEditingChanged();
            setFlag(ItemAcceptsInputMethod, editing);
            if (editing) {
                forceActiveFocus(Qt::OtherFocusReason);
                QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
                // Android: the canvas took the focus (at the press) before it took text, and the keyboard was bound
                // to no input then; reset() makes it bind again, now to the text. Without it the keyboard shows,
                // but what is typed goes nowhere.
                QGuiApplication::inputMethod()->reset();
                QGuiApplication::inputMethod()->show();  // tablets and phones: the on-screen keyboard
            } else {
                QGuiApplication::inputMethod()->hide();
                QGuiApplication::inputMethod()->update(Qt::ImEnabled);
            }
        });
        updateViewGeometry();
    }
    refreshPointer();
    Q_EMIT viewChanged();
    Q_EMIT viewportChanged();
    update();
}

void DocumentCanvasItem::setReadingOnly(bool on) {
    if (on == reading) {
        return;
    }
    reading = on;
    if (canvasView) {
        canvasView->setReadingOnly(on);
    }
    refreshPointer();  // (no eraser where nothing is erased)
    Q_EMIT readingOnlyChanged();
}

void DocumentCanvasItem::setEdgeTapWidth(qreal px) {
    if (qFuzzyCompare(px + 1, edgeWidth + 1)) {
        return;
    }
    edgeWidth = px;
    if (canvasView) {
        canvasView->setEdgeTapWidth(px);
    }
    Q_EMIT edgeTapWidthChanged();
}

void DocumentCanvasItem::setDarkPages(bool on) {
    if (on == darkShown) {
        return;
    }
    darkShown = on;
    update();  // (no page is drawn again: the tiles are turned dark where they are composed)
    Q_EMIT darkPagesChanged();
}

void DocumentCanvasItem::setRotatable(bool on) {
    if (on == turnable) {
        return;
    }
    turnable = on;
    if (canvasView) {
        canvasView->setRotatable(on);
    }
    Q_EMIT rotatableChanged();
}

void DocumentCanvasItem::setSnapVertically(bool on) {
    if (on == verticalSnap) {
        return;
    }
    verticalSnap = on;
    if (canvasView) {
        canvasView->setSnapVertically(on);
    }
    Q_EMIT snapVerticallyChanged();
}

bool DocumentCanvasItem::shownByAnother(const xqt::CanvasView* v) const {
    return std::any_of(allCanvases().begin(), allCanvases().end(),
                       [&](const DocumentCanvasItem* c) { return c != this && c->canvasView == v; });
}

bool DocumentCanvasItem::heldByAnother(bool DocumentCanvasItem::*grab) const {
    return std::any_of(allCanvases().begin(), allCanvases().end(), [&](const DocumentCanvasItem* c) {
        return c != this && c->filteredWindow == filteredWindow && c->*grab;
    });
}

namespace {
/// The axis of the upright view (0: x, 1: y) a screen axis (0: across, 1: down) runs along when the canvas is turned
/// by a multiple of 90°, and whether the same way (none: a free angle, there are no scroll bars then)
std::optional<std::pair<int, bool>> viewAxis(const xqt::ViewController& vc, int screenAxis) {
    if (!vc.rightAngled()) {
        return std::nullopt;
    }
    const QPointF along = vc.screenDeltaToView(screenAxis == 0 ? QPointF(1, 0) : QPointF(0, 1));
    return std::abs(along.x()) > 0.5 ? std::pair{0, along.x() > 0} : std::pair{1, along.y() > 0};
}
double component(QPointF p, int axis) { return axis == 0 ? p.x() : p.y(); }
double component(QSizeF s, int axis) { return axis == 0 ? s.width() : s.height(); }
}  // namespace

// The scroll bars are the screen's: turned by 90° or 270° the one at the right scrolls the view sideways (and the way
// the pages move on the screen); at a free angle there are none (no content size).
qreal DocumentCanvasItem::contentWidth() const { return screenContent(0).first; }

qreal DocumentCanvasItem::contentHeight() const { return screenContent(1).first; }

qreal DocumentCanvasItem::contentX() const { return screenContent(0).second; }

qreal DocumentCanvasItem::contentY() const { return screenContent(1).second; }

std::pair<qreal, qreal> DocumentCanvasItem::screenContent(int screenAxis) const {
    if (!canvasView) {
        return {0, 0};
    }
    const xqt::ViewController& vc = canvasView->getViewController();
    const auto axis = viewAxis(vc, screenAxis);
    if (!axis) {
        return {0, 0};
    }
    const auto [a, sameWay] = *axis;
    const double content = component(canvasView->documentLayout().contentSize(vc.zoom()), a);
    const double pos = component(vc.scrollPosition(), a);
    return {content, sameWay ? pos : std::max(0.0, content - pos - component(vc.viewSize(), a))};
}

void DocumentCanvasItem::scrollTo(qreal x, qreal y) {
    if (!canvasView) {
        return;
    }
    xqt::ViewController& vc = canvasView->getViewController();
    QPointF target = vc.scrollPosition();
    for (int screenAxis: {0, 1}) {
        const auto axis = viewAxis(vc, screenAxis);
        if (!axis) {
            return;
        }
        const auto [a, sameWay] = *axis;
        const double value = screenAxis == 0 ? x : y;
        const double content = component(canvasView->documentLayout().contentSize(vc.zoom()), a);
        const double pos = sameWay ? value : content - value - component(vc.viewSize(), a);
        (a == 0 ? target.rx() : target.ry()) = pos;
    }
    vc.setScrollPosition(target);
}

void DocumentCanvasItem::updateViewGeometry() {
    if (!canvasView || width() <= 0 || height() <= 0) {
        return;
    }
    if (window()) {
        canvasView->setDevicePixelRatio(window()->effectiveDevicePixelRatio());
        // (before the size: a view shown for the first time fits its page at the zoom of this screen)
        canvasView->setDisplay(xqt::ScreenCalibration::displayOf(window()->screen(),
                                                                 window()->effectiveDevicePixelRatio()));
    }
    canvasView->getViewController().setViewSize(size());
}

QPointF DocumentCanvasItem::toView(QPointF itemPos) const {
    return canvasView ? canvasView->getViewController().screenToView(itemPos) : itemPos;
}

QRectF DocumentCanvasItem::toItem(const QRectF& viewRect) const {
    return canvasView ? canvasView->getViewController().viewToScreen(viewRect) : viewRect;
}

void DocumentCanvasItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        updateViewGeometry();
        if (newGeometry.height() < oldGeometry.height()) {
            // Shorter while text is written (the soft keyboard came up below it): the cursor stays in view
            showTextCursor();
        }
    }
}

bool DocumentCanvasItem::showTextCursor() {
    return canvasView && canvasView->getTextInput() && canvasView->scrollToTextCursor();
}

void DocumentCanvasItem::itemChange(ItemChange change, const ItemChangeData& value) {
    if (change == ItemSceneChange) {
        if (filteredWindow) {
            filteredWindow->removeEventFilter(this);
        }
        disconnect(screenConnection);
        filteredWindow = value.window;
        if (filteredWindow) {
            // Runs before QQuickWindow::event() and thus before Qt Quick's delivery agent.
            filteredWindow->installEventFilter(this);
            screenConnection = connect(filteredWindow, &QWindow::screenChanged, this,
                                       &DocumentCanvasItem::updateViewGeometry);
            updateViewGeometry();
        }
        windowCursorBeforePen.reset();
        refreshPointer();  // (the cursor's pixels: this window's pixel ratio)
    } else if (change == ItemDevicePixelRatioHasChanged) {
        updateViewGeometry();
        refreshPointer();
    } else if (change == ItemVisibleHasChanged) {
        placeHoverMark();
    }
    QQuickItem::itemChange(change, value);
}

namespace {
/// Deepest visible item at a scene position, following the stacking order (topmost first), like Qt Quick's
/// delivery. Popups, dialogs and their modal dimmer live in the window's overlay, which is above the content.
/// Items that only draw something over the canvas (the knobs of a PDF text selection and the like) say so with
/// `inputTransparent: true`, and are looked through - as Qt does with them when it delivers a press.
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
            if (top == *it && ((*it)->inherits("QQuickOverlay") || (*it)->property("inputTransparent").toBool())) {
                continue;
            }
            return top;
        }
    }
    return item;
}

/// Is a menu open in this window - one that a press somewhere else closes? (Tool tips stay out of the way, they
/// do not take presses.) Popups live in the window's overlay; a popup item belongs to its popup.
bool menuIsOpen(QQuickWindow* window) {
    constexpr int CLOSES_ON_PRESS_OUTSIDE = 0x01 | 0x02;  // QQuickPopup::CloseOnPressOutside(Parent)
    if (!window) {
        return false;
    }
    for (QQuickItem* child: window->contentItem()->childItems()) {
        if (!child->inherits("QQuickOverlay")) {
            continue;
        }
        for (QQuickItem* item: child->childItems()) {
            QObject* popup = item->isVisible() ? item->parent() : nullptr;
            if (popup && popup->inherits("QQuickPopup") && !popup->inherits("QQuickToolTip") &&
                (popup->property("closePolicy").toInt() & CLOSES_ON_PRESS_OUTSIDE)) {
                return true;
            }
        }
    }
    return false;
}
}  // namespace

bool DocumentCanvasItem::claims(QPointF scenePos) const {
    const xqt::PerfScope measure(xqt::Perf::HitTest);
    if (!isVisible() || !isEnabled() || !window() ||
        !QRectF(0, 0, width(), height()).contains(mapFromScene(scenePos))) {
        return false;
    }
    if (menuIsOpen(window())) {
        return false;  // a menu is open: this press closes it (and draws nothing)
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
    // A tap on the text being written brings the on-screen keyboard back (Android shows it only when asked)
    if (textEditing() && !QGuiApplication::inputMethod()->isVisible()) {
        QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
        QGuiApplication::inputMethod()->show();
    }
}

bool DocumentCanvasItem::eventFilter(QObject* watched, QEvent* e) {
    if (!input) {
        return false;
    }
    if (watched == qApp) {
        if (e->type() == QEvent::TabletEnterProximity || e->type() == QEvent::TabletLeaveProximity) {
            xqt::inputlog::event(e);
            input->proximityEvent(e->type() == QEvent::TabletEnterProximity);
            if (e->type() == QEvent::TabletLeaveProximity && linkHoverAt && !linkHoverByMouse) {
                endLinkHover();  // (the pen went away)
            }
            if (e->type() == QEvent::TabletLeaveProximity) {
                pointerGone(PointerSource::Pen);
                giveBackWindowCursor();
            }
        }
        return false;
    }
    if (watched != filteredWindow) {
        return false;
    }
    if (xqt::inputlog::enabled()) {
        xqt::inputlog::event(e);
    }
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto* t = static_cast<QTabletEvent*>(e);
            xqt::Perf::add(xqt::Perf::PenEvents);
            if (!penGrab && (heldByAnother(&DocumentCanvasItem::penGrab) || !claims(t->position()))) {
                if (linkHoverAt && !linkHoverByMouse) {
                    endLinkHover();  // (the pen hovers a control now)
                }
                pointerGone(PointerSource::Pen);
                giveBackWindowCursor();
                xqt::inputlog::decision(e, false, "not on this canvas (a control, a menu, another canvas)");
                return false;  // unaccepted: Qt synthesizes mouse events for the QML controls
            }
            if (xqt::inputlog::enabled()) {
                const bool eraser = t->pointerType() == QPointingDevice::PointerType::Eraser;
                const bool pressure = canvasView && canvasView->getSession().getSettings()->isPressureSensitivity();
                xqt::inputlog::decision(e, true,
                                        eraser     ? "eraser"
                                        : pressure ? "pen, its pressure sets the width"
                                                   : "pen, pressure ignored (\"Pressure changes the line width\" is off)");
            }
            if (e->type() == QEvent::TabletPress && t->button() == Qt::LeftButton) {
                penGrab = true;
                takeKeyboardFocus();
            } else if (e->type() == QEvent::TabletRelease && t->button() == Qt::LeftButton) {
                penGrab = false;
            }
            input->tabletEvent(t, toView(mapFromScene(t->position())));
            pointerMoved(mapFromScene(t->position()), PointerSource::Pen,
                         t->pointerType() == QPointingDevice::PointerType::Eraser);
            showCursorForPen();
            // The pen hovering: where a link under it leads (the pen itself keeps its tool: no pointing hand)
            if (e->type() == QEvent::TabletMove && t->buttons() == Qt::NoButton && !penGrab) {
                linkHovers(mapFromScene(t->position()), t->modifiers(), false);
            } else if (e->type() == QEvent::TabletPress) {
                endLinkHover();
            }
            t->accept();
            return true;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto* t = static_cast<QTouchEvent*>(e);
            xqt::Perf::add(xqt::Perf::TouchEvents);
            if (e->type() == QEvent::TouchBegin) {
                touchSessionOwned = !t->points().isEmpty() && !heldByAnother(&DocumentCanvasItem::touchSessionOwned) &&
                                    claims(t->points().first().scenePosition());
                if (touchSessionOwned) {
                    takeKeyboardFocus();
                }
            }
            if (!touchSessionOwned) {
                xqt::inputlog::decision(e, false, "not on this canvas");
                return false;
            }
            xqt::inputlog::decision(e, true, "touch");
            input->touchEvent(t, [this](QPointF scenePos) { return toView(mapFromScene(scenePos)); });
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
            xqt::Perf::add(xqt::Perf::MouseEvents);
            const auto mouseType = m->device() ? m->device()->type() : QInputDevice::DeviceType::Mouse;
            const bool realMouse =
                    mouseType == QInputDevice::DeviceType::Mouse || mouseType == QInputDevice::DeviceType::TouchPad;
            if (realMouse) {
                giveBackWindowCursor();  // (Qt Quick sets the window's cursor for the mouse again)
                const QPointF itemPos = mapFromScene(m->scenePosition());
                if (QRectF(0, 0, width(), height()).contains(itemPos)) {
                    pointerMoved(itemPos, PointerSource::Mouse);
                } else {
                    pointerGone(PointerSource::Mouse);
                }
            }
            // Without the hit test of the item under the pointer: moving without a button never draws, and a drag
            // that began elsewhere (a scroll bar) stays there - the mouse sends more moves than there are frames.
            if (!mouseGrab && (m->buttons() == Qt::NoButton ? e->type() == QEvent::MouseMove : mouseElsewhere)) {
                if (e->type() == QEvent::MouseMove && m->buttons() == Qt::NoButton) {
                    mouseHovers(m->scenePosition());
                    const auto type = m->device() ? m->device()->type() : QInputDevice::DeviceType::Mouse;
                    if (type == QInputDevice::DeviceType::Mouse || type == QInputDevice::DeviceType::TouchPad) {
                        linkHovers(mapFromScene(m->scenePosition()), m->modifiers(), true);
                    }
                }
                return false;
            }
            setMathError({}, {});
            if (e->type() == QEvent::MouseButtonPress) {
                // (a click may follow it: the sheet or the page comes; a Markdown box's width handle keeps its
                // cursor while it is dragged)
                endLinkHover(pointerKind == PointerKind::WidthHandle);
            }
            const bool inside = !heldByAnother(&DocumentCanvasItem::mouseGrab) && claims(m->scenePosition());
            xqt::Perf::add(xqt::Perf::MouseClaimed, inside ? 1 : 0);
            if (!mouseGrab && !inside) {
                mouseElsewhere = m->buttons() != Qt::NoButton;
                xqt::inputlog::decision(e, false, "not on this canvas");
                return false;
            }
            if (xqt::inputlog::enabled()) {
                const auto type = m->device() ? m->device()->type() : QInputDevice::DeviceType::Mouse;
                xqt::inputlog::decision(e, true,
                                        type == QInputDevice::DeviceType::Mouse ||
                                                        type == QInputDevice::DeviceType::TouchPad
                                                ? "mouse, no pressure"
                                                : "mouse event made from a pen or a finger: ignored here");
            }
            if (e->type() == QEvent::MouseButtonPress) {
                mouseGrab = true;
                takeKeyboardFocus();
            } else if (e->type() == QEvent::MouseButtonRelease && m->buttons() == Qt::NoButton) {
                mouseGrab = false;
                mouseElsewhere = false;
            }
            input->mouseEvent(m, toView(mapFromScene(m->scenePosition())));
            m->accept();
            return true;
        }
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            // Ctrl pressed or let go over a link in text being written: Ctrl + click follows it (the cursor says so)
            if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Control && linkHoverAt && linkHoverByMouse &&
                !mouseGrab) {
                linkHovers(*linkHoverAt, static_cast<QKeyEvent*>(e)->modifiers(), true);
            }
            return false;
        case QEvent::Leave:
            mouseOverWindow = false;
            pointerGone(PointerSource::Mouse);
            if (linkHoverAt && linkHoverByMouse) {
                endLinkHover();  // the mouse left the window
            }
            return false;
        case QEvent::Wheel: {
            auto* w = static_cast<QWheelEvent*>(e);
            if (!claims(w->scenePosition())) {
                return false;
            }
            input->wheelEvent(w, toView(mapFromScene(w->scenePosition())));
            w->accept();
            return true;
        }
        case QEvent::NativeGesture: {
            auto* g = static_cast<QNativeGestureEvent*>(e);
            if (!claims(g->scenePosition())) {
                return false;
            }
            input->nativeGestureEvent(g, toView(mapFromScene(g->scenePosition())));
            g->accept();
            return true;
        }
        default:
            return false;
    }
}

void DocumentCanvasItem::releaseResources() { viewReplaced = true; }

namespace {
bool isClipboardKey(QKeyEvent* e) {
    return e->matches(QKeySequence::Copy) || e->matches(QKeySequence::Cut) || e->matches(QKeySequence::Paste);
}
}  // namespace

bool DocumentCanvasItem::event(QEvent* e) {
    // While editing text, typing keys belong to the editor, not to the window's shortcuts (Ctrl+C, Delete, ...).
    // A selected sticky note takes Ctrl+C/X/V (the window's shortcuts: the whole note), not a text being written.
    if (e->type() == QEvent::ShortcutOverride && canvasView && canvasView->getTextInput() &&
        canvasView->getTextInput()->wantsKeyEvent(static_cast<QKeyEvent*>(e)) &&
        !(canvasView->notes().hasSelection() && isClipboardKey(static_cast<QKeyEvent*>(e)))) {
        e->accept();
        return true;
    }
    return QQuickItem::event(e);
}

// The text the on-screen keyboard (or an input method) sends, and what it asks: QT_LOGGING_RULES="xqt.input.text=true"
// (on Android through the launch intent, qt/docs/android.md)
Q_LOGGING_CATEGORY(lcInputText, "xqt.input.text", QtWarningMsg)

void DocumentCanvasItem::keyPressEvent(QKeyEvent* e) {
    qCDebug(lcInputText) << "keyPressEvent" << e->key() << e->text();
    xqt::CanvasTextInput* editor = canvasView ? canvasView->getTextInput() : nullptr;
    if (!editor && canvasView && !e->text().isEmpty() && e->text().at(0).isPrint() &&
        !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        (canvasView->textMode() || canvasView->typesIntoFlow()) && canvasView->ensureTextEditor()) {
        // A text file, a text document of notes (qt/docs/md-pdf.md): typing starts writing (at the top of the page in
        // view)
        editor = canvasView->getTextInput();
    }
    bool finish = false;
    if (editor && canvasView->textKeyPressed(e, finish)) {  // (the emoji suggestions, then the editor)
        if (!finish) {
            showTextCursor();  // (the Markdown editor follows its cursor itself, an ordinary text does not)
            QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle | Qt::ImSurroundingText |
                                                   Qt::ImCursorPosition | Qt::ImAnchorPosition);
        }
        e->accept();
        return;
    }
    QQuickItem::keyPressEvent(e);
}

void DocumentCanvasItem::inputMethodEvent(QInputMethodEvent* e) {
    qCDebug(lcInputText) << "inputMethodEvent commit" << e->commitString() << "preedit" << e->preeditString()
                         << "replace" << e->replacementStart() << e->replacementLength() << "editing"
                         << (canvasView && canvasView->getTextInput() != nullptr);
    if (xqt::CanvasTextInput* editor = canvasView ? canvasView->getTextInput() : nullptr) {
        editor->inputMethodEvent(e);
        canvasView->refreshEmojiCompletion();
        showTextCursor();  // (the soft keyboard's text: the cursor stays in view above it)
        e->accept();
        return;
    }
    QQuickItem::inputMethodEvent(e);
}

bool DocumentCanvasItem::textEditing() const { return canvasView && canvasView->getTextInput(); }

QVariantList DocumentCanvasItem::emojiCompletions() const {
    QVariantList out;
    if (canvasView) {
        for (const auto& c: canvasView->emojiCompletion().suggestions()) {
            out.append(QVariantMap{{QStringLiteral("emoji"), QString::fromUtf8(c.emoji->emoji)},
                                   {QStringLiteral("name"), QString::fromUtf8(c.name.data(),
                                                                              static_cast<qsizetype>(c.name.size()))}});
        }
    }
    return out;
}

int DocumentCanvasItem::emojiCompletionIndex() const {
    return canvasView ? canvasView->emojiCompletion().selected() : 0;
}

QRectF DocumentCanvasItem::emojiCompletionRect() const {
    return canvasView && canvasView->emojiCompletion().active()
                   ? inputMethodQuery(Qt::ImCursorRectangle).toRectF()
                   : QRectF();
}

QRectF DocumentCanvasItem::noteTextHint() const { return canvasView ? toItem(canvasView->noteTextHintBox()) : QRectF(); }

void DocumentCanvasItem::chooseEmojiCompletion(int index) {
    if (canvasView) {
        QGuiApplication::inputMethod()->reset();  // (the keyboard's word being typed goes with the shortcode)
        canvasView->chooseEmojiCompletion(index);
        QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
    }
}

bool DocumentCanvasItem::insertText(const QString& text) {
    if (!canvasView || !canvasView->getTextInput()) {
        return false;
    }
    QGuiApplication::inputMethod()->commit();
    canvasView->insertAtTextCursor(text.toStdString());
    QGuiApplication::inputMethod()->update(Qt::ImQueryAll);
    forceActiveFocus(Qt::OtherFocusReason);
    return true;
}

QVariant DocumentCanvasItem::inputMethodQuery(Qt::InputMethodQuery query) const {
    xqt::CanvasTextInput* editor = canvasView ? canvasView->getTextInput() : nullptr;
    if (lcInputText().isDebugEnabled() && query != Qt::ImCursorRectangle && query != Qt::ImAnchorRectangle) {
        qCDebug(lcInputText) << "inputMethodQuery" << query << "editing" << (editor != nullptr);
    }
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
        return toItem(QRectF(origin + r.topLeft() * zoom, r.size() * zoom));  // (on the screen: the canvas may be turned)
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
        selectionStats = {};
        return;
    }
    // Where the page's tiles are: on a whole device pixel
    const QRectF pageRect = canvasView->pageViewRect(*idx);
    const QPointF pageOrigin(snap(pageRect.x(), dpr), snap(pageRect.y(), dpr));
    // Dark pages: the selection's picture as its page is shown (on the CPU: it is drawn only when it changes)
    xqt::CanvasPage* selPage = canvasView->getPage(*idx);
    const xqt::CanvasPage::DarkTone tone = darkShown && selPage ? selPage->darkTone() : xqt::CanvasPage::DarkTone{};
    if (!root->selection || root->selectionRevision != canvasView->selectionRevision() || root->selectionZoom != zoom ||
        root->selectionDpr != dpr || root->selectionDark != tone.dark) {
        // Upstream's XournalWidget draws the selection in its page's pixel coordinates: selection->paint(cr, zoom).
        // Render the part around it (handles, rotation) into a texture, of whole device pixels from the page's top
        // left (so it lands on the screen's pixels as the tiles do, also at 125 % or 150 %).
        const double cx = (sel->getXOnView() + sel->getWidth() / 2) * zoom;
        const double cy = (sel->getYOnView() + sel->getHeight() / 2) * zoom;
        const double r = std::hypot(sel->getWidth(), sel->getHeight()) / 2 * zoom + 60;
        const int left = static_cast<int>(std::floor((cx - r) * dpr));
        const int top = static_cast<int>(std::floor((cy - r) * dpr));
        const QRect pixels(left, top, std::max(1, static_cast<int>(std::ceil((cx + r) * dpr)) - left),
                           std::max(1, static_cast<int>(std::ceil((cy + r) * dpr)) - top));
        QImage img(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
                img.bits(), CAIRO_FORMAT_ARGB32, img.width(), img.height(), static_cast<int>(img.bytesPerLine()));
        cairo_t* cr = cairo_create(surface);
        cairo_translate(cr, -pixels.x(), -pixels.y());
        cairo_scale(cr, dpr, dpr);
        sel->paint(cr, zoom);
        canvasView->boxResize().paintOverSelection(cr, zoom);  // (a Markdown text box: its right knob sets the width)
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
        if (tone.dark) {
            xqt::dark::apply(img, tone.paper);
        }
        root->selectionDark = tone.dark;
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
        root->selectionDpr = dpr;
        root->selectionRegion = QRectF(pixels.x() / dpr, pixels.y() / dpr, pixels.width() / dpr, pixels.height() / dpr);
    }
    root->selection->setRect(root->selectionRegion.translated(pageOrigin));
    selectionStats = {true, root->selection->rect(), root->selection->texture()->textureSize(), root->selectionDpr};
}

void DocumentCanvasItem::updateGeometryNode(QSGNode* rootNode, double zoom, double dpr) {
    GeometryNode* g = static_cast<CanvasRootNode*>(rootNode)->geometry;
    const xqt::GeometryToolLayer& layer = canvasView->geometryTool();
    xqt::GeometryToolPicture* picture = layer.picture();
    xqt::CanvasPage* page = layer.visible() ? layer.page() : nullptr;
    const auto index = page ? canvasView->indexOf(page) : std::nullopt;
    const auto [first, last] = canvasView->visiblePages();
    if (!picture || !index || *index < first || *index > last) {
        g->clear();  // (its textures go; it is drawn anew when it comes into view)
        geometryStats.shown = false;
        return;
    }
    if (g->of != picture->serial()) {  // another tool
        g->clear();
        g->of = picture->serial();
    }
    const GeometryToolType type = picture->type();
    const double toolHeight = layer.height();
    const double rotation = layer.rotation();
    const QPointF middle = layer.middle();
    const QRectF r = canvasView->pageViewRect(*index);
    const QPointF pageAt(snap(r.x(), dpr), snap(r.y(), dpr));
    const double wanted = zoom * dpr;

    // Nothing changed for a moment (neither the tool, nor the zoom, nor the scroll position): a new size or zoom is
    // drawn anew. Until then the pictures are scaled on the GPU.
    const std::array<double, 10> key{toolHeight, rotation, middle.x(), middle.y(), zoom,
                                     dpr,        pageAt.x(), pageAt.y(), canvasView->getViewController().viewSize().width(),
                                     canvasView->getViewController().viewSize().height()};
    if (key != lastGeometryKey) {
        lastGeometryKey = key;
        geometrySettled = false;
        QMetaObject::invokeMethod(this, [this] { geometryTimer.start(); }, Qt::QueuedConnection);
    }
    const bool settled = geometrySettled;

    // The whole tool, in its own coordinates
    const QRectF bounds = xqt::GeometryToolPicture::bounds(type, toolHeight);
    const double baseScale = std::min(wanted, MAX_PICTURE / std::max(bounds.width(), bounds.height()));
    if (!g->base || (settled && (g->baseHeight != toolHeight || g->baseScale != baseScale))) {
        g->drop(g->body, g->patch);
        g->show(window(), g->body, g->base, picture->body(toolHeight, baseScale), statPixels);
        g->baseHeight = toolHeight;
        g->baseScale = baseScale;
        ++geometryStats.bodies;
    }
    QMatrix4x4 m;
    m.translate(static_cast<float>(pageAt.x()), static_cast<float>(pageAt.y()));
    m.scale(static_cast<float>(zoom));
    m.translate(static_cast<float>(middle.x()), static_cast<float>(middle.y()));
    m.rotate(static_cast<float>(rotation * 180 / M_PI), 0, 0, 1);
    m.scale(static_cast<float>(toolHeight / g->baseHeight));  // (a new size: drawn anew once it is stable)
    g->body->setMatrix(m);
    g->clip->setRect(QRectF(pageAt, r.size()));

    // A big tool at a high zoom is drawn smaller than it is shown: once it rests, the part of it in view is drawn sharp
    // over that. It is in the tool's own coordinates, so it stays right while the tool moves on (only a part that comes
    // into view is not sharp until it rests again).
    if (baseScale < wanted) {
        const QRectF inView = QRectF(QPointF(0, 0), canvasView->getViewController().viewSize())
                                      .intersected(QRectF(pageAt, r.size()));
        if (settled && !inView.isEmpty()) {
            const double most = MAX_PICTURE / wanted;  // points a side
            auto limited = [&](QRectF part) {
                part = part.intersected(bounds);
                const QPointF c = part.center();
                part.setWidth(std::min(part.width(), most));
                part.setHeight(std::min(part.height(), most));
                part.moveCenter(c);
                return part;
            };
            const QRectF needed = limited(m.inverted().mapRect(inView));
            if (!needed.isEmpty() && (!g->patch || g->patchScale != wanted || !g->patchRect.contains(needed))) {
                const double margin = 128 / wanted;
                const QRectF part = limited(needed.adjusted(-margin, -margin, margin, margin));
                auto image = picture->body(toolHeight, wanted, part);
                g->patchRect = image.rect;
                g->patchScale = wanted;
                g->show(window(), g->body, g->patch, std::move(image), statPixels);
                ++geometryStats.bodies;
            }
        }
    } else {
        g->drop(g->body, g->patch);
    }

    // The angle display: upright, drawn anew when its number changes
    const double displayScale = settled || !g->display ? wanted : g->displayScale;
    std::string text = xqt::GeometryToolPicture::displayText(rotation);
    if (!g->display || text != g->displayText || displayScale != g->displayScale) {
        g->show(window(), g->displayAt, g->display, picture->display(toolHeight, rotation, displayScale), statPixels);
        g->displayText = std::move(text);
        g->displayScale = displayScale;
        ++geometryStats.displays;
    }
    const QPointF c = xqt::GeometryToolPicture::displayCentre(type, toolHeight);
    const QPointF displayMiddle = middle + QPointF(c.x() * std::cos(rotation) - c.y() * std::sin(rotation),
                                                   c.x() * std::sin(rotation) + c.y() * std::cos(rotation));
    // Its middle on a whole device pixel: its picture is drawn on whole pixels around it, so the number is sharp
    const QPointF displayAt = pageAt + displayMiddle * zoom;
    QMatrix4x4 d;
    d.translate(static_cast<float>(snap(displayAt.x(), dpr)), static_cast<float>(snap(displayAt.y(), dpr)));
    if (const double turned = canvasView->getViewController().rotation(); turned != 0) {
        d.rotate(static_cast<float>(-turned), 0, 0, 1);  // (the canvas turned: the number upright on the screen)
    }
    d.scale(static_cast<float>(zoom));
    g->displayAt->setMatrix(d);

    geometryStats.shown = true;
    geometryStats.body = m;
    geometryStats.display = d;
    geometryStats.scale = g->baseScale;
    geometryStats.sharpPart = g->patch != nullptr;
}

void DocumentCanvasItem::updateCurtainNode(QSGNode* rootNode, double zoom, double dpr) {
    CurtainNode* c = static_cast<CanvasRootNode*>(rootNode)->curtain;
    const xqt::CurtainLayer& curtain = canvasView->curtain();
    if (!curtain.visible()) {
        c->clear();
        curtainStats = {};
        return;
    }
    xqt::CanvasPage* page = curtain.page();
    const auto index = canvasView->indexOf(page);
    if (!index) {
        c->clear();
        curtainStats = {};
        return;
    }
    const QRectF r = canvasView->pageViewRect(*index);
    const QPointF pageAt(snap(r.x(), dpr), snap(r.y(), dpr));
    const QPointF middle = curtain.centre();
    QMatrix4x4 m;
    m.translate(static_cast<float>(pageAt.x()), static_cast<float>(pageAt.y()));
    m.scale(static_cast<float>(zoom));
    m.translate(static_cast<float>(middle.x()), static_cast<float>(middle.y()));
    m.rotate(static_cast<float>(curtain.rotation() * 180 / M_PI), 0, 0, 1);
    c->body->setMatrix(m);
    // (the view: the canvas item clips what of it is off the screen, also while the canvas is turned)
    const QSizeF view = canvasView->getViewController().viewSize();
    c->clip->setRect(QRectF(QPointF(0, 0), view));
    const QSizeF size = curtain.size();
    const bool spotlight = curtain.shape() == xqt::CurtainLayer::Shape::Spotlight;
    if (spotlight) {
        // Black as far as the farthest corner of the canvas (whichever way it is turned)
        const QPointF centre = m.map(QPointF(0, 0));
        double farthest = 0;
        for (const QPointF corner: {QPointF(0, 0), QPointF(view.width(), 0), QPointF(0, view.height()),
                                    QPointF(view.width(), view.height())}) {
            farthest = std::max(farthest, std::hypot(corner.x() - centre.x(), corner.y() - centre.y()));
        }
        const double reach = farthest / std::max(zoom, 1e-6) + std::max(size.width(), size.height()) + 10;
        c->sheet->setRect(QRectF());
        c->showHole(window(), size.width(), size.height(), curtain.cornerRadius(), reach);
    } else {
        c->hideHole();
        c->sheet->setRect(QRectF(QPointF(-size.width() / 2, -size.height() / 2), size));
    }

    // The handles: squares at the corners and edges, the knob above (item pixels, not turned)
    const auto handles = curtain.handles();
    int square = 0;
    bool knob = false;
    for (const auto& [handle, at]: handles) {
        if (handle == xqt::CurtainLayer::Handle::Rotate) {
            c->showKnob(window(), at, dpr);
            knob = true;
        } else if (square < CurtainNode::HANDLES) {
            // Whole device pixels (at 125 % or 150 % a frame of 2 logical pixels was 2 or 3 pixels thick by side)
            const double h = std::round(CurtainNode::HANDLE * dpr) / dpr;
            const double frame = std::max(1.0, std::round(2 * dpr)) / dpr;
            const QPointF corner = snapPoint(at - QPointF(h / 2, h / 2), dpr);
            c->frames[square]->setRect(QRectF(corner, QSizeF(h, h)));
            c->fills[square]->setRect(QRectF(corner + QPointF(frame, frame), QSizeF(h - 2 * frame, h - 2 * frame)));
            ++square;
        }
    }
    for (int i = square; i < CurtainNode::HANDLES; ++i) {
        c->frames[i]->setRect(QRectF());
        c->fills[i]->setRect(QRectF());
    }
    if (!knob && c->knob && c->knob->parent()) {
        c->clip->removeChildNode(c->knob);
    }
    curtainStats.shown = true;
    curtainStats.body = m;
    curtainStats.sheet = QRectF(QPointF(-size.width() / 2, -size.height() / 2), size);
    curtainStats.spotlight = spotlight;
    curtainStats.handles = static_cast<int>(handles.size());
    curtainStats.handleFrames.clear();
    curtainStats.handleFills.clear();
    for (int i = 0; i < square; ++i) {
        curtainStats.handleFrames.push_back(c->frames[i]->rect());
        curtainStats.handleFills.push_back(c->fills[i]->rect());
    }
    curtainStats.knob = knob ? c->knob->rect() : QRectF();
    curtainStats.knobPixels = knob ? c->knob->texture()->textureSize() : QSize();
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
    const auto* places = search.placesOn(pageIndex);  // (asked for if not known: drawn when they are)
    if (!places) {
        return;
    }
    const int current = search.currentPage() == pageIndex ? search.currentOnPage() : -1;
    for (size_t i = 0; i < places->size(); ++i) {
        // (handwriting the recogniser was unsure of: lighter)
        const bool faint = (*places)[i].faint;
        const QColor color = static_cast<int>(i) == current ? QColor(255, 120, 0, faint ? 100 : 150)
                                                             : QColor(255, 210, 0, faint ? 60 : 110);
        for (const QRectF& rect: {(*places)[i].rect, (*places)[i].more}) {
            if (rect.isNull()) {
                continue;
            }
            const QRectF r(rect.x() * scale, rect.y() * scale, rect.width() * scale, rect.height() * scale);
            node->searchRoot->appendChildNode(new QSGSimpleRectNode(r.adjusted(-1, -1, 1, 1), color));
        }
    }
}

QSGNode* DocumentCanvasItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    xqt::Perf::add(xqt::Perf::Frames);
    const xqt::PerfScope measure(xqt::Perf::SyncTime);
    QElapsedTimer syncClock;
    syncClock.start();
    struct Count {  // (on every way out)
        DocumentCanvasItem* item;
        QElapsedTimer& clock;
        ~Count() {
            ++item->statFrames;
            item->statSyncNanos += clock.nsecsElapsed();
        }
    } count{this, syncClock};
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
        root->geometry->clear();
        root->curtain->clear();
        viewReplaced = false;
    }
    if (!canvasView) {
        root->curtain->clear();
        return root;
    }

    const xqt::ViewController& vc = canvasView->getViewController();
    const double zoom = vc.zoom();
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const auto [first, last] = canvasView->visiblePages();
    // The canvas turned (qt/docs/canvas-rotation.md): everything below is drawn in the upright view, which this turns
    // onto the screen. The pages' pictures stay upright; the GPU turns their tiles.
    {
        QMatrix4x4 turn;  // (the identity while upright)
        if (vc.rotated()) {
            QPointF at = vc.viewToScreen(QPointF(0, 0));  // where the view's origin is on the screen
            if (vc.rightAngled()) {
                // A quarter turn maps whole device pixels onto whole device pixels, once the view's origin lies on
                // one: the tiles are shown pixel for pixel, as upright (qt/docs/hidpi.md). (Input is mapped without
                // this, less than a device pixel away.)
                at = snapPoint(at, dpr);
            }
            const auto c = static_cast<float>(vc.rotationCos()), s = static_cast<float>(vc.rotationSin());
            turn = QMatrix4x4(c, -s, 0, static_cast<float>(at.x()), s, c, 0, static_cast<float>(at.y()), 0, 0, 1, 0,
                              0, 0, 0, 1);
        }
        if (root->matrix() != turn) {
            root->setMatrix(turn);
        }
    }
    const QRectF viewRect(QPointF(0, 0), vc.viewSize());

    // Composing and uploading tiles is what a scroll pays for: while the view moves, only a few per frame (the rest
    // of a page shows its preview and follows in the next frames), when it stands still more.
    const QPointF scroll = canvasView->getViewController().scrollPosition();
    const bool moving = (scroll - lastScroll).manhattanLength() > 2 || zoom != lastZoom;
    lastScroll = scroll;
    lastZoom = zoom;
    int tileBudget = moving ? TILES_WHILE_MOVING : TILES_WHEN_STILL;
    const int budgetOfTheFrame = tileBudget;
    const QRectF viewport = viewRect.adjusted(-TILE, -TILE, TILE, TILE);
    // Turned, the view is the bounding box of the screen: a tile is composed only when it meets the screen itself (the
    // separating axes of two rectangles: the view's, then the screen's)
    const QRectF screenport = QRectF(QPointF(0, 0), vc.screenSize()).adjusted(-TILE, -TILE, TILE, TILE);
    const auto meetsScreen = [&](const QRectF& inView) {
        return inView.intersects(viewport) && (!vc.rotated() || vc.viewToScreen(inView).intersects(screenport));
    };
    bool more = false;  // tiles left for the next frame

    // Dark pages (qt/docs/dark-pages.md): the tiles turned dark where they are composed, by a shader (one lookup in a
    // table per pixel; the software renderer has no shaders: the same table on the CPU, when a tile is composed)
    const bool gpu = DarkTileMaterial::available() && window() &&
                     window()->rendererInterface()->graphicsApi() != QSGRendererInterface::Software;
    QSGTexture* darkTable = nullptr;
    if (darkShown && gpu) {
        const quint64 generation = xqt::dark::tableGeneration();
        if (!root->darkTable || root->darkTableGeneration != generation) {
            delete root->darkTable;
            root->darkTable = window()->createTextureFromImage(xqt::dark::table());
            root->darkTableGeneration = generation;
        }
        darkTable = root->darkTable;
    }
    darkOnGpu = gpu;

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
        // How it is shown dark (not at all: as it is)
        const xqt::CanvasPage::DarkTone tone = darkShown ? page->darkTone() : xqt::CanvasPage::DarkTone{};
        const bool toneOnGpu = tone.dark && darkTable;
        const bool wasCpuDark = node->tone.dark && !node->toneOnGpu;
        const bool toneChanged = !(tone == node->tone) || toneOnGpu != node->toneOnGpu ||
                                 (toneOnGpu && node->darkTable != darkTable);
        node->tone = tone;
        node->toneOnGpu = toneOnGpu;
        node->darkTable = darkTable;
        const bool cpuDark = tone.dark && !toneOnGpu;
        {
            const QColor paper = tone.dark ? QColor(xqt::dark::darkPaper()) : QColor(Qt::white);
            if (node->placeholder->color() != paper) {
                node->placeholder->setColor(paper);
            }
        }
        // A tile (or the preview) on the GPU: dark with the pictures in it kept, or as it is
        const auto gpuTone = [&](TileNode* tile, std::vector<QRectF> keepRects) {
            tile->setDark(toneOnGpu ? darkTable : nullptr);
            if (toneOnGpu) {
                const QRgb p = tone.paper;
                const auto k = [](int v) { return 255.0f / static_cast<float>(std::max(v, 16)); };
                const QVector4D balance(k(qRed(p)), k(qGreen(p)), k(qBlue(p)), 1);
                if (tile->dark.setKeep(keepRects) || tile->dark.balance != balance) {
                    tile->dark.balance = balance;
                    tile->markDirty(QSGNode::DirtyMaterial);
                }
            }
        };
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
            // Not rendered yet: its preview (drawn in advance, never in front of the page), else white
            node->showPreview(window(), canvasView->preview(i), r.size(), cpuDark);
            if (node->preview) {
                gpuTone(node->preview, {});
            }
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
                             node->pixelSize != info.pixelSize || node->origin != info.origin;
        // The tiles cover the buffer: the whole page, or the part of a big page drawn (from its top left, origin)
        const QPointF origin = info.origin;
        if (rebuild) {
            node->clearTiles();
            node->cols = (info.pixelSize.width() + TILE - 1) / TILE;
            node->rows = (info.pixelSize.height() + TILE - 1) / TILE;
            node->bufferZoom = info.zoom;
            node->dpiScale = info.dpiScale;
            node->pixelSize = info.pixelSize;
            node->origin = info.origin;
            for (int t = 0; t < node->cols * node->rows; ++t) {
                auto* tile = new TileNode;
                tile->setFiltering(QSGTexture::Linear);
                const QRect px = tileRect(t, node->cols, info.pixelSize);
                tile->setRect(QRectF(origin.x() + px.x() / info.dpiScale, origin.y() + px.y() / info.dpiScale,
                                     px.width() / info.dpiScale, px.height() / info.dpiScale));
                node->tiles.push_back(tile);
            }
            node->composed.assign(node->tiles.size(), false);
        } else if (all || (toneChanged && (cpuDark || wasCpuDark))) {
            // (dark pages on the CPU: composed anew, dark or as they are)
            node->composed.assign(node->tiles.size(), false);
        } else {
            for (const QRect& d: dirty) {
                const int x0 = d.left() / TILE, x1 = d.right() / TILE, y0 = d.top() / TILE, y1 = d.bottom() / TILE;
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        const int t = y * node->cols + x;
                        if (t >= 0 && t < static_cast<int>(node->composed.size())) {
                            node->composed[static_cast<size_t>(t)] = false;
                        }
                    }
                }
            }
        }
        // The pictures kept (dark pages) in a tile: its pixels
        const auto keptIn = [&](const QRect& px) {
            std::vector<QRectF> out;
            for (const QRectF& k: tone.keep) {
                const QRectF b((k.left() * info.zoom - origin.x()) * info.dpiScale - px.x(),
                               (k.top() * info.zoom - origin.y()) * info.dpiScale - px.y(),
                               k.width() * info.zoom * info.dpiScale, k.height() * info.zoom * info.dpiScale);
                const QRectF in = b.intersected(QRectF(0, 0, px.width(), px.height()));
                if (!in.isEmpty()) {
                    out.push_back(in);
                }
            }
            return out;
        };
        const auto texCoords = [](std::vector<QRectF> rects, const QRect& px) {
            for (QRectF& k: rects) {
                k = QRectF(k.x() / px.width(), k.y() / px.height(), k.width() / px.width(), k.height() / px.height());
            }
            return rects;
        };
        if (toneChanged && !cpuDark) {
            // On the GPU (or as they are): the composed tiles keep their pictures, only their material changes
            for (int t = 0; t < static_cast<int>(node->tiles.size()); ++t) {
                if (node->composed[static_cast<size_t>(t)]) {
                    const QRect px = tileRect(t, node->cols, info.pixelSize);
                    gpuTone(node->tiles[static_cast<size_t>(t)], texCoords(keptIn(px), px));
                }
            }
        }
        // Only the tiles that are in view, and only as many as this frame allows: a page is a few dozen tiles (about
        // 30 MB), and a fast scroll passes many pages. What is not composed yet shows the page's preview.
        int missing = 0;
        for (int t = 0; t < static_cast<int>(node->tiles.size()); ++t) {
            if (node->composed[static_cast<size_t>(t)]) {
                continue;
            }
            const QRect px = tileRect(t, node->cols, info.pixelSize);
            const QRectF inItem(r.x() + (origin.x() + px.x() / info.dpiScale) * scale,
                                r.y() + (origin.y() + px.y() / info.dpiScale) * scale,
                                px.width() / info.dpiScale * scale, px.height() / info.dpiScale * scale);
            if (!meetsScreen(inItem)) {
                continue;  // (composed when it comes into view)
            }
            if (tileBudget <= 0) {
                ++missing;
                continue;
            }
            --tileBudget;
            TileNode* tile = node->tiles[static_cast<size_t>(t)];
            QImage img = page->composeTile(px);
            if (cpuDark) {
                std::vector<QRect> kept;
                for (const QRectF& k: keptIn(px)) {
                    kept.push_back(k.toAlignedRect());
                }
                xqt::dark::apply(img, tone.paper, kept);
            }
            QSGTexture* previous = tile->texture();
            tile->setTexture(window()->createTextureFromImage(img, QQuickWindow::TextureIsOpaque));
            delete previous;
            gpuTone(tile, texCoords(keptIn(px), px));
            node->composed[static_cast<size_t>(t)] = true;
            xqt::Perf::add(xqt::Perf::Tiles);
            ++statTiles;
            statPixels += static_cast<qint64>(img.width()) * img.height();
            if (!tile->parent()) {
                node->insertChildNodeBefore(tile, node->searchRoot);
            }
        }
        // A big page drawn in part whose part in view is not all drawn (scrolled on; drawn anew in a moment): the
        // preview under the tiles
        bool uncovered = false;
        if (!info.whole) {
            const QRectF inView = r.intersected(viewRect);
            const QRectF drawn(r.x() + info.area.x() * zoom, r.y() + info.area.y() * zoom, info.area.width() * zoom,
                               info.area.height() * zoom);
            uncovered = !inView.isEmpty() && !drawn.adjusted(-1, -1, 1, 1).contains(inView);
        }
        if (missing > 0 || uncovered) {
            node->showPreview(window(), canvasView->preview(i), bufferLogical, cpuDark);
            if (node->preview) {
                gpuTone(node->preview, {});
            }
            more = true;
        } else {
            node->hidePreview();
        }
    }
    // Pages that scrolled out of view: free their textures.
    for (auto& [page, node]: root->pages) {
        root->pagesRoot->removeChildNode(node);
        delete node;
    }
    mostTiles = std::max(mostTiles.load(), budgetOfTheFrame - tileBudget);
    int previews = 0;  // pages shown by their preview, whole or in part (tests)
    for (const auto& [page, node]: keep) {
        previews += node->preview != nullptr;
    }
    root->pages = std::move(keep);
    shownPreviews = previews;
    previewFrames += previews > 0;
    if (more) {  // the next frame composes further tiles
        QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    }
    updateGeometryNode(root, zoom, dpr);
    updateSelectionNode(root, zoom, dpr);
    updateCurtainNode(root, zoom, dpr);
    return root;
}

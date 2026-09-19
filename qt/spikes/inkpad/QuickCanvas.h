/*
 * xournal-qt M0 spike: Qt Quick canvas host.
 *
 * - The CPU page buffer is shown as a grid of 256 px texture tiles; only dirty tiles are re-uploaded.
 * - Pan/zoom is a scene-graph transform (GPU), the buffer is re-rendered after zoom settles.
 * - All canvas input is taken by an event filter on the QQuickWindow *before* Qt Quick's delivery agent.
 *   Tablet events inside the canvas are accepted (so Qt synthesizes no mouse events); outside they are
 *   left alone so the pen keeps working on the QML controls.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <set>

#include <QQuickItem>
#include <QRect>

class SpikeContext;

class QuickCanvas: public QQuickItem {
    Q_OBJECT
    QML_ELEMENT
public:
    explicit QuickCanvas(QQuickItem* parent = nullptr);

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void markDirty(QRect bufferRect);
    bool claimsScenePoint(QPointF scenePos) const;

    SpikeContext* ctx = nullptr;
    QQuickWindow* filteredWindow = nullptr;
    std::set<int> dirtyTiles;  // tile index = row * columns + column
    bool touchSessionOwned = false;
    bool penGrab = false;
    bool mouseGrab = false;
};

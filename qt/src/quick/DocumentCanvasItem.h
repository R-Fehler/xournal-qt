/*
 * xournal-qt: Qt Quick item showing a CanvasView (one document) and feeding it with input.
 *
 * Input: an event filter on the QQuickWindow takes tablet, touch, mouse, wheel and native gesture events inside the
 * item before Qt Quick's delivery agent (see ADR-0001). Accepted tablet events are not synthesized into mouse
 * events; events outside the item are left alone, so the pen keeps working on the QML controls.
 * Rendering: each visible page is a transform node with texture tiles (256 px) composed from the page buffer and
 * the overlay views (live strokes). Only dirty tiles are re-composed and re-uploaded; zooming scales the existing
 * tiles on the GPU until the page has been re-rendered at the new zoom.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

#include <QPointer>
#include <QQuickItem>

namespace xqt {
class CanvasInput;
class CanvasView;

/// Registers the QML types of the canvas: `import XournalQt.Canvas` provides `DocumentCanvas`.
void registerQuickTypes();
}  // namespace xqt

class DocumentCanvasItem: public QQuickItem {
    Q_OBJECT
    /// The xqt::CanvasView to show (set from C++ through the application controller).
    Q_PROPERTY(QObject* view READ view WRITE setView NOTIFY viewChanged)
    /// Size of the scrollable content and the scroll position (for scroll bars), in logical pixels.
    Q_PROPERTY(qreal contentWidth READ contentWidth NOTIFY viewportChanged)
    Q_PROPERTY(qreal contentHeight READ contentHeight NOTIFY viewportChanged)
    Q_PROPERTY(qreal contentX READ contentX NOTIFY viewportChanged)
    Q_PROPERTY(qreal contentY READ contentY NOTIFY viewportChanged)
public:
    explicit DocumentCanvasItem(QQuickItem* parent = nullptr);
    ~DocumentCanvasItem() override;

    QObject* view() const;
    void setView(QObject* view);

    qreal contentWidth() const;
    qreal contentHeight() const;
    qreal contentX() const;
    qreal contentY() const;
    /// Scroll so that the content position (x, y) is at the top-left corner.
    Q_INVOKABLE void scrollTo(qreal x, qreal y);

Q_SIGNALS:
    void viewChanged();
    void viewportChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void releaseResources() override;

private:
    void updateSearchHits(QSGNode* pageNode, size_t pageIndex, double scale);
    void takeKeyboardFocus();
    bool claims(QPointF scenePos) const;
    void updateViewGeometry();

    QPointer<xqt::CanvasView> canvasView;
    std::unique_ptr<xqt::CanvasInput> input;
    QQuickWindow* filteredWindow = nullptr;
    bool penGrab = false;
    bool mouseGrab = false;
    bool touchSessionOwned = false;
    bool viewReplaced = false;
};

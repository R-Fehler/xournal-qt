/*
 * xournal-qt: Qt Quick item showing a CanvasView (one document) and feeding it with input.
 *
 * Input: an event filter on the QQuickWindow takes tablet, touch, mouse, wheel and native gesture events inside the
 * item before Qt Quick's delivery agent (see ADR-0001). Accepted tablet events are not synthesized into mouse
 * events; events outside the item are left alone, so the pen keeps working on the QML controls.
 * Rendering: each visible page is a transform node with texture tiles (256 px) composed from the page buffer and
 * the overlay views (live strokes). Only dirty tiles are re-composed and re-uploaded; zooming scales the existing
 * tiles on the GPU until the page has been re-rendered at the new zoom. The setsquare or compass is a node of its own
 * over its page: pictures of it (GeometryToolPicture) under a transform, which is all that changes while it is moved,
 * turned or sized; they are drawn anew only for a new size or zoom, once that has been stable for a moment. They are
 * this item's (one set per canvas that shows the tool), at most 4096 pixels a side.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <QMatrix4x4>
#include <QPointF>
#include <QPointer>
#include <QQuickItem>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <QVariantList>

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
    /// The view is shown for reading only (the reference beside the document of a tab, CanvasView::setReadingOnly).
    Q_PROPERTY(bool readingOnly READ readingOnly WRITE setReadingOnly NOTIFY readingOnlyChanged)
    /// The mouse rests on a formula of a Markdown text that cannot be drawn: why (empty: none), and where it is (item
    /// coordinates). The window shows it as a tool tip.
    Q_PROPERTY(QString mathError READ mathError NOTIFY mathErrorChanged)
    Q_PROPERTY(QRectF mathErrorRect READ mathErrorRect NOTIFY mathErrorChanged)
    /// Text is being written on the canvas (a text box, Markdown): the emoji picker inserts there.
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditingChanged)
    /// The emoji suggested for the shortcode being typed (":smi"; EmojiCompletion): {emoji, name} each, the one chosen
    /// with Up / Down, and the cursor (item coordinates; empty when there are none).
    Q_PROPERTY(QVariantList emojiCompletions READ emojiCompletions NOTIFY emojiCompletionChanged)
    Q_PROPERTY(int emojiCompletionIndex READ emojiCompletionIndex NOTIFY emojiCompletionChanged)
    Q_PROPERTY(QRectF emojiCompletionRect READ emojiCompletionRect NOTIFY emojiCompletionChanged)
public:
    explicit DocumentCanvasItem(QQuickItem* parent = nullptr);
    ~DocumentCanvasItem() override;

    QObject* view() const;
    void setView(QObject* view);

    bool readingOnly() const { return reading; }
    void setReadingOnly(bool on);

    QString mathError() const { return mathErrorText; }

    bool textEditing() const;
    QVariantList emojiCompletions() const;
    int emojiCompletionIndex() const;
    QRectF emojiCompletionRect() const;
    /// A suggestion tapped: its emoji goes in place of the shortcode.
    Q_INVOKABLE void chooseEmojiCompletion(int index);
    /// Text (an emoji of the picker) at the cursor of the text being written. False if none is.
    Q_INVOKABLE bool insertText(const QString& text);
    QRectF mathErrorRect() const { return mathErrorArea; }

    qreal contentWidth() const;
    qreal contentHeight() const;
    qreal contentX() const;
    qreal contentY() const;
    /// Scroll so that the content position (x, y) is at the top-left corner.
    Q_INVOKABLE void scrollTo(qreal x, qreal y);
    /// Pages the last frame showed by their preview, whole or in part (tests)
    Q_INVOKABLE int previewsShown() const { return shownPreviews; }
    /// Most tiles composed and uploaded in one frame so far, and frames that showed a preview (tests)
    Q_INVOKABLE int mostTilesInAFrame() const { return mostTiles; }
    Q_INVOKABLE int framesWithPreviews() const { return previewFrames; }
    Q_INVOKABLE void forgetTileCount() {
        mostTiles = 0;
        previewFrames = 0;
    }
    /// What the frames cost so far (tests, benchmarks): the frames, their time in the scene graph sync (composing
    /// and uploading happen there, and the UI thread waits for it), the page tiles composed and the pixels uploaded.
    struct FrameStats {
        qint64 frames = 0;
        qint64 syncNanos = 0;
        qint64 tiles = 0;
        qint64 uploadedPixels = 0;
    };
    FrameStats frameStats() const { return {statFrames, statSyncNanos, statTiles, statPixels}; }
    void forgetFrameStats() { statFrames = statSyncNanos = statTiles = statPixels = 0; }
    /// The setsquare or compass in the last frame (tests): shown or not, the transform of its body (its own
    /// coordinates to the item's), and how many pictures of it this canvas uploaded so far.
    struct GeometryShown {
        bool shown = false;
        QMatrix4x4 body;
        QMatrix4x4 display;
        double scale = 0;  ///< pixels per point of its whole picture
        bool sharpPart = false;  ///< a sharp picture of the part in view over it
        int bodies = 0;
        int displays = 0;
    };
    GeometryShown geometryShown() const { return geometryStats; }

Q_SIGNALS:
    void viewChanged();
    void viewportChanged();
    void readingOnlyChanged();
    void mathErrorChanged();
    void textEditingChanged();
    void emojiCompletionChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void releaseResources() override;
    // Text tool: keys and input methods (on-screen keyboard) go to the text editor.
    bool event(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    void updateSearchHits(QSGNode* pageNode, size_t pageIndex, double scale);
    void takeKeyboardFocus();
    void updateSelectionNode(QSGNode* root, double zoom, double dpr);
    void updateGeometryNode(QSGNode* root, double zoom, double dpr);
    bool claims(QPointF scenePos) const;
    /// Another canvas of the window holds the pen, the mouse or the touch (`grab` of that canvas): a stroke that began
    /// there stays there, also where it crosses this canvas.
    bool heldByAnother(bool DocumentCanvasItem::*grab) const;
    /// Another canvas of the process shows this view (while two canvases swap their views).
    bool shownByAnother(const xqt::CanvasView* v) const;
    void updateViewGeometry();
    /// The mouse moved without a button: the formula error under it, once it rests (hoverTimer).
    void mouseHovers(QPointF scenePos);
    void setMathError(const QString& error, const QRectF& rect);

    QPointer<xqt::CanvasView> canvasView;
    std::unique_ptr<xqt::CanvasInput> input;
    QQuickWindow* filteredWindow = nullptr;
    bool penGrab = false;
    bool mouseGrab = false;
    bool touchSessionOwned = false;
    bool viewReplaced = false;
    bool reading = false;
    bool mouseElsewhere = false;  ///< a mouse drag that began outside the canvas (e.g. on a scroll bar)
    QTimer hoverTimer;            ///< the mouse rests (mouseHovers)
    QPointF hoverScenePos;
    QString mathErrorText;
    QRectF mathErrorArea;
    std::atomic<int> shownPreviews{0};
    std::atomic<int> mostTiles{0};
    std::atomic<int> previewFrames{0};
    std::atomic<qint64> statFrames{0}, statSyncNanos{0}, statTiles{0}, statPixels{0};
    QPointF lastScroll;  ///< of the last frame (scene graph thread): whether the view is moving
    double lastZoom = 0;
    /// The setsquare or compass: what it was like in the last frame (size, place, zoom, ...), and whether that has not
    /// changed for a moment (then a new size or zoom is drawn anew)
    std::array<double, 10> lastGeometryKey{};
    std::atomic<bool> geometrySettled{false};
    QTimer geometryTimer;
    GeometryShown geometryStats;
};

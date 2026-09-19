/*
 * xournal-qt M0 spike: raster QWidget canvas host (QPainter blits of the CPU buffer, partial updates).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QWidget>

class SpikeContext;
class QLabel;

class WidgetCanvas: public QWidget {
    Q_OBJECT
public:
    explicit WidgetCanvas(SpikeContext* ctx, QWidget* parent = nullptr);

protected:
    bool event(QEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateBufferRect(QRect bufferRect);
    SpikeContext* ctx;
    QRectF lastHoverRect;
};

/// Top-level window: a tool bar row (HUD, buttons, text field for the OSK test) plus the canvas.
class WidgetHostWindow: public QWidget {
    Q_OBJECT
public:
    explicit WidgetHostWindow(SpikeContext* ctx);

private:
    QLabel* hud;
};

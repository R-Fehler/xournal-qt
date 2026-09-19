#include "WidgetHost.h"

#include <cmath>

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QNativeGestureEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "SpikeContext.h"

WidgetCanvas::WidgetCanvas(SpikeContext* ctx, QWidget* parent): QWidget(parent), ctx(ctx) {
    setAttribute(Qt::WA_TabletTracking);  // hover TabletMove events
    setAttribute(Qt::WA_AcceptTouchEvents);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(false);
    connect(ctx->model(), &InkModel::dirty, this, &WidgetCanvas::updateBufferRect);
    connect(ctx->model(), &InkModel::fullyDirty, this, qOverload<>(&QWidget::update));
    connect(ctx->viewport(), &Viewport::changed, this, qOverload<>(&QWidget::update));
    connect(ctx->router(), &InputRouter::hoverChanged, this, [this] {
        update(lastHoverRect.toAlignedRect().adjusted(-2, -2, 2, 2));
        if (auto h = this->ctx->router()->hoverPos()) {
            lastHoverRect = QRectF(h->x() - 3, h->y() - 3, 6, 6);
            update(lastHoverRect.toAlignedRect().adjusted(-2, -2, 2, 2));
        }
    });
}

void WidgetCanvas::updateBufferRect(QRect r) {
    const double rs = ctx->model()->renderScale();
    const Viewport* vp = ctx->viewport();
    const QPointF a = vp->toView(QPointF(r.left(), r.top()) / rs);
    const QPointF b = vp->toView(QPointF(r.right() + 1, r.bottom() + 1) / rs);
    update(QRectF(a, b).toAlignedRect().adjusted(-2, -2, 2, 2));
}

void WidgetCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    ctx->viewport()->setViewSize(size());
}

bool WidgetCanvas::event(QEvent* e) {
    InputRouter* router = ctx->router();
    switch (e->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto* t = static_cast<QTabletEvent*>(e);
            router->tablet(t, t->position());
            t->accept();
            return true;
        }
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto* t = static_cast<QTouchEvent*>(e);
            router->touch(t, [this](QPointF scenePos) { return mapFrom(window(), scenePos); });
            t->accept();
            return true;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseMove: {
            auto* m = static_cast<QMouseEvent*>(e);
            router->mouse(m, m->position());
            m->accept();
            return true;
        }
        case QEvent::Wheel: {
            auto* w = static_cast<QWheelEvent*>(e);
            router->wheel(w, w->position());
            w->accept();
            return true;
        }
        case QEvent::NativeGesture: {
            auto* g = static_cast<QNativeGestureEvent*>(e);
            router->nativeGesture(g, g->position());
            g->accept();
            return true;
        }
        default:
            return QWidget::event(e);
    }
}

void WidgetCanvas::paintEvent(QPaintEvent* e) {
    ctx->router()->syncPoint();
    QPainter p(this);
    p.fillRect(e->rect(), QColor(0x3c, 0x3c, 0x40));
    const Viewport* vp = ctx->viewport();
    const InkModel* model = ctx->model();
    p.save();
    p.translate(vp->offset());
    p.scale(vp->scale(), vp->scale());
    p.fillRect(QRectF(3.0 / vp->scale(), 3.0 / vp->scale(), InkModel::PAGE_W, InkModel::PAGE_H), QColor(0, 0, 0, 90));
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRectF(0, 0, InkModel::PAGE_W, InkModel::PAGE_H), model->bufferImage());
    p.restore();
    if (auto h = ctx->router()->hoverPos()) {
        p.fillRect(QRectF(h->x() - 3, h->y() - 3, 6, 6),
                   ctx->router()->isEraserHover() ? QColor(0xd0, 0x30, 0x30) : QColor(0x1d, 0x2b, 0x8f));
    }
    p.end();
    // Approximation: the backing store is flushed right after paintEvent returns.
    ctx->router()->framePresented();
}

WidgetHostWindow::WidgetHostWindow(SpikeContext* ctx) {
    setWindowTitle("xournal-qt inkpad spike (widget host)");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget;
    auto* barLayout = new QHBoxLayout(bar);
    hud = new QLabel;
    hud->setTextInteractionFlags(Qt::NoTextInteraction);
    QFont mono("monospace");
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(8.5);
    hud->setFont(mono);
    barLayout->addWidget(hud, 1);
    auto* text = new QLineEdit;
    text->setPlaceholderText("tap: on-screen keyboard?");
    text->setMinimumWidth(180);
    barLayout->addWidget(text);
    for (const auto& [label, fn]: std::initializer_list<std::pair<const char*, void (SpikeContext::*)()>>{
                 {"Undo", &SpikeContext::undo},
                 {"Redo", &SpikeContext::redo},
                 {"Fit", &SpikeContext::fitWidth},
                 {"Clear", &SpikeContext::clear}}) {
        auto* b = new QPushButton(label);
        b->setMinimumSize(64, 44);
        connect(b, &QPushButton::clicked, ctx, fn);
        barLayout->addWidget(b);
    }
    layout->addWidget(bar);
    layout->addWidget(new WidgetCanvas(ctx), 1);

    connect(ctx, &SpikeContext::hudChanged, this, [this, ctx] {
        hud->setText("[widget] " + ctx->environment() + "\n" + ctx->hud());
    });
    resize(1200, 900);
}

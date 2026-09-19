/*
 * xournal-qt M0 spike: a single A4 page rendered with cairo into a CPU buffer.
 *
 * Strokes are drawn incrementally segment by segment (like Xournal++'s live stroke views) and the
 * buffer is re-rendered completely when the render resolution changes (after a zoom settles).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRect>

#include <cairo.h>

class InkModel: public QObject {
    Q_OBJECT
public:
    static constexpr double PAGE_W = 595.0;  // A4 in points
    static constexpr double PAGE_H = 842.0;

    struct Pt {
        double x, y, pressure;
    };
    struct Stroke {
        std::vector<Pt> pts;
        double width;  // in points, at pressure 1
        QColor color;
    };

    explicit InkModel(QObject* parent = nullptr);
    ~InkModel() override;

    /// Pixels per page point of the CPU buffer. Changing it re-renders everything.
    void setRenderScale(double pxPerPt);
    double renderScale() const { return scale; }
    QSize bufferSize() const;
    /// Zero-copy view of the cairo buffer. Valid until the next setRenderScale().
    QImage bufferImage() const;
    /// Incremented whenever the buffer is reallocated / completely re-rendered.
    quint64 generation() const { return gen; }

    void beginStroke(QPointF pagePt, double pressure, QColor color, double width);
    void extendStroke(QPointF pagePt, double pressure);
    void endStroke();
    bool isDrawing() const { return current.has_value(); }

    /// Removes all strokes touching a disc around pagePt. Returns true if something was erased.
    bool eraseAt(QPointF pagePt, double radiusPt);
    void clear();
    void undo();
    void redo();
    size_t strokeCount() const { return strokes.size(); }

Q_SIGNALS:
    /// A part of the buffer changed (buffer pixel coordinates).
    void dirty(QRect bufferRect);
    /// The buffer was re-created or completely re-rendered.
    void fullyDirty();

private:
    void renderAll();
    void drawBackground(cairo_t* cr) const;
    void drawStroke(cairo_t* cr, const Stroke& s) const;
    void drawSegment(cairo_t* cr, const Stroke& s, const Pt& a, const Pt& b) const;
    QRect segmentBufferRect(const Stroke& s, const Pt& a, const Pt& b) const;

    cairo_surface_t* surface = nullptr;
    double scale = 2.0;
    quint64 gen = 0;
    std::vector<Stroke> strokes;
    std::vector<Stroke> redoStack;
    std::optional<Stroke> current;
};

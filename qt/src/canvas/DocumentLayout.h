/*
 * xournal-qt: page layout of a document view (single column, like upstream's default vertical layout).
 *
 * Coordinates:
 *  - page coordinates: points on a page (what the document model uses);
 *  - content coordinates: logical pixels of the whole scrollable layout at a given zoom.
 * As in upstream gui/Layout.cpp, the paddings are fixed pixel sizes (independent of the zoom): XOURNAL_PADDING
 * around the document and XOURNAL_PADDING_BETWEEN between pages; pages are centered in the column.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <QPointF>
#include <QRectF>
#include <QSizeF>

class Document;

namespace xqt {

class DocumentLayout {
public:
    static constexpr double PADDING = 10.0;          ///< upstream XOURNAL_PADDING
    static constexpr double PADDING_BETWEEN = 15.0;  ///< upstream XOURNAL_PADDING_BETWEEN

    /// Re-read the page sizes. Call on the UI thread (takes a shared document lock).
    void update(Document& doc);

    size_t pageCount() const { return sizes.size(); }
    QSizeF pageSize(size_t page) const { return sizes[page]; }

    QRectF pageRect(size_t page, double zoom) const;
    QSizeF contentSize(double zoom) const;
    /// Page containing the content point, if any.
    std::optional<size_t> pageAt(QPointF content, double zoom) const;
    /// Page whose vertical extent (including half the padding around it) contains the content point.
    size_t nearestPage(QPointF content, double zoom) const;
    /// Range of pages intersecting a content rectangle.
    std::pair<size_t, size_t> pagesIn(const QRectF& content, double zoom) const;

private:
    double pageTop(size_t page, double zoom) const;

    std::vector<QSizeF> sizes;
    std::vector<double> heightPrefix;  ///< sum of the heights (points) of the pages before page i
    double maxWidth = 0;
};

}  // namespace xqt

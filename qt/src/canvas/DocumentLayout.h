/*
 * xournal-qt: page layout of a document view: one or more columns, optionally paired pages (book spreads).
 *
 * Coordinates:
 *  - page coordinates: points on a page (what the document model uses);
 *  - content coordinates: logical pixels of the whole scrollable layout at a given zoom.
 * As in upstream gui/Layout.cpp, the paddings are fixed pixel sizes (independent of the zoom): XOURNAL_PADDING
 * around the document and XOURNAL_PADDING_BETWEEN between pages; a column is as wide as its widest page and a row
 * as high as its highest page, pages are centered in their cell.
 * Pages are placed row by row (upstream's default horizontal layout with a fixed number of columns, see
 * LayoutMapper): `columns` per row; with paired pages the column count is even and the two pages of a pair meet in
 * the middle, `pairsOffset` empty slots before the first page (1: the cover stands alone, like a book).
 * Scrolling sideways (`horizontal`) places them column by column instead, in `rows` rows (upstream's vertical layout
 * with fixed rows): the document is one wide strip, a pair of pages stays side by side.
 *
 * A "group" is what the view steps through: a row of pages, or scrolling sideways a column (a pair of columns with
 * paired pages). Its pages are consecutive.
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

#include "model/PageRef.h"

class Document;

namespace xqt {

/// Upstream settings: viewColumns, showPairedPages, numPairsOffset; scrolling sideways: viewFixedRows (with
/// viewLayoutVert), viewRows.
struct DocumentLayoutConfig {
    size_t columns = 1;
    bool paired = false;
    size_t pairsOffset = 0;
    bool horizontal = false;
    size_t rows = 1;
    /// No margin around the pages (presenting: a page fills the screen)
    bool noMargins = false;
    bool operator==(const DocumentLayoutConfig&) const = default;
};

class DocumentLayout {
public:
    static constexpr double PADDING = 10.0;          ///< upstream XOURNAL_PADDING
    static constexpr double PADDING_BETWEEN = 15.0;  ///< upstream XOURNAL_PADDING_BETWEEN
    static constexpr double PAIR_GAP = 2.0;          ///< between the two pages of a pair

    using Config = DocumentLayoutConfig;

    /// Re-read the page sizes of the view's pages. Call on the UI thread (takes a shared document lock).
    /// Like upstream's Layout, this follows the view's own page list, not the document: upstream fires
    /// "page deleted" before it removes the page from the document.
    void update(Document& doc, const std::vector<PageRef>& pages, Config config = {});
    const Config& getConfig() const { return config; }

    size_t pageCount() const { return sizes.size(); }
    QSizeF pageSize(size_t page) const { return sizes[page]; }
    /// The widest and the highest page's size (points; none: empty)
    QSizeF largestPage() const { return largest; }
    size_t columns() const { return cols; }
    size_t rows() const { return rowCount; }
    bool horizontal() const { return config.horizontal; }
    /// The margin around the pages (pixels)
    double padding() const { return config.noMargins ? 0.0 : PADDING; }

    size_t groupCount() const;
    size_t groupOf(size_t page) const;
    /// The pages of a group (first, last)
    std::pair<size_t, size_t> groupPages(size_t group) const;
    /// The pages of a group, from the top left of the first to the bottom right of the last (content).
    QRectF groupRect(size_t group, double zoom) const;

    QRectF pageRect(size_t page, double zoom) const;
    QSizeF contentSize(double zoom) const;
    /// Page containing the content point, if any.
    std::optional<size_t> pageAt(QPointF content, double zoom) const;
    /// Page of the cell (including half the padding around it) that contains the content point.
    size_t nearestPage(QPointF content, double zoom) const;
    /// Range of pages in the rows intersecting a content rectangle (pages are placed row by row).
    std::pair<size_t, size_t> pagesIn(const QRectF& content, double zoom) const;
    /// Zoom at which the group of a page (its row; scrolling sideways its column or pair) fits into the view width:
    /// upstream's fit-to-width, but for the page in view rather than the widest one (a wide page elsewhere in the
    /// document does not make the others small).
    double fitWidthZoom(double viewWidth, size_t page) const;
    /// The pages of the group of `page`, from the left edge of the first to the right edge of the last (content).
    QRectF rowSpan(size_t page, double zoom) const { return groupRect(groupOf(page), zoom); }
    /// Zoom at which all rows fit into the view height (scrolling sideways: "fit to the window height").
    double fitHeightZoom(double viewHeight) const;

private:
    struct Cell {
        size_t col, row;
    };
    Cell cellOf(size_t page) const;
    double colX(size_t col, double zoom) const;
    double rowY(size_t row, double zoom) const;
    size_t rowAt(double y, double zoom) const;
    size_t colAt(double x, double zoom) const;
    double gapAfterColumn(size_t col) const;
    /// Where a page lies in its column (points: it grows with the zoom): centered, or pushed to its pair.
    double offsetInColumn(size_t page) const;
    /// The place (slot, counting the empty ones before the first page) at a cell
    size_t slotOf(size_t col, size_t row) const;
    /// Columns in a group (scrolling sideways: 2 for pairs)
    size_t groupColumns() const { return config.paired ? 2 : 1; }

    Config config;
    std::vector<QSizeF> sizes;
    QSizeF largest;
    size_t cols = 1;
    size_t rowCount = 0;
    size_t offset = 0;
    std::vector<double> colWidth, rowHeight;  ///< points
    std::vector<double> colPrefix, rowPrefix;  ///< sum of the widths/heights before (points)
    std::vector<double> gapPrefix;             ///< sum of the gaps before a column (pixels)
};

}  // namespace xqt

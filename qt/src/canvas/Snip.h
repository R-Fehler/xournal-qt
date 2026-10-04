/*
 * xournal-qt: the snip tool (qt/snip, qt/docs/snip.md): a rectangle or lasso dragged over a page copies a picture of
 * what is there to the clipboard.
 *
 * Armed (process-wide, like the tool in hand), the next rectangle or lasso of the select tools is a snip instead of a
 * selection: on release the area is drawn (render/RegionRender.h, off the UI thread) and the view says so
 * (CanvasView::snipped); the app puts it on the clipboard, with where it came from (the fork's MIME entry beside the
 * picture), and takes the tool used before again.
 *
 * Pasted into a document of the app, the picture goes in at the size it had on its page, and the view offers to add
 * a link to the source page next to it (CanvasView::snipLinkOffered, addSnipLink).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>

#include <QByteArray>
#include <QRectF>
#include <QString>

class QMimeData;

namespace xqt::snip {

enum class Shape { None, Rectangle, Lasso };

/// The next rectangle or lasso (of the select tools) is a snip of this shape. None: not armed.
void arm(Shape shape);
void disarm();
Shape armed();
inline bool isArmed() { return armed() != Shape::None; }

/// The fork's clipboard entry beside the picture: where it came from.
inline constexpr const char* MIME = "application/x-xournal-qt-snip";
struct Source {
    QString title;  ///< "kalman, page 4"
    /// A link to the page with the document's absolute path (links::write); empty: the document has no file yet
    QString link;
    int page = 0;   ///< 0-based
    QRectF area;    ///< on the page (points): the picture's size there
};
QByteArray encode(const Source& source);
std::optional<Source> decode(const QMimeData* mime);

}  // namespace xqt::snip

/*
 * xournal-qt: where the pictures of a page are, so that dark pages (DarkPages.h) keep their colors.
 *
 *  - images of the document (upstream's Image elements on visible layers): read from the document;
 *  - pictures of a PDF page (raster images in its content): poppler's image mapping, read on one background worker at
 *    the lowest priority with a poppler instance of its own (PdfLayoutReader), only for pages shown dark, and kept per
 *    PDF page (a few rectangles each; owner: the CanvasView, so at most its PDF's pages). The canvas never waits:
 *    until a page's pictures are known it is shown dark whole, then `known` asks for a frame.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QObject>
#include <QRectF>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "filesystem.h"

class XojPage;

namespace xqt {

/// The document's images on a page (visible layers; page coordinates). The caller holds the document lock.
std::vector<QRectF> imageRects(const XojPage& page);

class PdfPictures final: public QObject {
    Q_OBJECT
public:
    explicit PdfPictures(QObject* parent = nullptr);
    ~PdfPictures() override;

    /// The pictures of a page of `pdf` (0-based; PDF points from the page's top left), or nullopt while they are not
    /// known (they are read then, and `known` follows). Another file forgets what was read of the last one. UI thread.
    std::optional<std::vector<QRectF>> pictures(const fs::path& pdf, int pdfPage);
    /// Pages read so far (tests)
    int pagesRead() const;

    /// Of an open PDF page (the worker; tests): its pictures, as above
    static std::vector<QRectF> read(void* popplerPage);

Q_SIGNALS:
    void known();

private:
    struct Shared;
    std::shared_ptr<Shared> shared;
};

}  // namespace xqt

/*
 * xournal-qt: the one owner of the memory of the image caches. What each cache may keep is set here, in one table
 * (the words are those of qt/docs/architecture/image-caches.md):
 *
 * | Pictures                          | Kept by                  | Limit                                                |
 * | --------------------------------- | ------------------------ | ---------------------------------------------------- |
 * | page previews: sharp thumbnails   | ThumbnailProvider        | 3/4 of the setting "previewMemory" (default 256 MB)  |
 * | page previews: sketches           | PageSketches             | 1/4 of that setting                                  |
 * | stand-ins of pages on the canvas  | PageSketches             | 1/10 of the memory for rendered pages (CanvasMemory) |
 * | covers of documents (PNG)         | DocumentCovers           | COVER_BYTES                                          |
 * | pages with search hits            | HitPageProvider          | HIT_PAGE_BYTES, and HIT_PAGE_DOCUMENTS loaded        |
 * | pictures in the annotations panel | AnnotationImageProvider  | ANNOTATION_PICTURE_BYTES                             |
 * | Markdown snippet cards            | MdSnippetProvider        | SNIPPET_FILES parsed files                           |
 *
 * The setting "previewMemory" (Settings → Documents, "Page previews (sidebar, overviews)") is the memory for the page
 * previews: what the page sidebar, the page grid and the overview of open documents show. setPreviewMemory() hands
 * the thumbnails and the sketches their shares; the stand-ins follow the memory for rendered pages, which the canvas
 * owns (CanvasMemory::standInBudget). The others have fixed limits: they hold what one view of the library shows.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>

#include <QtGlobal>

namespace xqt {

class ImageMemory {
public:
    static constexpr qint64 MB = 1024 * 1024;
    /// The setting "previewMemory" when it is not set (MB)
    static constexpr qint64 DEFAULT_PREVIEW_MB = 256;
    static constexpr qint64 COVER_BYTES = 48 * MB;
    static constexpr qint64 HIT_PAGE_BYTES = 128 * MB;
    static constexpr size_t HIT_PAGE_DOCUMENTS = 12;
    static constexpr qint64 ANNOTATION_PICTURE_BYTES = 24 * MB;
    static constexpr size_t SNIPPET_FILES = 8;

    /// The memory for page previews (bytes; the setting): the thumbnails get three quarters, the sketches a quarter.
    static void setPreviewMemory(qint64 bytes);
    static qint64 previewMemory();
    /// The shares of the memory for page previews
    static constexpr qint64 thumbnailShare(qint64 previewMemory) { return previewMemory - previewMemory / 4; }
    static constexpr qint64 sketchShare(qint64 previewMemory) { return previewMemory / 4; }
    /// What the stand-ins may take: a part of the memory for rendered pages
    static qint64 standInBudget();
};

}  // namespace xqt

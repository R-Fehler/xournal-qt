/*
 * xournal-qt: an image file (.png, .jpg, .webp, .heic, ...) as a document of the library.
 *
 * The library shows images with a thumbnail. Opening one makes a new document with the image as the background of
 * its page, to write on (see document()).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>

#include <QImage>

#include "filesystem.h"

class Document;

namespace xqt::ImageFile {

/// The image as it is shown: turned upright by its orientation tag, scaled down to `width` pixels wide (0: as it
/// is; it is decoded at a smaller size when the format can). Any thread.
QImage read(const fs::path& file, int width = 0);

/// The longer side of the page of an image (points: that of A4).
constexpr double PAGE_SIDE = 841.89;
/// Images are kept at most this big (pixels, the longer side) when they are stored with the document.
constexpr int MAX_STORED_SIDE = 4096;

/// A new document with one page that shows the image as its background (upstream's image background), as big as
/// the image fits into a page whose longer side is PAGE_SIDE. A PNG or JPEG that needs no turning is used by its
/// path, as upstream refers to background images (a .xopp saved next to it stays small, and the library pairs the
/// two); any other image (a WebP, a HEIC, a photo turned by its orientation tag, which upstream would show
/// sideways) is stored with the document as a PNG, as upstream stores attached images ("name.xopp.bg_1.png").
/// nullptr if it cannot be read (`error`). Any thread.
std::unique_ptr<Document> document(const fs::path& file, std::string& error);

}  // namespace xqt::ImageFile

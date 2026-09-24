/*
 * xournal-qt: an image file (.png, .jpg, .webp, .heic, ...) as a document of the library.
 *
 * The library shows images with a thumbnail. Opening one makes a new document with the image as the background of
 * its page, to write on (see document()).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QImage>

#include "filesystem.h"

namespace xqt::ImageFile {

/// The image as it is shown: turned upright by its orientation tag, scaled down to `width` pixels wide (0: as it
/// is; it is decoded at a smaller size when the format can). Any thread.
QImage read(const fs::path& file, int width = 0);

}  // namespace xqt::ImageFile

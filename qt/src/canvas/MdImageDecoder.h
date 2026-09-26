/*
 * xournal-qt: the pictures of the Markdown text read with Qt (qt/docs/md-images.md): PNG, JPEG, GIF (its first frame),
 * WebP and SVG where Qt has the plugin, turned upright by their orientation tag. The Markdown renderer is Qt-free
 * (md::images); this is the decoder the app gives it.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

namespace xqt::MdImageDecoder {

/// md::images reads pictures with Qt from now on, and fetched web pictures are kept in the app cache (idempotent).
void install();

}  // namespace xqt::MdImageDecoder

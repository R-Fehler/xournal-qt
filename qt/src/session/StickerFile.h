/*
 * xournal-qt: a sticker's file (qt/docs/stickers.md): reusable content saved as a .xopp of one page, as large as the
 * content plus a margin, on plain paper of the colour of the page it came from. Ink, shapes, images and LaTeX in
 * "Layer 1", Markdown boxes in the page's "Markdown" layer (at the bottom), whole sticky notes as their layers on top,
 * and optionally a picture of the background behind it all (the PDF page) in a layer "Sticker picture" at the bottom.
 * Xournal++ opens it as it is.
 *
 * The content is what the canvas copies (sticky::Group: notes and elements with where they were); a sticker read back
 * is that again, to paste the way a copied selection is pasted. Any thread: nothing here touches an open document.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "model/Document.h"
#include "util/Color.h"
#include "util/Rectangle.h"

#include "StickyNote.h"
#include "filesystem.h"

namespace xqt::stickers {

/// Around the content on the sticker's page (points)
inline constexpr double MARGIN = 6;
/// The layer of the picture of the background behind the content (saved; fixed English, like "Markdown")
inline constexpr const char* PICTURE_LAYER = "Sticker picture";

/// The picture of the background behind the content (the PDF page there): a PNG, and where it lies in the
/// coordinates of the content (its page)
struct Picture {
    std::string png;
    xoj::util::Rectangle<double> area{0, 0, 0, 0};
};

/// The sticker's document: one page of the content's size plus MARGIN around it, plain paper of `paper`, the content
/// moved so its bounds begin at (MARGIN, MARGIN). Its elements and notes are taken (moved into the document).
std::unique_ptr<Document> makeDocument(sticky::Group content, Color paper, const std::optional<Picture>& picture = {});

/// Write a sticker's document to `target` (.xopp, with a preview of its page and the pictures of its Markdown boxes).
/// False with `error` when it could not be written.
bool write(Document& doc, const fs::path& target, std::string* error = nullptr);

/// A sticker read back as content to paste: the elements of its first page in their order (the picture first, then
/// the Markdown boxes, then the other layers' elements), its notes (bottom first) and the bounds around them all (the
/// page's coordinates). The elements of each layer they go into are one group (qt/docs/groups.md). Nothing (with
/// `error`) if it cannot be read or holds nothing.
std::optional<sticky::Group> read(const fs::path& file, std::string* error = nullptr);

/// The content in the clipboard's format of a selection of notes and elements (sticky::GROUP_CLIPBOARD_MIME)
std::string clipboardBytes(const sticky::Group& content);

/// A name for a sticker of this content: the first words of its first text (Markdown marks and characters that a
/// file name cannot have left out; at most 40 characters), empty when it has no text
std::string suggestedName(const sticky::Group& content);

/// A file name of `name` (characters a file name cannot have on some system replaced, trimmed; "" stays "")
std::string fileNameOf(const std::string& name);

}  // namespace xqt::stickers

/*
 * xournal-qt: the pictures of a document's Markdown and where they are kept (qt/docs/md-images.md).
 *
 * A `.md` keeps its pictures in "name.assets/" next to it; a PDF text document carries them inside (unpacked into the
 * app cache while it is open). Links to them are written "name.assets/file". The renderer finds them through the
 * document's root (md::images::Root), which the session registers while the document is open.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include "MdImages.h"
#include "filesystem.h"

namespace xqt::DocumentImages {

/// "name.assets" for "name.md" (or "name.pdf", "name.archive.pdf", "name.xopp").
std::string assetsName(const fs::path& document);
/// The folder next to a `.md` its pictures go into: "name.assets".
fs::path assetsFolder(const fs::path& markdownFile);
/// The root of a `.md` (or a text file shown as one): its folder, and "name.assets" next to it.
md::images::Root markdownRoot(const fs::path& markdownFile);

/// A file name as a link to it is written: what Markdown or a web address would read otherwise %-encoded (" " is
/// "%20", "(" "%28", …).
std::string linkEncoded(const std::string& name);

/// The text with its links into "oldName/" (a `.md`'s "name.assets" folder) pointing into "newName/" instead: the
/// document was renamed (qt/docs/md-images.md). Links as Markdown writes them: "](old/…", "](./old/…", "](<old/…",
/// a reference definition "]: old/…", an HTML src="old/…"; the name written as it is or %-encoded (then the new one
/// is written so too). Nothing else changes, byte for byte.
std::string renamedAssetLinks(const std::string& text, const std::string& oldName, const std::string& newName);

}  // namespace xqt::DocumentImages

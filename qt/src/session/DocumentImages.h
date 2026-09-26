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
#include <vector>

#include "MdImages.h"
#include "filesystem.h"

namespace xqt::DocumentImages {

/// "name.assets" for "name.md" (or "name.pdf", "name.archive.pdf", "name.xopp").
std::string assetsName(const fs::path& document);
/// The folder next to a `.md` its pictures go into: "name.assets".
fs::path assetsFolder(const fs::path& markdownFile);
/// The root of a `.md` (or a text file shown as one): its folder, and "name.assets" next to it.
md::images::Root markdownRoot(const fs::path& markdownFile);

// --- pictures carried inside a document (a PDF text document, qt/docs/md-images.md) --------------------------------

/// The folder in the app cache where the pictures a document carries are while it is open (one per document, by its
/// path): "<cache>/md-assets/<hash>". Links resolve there, and pictures added go there.
fs::path workFolder(const fs::path& document);
/// The root of such a document: its work folder (for every relative link, as the document's pictures are named),
/// with "name.assets" in it for new pictures.
md::images::Root embeddedRoot(const fs::path& document);
/// The root of the folder a document is in (relative links other than its own pictures).
md::images::Root folderRoot(const fs::path& document);

/// The relative links of a Markdown text that a document can carry: its pictures, as the paths they name ("./" and
/// %-escapes taken away), each once, in order. Not web addresses, not absolute paths, nothing with "..".
std::vector<std::string> carriedLinks(const std::string& markdown);
/// A carried path (see carriedLinks) as a path below `folder` (nothing when it is none).
fs::path below(const fs::path& folder, const std::string& carried);

/// The files of `pictures` (a folder with them under their carried paths) copied into the document's work folder
/// (the files there are kept; one of the same size is not copied again). The pictures changed (md::images).
void unpack(const fs::path& document, const fs::path& pictures);
/// The pictures a Markdown text links to (as the roots resolve them now) copied under their carried paths into
/// `folder` (another document's work folder, the folder next to an exported .md). Returns how many.
size_t copyLinked(const std::string& markdown, const fs::path& folder);

/// Export as Markdown (qt/docs/md-images.md): the pictures `markdown` links to, next to `mdFile` as it expects them:
/// links into another "…assets/" folder (the document's name changed since) are rewritten to "name.assets/" of
/// `mdFile`, the files copied there (other relative paths as they are). The text to write is the result; `copied`:
/// how many pictures were copied.
std::string exportPictures(const std::string& markdown, const fs::path& mdFile, size_t& copied);

/// The pictures (any files) in the "name.assets" folder of a `.md` that its text does not link to (images, links,
/// reference definitions, HTML src), sorted: what "Remove unused images" offers to move to the trash. A folder
/// that is not there: none.
std::vector<fs::path> unusedPictures(const fs::path& markdownFile, const std::string& text);

/// A file name as a link to it is written: what Markdown or a web address would read otherwise %-encoded (" " is
/// "%20", "(" "%28", …).
std::string linkEncoded(const std::string& name);

/// The text with its links into "oldName/" (a `.md`'s "name.assets" folder) pointing into "newName/" instead: the
/// document was renamed (qt/docs/md-images.md). Links as Markdown writes them: "](old/…", "](./old/…", "](<old/…",
/// a reference definition "]: old/…", an HTML src="old/…"; the name written as it is or %-encoded (then the new one
/// is written so too). Nothing else changes, byte for byte.
std::string renamedAssetLinks(const std::string& text, const std::string& oldName, const std::string& newName);

}  // namespace xqt::DocumentImages

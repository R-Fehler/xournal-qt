/*
 * xournal-qt: stamps and fingerprints of a document's files, which tell the library's caches whether a file changed
 * (the index, the covers, the page sketches, sharing).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

#include <QString>

#include "filesystem.h"
#include "DocumentFiles.h"

namespace xqt {

/// A string that changes when one of the document's files changes (size, modification time): the .xopp, the PDF next
/// to it, an attached PDF, the merged PDF of pasted pages (".name.pages.pdf").
QString documentStamp(const DocumentItem& item);
/// Size and modification time of one file ("" if it does not exist).
QString fileStamp(const fs::path& file);
/// documentStamp with the stamps of the files from `stampOf` (sharing as a zip: the files as they are in the zip).
QString documentStamp(const DocumentItem& item, const std::function<QString(const fs::path&)>& stampOf);
/// A hash of a file's content (BLAKE2b-256, hex; "" if it cannot be read): a cache entry whose file has another time
/// but the same size and this hash is the same file (copied, unzipped, synced: qt/docs/features/library.md, "Entries
/// that survive a copy").
QString contentHash(const fs::path& file);
/// The file whose stamp is a cache entry's own ("xopp" stamp): the .xopp, a Markdown file, a lone image or text file,
/// a lone PDF (none for a PDF with its .xopp: the PDF has its own stamp).
fs::path ownFileOf(const DocumentItem& item);
/// A hash of the start and end of a file (all of a small one), with its size: two files with the same size and time
/// are not taken for each other unless it is the same too ("": cannot be read).
QString contentSample(const fs::path& file);

}  // namespace xqt

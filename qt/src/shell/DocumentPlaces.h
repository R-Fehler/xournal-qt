/*
 * xournal-qt: the pages of a document that matter besides its content - the title page (its preview in the library
 * and in the overview of open documents; the first page unless chosen otherwise) and the page it was left at (to
 * open it there again, if wanted).
 *
 * A .xopp has no place for them, so they are kept aside, in a small JSON file: for the documents of the library in
 * its metadata folder (by their path in the library, so they follow the library when it is moved), for the others in
 * the user's cache (by their whole path). Renaming and moving in the app take the entries along.
 *
 * Safe from any thread (previews are drawn by workers).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <utility>
#include <vector>

#include <QtGlobal>

#include "filesystem.h"

namespace xqt {
struct DocumentItem;
}

namespace xqt::DocumentPlaces {

/// The file a document's entry is kept under: its PDF if it has one (so the entry stays when a .xopp is added to a
/// PDF), else its .xopp. The functions below take this file.
fs::path keyOf(const DocumentItem& item);
/// The same for any file of a document (looks at the files beside it).
fs::path keyOf(const fs::path& file);

/// The documents in `root` keep their pages in `file` (empty root: no library).
void setLibrary(const fs::path& root, const fs::path& file);
/// Where the documents outside the library keep theirs (default: the user's cache; tests use their own).
void setOutsideFile(const fs::path& file);

/// The title page (0-based; 0 unless chosen otherwise).
int titlePage(const fs::path& document);
void setTitlePage(const fs::path& document, int page);
/// The page the document was left at (-1: not known).
int lastPage(const fs::path& document);
void setLastPage(const fs::path& document, int page);
/// When the document was last read in this app - opened or closed (seconds since 1970; -1: never).
qint64 lastRead(const fs::path& document);
/// It is read now (or at `when`, seconds since 1970).
void setRead(const fs::path& document, qint64 when = -1);
/// Files or folders renamed or moved (old, new): their entries follow.
void moved(const std::vector<std::pair<fs::path, fs::path>>& moves);

}  // namespace xqt::DocumentPlaces

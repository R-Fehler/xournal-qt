/*
 * xournal-qt: the merged background PDF of a document (qpdf).
 *
 * A .xopp has one background PDF, and its pages refer to page numbers in it. PDF pages pasted from another PDF go
 * into a merged PDF that the document uses as its background instead: the pages it used of its own PDF, followed by
 * the pasted ones. It is written next to the .xopp (a hidden ".name.pages.pdf", or "name.pdf" for a document that
 * had no PDF), or into the app cache while the document is not saved yet. The PDF a document annotates is never
 * changed. A merged PDF carries a mark in its document information, so it can be told from the user's own PDFs.
 *
 * All writes are atomic: a temporary file next to the target is renamed over it. A PDF that is open (poppler) keeps
 * reading the old file.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include "filesystem.h"

namespace xqt::MergedPdf {

enum class Kind {
    None,        ///< not a merged PDF (a user's PDF)
    Own,         ///< only pasted pages: may be the "name.pdf" next to the .xopp
    WithSource,  ///< holds pages of the PDF the document annotates: always the hidden sidecar
};

/// ".lecture.pages.pdf" next to "lecture.xopp".
fs::path sidecarOf(const fs::path& xopp);
/// "lecture.pdf" next to "lecture.xopp".
fs::path pairOf(const fs::path& xopp);
/// Whether the file name is the one of a sidecar (".<name>.pages.pdf").
bool isSidecarName(const fs::path& pdf);
/// Where the merged PDFs of documents that were never saved are kept.
fs::path cacheFolder();
bool inCache(const fs::path& pdf);

/// The mark of a merged PDF (None: a user's PDF, or not readable).
Kind kindOf(const fs::path& pdf);

struct Result {
    bool ok = false;
    std::string error;
    size_t first = 0;  ///< append: the number of the first added page
    size_t pages = 0;  ///< the pages the written PDF has
};

/// These pages (0-based, in this order) of a PDF file as a new PDF in memory (`out`).
Result extract(const fs::path& pdf, const std::vector<size_t>& pages, std::string& out);
/// Write `target`: the pages of `base` (none if it is empty) followed by the pages of `addition` (a PDF in memory),
/// marked as a merged PDF of this kind. `base` may be `target`.
Result append(const fs::path& base, const std::string& addition, const fs::path& target, Kind kind);
/// Write `target` with only these pages of `source` (ascending numbers), keeping its mark. `source` may be `target`.
Result keepOnly(const fs::path& source, const std::vector<size_t>& pages, const fs::path& target);

}  // namespace xqt::MergedPdf

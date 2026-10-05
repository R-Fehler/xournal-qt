/*
 * xournal-qt: pages as files of their own (qt/docs/page-files.md): which pages a typed range names, how a document is
 * split, the selected pages of a document written as a new PDF with notes or .xopp (extract, split), and the names of
 * pages exported as pictures.
 *
 * A new document from some pages is a copy of those pages (as a save copies them) whose background PDF is the
 * source's own: the writer takes their PDF pages from it (qpdf, the text stays text, searchable) and nothing else of
 * it. A protected source (qt/docs/hybrid-pdf.md, "Encrypted PDFs") gives a PDF with notes protected the same way;
 * a .xopp of it is refused by the caller (a .xopp cannot be encrypted).
 *
 * Qt-free; any thread (the source document is read under its shared lock, the caller must not hold it).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "PdfEncryption.h"

#include "filesystem.h"

class Document;

namespace xqt::pagefiles {

/// The pages a range names: "1-3, 5, 8-" (1-based; "8-": to the end, "-3": from the first; spaces, commas and
/// semicolons between parts) as 0-based page indices of a document of `count` pages, ascending, each once. Empty
/// with `error` when a part is not a page number or a range of them, or when no page of the document is named.
std::vector<size_t> parseRange(const std::string& text, size_t count, std::string* error = nullptr);
/// 0-based pages as a range text, 1-based: {0, 1, 2, 4} → "1-3, 5" (parseRange reads it again).
std::string rangeText(const std::vector<size_t>& pages);

/// A document of `count` pages split into parts of `every` pages (the last one may be shorter).
std::vector<std::vector<size_t>> splitEvery(size_t count, size_t every);
/// A document of `count` pages split before each of `starts` (0-based; page 0 always starts the first part, pages out
/// of the document are left out).
std::vector<std::vector<size_t>> splitAt(size_t count, std::vector<size_t> starts);

/// Copies of these pages of `doc` (0-based, in this order) as a document of their own: their background PDF is the
/// source's file (with the same page numbers), its file path the source's (pictures and recordings are found as
/// there). Pages out of the document are left out.
std::unique_ptr<Document> subset(Document& doc, const std::vector<size_t>& pages);

struct Result {
    bool ok = false;
    std::string error;
};
/// Write `doc` (a subset) to `target`: a PDF with notes for ".pdf" (in full, encrypted as `encryption` says), else a
/// .xopp with its PDF pages in "name.pdf" next to it (DocumentSession::exportPdfFor: or ".name.pages.pdf" when that
/// name is taken). `pdfPageCount`: the pages of its background PDF (the source's getPdfPageCount()). Nothing is left
/// behind when it fails.
Result write(Document& doc, size_t pdfPageCount, const fs::path& target, const PdfEncryption::Encryption& encryption);

/// The file name of page `page` (0-based) exported as a picture: "lecture-p003.png", numbered from 1 with at least
/// three digits (more when the document has more pages), so that the files sort in page order. `extension` with its
/// dot.
std::string imageName(const std::string& stem, size_t page, size_t pageCount, const std::string& extension);

}  // namespace xqt::pagefiles

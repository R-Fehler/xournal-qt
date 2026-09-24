/*
 * xournal-qt: the hybrid PDF (qt/docs/hybrid-pdf.md).
 *
 * One PDF that any PDF app shows as the author sees it, and that also carries the whole Xournal document:
 * - base pages: the pages of the document's background PDF (copied with qpdf, its outline and links kept), or the
 *   generated backgrounds (ruled, graph, images) drawn by cairo;
 * - our drawing as annotations, one per visible layer of a page: /Ink (the strokes in /InkList) when the layer has
 *   strokes, else /Stamp. Their look is the appearance stream /AP, the layer drawn by our cairo renderer as a Form
 *   XObject (exact pressure widths, highlighter blending). Each is marked as ours: /NM "xopp:p<page>-l<layer>"
 *   and a private /XournalQt key;
 * - the document as an embedded file "document.xopp" whose PDF background is this file (page i of the .xopp shows
 *   base page i);
 * - a marker /XournalQt in the catalog: the format version, the name of the embedded data and a hash of each of our
 *   annotations as written (to notice when another app changed, moved or deleted one).
 *
 * Opening one takes the embedded document; its background is a clean copy of the file without our annotations
 * (made with qpdf into the app cache, keyed by the file's size and time), so the drawing is not shown twice.
 * Annotations of other apps stay in it (shown, and kept when saving).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "util/Util.h"  // npos

#include "filesystem.h"

class Document;
class XojPage;

namespace xqt::HybridPdf {

constexpr int FORMAT_VERSION = 1;
/// The name of the embedded Xournal document.
constexpr const char* DATA_NAME = "document.xopp";
/// The /NM of our annotations starts with this.
constexpr const char* NAME_PREFIX = "xopp:";

struct Result {
    bool ok = false;
    std::string error;
    size_t pages = 0;        ///< pages written
    size_t annotations = 0;  ///< our annotations written
};

/// The page of the background PDF a page of the document was when it was opened from a hybrid PDF (npos: none).
using BasePageOf = std::function<size_t(const XojPage*)>;

/// Write the document as a hybrid PDF `target` (atomically: a temporary file next to it, renamed over it). The
/// document is read under its shared lock (the caller must not hold it). `target` may be the document's background
/// PDF. `baseOf`: a page with a generated background keeps the annotations other apps put on that page of the
/// background PDF (the clean copy of the hybrid PDF it was opened from). `pdfPageCount`: the pages of the background
/// PDF, for a document whose PDF is not loaded (a copy of an open document written on a worker; npos: the document's).
/// `xoppExport`: this document keeps a .xopp for Xournal++ there up to date (recorded in the marker, see
/// xoppExportOf; relative to the PDF when it is in its folder or below).
Result write(Document& doc, const fs::path& target, const BasePageOf& baseOf = {}, size_t pdfPageCount = npos,
             const fs::path& xoppExport = {});

/// Export for Xournal++: a plain `xopp` whose background is `pdf`, the document's base pages in document order (the
/// hybrid PDF without our annotations and data). The document keeps its own files.
Result exportXopp(Document& doc, const fs::path& xopp, const fs::path& pdf, size_t pdfPageCount = npos);

/// The .xopp for Xournal++ that this hybrid PDF keeps up to date on every save (write's `xoppExport`; empty: none).
fs::path xoppExportOf(const fs::path& pdf);

/// Whether this PDF carries our marker (remembered by path, size and time).
bool isHybrid(const fs::path& pdf);

struct Opened {
    std::unique_ptr<Document> document;  ///< nullptr: not opened (`error`)
    std::string error;
    std::vector<std::string> warnings;  ///< from loading the embedded document
    /// Our annotations that another app changed, moved or deleted since we wrote them (their /NM).
    std::vector<std::string> changed;
    fs::path base;  ///< the clean copy: the document's background PDF
};
/// Open a hybrid PDF: its embedded document, with the clean copy as background PDF. The document's file path is
/// `pdf`. Works on any thread.
Opened open(const fs::path& pdf);

/// A clean copy of the hybrid PDF `pdf` in which these of our annotations (by /NM) stay, as plain annotations of
/// another app (they lose our mark). Empty on failure (`error`).
fs::path importCopy(const fs::path& pdf, const std::vector<std::string>& keep, std::string& error);

/// The page and layer (0-based) an annotation name of ours stands for.
bool parseName(const std::string& name, size_t& page, size_t& layer);
std::string nameOf(size_t page, size_t layer);

/// Where the clean copies are kept (the app cache).
fs::path cacheFolder();
bool inCache(const fs::path& file);
/// An open document uses this clean copy: it is not removed while retained (in this process). Clean copies of other
/// versions of a file, and those not used for a day, are removed when a file is opened.
void retain(const fs::path& base);
void release(const fs::path& base);
/// Mark a clean copy as used now (it is kept at least a day from now).
void touch(const fs::path& base);

}  // namespace xqt::HybridPdf

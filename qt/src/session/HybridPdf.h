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

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "util/Util.h"  // npos

#include "filesystem.h"

class Document;
class XojPage;

namespace xqt::HybridPdf {

constexpr int FORMAT_VERSION = 1;
/// An archive PDF (writeArchive): its ink is in the page content, not in annotations (older readers must refuse it).
constexpr int ARCHIVE_FORMAT_VERSION = 2;
/// The name of the embedded Xournal document.
constexpr const char* DATA_NAME = "document.xopp";
/// The /NM of our annotations starts with this.
constexpr const char* NAME_PREFIX = "xopp:";

/// A hybrid PDF as this app last wrote it (or as it was when it was opened): what an incremental save builds on
/// (qt/docs/hybrid-pdf.md, "Saving: incremental updates").
struct Revision {
    std::string stamp;  ///< the file's size and time then (another version of the file is written in full)
    /// The page objects of the file (object number, generation) by the page numbers of the document's background PDF
    /// (the clean copy's page k is the file's page k when it was opened); {0, 0}: none.
    std::vector<std::pair<int, int>> pages;
    bool valid() const { return !stamp.empty(); }
};

/// How write() saves an existing hybrid PDF.
struct WriteOptions {
    /// The file as last written or opened: when it is still that file, only what changed is appended (an
    /// incremental update), unless the policy says to write it anew (see compactAbove). nullptr: written in full.
    const Revision* revision = nullptr;
    /// Written anew in full, never appended to (Save as, Share: older revisions may hold deleted ink).
    bool compact = false;
    /// Receives what the file is now (for the next incremental save).
    Revision* written = nullptr;
};

/// An incremental save writes the whole file anew instead when the file would then have grown by more than this
/// share since it was last written in full (and when many pages were added or removed at once).
extern double compactAbove;

struct Result {
    bool ok = false;
    std::string error;
    size_t pages = 0;        ///< pages written
    size_t annotations = 0;  ///< our annotations written
    // --- an existing hybrid PDF saved again
    bool incremental = false;  ///< only the changes were appended
    uint64_t appended = 0;     ///< bytes appended
    std::string whyFull;       ///< why it was written in full although a revision was given (for measuring)
    // --- an archive PDF (writeArchive)
    size_t flattened = 0;             ///< layers merged into the page content
    bool pdfa = false;                ///< it carries the PDF/A-3b identification
    std::vector<std::string> notPdfA;   ///< why not (for the user; empty when pdfa)
    std::vector<std::string> adjusted;  ///< what was changed in the source PDF to conform
};

/// Where the links of an archive written into another folder lead (the library export): links are read from the
/// document's own folder `from`; a linked document that is archived too (`archived` gives its archive PDF, else an
/// empty path) is linked there, at the page of the document.
struct LinkMap {
    fs::path from;
    std::function<fs::path(const fs::path&)> archived;
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
/// `options`: an incremental save (see WriteOptions).
Result write(Document& doc, const fs::path& target, const BasePageOf& baseOf = {}, size_t pdfPageCount = npos,
             const fs::path& xoppExport = {}, const WriteOptions& options = {});

/// Write the document as an archive PDF (PDF/A-3b, ArchivePdf.h): the base pages with the ink merged into their
/// content, links as /Link annotations, the .xopp embedded as the file's source data, the marker (so the app opens it
/// again like a hybrid PDF). The result says whether it is PDF/A and, if not, why. Saving such a file again in the
/// app (write) keeps it an archive PDF.
Result writeArchive(Document& doc, const fs::path& target, const BasePageOf& baseOf = {}, size_t pdfPageCount = npos,
                    const LinkMap& links = {});

/// Export for Xournal++: a plain `xopp` whose background is `pdf`, the document's base pages in document order (the
/// hybrid PDF without our annotations and data). The document keeps its own files. `attached`: `pdf` is upstream's
/// attached PDF of the .xopp ("name.xopp.bg.pdf", referred to as domain "attach", "bg.pdf"; not written when no page
/// shows a PDF page), so Xournal++ opens the two as one document wherever they are put.
Result exportXopp(Document& doc, const fs::path& xopp, const fs::path& pdf, size_t pdfPageCount = npos,
                  bool attached = false);

/// The .xopp for Xournal++ that this hybrid PDF keeps up to date on every save (write's `xoppExport`; empty: none).
fs::path xoppExportOf(const fs::path& pdf);

/// Whether this PDF carries our marker (remembered by path, size and time).
bool isHybrid(const fs::path& pdf);
/// Whether it is an archive PDF (writeArchive; remembered likewise).
bool isArchive(const fs::path& pdf);
/// What our marker in a PDF's catalog says (read with isHybrid, and remembered with it: one read per file version).
/// qpdf reads the trailer, the cross-reference table, the catalog and the marker, not the pages or the embedded files.
struct Marker {
    bool hybrid = false;    ///< it carries our marker: a PDF with notes
    bool archive = false;   ///< an archive PDF (writeArchive)
    /// It carries a "name.md" for other apps (listed in the marker's /Files): a PDF text document (TextDocument.h)
    bool markdown = false;
};
Marker markerOf(const fs::path& pdf);
/// How often a marker was read from a file (not remembered) so far, in this process (tests: nothing read twice).
int markerReads();
/// Whether the file holds earlier revisions (incremental updates, by this app or another): older versions of the ink
/// may still be in it, so it is written anew before it is shared (remembered likewise).
bool hasEarlierRevisions(const fs::path& pdf);

/// Write the file anew in one piece, without its earlier revisions (the same content; qpdf), atomically. For a file
/// shared as it is.
bool compact(const fs::path& pdf, std::string& error);

/// The revision of `pdf` that `cleanCopy` (the background of a document opened from it) was made from, if the file is
/// still that version and was not edited in another app (else an invalid revision: the next save writes it in full).
Revision revisionOf(const fs::path& cleanCopy, const fs::path& pdf);

struct Opened {
    std::unique_ptr<Document> document;  ///< nullptr: not opened (`error`)
    std::string error;
    std::vector<std::string> warnings;  ///< from loading the embedded document
    /// Our annotations that another app changed, moved or deleted since we wrote them (their /NM).
    std::vector<std::string> changed;
    fs::path base;  ///< the clean copy: the document's background PDF
    /// A folder with the pictures a text document carries, under their names ("name.assets/…"; qt/docs/md-images.md)
    fs::path pictures;
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

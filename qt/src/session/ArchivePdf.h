/*
 * xournal-qt: the archive PDF (qt/docs/hybrid-pdf.md, "Archive PDF").
 *
 * A PDF/A-3b file meant for keeping: the ink is merged into the page content (so no viewer can hide or lose it), the
 * links stay /Link annotations, and the whole Xournal document is embedded as the file's source data (a PDF/A-3
 * associated file, /AFRelationship /Source), so xournal-qt opens it again for editing like a hybrid PDF. It is written
 * by HybridPdf::writeArchive; this part makes the assembled PDF conform:
 * - checks what PDF/A-3b forbids and cannot be repaired (fonts of the source PDF that are not embedded, CMYK colours
 *   without a profile, annotations without an appearance, PostScript, ...): the file is written anyway, without the
 *   PDF/A identification, and the report says why. Compliance is never claimed when a check failed;
 * - repairs what can be repaired without changing what the pages show (JavaScript and other actions removed, image
 *   smoothing off, annotation flags, embedded files made associated files, ...);
 * - writes the document information, its XMP metadata (with pdfaid part 3, conformance B when it conforms) and an
 *   sRGB output intent with an embedded ICC profile (Graeme Gill's public-domain sRGB profile, qt/resources/icc).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4  // as upstream's QPdfExport
#endif
#include <qpdf/QPDF.hh>

#include "filesystem.h"

namespace xqt::ArchivePdf {

/// The MIME type of the embedded Xournal document (a gzipped .xopp).
constexpr const char* XOPP_MIME = "application/x-xopp";

struct Report {
    /// The file carries the PDF/A-3b identification (every check passed).
    bool pdfa = false;
    /// Why it is not PDF/A (one line each, for the user).
    std::vector<std::string> problems;
    /// What was changed in the source PDF to conform (one line each; small repairs are not listed).
    std::vector<std::string> adjusted;
    /// Some stream uses a filter PDF/A forbids (LZW): the file must be written with its streams decoded again.
    bool recompress = false;
};

struct Metadata {
    std::string title;   ///< empty: the source PDF's title, else `fallbackTitle`
    std::string fallbackTitle;
};

/// Make `pdf` (assembled: pages, embedded files, marker) conform to PDF/A-3b as far as possible, see above.
Report conform(QPDF& pdf, const Metadata& meta);

/// A PDF that is not an archive PDF (a hybrid PDF, base pages for Xournal++) must not claim PDF/A: the PDF/A
/// identification (pdfaid:part, conformance, amd, rev) is removed from its XMP metadata, the rest of it stays. Whether
/// there was one.
bool dropPdfAClaim(QPDF& pdf);

/// The embedded sRGB ICC profile (bytes).
const std::string& srgbProfile();

/// "name.archive.pdf" next to the document (a .xopp, a PDF with notes, a PDF; "lecture.notes.pdf" → "lecture").
fs::path suggestedFile(const fs::path& document);

}  // namespace xqt::ArchivePdf

/*
 * xournal-qt: the revisions of a PDF (ISO 32000-1, 7.5.6): every incremental update appends objects, a
 * cross-reference section with /Prev and an "%%EOF"; the bytes before stay. So each earlier revision is a prefix of
 * the file: cut after its "%%EOF" and it is the complete file as it was saved then.
 *
 * read() walks the chain of cross-reference sections from the end of the file (classic tables and cross-reference
 * streams, our own updates and other apps'), and finds where each revision ends. It is our own small parser beside
 * IncrementalPdf::readTail: qpdf reads every section but only gives the latest state of each object. A section without
 * an "%%EOF" of its own (the main section of a linearized file, a hybrid-reference file's stream) belongs to the
 * revision that refers to it. Bytes after the last good revision (a damaged tail) are reported, not trusted.
 *
 * The version history of a PDF with notes (qt/docs/hybrid-pdf.md, "Version history") is built on this: a version is
 * one of these prefixes.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "IncrementalPdf.h"
#include "filesystem.h"

class InputSource;
class QPDF;

namespace xqt::PdfRevisions {

struct Revision {
    uint64_t start = 0;      ///< where its bytes begin: the end of the revision before it (0: the first)
    uint64_t end = 0;        ///< after its "%%EOF" and that line's end: the file as saved then is the first `end` bytes
    uint64_t startxref = 0;  ///< its cross-reference section (the one its "startxref" names)
    bool xrefStream = false; ///< that section is a cross-reference stream
    long long size = 0;      ///< its trailer's /Size
    /// The objects its sections define as in use (object number, generation; objects in an object stream: 0)
    std::vector<std::pair<int, int>> objects;
};

struct Chain {
    std::vector<Revision> revisions;  ///< oldest first; empty: none found (`error`)
    uint64_t size = 0;                ///< the file's length
    bool garbage = false;             ///< bytes follow the last good revision (a damaged or unfinished tail)
    std::string error;
    bool ok() const { return !revisions.empty(); }
    /// The end of the last good revision (0: none).
    uint64_t end() const { return revisions.empty() ? 0 : revisions.back().end; }
    /// The revision that ends at `end` (nullptr: none).
    const Revision* at(uint64_t end) const;
};

/// The revisions of `file` (reads the whole file: tens of milliseconds for a 10 MB PDF; never while listing).
Chain read(const fs::path& file);

/// The end of `file` as it was when the revision `r` was the last one (for an update appended after it).
IncrementalPdf::Tail tailOf(const fs::path& file, const Revision& r);

/// The first `end` bytes of `file`, as qpdf reads a file (read when needed, not copied).
std::shared_ptr<InputSource> prefixSource(const fs::path& file, uint64_t end);
/// Open the first `end` bytes of `file` with `q` (as it was then; qpdf reads only what is used).
void open(QPDF& q, const fs::path& file, uint64_t end);

/// The date the revision ending at `end` gives (its document information's /ModDate) as UTC ("2026-10-04T12:30:00Z");
/// empty: none.
std::string dateOf(const fs::path& file, uint64_t end);
/// A PDF date ("D:20261004143000+02'00'") as UTC in ISO 8601 ("2026-10-04T12:30:00Z"); empty when it is none.
std::string isoOfPdfDate(const std::string& pdfDate);

/// Write the first `end` bytes of `file` to `out` (a temporary file renamed over it).
bool extract(const fs::path& file, uint64_t end, const fs::path& out, std::string& error);

}  // namespace xqt::PdfRevisions

/*
 * xournal-qt: annotations of other apps made editable (qt/docs/adopt-annotations.md).
 *
 * A PDF that GoodNotes, Drawboard, Preview, Acrobat, Xodo or Zotero marked up carries their marks as PDF annotations
 * (ink, highlights, text boxes, shapes, notes, stamps). Here they are found (scan), turned into elements of ours
 * (prepare: strokes, highlighter strokes, text boxes, sticky notes, pictures), and a copy of the PDF without them is
 * written, so that the document shows ours instead (DocumentSession::adoptAnnotations takes them, undoably).
 *
 * Coordinates: an element is placed on the PDF page as it is shown (its crop box, its /Rotate applied), top left at
 * 0,0, in points; the caller moves it by the page's offset (its space for notes).
 *
 * Any thread (qpdf and poppler instances of their own).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "model/Element.h"
#include "model/Layer.h"

#include "MergedPdf.h"
#include "filesystem.h"

namespace xqt::adopt {

/// What a PDF has of other apps' annotations.
struct Scan {
    bool ok = false;
    std::string error;
    size_t count = 0;  ///< annotations that would be converted (on the pages asked for)
    /// The app that made them, for the user ("GoodNotes", "Drawboard PDF", "Preview", ...); empty: not known
    std::string app;
    std::map<std::string, size_t> kinds;  ///< by PDF type ("Ink", "Highlight", ...): how many
};

/// Look at the pages `pdfPages` of a PDF (0-based; empty: all) for annotations of other apps.
Scan scan(const fs::path& pdf, const std::vector<size_t>& pdfPages = {});

/// The app named by a PDF's /Producer or /Creator (empty: none known). (Exposed for tests.)
std::string appOfProducer(const std::string& producerAndCreator);

/// The name of the layer the converted marks go into ("From GoodNotes"; "From another app")
std::string layerName(const std::string& app);

/// The converted marks of one PDF page, placed on it as it is shown (see above).
struct PageMarks {
    std::vector<ElementPtr> elements;           ///< for the layer "From <app>", in their order on the page
    std::vector<std::unique_ptr<Layer>> notes;  ///< sticky notes (StickyNote.h), each a layer
    size_t converted = 0;                       ///< annotations of the page that were converted
};

struct Prepared {
    bool ok = false;
    std::string error;
    std::string app;
    size_t converted = 0;               ///< annotations converted (and removed from `copy`)
    size_t kept = 0;                    ///< annotations of other apps left as they were (not convertible)
    std::map<size_t, PageMarks> pages;  ///< by PDF page (0-based)
    fs::path copy;  ///< the PDF without the converted annotations (and their popups); the same pages, numbers
};

/// Convert the annotations of other apps on the pages `pdfPages` (0-based; empty: all) of `pdf`, and write the PDF
/// without them to `copy` (atomically). `mark`: the copy gets this merged-PDF mark (MergedPdf.h; nothing given: it
/// keeps the source's). Nothing is converted: ok, converted 0, no copy written.
Prepared prepare(const fs::path& pdf, const std::vector<size_t>& pdfPages, const fs::path& copy,
                 std::optional<MergedPdf::Kind> mark = std::nullopt);

}  // namespace xqt::adopt

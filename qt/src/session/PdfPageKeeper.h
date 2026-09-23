/*
 * xournal-qt: the merged background PDF of one open document (see MergedPdf.h).
 *
 * Pasted PDF pages from another PDF are appended to the document's merged PDF, which becomes its background. Its
 * page numbers only grow while the document is open, so every page (also one held by undo) keeps its number.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <set>
#include <string>

#include "model/DocumentListener.h"

#include "MergedPdf.h"
#include "filesystem.h"

namespace xqt {

class DocumentSession;

class PdfPageKeeper final: public DocumentListener {
public:
    explicit PdfPageKeeper(DocumentSession& session);
    ~PdfPageKeeper() override;

    /// Add the pages of a PDF in memory to the merged PDF (made from the document's background PDF the first time)
    /// and make it the background. Returns the number of the first added page in it, or npos (`error`).
    size_t add(const std::string& pdf, std::string& error);
    /// The PDF the document annotates for the user: its background PDF, or the one the merged PDF was made from
    /// while that is in the cache (a document not saved yet).
    fs::path annotatedPdf() const;
    /// Remove the merged PDFs this keeper wrote into the cache (the document is closed without being saved; also
    /// done when the keeper goes).
    void discardCached();

    void documentChanged(DocumentChangeType) override {}

private:
    MergedPdf::Kind kindOf(const fs::path& pdf);
    /// Load this PDF as the document's background (the pages keep their numbers).
    bool switchTo(const fs::path& pdf, std::string& error);

    DocumentSession& session;
    fs::path madeFrom;         ///< the user's PDF the merged PDF in the cache was made from
    fs::path knownPath;        ///< kindOf() of this file (same size and time) is `knownKind`
    std::string knownStamp;
    MergedPdf::Kind knownKind = MergedPdf::Kind::None;
    int cacheFiles = 0;
    std::set<fs::path> createdInCache;
};

}  // namespace xqt

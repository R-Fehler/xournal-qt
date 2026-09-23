/*
 * xournal-qt: the merged background PDF of one open document (see MergedPdf.h).
 *
 * Pasted PDF pages from another PDF are appended to the document's merged PDF, which becomes its background. Its
 * page numbers only grow between saves, so every page (also one held by undo) keeps its number.
 *
 * Saving puts the merged PDF next to the .xopp (from the cache, or from another .xopp after "Save as") and drops the
 * PDF pages no page of the document uses any more: the pages are renumbered then. Pages that may come back through
 * undo or redo are renumbered with them; if their PDF page was dropped, it is kept in memory and added again when
 * the page comes back. So is the PDF page of a page whose background was changed, if the change is undone.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <QtGlobal>

#include "model/DocumentListener.h"
#include "model/PageRef.h"

#include "util/Util.h"  // npos

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
    /// Before the document is written to `target`: the merged PDF goes next to it, without the PDF pages that are not
    /// used any more (see above). Problems are logged; the document stays as it is then.
    void beforeSave(const fs::path& target);
    /// Page numbers of the background PDF stay valid while this does not change (a save dropped PDF pages).
    quint64 numbering() const { return numberingNo; }
    /// Remove the merged PDFs this keeper wrote into the cache (the document is closed without being saved; also
    /// done when the keeper goes).
    void discardCached();

    void documentChanged(DocumentChangeType type) override;
    void pageInserted(size_t page) override;
    void pageChanged(size_t page) override;
    void pageSizeChanged(size_t page) override { pageChanged(page); }

private:
    /// PDF pages dropped from the merged PDF while pages that show them may come back (undo, redo)
    struct Limbo {
        std::string pdf;
    };
    /// A page that is or was in the document
    struct Tracked {
        std::weak_ptr<XojPage> page;
        size_t last = npos;           ///< its PDF page (also after its background was changed)
        std::vector<size_t> aliases;  ///< numbers it had before saves renumbered it (undo actions may hold them)
        std::shared_ptr<Limbo> limbo;  ///< its PDF page was dropped: in here
        size_t limboIndex = 0;
    };
    Tracked& track(const PageRef& page);
    void trackAll();
    /// Pages of a limbo come back: its PDF pages are added to the merged PDF again.
    void restore(std::shared_ptr<Limbo> limbo);

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
    std::unordered_map<const XojPage*, Tracked> tracked;
    quint64 numberingNo = 0;
};

}  // namespace xqt

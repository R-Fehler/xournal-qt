/*
 * xournal-qt: the merged background PDF of one open document (see MergedPdf.h).
 *
 * Pasted PDF pages from another PDF are appended to the document's merged PDF, which becomes its background. Its
 * page numbers only grow between saves, so every page (also one held by undo) keeps its number.
 *
 * Pasting writes into a merged PDF in the cache, never next to the document. Saving puts it next to the .xopp
 * (from the cache, or from another .xopp after "Save as") and drops the
 * PDF pages no page of the document uses any more: the pages are renumbered then. Pages that may come back through
 * undo or redo are renumbered with them; if their PDF page was dropped, it is kept in memory and added again when
 * the page comes back. So is the PDF page of a page whose background was changed, if the change is undone.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <QtGlobal>

#include "model/DocumentListener.h"
#include "model/PageRef.h"
#include "pdf/base/XojPdfDocument.h"
#include "pdf/base/XojPdfPage.h"

#include "util/Util.h"  // npos

#include "MergedPdf.h"
#include "filesystem.h"

namespace xqt {

class DocumentSession;

/// One PdfPageKeeper::add() on its way into the merged PDF.
struct PdfMerge {
    std::string pdf;    ///< the pages to add (a PDF in memory)
    size_t pages = 0;   ///< how many
    size_t first = 0;   ///< the number of the first of them in the merged PDF (as expected when it was added)
    XojPdfDocument shown;  ///< `pdf`, loaded: the pages are drawn from it until they are merged
    std::string key;       ///< (the same pages added again)
    // --- startMerge (this thread): what is written
    fs::path bg, target;
    MergedPdf::Kind bgKind = MergedPdf::Kind::None, kind = MergedPdf::Kind::None;
    bool inPlace = false;
    // --- writeMerge (worker)
    MergedPdf::Result result;
    bool loads = false;
};

class PdfPageKeeper final: public DocumentListener {
public:
    explicit PdfPageKeeper(DocumentSession& session);
    ~PdfPageKeeper() override;

    /// Add the pages of a PDF in memory to the merged PDF (made from the document's background PDF the first time),
    /// which becomes the background. Returns the number of the first added page in it at once, or npos (`error`).
    /// The merged PDF only grows, in order, so the numbers are known before it is written: that is done in the
    /// background (a Merge: DocumentSession runs them one after the other, before the saves that wait), and until then
    /// the views draw the pages from the PDF in memory (pendingPage). Pages whose merge fails get their PDF page as an image.
    /// The same pages added again (the same PDF) are not added twice.
    size_t add(const std::string& pdf, std::string& error);

    using Merge = PdfMerge;
    void startMerge(Merge& merge);
    static void writeMerge(Merge& merge);
    /// The document takes the merged PDF (or, if it could not be written, the pages get their PDF page as an image
    /// background; returns the error then).
    std::string finishMerge(Merge& merge);
    /// A PDF page that is being merged (any thread; nullptr: none): the views draw it from the PDF in memory.
    XojPdfPageSPtr pendingPage(size_t number) const;
    /// Tests: called on the worker before a merge is written.
    static std::function<void()> beforeMergeWritten;
    /// The PDF page as an (attached) image background of the page (the fallback when it cannot stay a PDF page).
    static bool toImageBackground(XojPage& page, const XojPdfPage& pdf, double dpi = 200);
    /// The PDF the document annotates for the user: its background PDF, or the one the merged PDF was made from
    /// while that is in the cache (a document not saved yet).
    fs::path annotatedPdf() const;
    /// Before the document is written to `target`: the merged PDF goes next to it, without the PDF pages that are not
    /// used any more (see above). Problems are logged; the document stays as it is then.
    /// In three steps, so that the PDF work runs on a worker while the document stays open for editing (see
    /// DocumentSession's background save): planSave (this thread: what to write), writePlanned (any thread, only
    /// files) and applySave (this thread: the document takes the written PDF, its pages renumbered).
    struct SavePlan {
        bool needed = false;  ///< false: nothing to write, the document stays as it is
        fs::path bg;          ///< the background PDF when planned
        size_t count = 0;     ///< its pages
        MergedPdf::Kind kind = MergedPdf::Kind::None;
        fs::path place, staging;
        fs::path grownFrom;           ///< (PdfPageKeeper::grownFrom when planned)
        bool compact = false;         ///< PDF pages are dropped
        std::vector<size_t> used;     ///< the PDF pages the document shows (kept)
        std::vector<size_t> dropped;  ///< PDF pages of pages that may come back (undo, redo): kept in memory
        // --- written by writePlanned
        bool written = false;  ///< the PDF was written (to writeTo): apply it
        fs::path writeTo;
        bool stage = false;  ///< written under another name (staging), gets its name after the .xopp refers to it
        std::string limbo;   ///< the dropped PDF pages as a PDF (empty: none)
        std::unordered_map<size_t, size_t> renumber;  ///< old -> new
    };
    SavePlan planSave(const fs::path& target);
    static void writePlanned(SavePlan& plan);
    /// False if the document shows PDF pages that the written PDF dropped (they came back meanwhile): plan again.
    bool applySave(SavePlan& plan);
    /// All three at once (on this thread).
    void beforeSave(const fs::path& target);
    /// beforeSave wrote a renumbered PDF under another name, and the .xopp was written referring to it: now it gets
    /// its name (the .xopp is written again then, and finishStaged() removes the other name).
    bool hasStaged() const { return !stagedAs.empty(); }
    /// The name the staged PDF gets.
    const fs::path& stagedName() const { return stagedAs; }
    void commitStaged();
    /// commitStaged in two steps: the file gets its name (any thread: a second link, renamed over the old one), then
    /// the document refers to it by that name (`committed`) or keeps referring to the other name.
    static bool commitFile(const fs::path& staged, const fs::path& name, std::string& error);
    void commitApplied(const fs::path& staged, bool committed);
    void finishStaged();
    /// Where the merged PDF goes when the document is saved as `xopp` (empty: it has none).
    fs::path placeFor(const fs::path& xopp);
    /// Tests: a save stops before this step (1: the PDF is written, 2: the .xopp refers to it under its other name,
    /// 3: the PDF has its name, 4: the .xopp refers to that) when this returns true, as if it crashed there. Step 0
    /// (the merged PDF is about to be written, on the worker) is only a place to wait.
    static std::function<bool(int)> stopSaveAt;
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
    fs::path grownFrom;    ///< the merged PDF in the cache is this file with pages added (same numbers)
    fs::path stagedAs;     ///< the name the staged PDF gets (commitStaged)
    fs::path leftStaging;  ///< the staged file, removed when the .xopp no longer refers to it
    std::unordered_map<const XojPage*, Tracked> tracked;
    quint64 numberingNo = 0;
    /// Merges not finished yet (their pages: from their PDF in memory)
    mutable std::mutex pendingMutex;
    std::vector<std::shared_ptr<Merge>> pending;
    size_t pendingPages = 0;
    /// The first number of pages added before, by their PDF (while the numbering stays)
    std::unordered_map<std::string, size_t> addedAt;
    quint64 addedNumbering = 0;
};

}  // namespace xqt

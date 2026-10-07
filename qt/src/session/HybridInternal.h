/*
 * xournal-qt: the parts of the PDF with notes (HybridPdf.h) that its files share. Not an API: only the HybridPdf files
 * include it.
 *
 * Where what is (qt/docs/hybrid-pdf.md says how it works):
 * - HybridCommon.cpp: small helpers of all of them (writing a QPDF, our annotations and marks, the text layer of the
 *   handwriting on a page, space for notes), and strip(), which takes everything of ours out of a PDF;
 * - HybridPrepare.cpp: prepare(), everything that needs the document (the .xopp, the drawings by cairo);
 * - HybridFullWrite.cpp: assemble(), the file written in full (plain, PDF with notes, archive PDF);
 * - HybridAppend.cpp: openExisting() and the incremental update of an existing file;
 * - HybridMarker.cpp: what both writers put into the file the same way (the marker, the document information, the
 *   text layer of the handwriting, the embedded files), through an ObjectSink;
 * - HybridHistory.cpp: writing the version history (PdfHistory.cpp reads it);
 * - HybridCache.cpp: the clean copies in the app cache;
 * - HybridOpen.cpp: reading the marker, opening, compacting;
 * - HybridPdf.cpp: write(), writeArchive(), exportXopp().
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <QtGlobal>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFObjectHandle.hh>

#include "model/NoteSpace.h"
#include "util/Color.h"

#include "FileIo.h"
#include "HybridPdf.h"
#include "IncrementalPdf.h"
#include "InkTextLayer.h"
#include "PdfBookmarks.h"
#include "PdfEncryption.h"
#include "PdfHistory.h"
#include "TextDocument.h"

class Document;

namespace xqt::HybridPdf::detail {

inline constexpr const char* MARKER = "/XournalQt";  ///< in the catalog, and the private key of our annotations
inline constexpr const char* CLEAN_NAME = "base.pdf";
inline constexpr const char* CHECK_NAME = "changed.txt";
/// The recordings it carries (qt/docs/audio.md)
inline constexpr const char* AUDIO_NAME = "audio";
/// The pictures a text document carries (qt/docs/md-images.md)
inline constexpr const char* PICTURES_NAME = "pictures";
/// The file's page objects the clean copy's pages are
inline constexpr const char* PAGES_NAME = "pages.txt";
/// A protected PDF's pictures and recordings are taken out (by this process): "unpacked-<pid>"
/// (removeProtectedLeftovers)
inline constexpr const char* UNPACKED_PREFIX = "unpacked-";
inline constexpr double MARGIN = 2.0;  ///< around a layer's elements (pt)
/// A base page with space for notes (qt/docs/note-space.md): its boxes as the PDF had them (/MediaBox, /CropBox)
inline constexpr const char* BOXES = "/XournalQtBoxes";

// --- small helpers (HybridCommon.cpp) -------------------------------------------------------------------------------

/// XQT_HYBRID_TIMES=1: the time of each step of writing and opening, on stderr (for measuring).
struct Steps {
    Steps(): on(qEnvironmentVariableIsSet("XQT_HYBRID_TIMES")), last(std::chrono::steady_clock::now()) {}
    void operator()(const char* step) {
        if (on) {
            const auto now = std::chrono::steady_clock::now();
            std::fprintf(stderr, "hybrid-pdf: %-28s %8.1f ms\n", step,
                         std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
        }
    }
    bool on;
    std::chrono::steady_clock::time_point last;
};

using fileio::stampOf;

/// `data` as the file's content, written in place (not atomically: the callers write into a temporary name or a folder
/// of their own, then rename it).
bool writeFile(const fs::path& p, const std::string& data);

/// How an archive PDF is written (PDF/A): never encrypted, at least PDF 1.7, streams with a forbidden filter decoded.
struct ArchiveWrite {
    bool on = false;
    bool recompress = false;
    /// Not an archive: how it is encrypted (nullptr: as the PDF written has it). An archive PDF never is.
    const PdfEncryption::Encryption* encryption = nullptr;
};

/// Written whole or not at all (fileio::AtomicFile); a file of the user's made durable, a cache entry (`sync` None)
/// not. Throws.
void writePdfTo(QPDF& pdf, const fs::path& target, ArchiveWrite archive = {},
                fileio::Sync sync = fileio::Sync::Durable);

std::string pdfDateNow();

/// Whether an annotation is one of ours.
bool isOurs(QPDFObjectHandle annot);

/// The hash of what another app may change of an annotation: its kind, place, ink and colour (not its appearance
/// stream, which some apps write again when they save).
std::string hashOf(QPDFObjectHandle annot);

/// A base page as the PDF has it: the boxes it had before we gave it space for notes. True if it had others.
bool restoreBoxes(QPDFObjectHandle page);
/// Give a base page space for notes: its crop box grows by the amounts (as the page is shown, its /Rotate undone), its
/// media box with it. The content stays as it is, so it lands at the offset, and its text, links and the annotations
/// of other apps stay where they are on it. Starts from the page's own boxes (restoreBoxes).
void setSpace(QPDFObjectHandle page, const NoteSpace& s);

/// Remove the text layer from a page (its stream and its font in the page's resources). Whether it had one.
bool removeInkText(QPDFObjectHandle page);

/// The recordings a marker lists (/Audio: attachment name, then the recording's name in the document, for each).
std::vector<std::pair<std::string, std::string>> audioListOf(QPDFObjectHandle marker);

/// Remove our annotations from every page (except `keep`, which lose our mark), our marker and our embedded files.
/// Returns the /NM of our annotations whose hash differs from the marker's, or that are missing.
std::vector<std::string> strip(QPDF& pdf, const std::set<std::string>& keep = {});

// --- what is written (HybridPrepare.cpp) ----------------------------------------------------------------------------

struct PageSpec {
    double width = 0, height = 0;
    size_t pdfPage = npos;    ///< base page from the background PDF
    size_t drawnPage = npos;  ///< base page drawn by cairo (page of `drawn`; npos: the file has it, see Reuse)
    size_t annotsFrom = npos; ///< a drawn page: the annotations of other apps on this page of the background PDF
    std::string sig;          ///< a drawn page: what it shows (its background as saved, its size)
    NoteSpace space;          ///< a page of the background PDF: its space for notes (qt/docs/note-space.md)
    std::string bookmark;     ///< the title of its bookmark in the outline (qt/docs/bookmarks.md); "": none
};

struct AnnotSpec {
    size_t page = 0, layer = 0;
    size_t drawnPage = npos;                       ///< its appearance (page of `drawn`; npos: the file has it)
    std::string sig;                               ///< what the layer shows (as saved, and the page's size)
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;         ///< its box on the page (page coordinates, y down)
    std::vector<std::vector<double>> strokes;      ///< x, y, x, y, ... (page coordinates)
    Color color{};
    double width = 0;
    std::string text;
};

/// A link of a Markdown box as a PDF /Link (qt/docs/links.md): a web address (/URI), or another PDF at a page (/GoToR).
struct LinkSpec {
    size_t page = 0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< where it is drawn (page coordinates, y down)
    std::string uri;                         ///< a web or mail address, or
    std::string file;                        ///< a PDF, relative to the PDF written
    int destPage = 0;                        ///< its page (0-based)
};

/// The drawings an existing file has already (by their sig): prepare() does not draw them again.
struct Reuse {
    std::set<std::string> layers, backgrounds;
};

struct Prepared {
    fs::path bg;
    std::vector<PageSpec> pages;
    std::vector<AnnotSpec> annots;
    std::vector<LinkSpec> links;
    std::string drawn;  ///< a PDF (cairo): the generated base pages and the appearance of each annotation
    std::string xopp;
    std::vector<std::pair<std::string, std::string>> extras;  ///< files next to the .xopp (attached images)
    std::vector<TextDocument::Attachment> attachments;        ///< files for other apps (a text document's "name.md")
    std::vector<std::vector<InkTextLayer::Word>> inkWords;    ///< per page: its text layer (InkTextLayer.h)
    std::string error;
    PdfEncryption::Encryption encryption;  ///< how the file is encrypted when written in full (WriteOptions)
};

/// What prepare() takes besides the document.
struct PrepareOptions {
    BasePageOf baseOf;           ///< as write()'s
    size_t pdfPageCount = npos;  ///< as write()'s
    /// Upstream's attached PDF of a .xopp is the PDF (exportXopp's `attached`)
    bool attach = false;
    /// The links of Markdown boxes become /Link annotations, relative to the PDF written into this folder; empty: none
    fs::path linkFolder;
    const LinkMap* linkMap = nullptr;  ///< an archive written into another folder (writeArchive's `links`)
    const Reuse* reuse = nullptr;      ///< what the existing file has drawn already (an incremental save)
    /// The recordings' names written into the .xopp instead of theirs (exportXopp)
    const std::map<std::string, std::string>* audioNames = nullptr;
    /// The recognised handwriting per page (WriteOptions::inkText; may be null)
    const std::vector<std::shared_ptr<const ink::PageText>>* inkText = nullptr;
    PdfEncryption::Encryption encryption;  ///< WriteOptions::encryption
};

/// How write() saves the document as `target` (WriteOptions; the links relative to its folder).
PrepareOptions preparing(const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                         const WriteOptions& options);

/// Everything that needs the document, for the PDF `pdfName`, with the files of the write in `work`: under its shared
/// lock (and briefly its lock).
Prepared prepare(Document& doc, const std::string& pdfName, const fs::path& work, const PrepareOptions& options);

// --- the file written in full (HybridFullWrite.cpp) -----------------------------------------------------------------

/// A base page we drew: what it shows (an incremental save keeps it while that stays the same).
void markDrawn(QPDFObjectHandle page, const std::string& sig);
/// The sig of a base page we drew (empty: not one).
std::string drawnSigOf(QPDFObjectHandle page);

/// A link of a Markdown box as our /Link annotation on its base page (a direct dictionary), and its name.
QPDFObjectHandle linkDict(const LinkSpec& l, int number, QPDFObjectHandle pageObj, const Prepared& prep, std::string& nm);

/// The marker's record of a layer: what its drawing shows, the drawing, and its annotation (null in an archive PDF). An
/// incremental save keeps what did not change without reading the pages (see Appending).
QPDFObjectHandle layerRecord(const std::string& sig, QPDFObjectHandle form, QPDFObjectHandle annot);

/// A layer's drawing as a Form XObject of `out`, placed on its base page by its /Matrix (the drawn page is in PDF space,
/// y up; the base page's crop box, its rotation undone), with the box it covers there.
struct Placed {
    QPDFObjectHandle form;
    QPDFObjectHandle::Rectangle rect;
    QPDFMatrix cm;
    QPDFObjectHandle::Rectangle box;  ///< the form's /BBox
};

/// Where a drawing of a page of this size goes on the base page `pageObj` (see Placed), and the box of the layer.
Placed placementOf(QPDFObjectHandle pageObj, const AnnotSpec& a, double w, double h);

/// Our annotation of a layer on its base page (a direct dictionary; the caller adds /M), placed as `p` says, its
/// appearance `form`.
QPDFObjectHandle annotDict(const AnnotSpec& a, QPDFObjectHandle pageObj, double h, const Placed& p, QPDFObjectHandle form);

enum class Mode {
    Plain,    ///< the base pages only (the export for Xournal++)
    Hybrid,   ///< and our annotations, data and marker
    Archive,  ///< and our layers merged into the pages, links, data as the source, marker; PDF/A-3b
};

/// The pages (their places) whose boxes we made larger for space for notes (an incremental save reads only those
/// again).
QPDFObjectHandle spacesList(const Prepared& prep);

/// The document's bookmarks for the outline (qt/docs/bookmarks.md): the base page of each bookmarked page.
std::vector<PdfBookmarks::Entry> bookmarksOf(const Prepared& prep, const std::vector<QPDFObjectHandle>& order);

/// Version history (PdfHistory.h): what our marker says about it after this save.
struct HistoryMark {
    std::vector<PdfHistory::Version> versions;  ///< oldest first; the last one is the version written
    uint64_t start = 0;                         ///< where the revision with this marker begins in the file
};

/// Put the history into our marker (`mark` null: none, the keys go); `stream` makes the /Versions stream.
void putHistory(QPDFObjectHandle marker, const HistoryMark* mark,
                const std::function<QPDFObjectHandle(const std::string&)>& stream);

/// The PDF with the base pages (and, by `mode`, our drawing, data and marker), written to `target`.
/// `writeTo`: written there instead of `target` (whose keywords it keeps; a whole document appended to it).
Result assemble(const Prepared& prep, const fs::path& target, Mode mode, const std::string& xoppExport = {},
                const std::string& title = {}, const HistoryMark* history = nullptr, const fs::path& writeTo = {});

/// What a file written in full is now, for the next incremental save: its pages by the page numbers of the document's
/// background PDF.
Revision revisionAfterFull(const fs::path& target, const Prepared& prep);

// --- what both writers write the same way (HybridMarker.cpp) -------------------------------------------------------

/// Where the objects a write makes go: a QPDF written in full (FullSink), or an incremental update of a file
/// (UpdateSink, which also needs to know what of the file changes). The marker, the text layer of the handwriting and
/// the embedded files are written through it, so both writers write them the same way.
class ObjectSink {
public:
    virtual ~ObjectSink() = default;
    /// A new indirect object with this (direct) value.
    virtual QPDFObjectHandle add(QPDFObjectHandle value) = 0;
    /// A new stream: its dictionary (direct, complete: it may not be changed afterwards) and data.
    virtual QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) = 0;
    /// A new stream of a file's bytes (read when the PDF is written where it can be, else now).
    virtual QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) = 0;
    /// This object of the file is about to be changed.
    virtual void touch(QPDFObjectHandle object) = 0;
};

/// A PDF written in full: new objects of `q`; nothing to touch.
class FullSink final: public ObjectSink {
public:
    explicit FullSink(QPDF& q): q(q) {}
    QPDFObjectHandle add(QPDFObjectHandle value) override;
    QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) override;
    QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) override;
    void touch(QPDFObjectHandle) override {}

private:
    QPDF& q;
};

/// An incremental update (IncrementalPdf::Update): new objects numbered by it, changed ones touched.
class UpdateSink final: public ObjectSink {
public:
    explicit UpdateSink(IncrementalPdf::Update& u): u(u) {}
    QPDFObjectHandle add(QPDFObjectHandle value) override { return u.add(value); }
    QPDFObjectHandle addStream(QPDFObjectHandle dict, const std::string& data) override {
        return u.addStream(dict, data);
    }
    QPDFObjectHandle addFileStream(QPDFObjectHandle dict, const fs::path& file) override;
    void touch(QPDFObjectHandle object) override { u.touch(object); }

private:
    IncrementalPdf::Update& u;
};

/// The name tree of the embedded files, and what holds it, are about to change.
void touchNames(ObjectSink& sink, QPDFObjectHandle root);

/// The embedded document as a new file specification in the name tree (whose nodes are touched here): after an older
/// version's .xopp became a delta (version history), and in a version cut out of the file.
void addDataSpec(QPDF& q, ObjectSink& sink, const std::string& name, const std::string& xopp);

/// The files a PDF with notes carries, as the marker lists them.
struct Embedded {
    std::string dataName = DATA_NAME;                              ///< /Data: the name of the embedded document
    QPDFObjectHandle files = QPDFObjectHandle::newArray();          ///< /Files: the files next to it, for other apps
    QPDFObjectHandle audio = QPDFObjectHandle::newArray();          ///< /Audio: the recordings (name, source)
};

/// The embedded files of `prep` into `q`: the .xopp, the files next to it (attached images), the recordings and the
/// files for other apps (an archive PDF: as associated files, PDF/A-3). Written in full (`had` null): each one new.
/// Saved again (`had`: the file's marker as it was): what did not change stays, the .xopp and changed files get new
/// streams in their file specifications, new pictures and recordings are added, recordings renamed; what an
/// incremental update cannot do (other background images, a recording removed, a new file of an archive PDF) throws,
/// and the file is written in full instead. `dataMayBeMissing`: the file's last .xopp became a delta (version
/// history): it is added again.
Embedded embedFiles(ObjectSink& sink, QPDF& q, const Prepared& prep, bool archive, const QPDFObjectHandle* had,
                    bool dataMayBeMissing = false);

/// The handwriting as invisible text on the base pages `order`, for other PDF viewers (InkTextLayer.h): a page whose
/// text layer `keep`s (by its page and sig; null: none) is not touched; the others lose theirs and get their words'
/// (the font made once, in `font`, which may hold the file's). Returns the marker's /InkText (a sig per page).
QPDFObjectHandle writeInkText(ObjectSink& sink, const Prepared& prep, const std::vector<QPDFObjectHandle>& order,
                              QPDFObjectHandle& font, const std::function<bool(size_t, const std::string&)>& keep);

/// What the marker says (qt/docs/hybrid-pdf.md, "The marker").
struct MarkerContent {
    bool archive = false;
    Embedded files;                                             ///< /Data, /Files, /Audio
    QPDFObjectHandle inkText = QPDFObjectHandle::newNull();     ///< /InkText (writeInkText)
    QPDFObjectHandle inkFont = QPDFObjectHandle::newNull();     ///< /InkFont (only an indirect one)
    QPDFObjectHandle annots = QPDFObjectHandle::newNull();      ///< /Annots: our annotations' hashes by name
    QPDFObjectHandle flattened = QPDFObjectHandle::newNull();   ///< /Flattened: an archive PDF's layers
    QPDFObjectHandle layers = QPDFObjectHandle::newNull();      ///< /Layers: the record of our layers
    std::string xoppExport;                                     ///< /XoppExport ("": none)
    const HistoryMark* history = nullptr;                       ///< /History, /Versions
    /// An incremental update: the file's size when it was last written in full and the updates since (/Base,
    /// /Updates); 0: none (written in full)
    long long base = 0;
    long long updates = 0;
};

/// The marker as `c` says (a new dictionary, or the file's marker, touched by the caller); what it does not say goes
/// (/Audio, /XoppExport, the history, the delta of the version before). /Drawn and /Spaces from `prep`.
void writeMarker(ObjectSink& sink, QPDFObjectHandle marker, const Prepared& prep, const MarkerContent& c);

/// The document information: /Producer, and (`modDate`) /ModDate now. Returns it.
QPDFObjectHandle writeInfo(ObjectSink& sink, QPDF& q, bool modDate);

// --- saving again: an incremental update (HybridAppend.cpp) ---------------------------------------------------------

/// The existing hybrid PDF an incremental save appends to (opened before the document is prepared: what it has
/// already need not be drawn again).
struct Existing {
    std::unique_ptr<QPDF> q = std::make_unique<QPDF>();
    IncrementalPdf::Tail tail;
    std::unique_ptr<IncrementalPdf::Update> update;
    QPDFObjectHandle root, pagesRoot, marker;
    std::vector<QPDFObjectHandle> tree;  ///< its pages, in order
    std::map<std::string, std::vector<QPDFObjectHandle>> drawnBySig;  ///< base pages we drew, by what they show
    std::map<std::string, QPDFObjectHandle> formBySig;                ///< drawings of layers, by what they show
    std::set<QPDFObjGen> ours;  ///< the pages our annotations or layers are on (by the marker's names)
    /// The marker's record of our layers (name -> what it shows, its record): pages whose layers are as recorded are
    /// not read at all
    struct Recorded {
        std::string sig;
        QPDFObjectHandle record;
    };
    std::map<std::string, Recorded> layers;
    Reuse reuse;
    long long base = 0;     ///< its size when it was last written in full
    long long updates = 0;  ///< incremental updates since
};

/// Open `target` for an incremental update from `rev` (nullptr and `why`: it cannot be appended to).
std::unique_ptr<Existing> openExisting(const fs::path& target, const Revision& rev, bool archive, std::string& why);

/// One incremental save: what changed of the document `prep` is put into the existing file `e`, then appended to
/// `target`. Not ok without an error, and `why`: the policy wants the whole file written anew.
Result appendChanges(Existing& e, const Prepared& prep, bool archive, const Revision& rev, const HistoryMark* history,
                     const std::string& xoppExport, const fs::path& target, Revision* written, std::string& why);

/// The dictionary `to` becomes `from` (the same object, written again).
void replaceAll(QPDFObjectHandle to, QPDFObjectHandle from);


// --- the clean copies (HybridCache.cpp) -----------------------------------------------------------------------------

/// The folder of the clean copy of this version of a file.
fs::path entryOf(const fs::path& pdf, const std::string& stamp);
/// Remove clean copies of other versions of this file, and those not used for a day (unless retained).
void prune(const fs::path& keep);
std::string pagesText(const std::vector<QPDFObjectHandle>& pages);
/// After an incremental update that kept the base pages as they were (the same pages in the same order), the clean
/// copy of the previous version is the clean copy of this one too: its cache entry gets it (a hard link), with the new
/// embedded document, so opening the file again does not make it again.
void keepCleanCopy(const fs::path& target, const std::string& was, const Prepared& prep,
                   const std::vector<QPDFObjectHandle>& pages);
/// The file changed in a way that keeps its clean copy (only the marker): the cache entry of the version `was` serves
/// the version it is now.
void keepCacheEntry(const fs::path& pdf, const std::string& was);

// --- version history (HybridHistory.cpp) ----------------------------------------------------------------------------

/// A save with version history on (WriteOptions::history).
Result writeKeeping(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const std::string& exportName, const WriteOptions& options, const fs::path& work);

// --- HybridPdf.cpp ----------------------------------------------------------------------------------------------------

/// The title of a PDF without one: its file name without ".pdf", ".notes.pdf", ".archive.pdf".
std::string titleOf(const fs::path& pdf);

}  // namespace xqt::HybridPdf::detail

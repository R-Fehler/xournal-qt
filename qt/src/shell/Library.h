/*
 * xournal-qt: a library, the folder of documents a window works in (like a workspace).
 *
 * A library is a plain folder with PDFs and .xopp files, in subfolders if wanted. By default it is
 * "<Documents>/Xournal_Libraries/Default"; any folder can be opened as one. Each process shows one library.
 * Metadata that only speeds things up (first-page previews, the search index) is kept per folder, in its hidden
 * folder ".xournal_library" or in the app cache (see LibraryCache.h); it can be deleted at any time.
 *
 * LibraryIndex keeps the text of every document for library-wide search, in two parts: the text of the PDF pages
 * (tied to the PDF the document uses and its size / modification time) and, per page, which PDF page it shows and
 * the text of its text elements. When only the .xopp changed (annotations, text elements, pages moved), it is read
 * again but the PDF text is kept; PDF text is read only for PDF pages not seen before, or when the PDF changed.
 * Documents renamed or moved by the app keep their entries; moved by another program, they are found again by
 * name, size and time. Everything happens in the background. Each folder stores the entries of its documents, by
 * file name, in two packs: "notes" (small, written again when a .xopp is saved) and "pdf-text" (big, written when
 * a PDF changed). Opening a library reads the packs of all its folders and merges them.
 *
 * A Markdown file's entry (in "notes") has the text of its passages (headings, paragraphs, list items, table rows,
 * code blocks; read through md4c, without the Markdown syntax; the start of a huge file, see MarkdownFile.h), which
 * of them are headings (for the heading path of a hit), and its links and wiki links. An image's entry has its name
 * only.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <QCborMap>
#include <QCborValue>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "filesystem.h"
#include "DocumentFiles.h"
#include "LibraryCache.h"

class Document;
class QThreadPool;

namespace xqt {

class Library {
public:
    explicit Library(const fs::path& root);

    /// "<Documents>/Xournal_Libraries"
    static fs::path librariesFolder();
    static fs::path defaultRoot();
    /// The user's Downloads folder: can be opened as a (quick) library, but its files are short-lived.
    static fs::path downloadsFolder();

    const fs::path& root() const { return rootDir; }
    QString name() const;
    bool isDefault() const;
    /// The Downloads folder or a folder in it: files there are often cleaned up (the UI warns before importing).
    bool isTemporary() const;
    /// Short hash of the root (one instance and one session journal per library).
    std::string key() const;
    /// The library's own state in the config folder, "~/.config/xournal-qt/libraries/<key>/" (created when
    /// needed): what is not a cache and must survive cleaning it, e.g. the reading positions.
    fs::path configDir() const;
    /// Where the library keeps its cache: in its folders (the default) or in the app's cache folder (for folders
    /// that sync clients upload). A setting of the library, kept in its config folder.
    CacheLocation::Mode cacheMode() const;
    void setCacheMode(CacheLocation::Mode mode) const;
    CacheLocation cacheLocation() const { return CacheLocation(rootDir, cacheMode()); }
    /// The reading positions (DocumentPlaces) of its documents. The ones kept in the library's
    /// ".xournal_library/pages.json" before are taken over the first time.
    fs::path placesFile() const;
    /// The file or folder is in the library.
    bool contains(const fs::path& p) const;
    /// Path relative to the root ("" for the root itself).
    std::string relative(const fs::path& p) const;

private:
    fs::path rootDir;
};

/// A string that changes when one of the document's files changes (size, modification time): the .xopp, the PDF next
/// to it, an attached PDF, the merged PDF of pasted pages (".name.pages.pdf").
QString documentStamp(const DocumentItem& item);
/// Size and modification time of one file ("" if it does not exist).
QString fileStamp(const fs::path& file);

class LibraryIndex final: public QObject {
    Q_OBJECT
public:
    /// The index of the library at `root`, kept in the cache folders of `location` (default: in each folder).
    explicit LibraryIndex(fs::path root, CacheLocation location = {}, QObject* parent = nullptr);
    /// Writes what is not written yet.
    ~LibraryIndex() override;

    /// Bring the index up to date with these documents, in the background: the stored packs of their folders are
    /// read (once), new and changed documents are read, documents that are gone are dropped.
    void update(std::vector<DocumentItem> items);
    bool busy() const { return running.load(); }
    int indexed() const { return doneCount.load(); }
    int total() const { return totalCount.load(); }
    /// Wait until the background work is done (tests).
    void waitForDone();
    /// Files and folders were renamed or moved by the app (old, new): the entries of the documents follow (their
    /// text did not change): nothing is read again, except a .xopp that was written again (a renamed pair: the new
    /// path of its PDF), without its PDF text. In the background, before the next update.
    void moved(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Write the changed packs now (else a few seconds after the last change, and when the index is closed).
    void flush();
    /// Stop: forget what is not written yet, and write or index nothing any more (its cache is being removed).
    void discard();

    /// The cache folder has files of the layout before the packs ("index/", "previews/", "pages.json").
    static bool hasOldLayout(const fs::path& dir);
    /// Convert them, in the background before the next update: the index entries (one JSON file per document)
    /// and the previews (PNG files) go into the packs of the documents' folders, nothing is read again. Once the
    /// packs are written, the old files are removed ("index/", "previews/", "pages.json" - which the library
    /// took over into the config folder when it was opened -, nothing else).
    void convertOldLayout(const fs::path& dir);
    /// Old caches converted so far (tests).
    int oldLayoutsConverted() const { return conversions.load(); }
    /// How long writes wait for more changes (tests; default: WriteScheduler's).
    void setWriteDelays(int quietMs, int maxDelayMs);
    const CacheLocation& location() const { return where; }
    /// Work done so far (tests): documents read, PDF pages whose text was read, packs written.
    int documentsRead() const { return docsRead.load(); }
    int pdfPagesRead() const { return pdfRead.load(); }
    int packsWritten() const { return packWrites.load(); }

    struct PageHits {
        int page = 0;        ///< 0-based
        int count = 0;       ///< matches on the page
        double aspect = 0;   ///< height / width of the page (0: unknown)
    };
    /// A passage of a Markdown file with matches (MdPassages.h).
    struct BlockHits {
        int block = 0;       ///< the passage (0-based, in the order of md::passages)
        int count = 0;       ///< matches in it
        QString headings;    ///< the headings above it: "Lecture 3 › Kalman filter › Prediction"
    };
    struct Hit {
        fs::path file;       ///< the document's main file
        int count = 0;       ///< matches in the text
        int pages = 0;       ///< pages with matches (a Markdown file: passages)
        int firstPage = -1;  ///< first page with a match (0-based)
        bool inName = false;
        QString snippet;     ///< text around the first match
        std::vector<PageHits> pageHits;  ///< the pages with matches, in order
        std::vector<BlockHits> blockHits;  ///< a Markdown file: the passages with matches, in order
    };
    /// The text of the pages of this PDF read before (by PDF page, 0-based), if it was read from the file as it is now
    /// (same size and time): an open document takes it for its search instead of reading it again.
    std::map<int, QString> knownPdfText(const fs::path& pdf) const;
    /// A document open in the app was saved: its entry is made from the document in memory and the PDF text the app
    /// knows (by PDF page), instead of reading the file again. False if that is not possible (not in the library, its
    /// folder's packs not read yet, PDF text missing): the next update reads it as usual.
    bool documentSaved(const fs::path& file, Document& doc, const std::map<int, QString>& pdfText);
    /// Entries taken over from saved documents so far (tests).
    int savedTakenOver() const { return handedOver.load(); }

    /// Search the text and the names of all indexed documents (case-insensitive, whitespace-insensitive).
    std::vector<Hit> search(const QString& query) const;
    /// Pages of an indexed document (-1: not indexed yet).
    int pageCount(const fs::path& file) const;

    /// Format of the stored entries (packs of another one are read anew).
    static constexpr int FORMAT = 4;
    /// The packs of a folder's cache
    static const QString NOTES_PACK;     ///< per document: its pages, the text of its text elements, its PDF
    static const QString PDF_TEXT_PACK;  ///< per document: the text of the PDF pages it shows
    /// Whitespace runs to one space (the PDF text has line breaks where the page has them).
    static QString simplified(const QString& text);

Q_SIGNALS:
    /// Documents were indexed (done / total changed, the busy state).
    void progress();

private:
    struct Entry {
        fs::path file;                   ///< the document's main file
        QString kind;                    ///< "xopp" (also .xoj), "pdf", "md", "image"
        QString name;
        QString xoppStamp;               ///< of the .xopp, the Markdown file, the image alone ("": a PDF alone)
        fs::path pdf;                    ///< the PDF it uses (next to it, elsewhere, attached; "": none)
        QString pdfStamp;
        std::map<int, QString> pdfText;  ///< simplified text of the PDF pages it shows
        std::vector<int> pdfPage;        ///< per page: the PDF page it shows (-1: none)
        QStringList elementText;         ///< per page: the text of its text elements (simplified)
        std::vector<double> aspects;     ///< per page: height / width
        // A Markdown file (no pages): its text, read through md4c without the syntax
        QStringList blockText;           ///< per passage (MdPassages.h): its text (simplified)
        std::vector<int> blockLevel;     ///< per passage: a heading's level (0: not a heading)
        QStringList links;               ///< link targets (for backlinks)
        QStringList wikiLinks;           ///< [[wiki link]] targets
        int pageCount() const { return static_cast<int>(elementText.size()); }
        bool showsPdfPages() const;
        /// Nothing changed since it was read.
        bool upToDate(const DocumentItem& item) const;
    };
    using EntryPtr = std::shared_ptr<const Entry>;
    /// The documents directly in one folder, as in its packs.
    struct Folder {
        bool loaded = false;                   ///< its packs were read
        std::map<std::string, EntryPtr> docs;  ///< by file name
        bool notesChanged = false, textChanged = false;
        std::set<QString> changedText;         ///< documents whose PDF text changed (big ones have a file of their own)
    };

    void run(std::vector<DocumentItem> items, quint64 generation);
    void applyMoves(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Read a document; PDF text is taken from `previous` or another entry with the same PDF where possible.
    std::shared_ptr<Entry> read(const DocumentItem& item, const EntryPtr& previous);
    /// An entry with the PDF text of `e`'s PDF (the same size and time): `previous`, else any (the lock is not held).
    EntryPtr donorFor(const Entry& e, const EntryPtr& previous) const;
    /// The pages of a document (locked by the caller) into `e`; PDF text from `donor`, else read (`readMissing`) or
    /// give up (false).
    bool fillPages(Entry& e, Document& doc, const EntryPtr& donor, bool readMissing);
    /// Read the packs of a folder, if not done yet (the lock is not held).
    void load(const fs::path& folder);
    /// The entry of a document (the lock is held).
    EntryPtr find(const fs::path& file) const;
    /// Set or drop the entry of a document; its folder's packs are written later (the lock is held).
    void put(const EntryPtr& e);
    void erase(const fs::path& file);
    /// An entry of a file that is gone, with this name and the same files (moved by another program).
    EntryPtr movedHere(const DocumentItem& item, std::multimap<std::string, EntryPtr>& orphans, bool& collected);
    /// Returns whether everything could be written.
    bool writeChanged();
    /// Remove `dir` and its parents while they are empty folders in the library's folder in the app cache.
    void removeEmptyMirrors(fs::path dir) const;
    void convert(const fs::path& dir);
    QCborMap notesOf(const Entry& e) const;
    std::shared_ptr<Entry> entryOf(const fs::path& folder, const QString& name, const QCborMap& notes,
                                   const QCborValue& text) const;

    fs::path rootDir;
    CacheLocation where;
    std::unique_ptr<QThreadPool> pool;    ///< reading, one document after the other (also orders moves and updates)
    std::unique_ptr<QThreadPool> writer;  ///< writing packs (while reading goes on)
    std::unique_ptr<WriteScheduler> scheduler;
    mutable std::mutex mtx;
    std::mutex writeMtx;
    std::map<fs::path, Folder> folders;  ///< by folder
    bool firstRun = true;                ///< (the worker's) the stored packs of all folders are read once
    std::atomic<quint64> generation{0};
    std::atomic<bool> running{false};
    std::atomic<bool> discarded{false};
    std::atomic<int> doneCount{0}, totalCount{0};
    std::atomic<int> docsRead{0}, pdfRead{0}, packWrites{0}, conversions{0}, handedOver{0};
};

}  // namespace xqt

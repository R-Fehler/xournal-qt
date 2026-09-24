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
 * Documents renamed or moved by the app keep their entries; moved or renamed by another program (or seen under
 * their new names before the app told the index about the move), they are found again by size and time, with the
 * same name or the same content (a sample of the file). Everything happens in the background. Each folder stores the entries of its documents, by
 * file name, in two packs: "notes" (small, written again when a .xopp is saved) and "pdf-text" (big, written when
 * a PDF changed). Opening a library reads the packs of all its folders and merges them.
 *
 * A Markdown file's entry (in "notes") has the text of its passages (headings, paragraphs, list items, table rows,
 * code blocks; read through md4c, without the Markdown syntax; the start of a huge file, see MarkdownFile.h), which
 * of them are headings (for the heading path of a hit), and its links and wiki links. An image's entry has its name
 * only. A text or code file's entry (kind "text") has its text, if the file is not bigger than TEXT_LIMIT (else its
 * name only). Other files are not in the index.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
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
#include "session/Vocabulary.h"

class Document;
class QThreadPool;

namespace xqt {

class FuzzyQuery;

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
    /// In the standard folder of libraries ("<Documents>/Xournal_Libraries", any depth).
    bool isInLibrariesFolder() const;
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
    /// Which kinds of files the library shows ("Show" in the library; the defaults when never set). Kept in its config
    /// folder, next to the cache mode.
    ShowFilter showFilter() const;
    void setShowFilter(const ShowFilter& filter) const;
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
    /// Called on the worker for each document of an update once it was found on disk, before its entry is looked at
    /// (tests: a move that lands just then). Set it while the index is idle.
    void setCheckHook(std::function<void(const fs::path&)> hook) { checkHook = std::move(hook); }

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
        int nameScore = 0;               ///< fuzzy search: fzf's score of the name and folder path (0: not in them)
        std::vector<int> nameMarks;      ///< fuzzy search: the characters of the name that matched
        bool fuzzyOnly = false;          ///< fuzzy search: its hits in the text are all words that only match
                                         ///< fuzzily (WordMatch.h: ranked after documents with exact ones)
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
    /// The fuzzy search (FuzzyQuery.h; one that is not valid: the plain search of its text). A document is a hit when
    /// the expression holds with each term found in its name or folder path (fzf's matching, relative to the library)
    /// or in its text (TextMatch). Its count is the hits of the terms that are not negated; its pages (passages of a
    /// Markdown file) are those with hits on which the expression holds, a term counting as found on a page when the
    /// page, the name or the folder path has it - or, if it holds on none, all pages with hits. Fuzzy terms match
    /// words (WordMatch.h), counted from the vocabularies of the pages (made at the first such search, kept until the
    /// document changes). Ordered by the score of the name, then documents with exact hits in the text before those
    /// whose words only match fuzzily, then the count.
    std::vector<Hit> search(const FuzzyQuery& query) const;
    /// Memory of the kept vocabularies (tests, measurements).
    size_t vocabularyBytes() const;
    /// Make the vocabularies of all documents in the background (the fuzzy search is on: its first search does not
    /// wait for them). Documents that have them are skipped.
    void prepareWords();
    /// Pages of an indexed document (-1: not indexed yet).
    int pageCount(const fs::path& file) const;
    /// The indexed documents whose file name is `name`, case ignored (links whose path is gone, wiki links). With
    /// `withoutExtension`, `name` has no extension ("turbines" finds "turbines.md", "Turbines.xopp").
    std::vector<fs::path> filesNamed(const QString& name, bool withoutExtension = false) const;

    /// Format of the stored entries (packs of another one are read anew).
    static constexpr int FORMAT = 4;
    /// Text files up to this size are indexed with their text, bigger ones by name only.
    static constexpr qint64 TEXT_LIMIT = 1024 * 1024;
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
        QString kind;                    ///< "xopp" (also .xoj), "pdf", "md", "image", "text"
        QString name;
        QString xoppStamp;               ///< of the .xopp, the Markdown file, the image alone ("": a PDF alone)
        fs::path pdf;                    ///< the PDF it uses (next to it, elsewhere, attached; "": none)
        QString pdfStamp;
        QString sample;                  ///< a hash of the start and end of its main file ("": not known, entries
                                         ///< of older versions): tells two files with the same size and time apart
        std::map<int, QString> pdfText;  ///< simplified text of the PDF pages it shows
        std::vector<int> pdfPage;        ///< per page: the PDF page it shows (-1: none)
        QStringList elementText;         ///< per page: the text of its text elements (simplified)
        std::vector<double> aspects;     ///< per page: height / width
        // A Markdown file (no pages): its text, read through md4c without the syntax; a text file: its text, one block
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
    /// The vocabularies of an entry: per passage (a Markdown or text file), else per page (Vocabulary.h).
    struct EntryWords {
        std::vector<words::Vocabulary> units;
    };
    /// Of these entries: kept ones, the others made now (then only these are kept).
    std::vector<std::shared_ptr<const EntryWords>> wordsOf(const std::vector<EntryPtr>& entries) const;
    /// The documents directly in one folder, as in its packs.
    struct Folder {
        bool loaded = false;                   ///< its packs were read
        std::map<std::string, EntryPtr> docs;  ///< by file name
        bool notesChanged = false, textChanged = false;
        std::set<QString> changedText;         ///< documents whose PDF text changed (big ones have a file of their own)
    };

    void run(std::vector<DocumentItem> items, quint64 generation);
    void applyMoves(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Read a document; PDF text is taken from `previous` or another entry with the same PDF where possible. Null
    /// when it could not be read because it is gone (moved meanwhile: its entry stays for the move).
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
    /// An entry of a file that is gone whose files have the same size and time as this document's, taken over under
    /// its path (moved or renamed by another program, or by the app before the index was told): one with the same
    /// name, else one with the same sample (without a sample: only by name). Orphans by kind and stamp.
    EntryPtr movedHere(const DocumentItem& item, std::multimap<QString, EntryPtr>& orphans, bool& collected);
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
    mutable std::mutex wordsMtx;
    std::atomic<bool> wordsQueued{false};
    /// The vocabularies of the entries searched last (with the entry, so an entry gone is not taken for a new one)
    mutable std::unordered_map<const Entry*, std::pair<EntryPtr, std::shared_ptr<const EntryWords>>> wordCache;
    std::map<fs::path, Folder> folders;  ///< by folder
    bool firstRun = true;                ///< (the worker's) the stored packs of all folders are read once
    std::atomic<quint64> generation{0};
    std::atomic<bool> running{false};
    std::atomic<bool> discarded{false};
    std::atomic<int> doneCount{0}, totalCount{0};
    std::atomic<int> docsRead{0}, pdfRead{0}, packWrites{0}, conversions{0}, handedOver{0};
    std::function<void(const fs::path&)> checkHook;
};

}  // namespace xqt

/*
 * xournal-qt: a library, the folder of documents a window works in (like a workspace).
 *
 * A library is a plain folder with PDFs and .xopp files, in subfolders if wanted. By default it is
 * "<Documents>/Xournal_Libraries/Default"; any folder can be opened as one. Each process shows one library.
 * Metadata that only speeds things up (first-page previews, the search index) lives in the hidden folder
 * ".xournal_library" of the library; it can be deleted at any time.
 *
 * LibraryIndex keeps the text of every document for library-wide search, in two parts: the text of the PDF pages
 * (tied to the PDF the document uses and its size / modification time) and, per page, which PDF page it shows and
 * the text of its text elements. When only the .xopp changed (annotations, text elements, pages moved), it is read
 * again but the PDF text is kept; PDF text is read only for PDF pages not seen before, or when the PDF changed.
 * Documents renamed or moved by the app keep their entries. Everything happens in the background; the text is
 * stored as one JSON file per document.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include "filesystem.h"
#include "DocumentFiles.h"

class Document;
class QThreadPool;

namespace xqt {

class Library {
public:
    explicit Library(const fs::path& root);

    /// "<Documents>/Xournal_Libraries"
    static fs::path librariesFolder();
    static fs::path defaultRoot();

    const fs::path& root() const { return rootDir; }
    QString name() const;
    bool isDefault() const;
    /// Short hash of the root (one instance and one session journal per library).
    std::string key() const;
    /// The metadata folder (created when needed). For folders that cannot be written: in the user's cache.
    fs::path metaDir() const;
    /// The file or folder is in the library.
    bool contains(const fs::path& p) const;
    /// Path relative to the root ("" for the root itself).
    std::string relative(const fs::path& p) const;

private:
    fs::path rootDir;
};

/// A string that changes when one of the document's files changes (size, modification time): the .xopp, the PDF next
/// to it, an attached PDF.
QString documentStamp(const DocumentItem& item);
/// Size and modification time of one file ("" if it does not exist).
QString fileStamp(const fs::path& file);

class LibraryIndex final: public QObject {
    Q_OBJECT
public:
    /// `dir`: where the index files are stored.
    LibraryIndex(fs::path root, fs::path dir, QObject* parent = nullptr);
    ~LibraryIndex() override;

    /// Bring the index up to date with these documents, in the background: new and changed documents are read,
    /// documents that are gone are dropped.
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
    /// Work done so far (tests): documents read, PDF pages whose text was read.
    int documentsRead() const { return docsRead.load(); }
    int pdfPagesRead() const { return pdfRead.load(); }

    struct PageHits {
        int page = 0;        ///< 0-based
        int count = 0;       ///< matches on the page
        double aspect = 0;   ///< height / width of the page (0: unknown)
    };
    struct Hit {
        fs::path file;       ///< the document's main file
        int count = 0;       ///< matches in the text
        int pages = 0;       ///< pages with matches
        int firstPage = -1;  ///< first page with a match (0-based)
        bool inName = false;
        QString snippet;     ///< text around the first match
        std::vector<PageHits> pageHits;  ///< the pages with matches, in order
    };
    /// Search the text and the names of all indexed documents (case-insensitive, whitespace-insensitive).
    std::vector<Hit> search(const QString& query) const;
    /// Pages of an indexed document (-1: not indexed yet).
    int pageCount(const fs::path& file) const;

    /// Format of the stored index files (older ones are indexed again).
    static constexpr int FORMAT = 3;
    /// Whitespace runs to one space (the PDF text has line breaks where the page has them).
    static QString simplified(const QString& text);

Q_SIGNALS:
    /// Documents were indexed (done / total changed, the busy state).
    void progress();

private:
    struct Entry {
        fs::path file;                   ///< the document's main file
        QString name;
        QString xoppStamp;               ///< of the .xopp ("": a PDF alone)
        fs::path pdf;                    ///< the PDF it uses (next to it, elsewhere, attached; "": none)
        QString pdfStamp;
        std::map<int, QString> pdfText;  ///< simplified text of the PDF pages it shows
        std::vector<int> pdfPage;        ///< per page: the PDF page it shows (-1: none)
        QStringList elementText;         ///< per page: the text of its text elements (simplified)
        std::vector<double> aspects;     ///< per page: height / width
        int pageCount() const { return static_cast<int>(elementText.size()); }
        /// Nothing changed since it was read.
        bool upToDate(const DocumentItem& item) const;
    };
    void run(std::vector<DocumentItem> items, quint64 generation);
    void applyMoves(const std::vector<std::pair<fs::path, fs::path>>& moves);
    /// Read a document; PDF text is taken from `previous` or another entry with the same PDF where possible.
    std::shared_ptr<Entry> read(const DocumentItem& item, const std::shared_ptr<const Entry>& previous);
    std::shared_ptr<const Entry> loadStored(const fs::path& file) const;
    void store(const Entry& e) const;
    fs::path indexFile(const fs::path& file) const;

    fs::path rootDir, indexDir;
    std::unique_ptr<QThreadPool> pool;
    mutable std::mutex mtx;
    std::map<fs::path, std::shared_ptr<const Entry>> entries;  ///< by main file
    std::atomic<quint64> generation{0};
    std::atomic<bool> running{false};
    std::atomic<int> doneCount{0}, totalCount{0};
    std::atomic<int> docsRead{0}, pdfRead{0};
};

}  // namespace xqt

/*
 * xournal-qt: a library, the folder of documents a window works in (like a workspace).
 *
 * A library is a plain folder with PDFs and .xopp files, in subfolders if wanted. By default it is
 * "<Documents>/Xournal_Libraries/Default"; any folder can be opened as one. Each process shows one library.
 * Metadata that only speeds things up (first-page previews, the search index) lives in the hidden folder
 * ".xournal_library" of the library; it can be deleted at any time.
 *
 * LibraryIndex keeps the text of every document (PDF text and text elements, per page) for library-wide search.
 * Changed documents are indexed again in the background; the text is stored as one JSON file per document.
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

/// A string that changes when one of the document's files changes (size, modification time).
QString documentStamp(const DocumentItem& item);

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

    /// The text of every page: the PDF text, then the text elements (upstream Text) of all layers.
    static QStringList extractText(Document& doc);
    /// Format of the stored index files (older ones are indexed again).
    static constexpr int FORMAT = 2;
    /// Whitespace runs to one space (the PDF text has line breaks where the page has them).
    static QString simplified(const QString& text);

Q_SIGNALS:
    /// Documents were indexed (done / total changed, the busy state).
    void progress();

private:
    struct Entry {
        QString stamp;
        QString name;
        fs::path file;
        QStringList pages;  ///< simplified text per page
        std::vector<double> aspects;  ///< height / width per page
    };
    void run(std::vector<DocumentItem> items, quint64 generation);
    fs::path indexFile(const fs::path& file) const;

    fs::path rootDir, indexDir;
    std::unique_ptr<QThreadPool> pool;
    mutable std::mutex mtx;
    std::map<fs::path, std::shared_ptr<const Entry>> entries;  ///< by main file
    std::atomic<quint64> generation{0};
    std::atomic<bool> running{false};
    std::atomic<int> doneCount{0}, totalCount{0};
};

}  // namespace xqt

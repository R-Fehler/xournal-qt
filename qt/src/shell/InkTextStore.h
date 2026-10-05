/*
 * xournal-qt: the library's cache of recognised handwriting, the pack "ink-text" of each folder.
 *
 * Reading handwriting is slow (about 0.2 s per line), so what was read is kept, per document, in the folder's cache
 * (LibraryCache.h: in ".xournal_library" or the app cache, like the other packs): the library's search finds the
 * handwriting of documents that are not open, opening a document reads none of it again, and nothing needs the model
 * to search what was read once. Nothing is stored in the .xopp.
 *
 * An entry (by file name): the stamp of the document's main file when its pages were read (a .xopp, or a PDF with
 * notes), the recogniser's id (results of another model are read again), whether every line was read, per page its
 * lines (hash and origin, 0.1 pt), and per line hash its words: [x, y, w, h (0.1 pt, relative to the line's origin),
 * confidence (0-255), reading, share, reading, share, ...] (a share: 0-255, plus the models that gave the reading
 * times 256 when several models read the line, ink::Candidate::models), then those models as a number. A line
 * written on several pages is stored once.
 * About 2-5 KB per page of handwriting, compressed. An entry of over 1 MB gets a file of its own (Packs).
 *
 * An entry also keeps the document's handwriting language (hwr/LanguagePlan.h): the one decided from its first lines
 * ("lang") and the one the user chose ("langChoice", ⋮ → Document → Handwriting language): the cache, not the .xopp,
 * which stays as Xournal++ writes it.
 *
 * Entries are written when the app saves a document whose handwriting it read (the hand-over of its open indexer) and
 * by the library's background job (LibraryInkJob); a few seconds after the last change (WriteScheduler), and when the
 * store goes. In memory, an entry keeps its pages put together for the search (ink::PageText).
 *
 * Thread-safe.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <QCborMap>
#include <QObject>
#include <QString>

#include "LibraryCache.h"
#include "filesystem.h"
#include "hwr/InkRecognitionService.h"

class QThreadPool;

namespace xqt {

struct InkDoc {
    QString stamp;          ///< of the document's main file (fileStamp) when it was read
    QString recognizer;     ///< the recogniser's id
    bool complete = false;  ///< every line of every page was read
    /// Its handwriting language (hwr/LanguagePlan.h): the one decided for this recogniser ("en", "de"; "": none),
    /// and the user's choice ("auto" or "": automatic, "en", "de", "both"; kept by put(), set by setLanguageChoice())
    QString language;
    QString languageChoice;
    std::vector<std::vector<hwr::LineRef>> pages;
    /// The pages put together (made by the store)
    std::vector<std::shared_ptr<const ink::PageText>> texts;
    /// Fill `texts` from `pages`.
    void assemble();
    size_t bytes() const;
};

class InkTextStore final: public QObject {
    Q_OBJECT
public:
    static const QString PACK;
    static constexpr int FORMAT = 1;

    explicit InkTextStore(CacheLocation location, QObject* parent = nullptr);
    /// Writes what is not written yet.
    ~InkTextStore() override;

    /// Read the pack of a folder (once; any thread).
    void load(const fs::path& folder);
    /// The entry of a document (null: none, or its folder not loaded).
    std::shared_ptr<const InkDoc> find(const fs::path& file) const;
    /// Set the entry of a document (its folder is loaded first); written later. The language the user chose for it
    /// stays.
    void put(const fs::path& file, InkDoc doc);
    /// The handwriting language the user chose for a document ("auto", "en", "de", "both"); an entry without results
    /// is made if it has none.
    void setLanguageChoice(const fs::path& file, const QString& choice);
    void erase(const fs::path& file);
    /// A document was renamed or moved by the app: its entry follows.
    void moved(const fs::path& from, const fs::path& to);
    /// A document's file has another stamp but the same content (copied, unzipped: LibraryIndex adopts its entry):
    /// its entry read from the file stamped `from` now holds for `to`.
    void restamp(const fs::path& file, const QString& from, const QString& to);
    /// Write the changed packs now (and wait for it).
    void flush();
    /// Forget what is not written, write nothing any more (the cache is being removed).
    void discard();
    /// How long writes wait for more changes (tests).
    void setWriteDelays(int quietMs, int maxDelayMs);
    /// Memory of the entries (tests, measurements), packs written so far (tests).
    size_t bytes() const;
    int packsWritten() const { return writes; }

    /// An entry as stored, and back (null: not one of ours).
    static QCborMap encode(const InkDoc& doc);
    static std::shared_ptr<InkDoc> decode(const QCborMap& map);

private:
    struct Folder {
        bool loaded = false;
        bool changed = false;
        std::map<std::string, std::shared_ptr<const InkDoc>> docs;  ///< by file name
    };
    void writeChanged();

    CacheLocation where;
    mutable std::mutex mtx;
    std::map<fs::path, Folder> folders;
    std::unique_ptr<WriteScheduler> scheduler;
    bool discarded = false;
    int writes = 0;
};

}  // namespace xqt

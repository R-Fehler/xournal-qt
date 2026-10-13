/*
 * xournal-qt: reads the handwriting of the library's documents in the background
 * (qt/docs/features/handwriting-search.md).
 *
 * Open documents are read by their indexers (InkTextIndexer). The rest of the library is read while the handwriting
 * search is on and its recogniser is ready (the model is there), whatever the power source (the author's choice: the
 * model is small and fast; the work stays at the lowest priority).
 * One document at a time: it is loaded on a worker at low priority, its pages go to the recognition worker
 * (InkRecognitionService) with the lowest priority (after every open document), two at a time, each a copy of its
 * strokes; lines read before (its entry in the library's cache, also of an older version of the file) are not read
 * again. With several models, the document's language decides which read its lines (hwr/LanguagePlan.h: the user's
 * choice and the decision kept in its entry, else found from its first lines and kept). The result goes into the
 * folder's "ink-text" pack (InkTextStore). Documents whose handwriting is in the pack
 * for the file as it is now are skipped (LibraryIndex::inkCandidates).
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <deque>
#include <map>
#include <memory>
#include <vector>

#include <QObject>
#include <QPointer>

#include "filesystem.h"
#include "hwr/InkRecognitionService.h"

class Document;
class QThreadPool;

namespace xqt {

class LibraryIndex;

class LibraryInkJob final: public QObject {
    Q_OBJECT
public:
    explicit LibraryInkJob(hwr::InkRecognitionService& service, QObject* parent = nullptr);
    ~LibraryInkJob() override;

    /// The library's index (null: none; another library: the work for the last one stops).
    void setIndex(LibraryIndex* index);
    /// The handwriting search is on.
    void setEnabled(bool on);
    /// Look for documents to read now (the index was updated, a document saved).
    void check();

    bool running() const { return current != nullptr || !waiting.empty(); }
    int documentsLeft() const { return static_cast<int>(waiting.size()) + (current ? 1 : 0); }
    int pagesRead() const { return pagesDone; }

Q_SIGNALS:
    void progress();

private:
    struct Current;
    void next();
    void loaded(std::shared_ptr<Current> doc);
    void pump();
    void finish();
    void stop();
    bool allowed() const;

    hwr::InkRecognitionService& service;
    QPointer<LibraryIndex> index;
    bool enabled = false;
    std::deque<fs::path> waiting;
    std::map<fs::path, QString> tried;  ///< read in this run of the app, with their stamps (not read again)
    std::shared_ptr<Current> current;
    std::unique_ptr<QThreadPool> loader;
    int pagesDone = 0;
    quint64 generation = 0;
};

}  // namespace xqt

/*
 * xournal-qt: keeps the handwriting of one open document searchable (qt/docs/handwriting-search.md).
 *
 * Per open document (tab) while the handwriting search is on. It hands the pages to the recognition worker
 * (InkRecognitionService.h) and the words read to the document's text index (DocumentTextIndex::setInk), where the
 * search finds them:
 *  - every page once at the start (DELAY after the document opened), then every page whose picture changed
 *    (DocumentSession::pageRevision), DELAY after the edits paused. Only lines not read before are read again (the
 *    worker knows lines by their hash): an edit costs the lines it touched;
 *  - the page in view first, then the others from there outwards; the document in front before the others
 *    (setFocused);
 *  - two pages at a time at most are with the worker (each a copy of its strokes);
 *  - pages left incomplete because the recogniser was not ready are read again when it is (recognizerChanged).
 * What it read (the lines of each page and their results) is what the library keeps for the document when it is saved
 * (pages()).
 *
 * UI thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <QObject>
#include <QTimer>

#include "InkRecognitionService.h"

class XojPage;

namespace xqt {
class DocumentSession;
}

namespace xqt::hwr {

class InkTextIndexer final: public QObject {
    Q_OBJECT
public:
    static constexpr int DELAY_MS = 2000;

    InkTextIndexer(DocumentSession& session, InkRecognitionService& service, QObject* parent = nullptr);
    ~InkTextIndexer() override;

    /// How long after opening and after the last edit pages are read (tests; default DELAY_MS).
    static void setDelay(int ms);
    /// The document is the one in front: its pages come before those of the other documents.
    void setFocused(bool focused);
    /// Start now, without the delay (tests, a search started).
    void start();

    /// What was read per page (in the document's order): its lines, and whether all of them were read. Pages not read
    /// yet (or changed since) have no entry (`known` false).
    struct PageLines {
        bool known = false;
        bool complete = false;
        std::vector<LineRef> lines;
    };
    std::vector<PageLines> pages() const;

    /// Pages read so far / waiting (changed, not read yet); done: nothing waits.
    int pagesRead() const { return readCount; }
    int pagesWaiting() const { return static_cast<int>(dirty.size() + outstanding.size()); }
    bool done() const { return dirty.empty() && outstanding.empty() && !timer.isActive(); }

Q_SIGNALS:
    void progress();

private:
    struct Indexed {
        std::shared_ptr<XojPage> ref;  ///< (kept while indexed: the key is its address)
        quint64 revision = 0;
        bool complete = false;
        std::shared_ptr<const ink::PageText> text;
        std::vector<LineRef> lines;
    };
    struct Outstanding {
        std::shared_ptr<XojPage> ref;
        quint64 revision = 0;
    };
    void scan();
    void pump();
    void received(quint64 id, PageResult result);
    void reapply();
    double priorityOf(size_t page) const;

    DocumentSession& session;
    InkRecognitionService& service;
    std::map<const XojPage*, Indexed> indexed;
    std::set<const XojPage*> dirty;
    std::map<quint64, Outstanding> outstanding;
    QTimer timer;
    bool focused = false;
    bool started = false;
    int readCount = 0;
};

}  // namespace xqt::hwr

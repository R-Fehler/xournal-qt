/*
 * xournal-qt: text search in one document (the PDF background text and text elements).
 *
 * Per page it does what upstream's SearchControl::search does; upstream's SearchBar then walks the pages from the
 * current one. Here the whole document is searched incrementally on the UI thread (a few pages per event loop
 * iteration, so big PDFs never block input), and the hits form one list with a current hit.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QElapsedTimer>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QTimer>

#include "model/DocumentListener.h"

class Document;

namespace xqt {

class DocumentSession;

class DocumentSearch final: public QObject, public DocumentListener {
    Q_OBJECT
public:
    struct Hit {
        size_t page = 0;
        QRectF rect;  ///< page points
    };

    explicit DocumentSearch(DocumentSession& session);
    ~DocumentSearch() override;

    /// Search for `text` (case-insensitive; empty: clear). With `jump`, the first hit from the current page on
    /// becomes current as soon as it is found (and is scrolled to).
    void setQuery(const QString& text, bool jump = true);
    const QString& query() const { return text; }
    void clear() { setQuery({}); }

    bool isRunning() const { return nextPage < pageCount && !text.isEmpty(); }
    /// Pages searched so far (progress).
    size_t pagesSearched() const { return nextPage; }
    const std::vector<Hit>& hits() const { return found; }
    int currentHit() const { return current; }
    /// Increased with every change of the hits or the current hit (for views).
    quint64 revision() const { return rev; }

    /// The hits on one page of a document (page points, reading order): PDF text and visible text elements. Takes a
    /// shared document lock; usable on any thread for a document no session changes meanwhile.
    static std::vector<QRectF> findOnPage(Document& doc, size_t page, const std::string& utf8);

    void next();
    void previous();
    /// The first hit at or after the current page becomes current (wrapping around).
    void jumpToFirstFromCurrentPage();

    // DocumentListener: page structure changes invalidate the hits.
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;

Q_SIGNALS:
    /// Hits or the current hit changed.
    void changed();
    /// All pages searched.
    void finished();

private:
    void restart();
    void step();
    void setCurrent(int index, bool scroll);
    void searchPage(size_t page);

    DocumentSession& session;
    QString text;
    std::string utf8;
    std::vector<Hit> found;
    int current = -1;
    bool pendingJump = false;
    bool jumpScrolls = true;
    size_t startPage = 0;
    size_t nextPage = 0;
    size_t pageCount = 0;
    quint64 rev = 0;
    QTimer stepTimer;
    QTimer restartTimer;
};

}  // namespace xqt

/*
 * xournal-qt: the annotations of the current document for the page sidebar's Annotations panel
 * (qt/docs/annotations-md.md), and the pictures of its handwriting.
 *
 * It works only while the panel is shown (`active`). Changes are collected (a page's revision changed, pages came or
 * went) and read once the writing pauses, on a background worker at low priority: a page whose revision is the one
 * read before keeps its items, so an edit reads one page again, not the document. The worker holds the session
 * (ThumbnailProvider::acquireSession) only while it reads a page, so closing a document waits for one page at most.
 * The items of each open document are kept while it is open (switching tabs back shows them at once).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QAbstractListModel>
#include <QQuickAsyncImageProvider>
#include <QTimer>

#include "Annotations.h"

namespace xqt {

class DocumentSession;

class AnnotationsModel final: public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// All items, whatever the filter shows.
    Q_PROPERTY(int total READ total NOTIFY countChanged)
    /// The panel is shown: only then are the annotations read.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// Reading (the first time, or after changes).
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// The document has annotations to show (not a text file).
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    /// The kinds shown (annotations::groupOf; a PDF highlight is a highlight): names, e.g. ["highlight", "ink"].
    Q_PROPERTY(QStringList shownKinds READ shownKinds WRITE setShownKinds NOTIFY filterChanged)
public:
    enum Roles {
        KindRole = Qt::UserRole + 1,  ///< annotations::nameOf
        PageRole,                     ///< 0-based
        TextRole,
        CommentRole,
        TargetRole,
        ColorRole,     ///< "#rrggbb"
        RectRole,      ///< on the page (points)
        PictureRole,   ///< handwriting: "image://annotation/…"; else empty
        AspectRole,    ///< height / width of its place
    };

    explicit AnnotationsModel(QObject* parent = nullptr);
    ~AnnotationsModel() override;

    void setSession(DocumentSession* session);
    /// How long changes are collected before they are read (ms; tests).
    void setDelay(int ms) { timer.setInterval(ms); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(rows.size()); }
    int total() const { return static_cast<int>(items.size()); }
    bool active() const { return isActive; }
    void setActive(bool on);
    bool busy() const { return running; }
    bool available() const;
    QStringList shownKinds() const;
    void setShownKinds(const QStringList& kinds);
    /// Items of a kind (annotations::nameOf of its group), whatever the filter shows.
    Q_INVOKABLE int countOf(const QString& kind) const;

    /// How many pages were read for the current document since it was opened (tests: an edit reads its page only).
    int pagesRead() const;
    /// All items as they are now (in page order).
    const std::vector<annotations::Item>& all() const { return items; }
    /// Read what changed now (not after the pause), then call `then` with the items up to date: at once when nothing
    /// is waiting. Works when the panel is not shown too.
    void whenCurrent(std::function<void()> then);

Q_SIGNALS:
    void countChanged();
    void activeChanged();
    void busyChanged();
    void availableChanged();
    void filterChanged();

private:
    struct State;
    void schedule();
    void start();
    void finished(const std::shared_ptr<State>& state, quint64 generation, bool gone,
                  std::vector<annotations::Item> result, std::vector<quint64> revisions);
    /// Show these items (only the rows that changed change).
    void apply(std::vector<annotations::Item> result, std::vector<quint64> revisions, bool reset = false);
    void refilter();

    DocumentSession* session = nullptr;
    quint64 sessionId = 0;
    std::map<const DocumentSession*, std::shared_ptr<State>> states;  ///< per open document
    std::vector<QMetaObject::Connection> connections;
    QTimer timer;
    bool isActive = false;
    bool running = false;
    bool dirty = false;  ///< changed while reading: read again after it
    bool stale = true;   ///< changed since the last read
    quint64 generation = 0;
    std::vector<annotations::Item> items;
    std::vector<quint64> revisions;  ///< the revision of each item's page (its picture)
    std::vector<size_t> rows;  ///< the items shown (filter)
    unsigned shown = ~0U;      ///< bits: annotations::Kind
    std::vector<std::function<void()>> waiting;  ///< whenCurrent
};

/// "image://annotation/<session>/<page revision>/<x>,<y>,<w>,<h>": a part of a page (its layers, no background), for
/// the handwriting in the Annotations panel. A revision that is gone gives nothing (the panel has new items then).
class AnnotationImageProvider final: public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};

}  // namespace xqt

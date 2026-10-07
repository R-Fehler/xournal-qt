/*
 * xournal-qt: the document a window shows now (its current tab's DocumentSession and CanvasView) and its signals,
 * relayed. Whoever cares about "the current document" (the window's controller now, the feature objects split off it
 * later) connects to this object once, instead of following tab changes and reconnecting to every new document:
 * the window calls follow() when its current tab changes, then announce() once it has set itself up for it, and
 * each state signal below fires then as if it had changed (a new document: all of it may have).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include <QImage>
#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QPolygonF>
#include <QRectF>
#include <QString>

namespace xqt {

class CanvasView;
class DocumentSession;

class CurrentDocument final: public QObject {
    Q_OBJECT
public:
    explicit CurrentDocument(QObject* parent = nullptr);
    ~CurrentDocument() override;

    /// The document and the view shown from now on (nullptr: none); their signals are relayed from now on.
    void follow(DocumentSession* session, CanvasView* view);
    /// Another document is shown: changed(), then every state signal once.
    void announce();
    DocumentSession* session() const;
    CanvasView* view() const;

Q_SIGNALS:
    /// Another document (or none) is the current one (emitted by announce, before the state signals)
    void changed();

    // --- states (also emitted by announce) ---
    // of the document
    void modifiedChanged();
    void savingChanged();
    void undoRedoChanged();
    void fileChanged();
    void bookmarksChanged();
    void pageChanged();
    void searchChanged();
    // of the view
    void pagesChanged();
    void notesChanged();
    void textEditingChanged();
    void markdownCursorChanged();
    void markdownUndoChanged();
    void geometryChanged();
    void curtainChanged();
    void zoomChanged();
    void rotationChanged();

    // --- events (as the document's and the view's, with their arguments) ---
    void pageActionUndone(const QString& text, bool undone);
    void linkTapped(const QString& uri, int page, QRectF viewRect);
    void markdownRequested(int page);
    void markdownBoxRequested(int page, double x, double y);
    void contextRequested(QPointF viewPos);
    void imageLoadRequested(const QString& url);
    void snipped(const QImage& image, int page, const QRectF& area, bool capped);
    void snipLinkOffered(const QString& title);
    void inkSwept(int page, const QPolygonF& path);
    void messageRequested(const QString& title, const QString& text);
    void playRequested(const QString& name, qint64 ts);

private:
    QPointer<DocumentSession> shownSession;
    QPointer<CanvasView> shownView;
    std::vector<QMetaObject::Connection> connections;
};

}  // namespace xqt

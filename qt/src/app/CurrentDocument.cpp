#include "CurrentDocument.h"

#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"

#include "CanvasView.h"
#include "ViewController.h"

namespace xqt {

CurrentDocument::CurrentDocument(QObject* parent): QObject(parent) {}

DocumentSession* CurrentDocument::session() const { return shownSession.data(); }
CanvasView* CurrentDocument::view() const { return shownView.data(); }

CurrentDocument::~CurrentDocument() {
    for (auto& c: connections) {
        disconnect(c);
    }
}

void CurrentDocument::follow(DocumentSession* s, CanvasView* v) {
    for (auto& c: connections) {
        disconnect(c);
    }
    connections.clear();
    shownSession = s;
    shownView = v;
    auto relay = [this](auto sender, auto signal, auto to) { connections.push_back(connect(sender, signal, this, to)); };
    if (s) {
        relay(s, &DocumentSession::modifiedChanged, &CurrentDocument::modifiedChanged);
        relay(s, &DocumentSession::savingChanged, &CurrentDocument::savingChanged);
        relay(s, &DocumentSession::undoRedoStateChanged, &CurrentDocument::undoRedoChanged);
        relay(s, &DocumentSession::pageActionUndone, &CurrentDocument::pageActionUndone);
        relay(s, &DocumentSession::filePathChanged, &CurrentDocument::fileChanged);
        relay(s, &DocumentSession::bookmarksChanged, &CurrentDocument::bookmarksChanged);
        relay(s, &DocumentSession::currentPageChanged, &CurrentDocument::pageChanged);
        relay(&s->search(), &DocumentSearch::changed, &CurrentDocument::searchChanged);
        relay(&s->search(), &DocumentSearch::finished, &CurrentDocument::searchChanged);
    }
    if (v) {
        relay(v, &CanvasView::pagesChanged, &CurrentDocument::pagesChanged);
        relay(v, &CanvasView::notesChanged, &CurrentDocument::notesChanged);
        relay(v, &CanvasView::textEditingChanged, &CurrentDocument::textEditingChanged);
        relay(v, &CanvasView::markdownCursorChanged, &CurrentDocument::markdownCursorChanged);
        relay(v, &CanvasView::markdownUndoChanged, &CurrentDocument::markdownUndoChanged);
        relay(v, &CanvasView::geometryChanged, &CurrentDocument::geometryChanged);
        relay(v, &CanvasView::curtainChanged, &CurrentDocument::curtainChanged);
        relay(&v->getViewController(), &ViewController::zoomChanged, &CurrentDocument::zoomChanged);
        relay(&v->getViewController(), &ViewController::zoom100Changed, &CurrentDocument::zoomChanged);
        relay(&v->getViewController(), &ViewController::rotationChanged, &CurrentDocument::rotationChanged);
        relay(v, &CanvasView::linkTapped, &CurrentDocument::linkTapped);
        relay(v, &CanvasView::markdownRequested, &CurrentDocument::markdownRequested);
        relay(v, &CanvasView::markdownBoxRequested, &CurrentDocument::markdownBoxRequested);
        relay(v, &CanvasView::contextRequested, &CurrentDocument::contextRequested);
        relay(v, &CanvasView::imageLoadRequested, &CurrentDocument::imageLoadRequested);
        relay(v, &CanvasView::snipped, &CurrentDocument::snipped);
        relay(v, &CanvasView::snipLinkOffered, &CurrentDocument::snipLinkOffered);
        relay(v, &CanvasView::inkSwept, &CurrentDocument::inkSwept);
        relay(v, &CanvasView::messageRequested, &CurrentDocument::messageRequested);
        relay(v, &CanvasView::playRequested, &CurrentDocument::playRequested);
    }
}

void CurrentDocument::announce() {
    Q_EMIT changed();
    Q_EMIT modifiedChanged();
    Q_EMIT savingChanged();
    Q_EMIT undoRedoChanged();
    Q_EMIT fileChanged();
    Q_EMIT bookmarksChanged();
    Q_EMIT pageChanged();
    Q_EMIT searchChanged();
    Q_EMIT pagesChanged();
    Q_EMIT notesChanged();
    Q_EMIT textEditingChanged();
    Q_EMIT markdownCursorChanged();
    Q_EMIT markdownUndoChanged();
    Q_EMIT geometryChanged();
    Q_EMIT curtainChanged();
    Q_EMIT zoomChanged();
    Q_EMIT rotationChanged();
}

}  // namespace xqt

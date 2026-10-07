/*
 * xournal-qt: what a window's feature objects get from their window (AudioControl, TimelineControl, and the objects
 * split off AppController later): the process's shared parts (AppServices), the window's tabs and its current
 * document. A window (AppController::windowContext) hands it over at construction; it outlives every feature object
 * of its window.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

namespace xqt {

class AppServices;
class CanvasView;
class CurrentDocument;
class DocumentSession;
class TabManager;

struct WindowContext {
    /// What the windows of the process share (settings and tools, the library, the open documents, background jobs)
    AppServices& services;
    /// The window's tabs
    TabManager& tabs;
    /// The window's current document and view, with their signals relayed (connect here for "the current document")
    CurrentDocument& current;

    /// The current tab's document and view (nullptr: none)
    DocumentSession* session() const;
    CanvasView* view() const;
};

}  // namespace xqt

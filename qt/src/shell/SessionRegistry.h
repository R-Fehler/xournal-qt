/*
 * xournal-qt: the open documents the image providers may draw from, by a number that their image URLs carry
 * ("image://thumbnail/<id>/...", "image://sketch/<id>/...", "image://annotation/<id>/...").
 *
 * Only the owner of a session registers it: TabManager, when a tab opens, and removes it when the tab closes (before
 * the session is destroyed). Everything else (the page sidebar, the annotations panel, the presenter console, the
 * reference view) only looks up the id of a session it is shown; a session that is not registered has none (0) and
 * its pictures are not drawn. So no registered session outlives its owner.
 *
 * A worker that draws from a session acquires it and releases it when done; remove() waits until no worker holds it,
 * then the kept thumbnails and the sketches of the session go (ThumbnailProvider, PageSketches).
 *
 * Safe from any thread.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <QtGlobal>

namespace xqt {

class DocumentSession;

namespace SessionRegistry {

/// Make a session's pages available to the image providers (the owner of the session only); returns its id. The same
/// session again: the same id.
quint64 add(DocumentSession* session);
/// Before the session goes: waits until no worker holds it, then drops its thumbnails and sketches.
void remove(DocumentSession* session);
/// The id of a registered session (0: not registered).
quint64 idOf(const DocumentSession* session);
/// A registered session to draw from on a worker: it stays until release (nullptr: none, or not any more).
DocumentSession* acquire(quint64 id);
void release(quint64 id);

}  // namespace SessionRegistry

}  // namespace xqt

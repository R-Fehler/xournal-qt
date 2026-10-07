/*
 * xournal-qt: documents that no session or view owns: a copy a save writes from (DocumentSave.cpp), a file read for its
 * pages (page files, templates, stickers, Markdown and image files) and a loaded document until a session takes it.
 * Their events go to one shared handler that has no listeners.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>

class Document;
class DocumentHandler;

namespace xqt {

/// The handler of every detached document. It has no listeners (any thread may fire its events).
DocumentHandler& detachedHandler();

/// A new empty document with the detached handler.
std::unique_ptr<Document> newDetachedDocument();

}  // namespace xqt

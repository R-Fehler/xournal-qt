/*
 * xournal-qt: links between documents (qt/docs/links.md) applied to documents: what a document offers a link to
 * lead to (its chapters, its pages), where a link leads in an open document, which file a link means, and the link
 * to a place in a document.
 *
 * The format itself and the resolution are in session/DocumentLink.h.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "filesystem.h"
#include "LinkRewrite.h"
#include "session/DocumentLink.h"

class Document;

namespace xqt {

class DocumentSession;
class LibraryIndex;

namespace DocumentLinks {

/// The chapters of a document as its contents list them: the PDF outline (at the first page showing its PDF page),
/// else the chapters written in it (DocumentChapters.h).
std::vector<links::Chapter> chaptersOf(Document& doc);
/// Its pages: the PDF page each shows (1-based) and the text of its text elements.
std::vector<links::Page> pagesOf(Document& doc);

/// Where a link leads in an open document: its page (0-based) and the note when something was not found. A text
/// document (a .md) by its heading and line.
links::Place placeIn(DocumentSession& session, const links::Link& link);

/// The file a link means, from the document holding it (`from`: its file; empty: a new document, then relative to
/// `libraryRoot`). A wiki link's name is looked up: next to `from`, then in the library (by file name, `.md` added
/// when it has no extension; the one closest to `from` wins). The file as the library opens it: a PDF with its
/// .xopp is the .xopp. Empty when nothing is found.
fs::path targetOf(const links::Link& link, const fs::path& from, const fs::path& libraryRoot,
                  const LibraryIndex* index = nullptr);

/// The link to page `page` (0-based) of an open document, for a document at `from` (empty: an absolute path): its
/// PDF page, or the fingerprint of its text. `chapter`: a chapter's title (its page is `page`).
links::Link linkTo(DocumentSession& session, size_t page, const fs::path& from, const QString& chapter = {});
/// The link to a document (a library card), with no place in it.
links::Link linkToFile(const fs::path& file, const fs::path& from);

/// The documents whose links lead to `target` (any of its files: a PDF with its .xopp is one document; a wiki link
/// by its name), each once, in the order of `sources`. `target` itself is left out.
std::vector<fs::path> backlinks(const std::vector<LinkRewrite::Source>& sources, const fs::path& target);

/// A link whose file is gone (moved or renamed by another program): the document it probably means, from the library
/// index - one of that file name (the closest to `from`), else one with a page that has the link's fingerprint.
/// Empty when there is none.
fs::path findMoved(const links::Link& link, const fs::path& from, const LibraryIndex& index);

/// The Markdown text of a link to `target` as the document at `from` writes it: its path relative to `from`
/// (encoded), with the fragment of `written` (the link as it was).
QString relinked(const QString& written, const fs::path& from, const fs::path& target);

}  // namespace DocumentLinks
}  // namespace xqt

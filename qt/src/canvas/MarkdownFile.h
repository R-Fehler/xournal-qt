/*
 * xournal-qt: a Markdown file (.md) or a text file as a document (qt/docs/md-editor.md).
 *
 * The file's text is the page's Markdown text of a new document of plain A4 pages: it flows over the pages as a
 * Markdown text written on a page does (MdPaginate.h, MarkdownSession.h), each page holding its part in a box at the
 * page margins. Edited, the text is written back to the file (DocumentSession::setTextFile, TextFile.h), never as a
 * .xopp. The library's preview of a .md is the first page of this document.
 *
 * A text or code file shown read-only is one fenced code block (monospaced, highlighted by its extension).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "filesystem.h"

#include "MdLayout.h"

class Document;

namespace xqt {
class DocumentSession;
class TextFile;
}  // namespace xqt

namespace xqt::MarkdownFile {

/// At most this much of a file is read (a longer one: its start, cut at the end of a line).
constexpr size_t MAX_BYTES = 2 * 1024 * 1024;
/// Plain A4 pages (points)
constexpr double PAGE_WIDTH = 595.276;
constexpr double PAGE_HEIGHT = 841.89;

/// The text of a Markdown file (UTF-8, without a byte order mark): at most `maxBytes` of it, cut at the end of a
/// line. Empty if it cannot be read. `cut`: whether the file is longer.
std::string read(const fs::path& file, size_t maxBytes = MAX_BYTES, bool* cut = nullptr);

/// A text or code file as a Markdown text that shows it as it is: one fenced code block (a fence longer than any run
/// of backticks in it), in the language of its extension for highlighting (none for .txt and the like). At most
/// `maxBytes` of it, cut at the end of a line (`cut`: whether the file is longer).
std::string readAsPlainText(const fs::path& file, size_t maxBytes = MAX_BYTES, bool* cut = nullptr);
/// The Markdown text that shows `text` as it is (see readAsPlainText).
std::string plainText(const std::string& text, const std::string& language);

/// How the text of a Markdown file is drawn.
md::Style style();
/// How the text of this file is drawn while it is edited.
md::Style style(const TextFile& file);

/// The document that edits a text file: its text on plain A4 pages.
std::unique_ptr<Document> textDocument(const TextFile& file);
/// The pages of a text file's document get this text (one undo step, as an edit): e.g. the file as it is on disk
/// now. Text written on the canvas ends first.
void setText(DocumentSession& session, const std::string& text);

/// A new document showing `source` on plain A4 pages (at most `maxPages`; the rest is left out). Any thread.
std::unique_ptr<Document> document(const std::string& source, size_t maxPages = static_cast<size_t>(-1));

/// Where the part of the text on each page of a document made by document() begins (bytes of the text), from its
/// pages as they are now.
std::vector<size_t> pageStarts(Document& doc);
/// The page of a document made by document() that shows byte `offset` of its text (0 if it shows none).
size_t pageOf(Document& doc, size_t offset);

}  // namespace xqt::MarkdownFile

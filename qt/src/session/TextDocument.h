/*
 * xournal-qt: text documents in the notes model (qt/docs/md-pdf.md).
 *
 * A Markdown text is a flow over a run of pages: the page's Markdown text (MarkdownSession.h), one part per page in
 * the page's box at the margins, the parts after the first starting with "<!-- xqt:cont … -->" (MdPaginate.h). A
 * notes document whose page 1 starts such a flow is a text document; saved as a PDF with notes it is a "PDF text
 * document", which also carries its text as a plain "name.md" for other apps (attachments()).
 *
 * The functions that take a Document read it: the caller holds its lock (shared is enough).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include "model/PageRef.h"

class Document;
class Text;

namespace xqt::TextDocument {

/// The box of the page's Markdown text on this page (at the page's margins; nullptr if none).
Text* pageBoxOf(const PageRef& page);

/// Page 1 starts the page's Markdown text (a notes document that is a text document), or has the empty Markdown layer
/// of an empty one.
bool isTextDocument(Document& doc);
/// Some page has the page's Markdown text (Export as Markdown is offered).
bool hasMarkdownText(Document& doc);

/// The Markdown of the flow that starts on page `first` (its parts joined, without the continuation lines), and the
/// page after its last one (`end`). Empty when the page has no page text.
std::string flowText(Document& doc, size_t first = 0, size_t* end = nullptr);
/// The Markdown of all flows of the document in page order: one flow as it is; several joined with a page break
/// between them (Export as Markdown).
std::string markdown(Document& doc);

/// The page break of the formatting bar (qt/docs/md-editor.md, "Page breaks").
constexpr const char* PAGE_BREAK = "<div style=\"page-break-after: always\"></div>";

/// "name.md" for the PDF "name.pdf" (an archive's "name.archive.pdf" too).
std::string markdownName(const std::string& pdfName);

/// A file a PDF with notes carries for other apps, next to its embedded Xournal data.
struct Attachment {
    std::string name;
    std::string data;
    std::string mime;
    std::string description;
    /// PDF/A-3's /AFRelationship in an archive PDF (e.g. "/Alternative")
    std::string relationship;
    /// Its data never changes under its name (a picture): an incremental save keeps the one the file has, and adds
    /// it only when the file has none.
    bool fixed = false;
};
/// What the PDF `pdfName` written from this document carries for other apps: a text document its "name.md" (the
/// flow) and the pictures it links to, under the paths its links name ("name.assets/image-….png";
/// qt/docs/md-images.md). The pictures are found through the roots (md::images): the document's work folder while it
/// is open.
std::vector<Attachment> attachments(Document& doc, const std::string& pdfName);
/// The MIME type of a picture by its name ("image/png", "image/jpeg", …; "application/octet-stream" if unknown).
std::string pictureMime(const std::string& name);

}  // namespace xqt::TextDocument

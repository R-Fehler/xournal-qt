/*
 * xournal-qt: a document's annotations - its highlights and notes - as a list, and as Markdown
 * (qt/docs/annotations-md.md).
 *
 * What counts, page by page:
 *  - highlights over PDF text: the highlighter's strokes (drawn by hand or made from selected PDF text: highlight,
 *    underline, strike through) with the PDF text under them; strokes drawn one after another close together in one
 *    color are one highlight;
 *  - the highlight annotations of the PDF itself (highlight, underline, squiggly, strike out, made in other apps),
 *    with the text under them and their note;
 *  - text boxes and Markdown boxes, with their text; a Markdown box that is only a link (a link marker,
 *    qt/docs/links.md) is a link;
 *  - handwriting in the margins and free areas: the pen's strokes, grouped - a stroke joins the group written just
 *    before it when it is near it, groups that overlap become one - and not the ink over PDF text (marks, not notes);
 *  - notes from a NoteSource (sticky notes, another block).
 *
 * Reading is split so that a big document stays cheap: read() takes what a page shows under the document's read
 * lock (plain values, no drawing), itemsOf() works out the items without the lock (the PDF text with a poppler
 * instance of its own). A page's items only change with its revision (DocumentSession::pageRevision), so the panel
 * keeps them per revision (AnnotationsModel).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <QImage>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "model/PageRef.h"
#include "session/DocumentLink.h"
#include "filesystem.h"

class Document;
class XojPage;

namespace xqt {

class PdfLayoutReader;

namespace annotations {

enum class Kind : uint8_t {
    Highlight,     ///< the highlighter over PDF text
    PdfHighlight,  ///< a highlight annotation of the PDF
    Text,          ///< a text box
    Markdown,      ///< a Markdown box
    Ink,           ///< handwriting (a group of strokes)
    Link,          ///< a link marker
    Note,          ///< a sticky note (NoteSource)
};
constexpr int KIND_COUNT = 7;
/// "highlight", "pdfHighlight", "text", "markdown", "ink", "link", "note"
QString nameOf(Kind kind);
/// The kinds shown together (the panel's filter): a PDF highlight is a highlight.
inline Kind groupOf(Kind kind) { return kind == Kind::PdfHighlight ? Kind::Highlight : kind; }

struct Item {
    Kind kind = Kind::Text;
    size_t page = 0;   ///< 0-based
    QRectF rect;       ///< where it is on its page (page points)
    QString text;      ///< the highlighted text, the text of a box, a link's title, a note
    QString comment;   ///< the note of a PDF highlight annotation
    QString target;    ///< a link: its target as written
    uint32_t color = 0;  ///< RGB

    bool operator==(const Item& o) const = default;
};

/// What a page shows that its items are made of, read under the document's lock (plain values).
struct PageContent {
    int pdfPage = -1;  ///< the PDF page it shows (0-based), -1: none
    double width = 0, height = 0;
    struct Box {
        bool markdown = false;
        QRectF rect;
        QString text;
        uint32_t color = 0;
    };
    std::vector<Box> boxes;
    struct Mark {  ///< a highlighter stroke
        QRectF box;
        std::vector<QPointF> points;
        double width = 1;
        uint32_t color = 0;
    };
    std::vector<Mark> marks;
    std::vector<QRectF> ink;  ///< the pen's strokes, in the order they were written
    std::vector<Item> notes;  ///< from the NoteSource
};

/// What the page shows (visible layers). The caller holds the document lock (shared); any thread.
PageContent read(const XojPage& page);
/// The items of a page, in reading order (top to bottom), `page` set to `index`. `pdf`: the document's PDF, read with
/// an instance of its own (may be null: no PDF text, no PDF annotations). No lock needed; any thread.
std::vector<Item> itemsOf(const PageContent& content, size_t index, PdfLayoutReader* pdf);
/// All pages (tests, and what the panel does page by page): takes the document's read lock page by page.
std::vector<Item> collect(Document& doc, PdfLayoutReader* pdf);

/// Where sticky notes come from: called for every page read, under the document's read lock; it appends
/// its notes (kind Note, with their place and text). By default the sticky notes of qt/sticky-notes (their texts, or
/// "(handwriting)"); what is on a note is not listed as a box or ink of the page. Empty: no notes.
using NoteSource = std::function<void(const XojPage& page, std::vector<Item>& notes)>;
void setNoteSource(NoteSource source);

/// A picture of a part of a page: its visible layers (no background) on white, `scale` pixels per point. Takes the
/// document's read lock; any thread.
QImage drawArea(Document& doc, const PageRef& page, const QRectF& rect, double scale);

// --- the Markdown export ---------------------------------------------------------------------------------------

/// A chapter of the document, as the contents list shows it: the PDF outline (at the first page showing its PDF
/// page), else the chapters written in the document.
struct Chapter {
    QString title;
    int level = 0;
    size_t page = 0;
};
std::vector<Chapter> chaptersOf(Document& doc);

struct ExportInput {
    fs::path document;               ///< the file the links lead to (DocumentSession::documentFile)
    QString title;                   ///< the document's name
    std::vector<Chapter> chapters;   ///< headings (none: a heading per page)
    std::vector<links::Page> pages;  ///< per page: its PDF page and text (DocumentLinks::pagesOf), for the links
    /// Handwriting as pictures in "<name>.assets/" (Typora's convention); else a link to its page, "(handwriting)".
    bool inkImages = false;
};
/// A picture the Markdown refers to: `file` relative to the Markdown file's folder, of `item`.
struct Picture {
    QString file;
    size_t item = 0;
};
/// The Markdown text: a title, a link to the document, a heading per chapter (or page), each item as a quote or a
/// bullet with a link to its page (qt/docs/links.md). `markdownFile`: where it goes (links are relative to it).
/// `pictures` (with inkImages): the pictures to write next to it.
std::string markdown(const std::vector<Item>& items, const ExportInput& input, const fs::path& markdownFile,
                     std::vector<Picture>* pictures = nullptr);
/// The link to an item's page, relative to `markdownFile`.
links::Link pageLink(const ExportInput& input, size_t page, const fs::path& markdownFile);

/// Where the export goes by default: "<name>.annotations.md" next to the document.
fs::path defaultFile(const fs::path& document);
/// The folder of its pictures: "<name>.assets" next to it.
fs::path assetsFolder(const fs::path& markdownFile);

}  // namespace annotations
}  // namespace xqt

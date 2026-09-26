/*
 * xournal-qt: the bookmarks of a PDF with notes in its own outline (qt/docs/bookmarks.md).
 *
 * They are the children of a top-level outline item "Bookmarks", the last one, so every PDF viewer lists them next
 * to the document's own table of contents, which stays as it is. Each child goes to its page ([page /XYZ null null
 * null]: the viewer keeps its zoom). The item carries our private key /XournalQt; an item without it counts as ours
 * too when it is called "Bookmarks" and all its children are plain entries that go to a page (a file whose outline
 * another app wrote again).
 *
 * The same code writes a whole file (HybridPdf's full write) and an incremental update (IncrementalPdf::Update: the
 * objects of the file that change are touched first, new ones are made through the update). An update touches only
 * what changes: nothing when the bookmarks are as the file has them; else the item (its children are new objects),
 * and, when the item comes or goes, the outline dictionary and its neighbour.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>

namespace xqt::IncrementalPdf {
class Update;
}

namespace xqt::PdfBookmarks {

struct Entry {
    QPDFObjectHandle page;  ///< the page object it goes to
    std::string title;      ///< UTF-8
};

/// Make our "Bookmarks" item of `pdf`'s outline list `entries` (in this order): written again when it differs,
/// removed when there are none, added as the last top-level item when missing. The rest of the outline stays.
/// `update`: an incremental save of `pdf`. Returns whether the outline changed.
bool write(QPDF& pdf, const std::vector<Entry>& entries, IncrementalPdf::Update* update = nullptr);

/// The entries of our item (empty: none), for tests and for other readers than poppler.
std::vector<Entry> read(QPDF& pdf);

}  // namespace xqt::PdfBookmarks

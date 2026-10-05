/*
 * xournal-qt: a page template's file (qt/docs/templates.md): a page saved to be added again, as a .xopp of one page
 * that Xournal++ opens as it is.
 *
 * With its background the page keeps it as it is: ruled, graph, plain paper with its colour, a picture, or a PDF page.
 * A PDF page goes along as upstream's attached PDF of the .xopp ("name.xopp.bg.pdf", one page, referred to as domain
 * "attach", "bg.pdf"), so using the template is the same as pasting that copied page: its PDF page joins the
 * document's merged PDF (PageClipboard, MergedPdf), its text stays searchable. Without its background the page is plain
 * white paper whose background carries the name NO_BACKGROUND: added to a document, it gets the background new pages
 * get there. With its content: every layer as it is (ink, text, pictures, Markdown boxes, sticky notes); without it:
 * its layers stay, empty (no notes, no Markdown layer).
 *
 * Any thread: nothing here touches an open document.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

#include "model/PageRef.h"

#include "filesystem.h"

class XojPage;

namespace xqt::templates {

/// The name of the background of a template saved without its background (saved, fixed English; Xournal++ keeps it)
inline constexpr const char* NO_BACKGROUND = "xournal-qt: template without background";

struct Options {
    bool background = true;  ///< with the page's background (a PDF page: the PDF page itself)
    bool content = true;     ///< with what is on the page (every layer's elements, sticky notes)
};

/// The template's page from a copy of a page (the copy is changed and returned): its bookmark goes; without its
/// background plain white paper named NO_BACKGROUND, without the content its layers emptied. A PDF background stays a
/// PDF page (its number is the caller's: write() makes it 0).
PageRef makePage(PageRef copy, const Options& options);

/// The template has no background of its own (saved without it). Read it from the loaded file: a copy of a page
/// (XojPage's copy constructor) does not keep the background's name.
bool withoutBackground(const XojPage& page);

/// Write the template `page` to `target` (.xopp) with a preview of it. `pdf`: its PDF page as a PDF of one page (a
/// page whose background is a PDF page needs it), written next to it as "<target>.bg.pdf" and attached. False with
/// `error` when it could not be written (nothing is left behind).
bool write(PageRef page, const std::string& pdf, const fs::path& target, std::string* error = nullptr);

/// "<xopp>.bg.pdf": the attached PDF of a template (upstream's name)
fs::path attachedPdfOf(const fs::path& xopp);

}  // namespace xqt::templates

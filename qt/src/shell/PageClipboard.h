/*
 * xournal-qt: copied pages (Ctrl+C in the sidebar or page grid), to paste into the same or another document.
 *
 * The pages are copied when copying (later edits do not change the clipboard) and again when pasting (every paste
 * gives new pages). A page with a PDF background keeps it when pasted into a document with the same PDF; in another
 * document the PDF page becomes an image background (the PDF page is only rendered then).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <vector>

#include "model/PageRef.h"
#include "pdf/base/XojPdfPage.h"

#include "filesystem.h"

class Document;

namespace xqt {

class PageClipboard {
public:
    /// Copy these pages of a document (indices). Takes a shared document lock.
    void copy(Document& doc, const std::vector<size_t>& pages);
    bool isEmpty() const { return pages.empty(); }
    size_t size() const { return pages.size(); }
    /// New copies of the pages, ready to be inserted into `target`.
    std::vector<PageRef> pagesFor(Document& target) const;

    /// Resolution of PDF pages turned into image backgrounds.
    static constexpr double IMAGE_DPI = 200;

private:
    std::vector<PageRef> pages;
    std::vector<XojPdfPageSPtr> pdfPages;  ///< per page: its PDF background page, or nullptr
    fs::path pdfFile;                       ///< the PDF of the source document
};

}  // namespace xqt

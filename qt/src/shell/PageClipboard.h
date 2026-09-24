/*
 * xournal-qt: copied pages (Ctrl+C in the sidebar or page grid), to paste into the same or another document.
 *
 * The pages are copied when copying (later edits do not change the clipboard) and again when pasting (every paste
 * gives new pages). A page with a PDF background keeps it when pasted into a document with the same PDF. In another
 * document its PDF page joins that document's merged background PDF (MergedPdf.h), so its text stays searchable and
 * selectable: the PDF pages are copied (as a PDF in memory) when copying. Only if that fails does the PDF page become
 * an image background.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include <QtGlobal>

#include "model/PageRef.h"
#include "pdf/base/XojPdfPage.h"

#include "filesystem.h"

namespace xqt {

class DocumentSession;

class PageClipboard {
public:
    /// Copy these pages of a document (indices). `withPdf`: also copy their PDF pages (for pasting elsewhere).
    void copy(DocumentSession& session, const std::vector<size_t>& pages, bool withPdf = true);
    bool isEmpty() const { return pages.empty(); }
    size_t size() const { return pages.size(); }
    /// New copies of the pages, ready to be inserted into `target`. `addedToMergedPdf`: their PDF pages were added to
    /// the target's merged PDF (it is written in the background).
    std::vector<PageRef> pagesFor(DocumentSession& target, bool* addedToMergedPdf = nullptr) const;

    /// Resolution of PDF pages turned into image backgrounds.
    static constexpr double IMAGE_DPI = 200;

private:
    std::vector<PageRef> pages;
    std::vector<XojPdfPageSPtr> pdfPages;  ///< per page: its PDF background page, or nullptr
    fs::path pdfFile;                       ///< the PDF of the source document
    std::string pdfStamp;                   ///< its size and time when copied
    quint64 sourceSession = 0;              ///< the source document (DocumentSession::serial)
    quint64 sourceNumbering = 0;            ///< its DocumentSession::pdfNumbering
    std::string pdfData;                    ///< the PDF pages of the copied pages, as a PDF
    std::vector<size_t> pdfIndex;           ///< per page: its page in pdfData, or npos
    /// The last paste of these PDF pages into a document: pasted again, the pages are already in its PDF.
    mutable struct {
        quint64 session = 0;
        fs::path pdf;
        std::string stamp;
        size_t first = 0;
    } merged;
};

}  // namespace xqt

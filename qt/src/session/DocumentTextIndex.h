/*
 * xournal-qt: the text of an open document, page by page, for its search (DocumentSearch).
 *
 * Searching an open document used to ask poppler to find the text on every page for every query: poppler builds the
 * text of a page anew on each call, and it shares the document with the renderer. Now the text is read once and kept
 * here, and a search is a scan over strings (TextMatch: milliseconds for a 1,300-page manual); poppler is only asked
 * where the hits of the pages in view are drawn.
 *
 * What it keeps:
 *  - the PDF text by PDF page (simplified, as the library index keeps it). On opening it is taken from the library
 *    index when that read this PDF as it is now (same size and time: setSeeder); what is missing is read in the
 *    background, a few seconds after opening or at the first search, the pages around the current one first, only
 *    while the canvas has no page in view to render (RenderService::visiblePagesBusy), with a poppler instance of
 *    its own (the canvas never waits for it);
 *  - per page the text its text elements show (plain texts whole, Markdown boxes as drawn), from the document in
 *    memory, so unsaved edits are searched: a changed page (also through undo and redo) is read again once the
 *    edits pause; pages that come, go or move take their entries along;
 *  - where the characters of a few PDF pages are drawn (layout(): the last pages whose hits were marked), read on
 *    demand before any other work.
 *
 * UI thread, except the reading of PDF text, which runs on one background worker for all documents.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include <QObject>
#include <QRectF>
#include <QString>
#include <QTimer>

#include "TextMatch.h"
#include "Vocabulary.h"

#include "model/DocumentListener.h"
#include "filesystem.h"

class Text;
class XojPage;

namespace xqt {

class DocumentSession;

/// The text of a PDF page and where each of its characters is drawn.
struct PdfPageLayout {
    QString text;               ///< simplified, as the index keeps it
    std::vector<QRectF> boxes;  ///< per character of `text` (page points; null for the spaces)
    /// Where characters [start, end) are drawn: a rectangle per line.
    std::vector<QRectF> rects(qsizetype start, qsizetype end) const;
    /// Read from a poppler page's text (UTF-8) and the box of each of its characters (x1, y1, x2, y2 each).
    static PdfPageLayout from(const char* utf8, const double* boxes, size_t count);
};

/// A piece of text shown by a text element of a page: a plain text whole, a Markdown box per text of its layout.
struct ElementText {
    const Text* element = nullptr;
    int item = -1;  ///< Markdown: the text item of its layout (-1: a plain text)
    QString shown;  ///< as shown (not simplified)
};
/// The texts the text elements of a page show, in order (visible layers only: what the reader sees). The caller
/// holds the document lock; UI thread (Markdown layouts are made there).
std::vector<ElementText> elementTexts(const XojPage& page);
/// Where characters [start, end) of a piece are shown (page points): a rectangle per line. UI thread.
std::vector<QRectF> elementRects(const ElementText& piece, qsizetype start, qsizetype end);

class DocumentTextIndex final: public QObject, public DocumentListener {
    Q_OBJECT
public:
    explicit DocumentTextIndex(DocumentSession& session);
    ~DocumentTextIndex() override;

    /// Where PDF text read before comes from (the app: the library index): the text of pages of this PDF by page
    /// number (0-based), as far as it is known for the file as it is now.
    using Seeder = std::function<std::map<int, QString>(const fs::path& pdf)>;
    static void setSeeder(Seeder seeder);
    /// How long after opening the missing PDF text is read (default 2 s; a search starts it at once).
    static void setStartDelay(int ms);

    /// Read the missing PDF text now (a search started). Idempotent.
    void start();
    /// PDF pages whose text was read here so far (not taken from the library index).
    int pdfPagesRead() const { return readCount; }
    /// PDF pages whose text was taken from the library index.
    int pdfPagesSeeded() const { return seededCount; }

    size_t pageCount() const { return pages.size(); }
    /// The text of every page is known (the PDF text of all pages that show one).
    bool complete() const { return unknownPages == 0; }
    /// Pages whose PDF text is not known yet.
    size_t pagesMissing() const { return unknownPages; }
    /// The text of a page is known.
    bool known(size_t page) const;
    /// Hits of a query (TextMatch::prepare) on a page, in the text known so far.
    int count(size_t page, QStringView query);
    /// Hits of several terms (TextMatch.h: overlapping hits of different terms count once).
    int count(size_t page, const std::vector<textmatch::Term>& terms);
    /// The same, for terms prepared once for all pages: fuzzy terms (words) are counted from the vocabularies of
    /// the page's texts (Vocabulary.h, made when first needed and kept until the text changes).
    int count(size_t page, const words::Terms& terms);
    /// Make the vocabularies of all pages whose text is known (before fuzzy terms are prepared: their words are
    /// matched at once then, not one by one).
    void prepareWords();
    /// A term is on the page (in the text known so far).
    bool contains(size_t page, const textmatch::Term& term);
    /// Term `i` of `terms` is on the page.
    bool contains(size_t page, const words::Terms& terms, size_t i);
    /// The PDF page a page shows (-1: none).
    int pdfPageOf(size_t page) const { return page < pages.size() ? pages[page].pdf : -1; }
    /// The PDF text known by page number (for the library index when the document is saved).
    std::map<int, QString> pdfTexts() const;
    const fs::path& pdfFile() const { return pdf; }

    /// Where the text of a PDF page is drawn; nullptr until it is read (then layoutReady): asked for first, before
    /// the reading of text that is missing. The last LAYOUTS pages are kept.
    const PdfPageLayout* layout(int pdfPage, bool urgent = false);
    static constexpr size_t LAYOUTS = 48;
    /// Memory of the kept text (bytes; tests, measurements), and of the vocabularies of its pages.
    size_t textBytes() const;
    size_t vocabularyBytes() const;
    size_t layoutBytes() const;

    /// The page the reader is at: missing text is read from there outwards.
    void setFocusPage(size_t page);
    /// No search needs it for now: the poppler instance of the worker may go (it is opened again when needed).
    void release();

    // DocumentListener
    void documentChanged(DocumentChangeType type) override;
    void pageChanged(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;

Q_SIGNALS:
    /// The text of these pages changed (read, edited).
    void textChanged(const std::vector<size_t>& pages);
    /// A page came (+1) or went (-1) at this place; the index has taken it into account.
    void pageMoved(size_t page, int delta);
    /// All pages anew (another document content).
    void reset();
    void layoutReady(int pdfPage);
    /// The PDF text of all pages is known now.
    void completed();

private:
    struct Page {
        int pdf = -1;       ///< the PDF page it shows
        QString elements;   ///< the texts of its text elements, simplified, joined by '\n'
        bool dirty = true;  ///< read it from the document again
        std::shared_ptr<const words::Vocabulary> words;  ///< of `elements` (null: not made yet)
    };
    /// The vocabularies of a page's texts (made if needed)
    const words::Vocabulary* elementWords(size_t page);
    const words::Vocabulary* pdfWordsOf(int pdfPage);
    struct Worker;
    void rebuild();
    void refresh(size_t page);
    void refreshDirty();
    void setPdfText(int pdfPage, QString text, std::vector<size_t>& changed);
    void received(std::vector<std::pair<int, QString>> texts);
    void receivedLayout(int pdfPage, std::shared_ptr<const PdfPageLayout> layout);
    void wantText();
    void countUnknown();

    DocumentSession& session;
    fs::path pdf;
    std::vector<Page> pages;
    std::vector<QString> pdfText;  ///< by PDF page
    std::vector<std::shared_ptr<const words::Vocabulary>> pdfWords;  ///< by PDF page, of its text (null: not made)
    std::vector<char> pdfKnown;
    size_t unknownPages = 0;
    bool started = false;
    int readCount = 0, seededCount = 0;
    std::list<std::pair<int, std::shared_ptr<const PdfPageLayout>>> layouts;  ///< most recently used first
    std::shared_ptr<Worker> worker;
    QTimer startTimer, dirtyTimer;
};

}  // namespace xqt

/*
 * xournal-qt: editing Markdown text (see qt/src/markdown/MdBox.h) in the editor beside the page.
 *
 * The page's Markdown text is the box at the top-left margin in the layer "Markdown". It flows onto the next pages:
 * pages whose box continues the one before (MdPaginate) are one text. While typing, the text is split onto the pages
 * again; pages are added after them when it gets longer (the same size and background; a PDF page's are plain) and
 * the pages added go again when it gets shorter. At the end, pages that only held an emptied box go as well.
 *
 * Other boxes in the layer are text boxes placed with the text tool; beginBox edits one of them (or a new one at a
 * point). They do not flow. On a sticky note beginBox edits the note's one Markdown text, in the note's layer, at
 * its top left and as wide as the note (qt/docs/sticky-notes.md, "Notes as containers").
 *
 * A new box goes into a layer "Markdown" at the bottom of the page (ink written with the pen goes on top of it, into
 * the layer it went into before); the page's text from the top-left margin to the right margin, a text box from its
 * point to the right margin. Its text is the Markdown source: Xournal++ shows the source, xournal-qt draws it
 * formatted. The boxes change while typing; the whole edit, pages included, is one undo step.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <QObject>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageRef.h"

#include "MdLayout.h"
#include "MdPaginate.h"

class GroupUndoAction;
class Text;

namespace xqt {

class DocumentSession;

class MarkdownSession final: public QObject {
    Q_OBJECT
public:
    explicit MarkdownSession(DocumentSession& session, QObject* parent = nullptr);
    ~MarkdownSession() override;

    /// Start editing the Markdown text of a page, with the pages it flows onto (a new box gets `style`'s font, size
    /// and color). Returns its source.
    std::string begin(size_t page, const md::Style& style);
    /// Start editing the Markdown text box drawn at a point of the page, or a new one there (made at the first
    /// change). Returns its source.
    std::string beginBox(size_t page, const md::Style& style, double x, double y);
    /// The page's text (not a text box) is edited.
    bool isPageText() const { return pageText; }
    /// A sticky note's text is edited: its width is the note's (no width of its own)
    bool isNoteText() const { return !pageText && !chain.empty() && chain.front().noteWidth > 0; }
    bool active() const { return !chain.empty(); }
    /// The first and the last page of the text (0-based; npos: not active).
    size_t pageIndex() const;
    size_t lastPageIndex() const;
    /// Replace the source. Returns how far the text goes below a page's bottom margin (points; 0: it fits). The
    /// page's text flows onto the next pages: only a block higher than a page goes below one.
    double update(const std::string& source);
    /// The size of the body text (points; the text's font size): the drawing follows. Returns the overflow.
    double setFontSize(double size);
    double fontSize() const { return style.size; }
    /// The width of a text box (points, its wrap width): its text flows anew. Returns the overflow. Not for the
    /// page's text, whose width is the page's between its margins.
    double setWidth(double width);
    /// Done. The edit is one undo step (made at the first change, so the document counts as modified).
    void finish();
    /// Back to the text as it was (pages added go again).
    void cancel();

    /// The text being edited, and what of it each page holds (the page's text: a part per page, in order).
    struct PagePart {
        PageRef page;
        Text* box = nullptr;  ///< nullptr: none yet (a new text box before its first change)
        double x = 0;         ///< the box's top left
        double y = 0;
        md::Part part;        ///< the box's text is part.prefix bytes, then text()[part.begin, part.end), ...
    };
    const std::string& text() const { return last; }
    std::vector<PagePart> parts() const;
    /// The style of a new box (its font, size and color).
    const md::Style& boxStyle() const { return style; }

private:
    /// A page of the text and its box.
    struct Page {
        PageRef page;
        Layer* layer = nullptr;            ///< its layer "Markdown" (a sticky note's: the note's layer)
        Text* box = nullptr;               ///< nullptr: none yet
        double noteWidth = 0;              ///< a sticky note's text: the width the note gives it (else 0)
        std::unique_ptr<Text> original;    ///< a copy of the box at the start (nullptr: there was none)
        double x = 0;                      ///< the box's top left
        double y = 0;
        bool createdPage = false;          ///< added by this edit
        bool createdLayer = false;
        Layer::Index selectedBefore = 0;
        bool recorded = false;             ///< its box is in the undo step
    };

    std::string start(size_t page, const md::Style& style, bool pageText, double x, double y);
    /// A page of the text: its Markdown layer (made if needed) and the box at (x, y) (for a text box: drawn there).
    Page pageOf(const PageRef& page, double x, double y);
    /// Set the text of a page's box (made if needed; the first change of a box goes into the undo step).
    void setBox(Page& p, const std::string& text);
    /// The page's text split onto the pages (pages added or removed).
    double distribute(const std::string& source);
    double overflow(const Page& p) const;
    /// Pages added and removed while typing (not undo steps of their own).
    PageRef addPageAfter(const PageRef& after);
    void removePage(const PageRef& page);
    size_t indexOf(const PageRef& page) const;
    void changedOnPage(const PageRef& page);
    /// A text file on one continuous page: the page is as high as the text.
    void fitContinuousPage();
    void end();

    DocumentSession& session;
    std::vector<Page> chain;
    md::Style style;
    std::string last;
    std::vector<md::Part> ranges;     ///< what of the text each page of `chain` holds
    md::Pagination split;             ///< the last split onto the pages, of the text `splitText` (typing: splits again
    std::string splitText;            ///< from there, only the pages around the change)
    GroupUndoAction* undo = nullptr;  ///< the edit's undo step (on the undo stack since the first change)
    bool pageText = true;
};

}  // namespace xqt

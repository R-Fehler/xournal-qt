/*
 * xournal-qt: editing the Markdown text of a page (see qt/src/markdown/MdBox.h), in the editor beside the page.
 *
 * The page's Markdown text is the box at the top-left margin in the layer "Markdown" (other boxes in that layer are
 * text boxes placed with the text tool; they are edited on the page). A new box goes into a layer "Markdown" at the
 * bottom of the page (ink written with the pen goes on top of it, into the layer it went into before), from the
 * top-left margin to the right margin. Its text is the Markdown source: Xournal++ shows the source, xournal-qt
 * draws it formatted. The box changes while typing; the edit is one undo step (as TextFlowSession).
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

class Text;

namespace xqt {

class DocumentSession;

class MarkdownSession final: public QObject {
    Q_OBJECT
public:
    explicit MarkdownSession(DocumentSession& session, QObject* parent = nullptr);
    ~MarkdownSession() override;

    /// Start editing the Markdown text of a page (a new box gets `style`'s font, size and color). Returns its source.
    std::string begin(size_t page, const md::Style& style);
    bool active() const { return static_cast<bool>(page); }
    size_t pageIndex() const;
    /// Replace the source. Returns how far the content goes below the bottom margin (points; 0: it fits).
    double update(const std::string& source);
    /// The size of the body text (points; the text's font size): the drawing follows. Returns the overflow.
    double setFontSize(double size);
    double fontSize() const { return style.size; }
    /// Done. The edit is one undo step (made at the first change, so the document counts as modified).
    void finish();
    /// Back to the box as it was.
    void cancel();

private:
    /// The box in the layer, changed in place (made at the first change if there is none).
    void apply(const std::string& source);
    void changedOnPage();
    double overflow() const;
    void end();

    DocumentSession& session;
    PageRef page;
    Layer* layer = nullptr;
    bool createdLayer = false;
    Layer::Index selectedBefore = 0;
    md::Style style;
    double boxX = 0;
    double boxY = 0;
    Text* box = nullptr;               ///< the box in the layer (nullptr: none yet)
    std::unique_ptr<Text> original;    ///< a copy of the box at the start (nullptr: there was none)
    std::string last;
    bool changed = false;
};

}  // namespace xqt

/*
 * xournal-qt: editing the Markdown box of a page (see qt/src/markdown/MdBox.h).
 *
 * A new box goes into a layer named "Markdown" at the bottom of the page (ink written with the pen goes on top of
 * it, into the layer it went into before), from the top-left page margin to the right margin. Its text is the
 * Markdown source: Xournal++ shows the source, xournal-qt draws it formatted. The box is replaced while typing;
 * finishing makes one undo step (as TextFlowSession).
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

    /// Start editing the box of a page (a new box gets `style`'s font and color). Returns its source.
    std::string begin(size_t page, const md::Style& style);
    bool active() const { return static_cast<bool>(page); }
    size_t pageIndex() const;
    /// Replace the source. Returns how far the content goes below the bottom margin (points; 0: it fits).
    double update(const std::string& source);
    /// Done. The edit is one undo step (made at the first change, so the document counts as modified).
    void finish();
    /// Back to the box as it was.
    void cancel();

private:
    void replaceBox(std::vector<ElementPtr> elements);
    std::unique_ptr<Text> makeBox(const std::string& source) const;
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
    std::vector<ElementPtr> original;  ///< copies of the texts of the Markdown layer at the start
    std::string last;
    bool changed = false;
};

}  // namespace xqt

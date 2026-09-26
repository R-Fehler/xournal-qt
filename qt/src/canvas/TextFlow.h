/*
 * DEPRECATED (2026-09-26): the text mode is no longer offered in the UI (Markdown written on the page replaces
 * it, qt/docs/text-mode.md); kept for now, with its tests.
 *
 * xournal-qt: typed text on a page, laid out like in a word processor (the text mode).
 *
 * Xournal++ compatible: the text is made of ordinary upstream Text elements, one per block (heading, paragraph,
 * list item), in a layer named "Text" at the bottom of the page. They are laid out from the top-left page margin
 * down and broken into lines at the right margin with line breaks (released Xournal++ versions have no wrap width;
 * reading the page back joins the lines again). A list item is its marker ("•", "1.") and its text as two
 * elements (a hanging indent Xournal++ shows the same way). Formatting is per block: font size, bold, italic (in the
 * font name, e.g. "Sans Bold Italic"), color; headings have fixed sizes. Xournal++ shows and edits these as normal
 * text boxes; xournal-qt reads the layer back into blocks (sizes and markers tell the kind of block).
 *
 * TextFlowSession edits the flow of one page: the elements are replaced while typing, finishing makes one undo step.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "model/Element.h"
#include "model/Layer.h"
#include "model/PageRef.h"
#include "util/Color.h"

namespace xqt {

class DocumentSession;

struct TextBlock {
    enum class Kind { Paragraph, Heading1, Heading2, Heading3, Bullet, Numbered };
    Kind kind = Kind::Paragraph;
    QString text;
    bool bold = false;
    bool italic = false;
    double size = 0;  ///< points (0: the body size); headings have fixed sizes
    Color color = Color(0, 0, 0);  ///< opaque (as Xournal++ saves text colors)
    int indent = 0;   ///< list level
    bool operator==(const TextBlock& o) const {
        return kind == o.kind && text == o.text && bold == o.bold && italic == o.italic && size == o.size &&
               color == o.color && indent == o.indent;
    }
};

namespace TextFlow {
constexpr const char* LAYER_NAME = "Text";
constexpr double MARGIN = 56.7;       ///< 2 cm (A5 and bigger; smaller pages less: session/PageMargins.h)
constexpr double LIST_INDENT = 20.0;  ///< per list level

struct Style {
    std::string family = "Sans";
    double bodySize = 12;
    double leftMargin = MARGIN;
    double rightMargin = MARGIN;
    double topMargin = MARGIN;
    double bottomMargin = MARGIN;
};
/// The style with the page's margins (PageMargins::of): smaller on pages smaller than A5, beside the margin line of a
/// ruled page with one (upstream's "lined").
Style styleFor(const PageRef& page, Style style);
double headingSize(TextBlock::Kind kind);
/// The page's text layer (nullptr if none).
Layer* textLayer(const PageRef& page);
/// The blocks of the page's text layer (with the page's margins, see styleFor).
std::vector<TextBlock> read(const PageRef& page, const Style& style);
/// A block to / from the editor (QML): { kind (int), text, bold, italic, size, color, indent }.
QVariantMap toVariant(const TextBlock& b);
TextBlock fromVariant(const QVariantMap& m);
/// Text elements for the blocks. `overflow`: how far the text goes below the bottom margin (0: it fits).
std::vector<ElementPtr> layout(const std::vector<TextBlock>& blocks, double pageWidth, double pageHeight,
                               const Style& style, double* overflow = nullptr);
}  // namespace TextFlow

class TextFlowSession final: public QObject {
    Q_OBJECT
public:
    explicit TextFlowSession(DocumentSession& session, QObject* parent = nullptr);
    ~TextFlowSession() override;

    /// Start editing the text of a page (the text layer is made if needed). Returns its blocks.
    std::vector<TextBlock> begin(size_t page, const TextFlow::Style& style);
    bool active() const { return static_cast<bool>(page); }
    /// Replace the text on the page. Returns how far it goes below the bottom margin (points; 0: it fits).
    double update(const std::vector<TextBlock>& blocks);
    /// Done. The edit is one undo step (made at the first change, so the document counts as modified).
    void finish();
    /// Back to the text as it was.
    void cancel();

private:
    void replaceTexts(std::vector<ElementPtr> elements);
    void end();

    DocumentSession& session;
    PageRef page;
    Layer* layer = nullptr;
    bool createdLayer = false;
    Layer::Index selectedBefore = 0;
    TextFlow::Style style;
    std::vector<ElementPtr> original;  ///< copies of the text elements at the start
    std::vector<TextBlock> last;
    bool changed = false;
};

}  // namespace xqt

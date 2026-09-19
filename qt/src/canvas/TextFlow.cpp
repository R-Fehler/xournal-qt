#include "TextFlow.h"

#include <QColor>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <regex>
#include <shared_mutex>

#include <pango/pango.h>

#include "control/Control.h"
#include "control/layer/LayerController.h"
#include "model/BackgroundConfig.h"
#include "model/Document.h"
#include "model/PageType.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"

namespace xqt {

namespace TextFlow {
namespace {
using Kind = TextBlock::Kind;

bool isList(Kind k) { return k == Kind::Bullet || k == Kind::Numbered; }
bool isHeading(Kind k) { return k == Kind::Heading1 || k == Kind::Heading2 || k == Kind::Heading3; }
/// Paragraph spacing (after a block) and the height of an empty paragraph.
double spacingAfter(double size) { return size * 0.35; }
double emptyLineHeight(double size) { return size * 1.25; }
double spaceBefore(Kind k, double size) { return isHeading(k) ? size * 0.6 : 0; }

std::string fontName(const Style& style, bool bold, bool italic) {
    return style.family + (bold ? " Bold" : "") + (italic ? " Italic" : "");
}

std::unique_ptr<Text> makeText(const QString& s, const std::string& font, double size, Color color, double x, double y,
                               double wrap) {
    auto t = std::make_unique<Text>();
    t->setText(s.toStdString());
    t->setFont(XojFont(font, size));
    t->setColor(color);
    t->setWrap(wrap);
    t->setTransformation(xoj::util::Matrix::TRANSLATION(x, y));
    return t;
}

/// Break the text into lines as Pango wraps it at the text's wrap width, as hard line breaks, and remove the wrap
/// width: released Xournal++ versions (up to 1.3) have no wrap width and would show one long line. The breaks come
/// after the spaces, so removing them gives the paragraph back (read()).
void breakIntoLines(Text& t) {
    auto layout = t.createPangoLayout();  // (width, font; the text is the caller's)
    const std::string text = t.getText();
    pango_layout_set_text(layout.get(), text.c_str(), static_cast<int>(text.length()));
    std::vector<int> starts;
    for (GSList* l = pango_layout_get_lines_readonly(layout.get()); l; l = l->next) {
        starts.push_back(static_cast<PangoLayoutLine*>(l->data)->start_index);
    }
    std::string broken;
    for (size_t i = 0; i < starts.size(); ++i) {
        const size_t from = static_cast<size_t>(starts[i]);
        const size_t to = i + 1 < starts.size() ? static_cast<size_t>(starts[i + 1]) : text.size();
        if (i > 0) {
            broken += '\n';
        }
        broken += text.substr(from, to - from);
    }
    t.setWrap(Text::NO_WRAP);
    t.setText(broken);
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

QVariantMap toVariant(const TextBlock& b) {
    return {{"kind", static_cast<int>(b.kind)},
            {"text", b.text},
            {"bold", b.bold},
            {"italic", b.italic},
            {"size", b.size},
            {"color", QColor(b.color.red, b.color.green, b.color.blue)},
            {"indent", b.indent}};
}

TextBlock fromVariant(const QVariantMap& m) {
    TextBlock b;
    b.kind = static_cast<Kind>(std::clamp(m.value("kind").toInt(), 0, 5));
    b.text = m.value("text").toString();
    b.bold = m.value("bold").toBool();
    b.italic = m.value("italic").toBool();
    b.size = m.value("size").toDouble();
    const QColor c = m.value("color").value<QColor>();
    if (c.isValid()) {
        b.color = Color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
    }
    b.indent = std::max(0, m.value("indent").toInt());
    return b;
}

Style styleFor(const PageRef& page, Style s) {
    const PageType bg = page->getBackgroundType();
    if (bg.format == PageTypeFormat::Lined) {
        double margin = 72;  // (upstream's default: 1 inch; negative: on the right)
        BackgroundConfig(bg.config).loadValue(background_config_strings::CFG_MARGIN, margin);
        if (margin >= 0) {
            s.leftMargin = std::max(s.leftMargin, margin + 10);
        } else {
            s.rightMargin = std::max(s.rightMargin, -margin + 10);
        }
    }
    return s;
}

double headingSize(Kind kind) {
    switch (kind) {
        case Kind::Heading1:
            return 24;
        case Kind::Heading2:
            return 18;
        case Kind::Heading3:
            return 15;
        default:
            return 0;
    }
}

Layer* textLayer(const PageRef& page) {
    for (Layer* l: page->getLayers()) {
        if (l->hasName() && l->getName() == LAYER_NAME) {
            return l;
        }
    }
    return nullptr;
}

std::vector<TextBlock> read(const PageRef& page, const Style& pageStyle) {
    const Style style = styleFor(page, pageStyle);  // (the margins of this page)
    std::vector<TextBlock> blocks;
    Layer* layer = textLayer(page);
    if (!layer) {
        return blocks;
    }
    std::vector<const Text*> texts;
    for (const auto& e: layer->getElementsView()) {
        if (e->getType() == ELEMENT_TEXT) {
            texts.push_back(static_cast<const Text*>(e));
        }
    }
    // In reading order (the layer keeps it, but texts added by hand may be anywhere)
    std::stable_sort(texts.begin(), texts.end(), [](const Text* a, const Text* b) {
        const auto& ba = a->getBoundingBox();
        const auto& bb = b->getBoundingBox();
        if (std::abs(ba.y - bb.y) > 2) {
            return ba.y < bb.y;
        }
        return ba.x < bb.x;
    });
    static const std::regex marker(R"(^(•|◦|▪|\d+[.)])$)");
    double expectedY = MARGIN;  // where the next block would start without empty lines
    for (size_t i = 0; i < texts.size(); ++i) {
        const Text* t = texts[i];
        const auto& box = t->getBoundingBox();
        TextBlock b;
        const Text* body = t;
        if (std::regex_match(t->getText(), marker) && i + 1 < texts.size() &&
            std::abs(texts[i + 1]->getBoundingBox().y - box.y) < 2 && texts[i + 1]->getBoundingBox().x > box.x) {
            b.kind = t->getText() == "•" || t->getText() == "◦" || t->getText() == "▪" ? Kind::Bullet : Kind::Numbered;
            b.indent = std::max(0, static_cast<int>(std::lround((box.x - style.leftMargin) / LIST_INDENT)));
            body = texts[++i];
        }
        const std::string name = body->getFontName();
        b.bold = name.find(" Bold") != std::string::npos;
        b.italic = endsWith(name, " Italic");
        b.size = body->getFontSize();
        b.color = body->getColor();
        b.color.alpha = 0xff;  // (texts are opaque; files have it, new elements may not)
        b.text = QString::fromStdString(body->getText()).remove('\n');  // (the line breaks of the layout)
        if (!isList(b.kind) && b.bold) {
            for (Kind k: {Kind::Heading1, Kind::Heading2, Kind::Heading3}) {
                if (std::abs(b.size - headingSize(k)) < 0.01) {
                    b.kind = k;
                }
            }
        }
        if (isHeading(b.kind)) {
            b.size = 0;
            b.bold = false;  // (headings are bold anyway)
        } else if (std::abs(b.size - style.bodySize) < 0.01) {
            b.size = 0;
        }
        // Empty paragraphs before it: the extra space
        const double size = isHeading(b.kind) ? headingSize(b.kind) : (b.size > 0 ? b.size : style.bodySize);
        const double extra = box.y - (expectedY + (blocks.empty() ? 0 : spaceBefore(b.kind, size)));
        const int emptyLines = static_cast<int>(std::floor(extra / emptyLineHeight(style.bodySize) + 0.3));
        for (int k = 0; k < emptyLines; ++k) {
            blocks.push_back(TextBlock{});
        }
        const double bottom = std::max(box.y + box.height, body->getBoundingBox().y + body->getBoundingBox().height);
        expectedY = bottom + spacingAfter(size);
        blocks.push_back(std::move(b));
    }
    return blocks;
}

std::vector<ElementPtr> layout(const std::vector<TextBlock>& blocks, double pageWidth, double pageHeight,
                               const Style& style, double* overflow) {
    std::vector<ElementPtr> elements;
    const double right = pageWidth - style.rightMargin;
    double y = MARGIN;
    std::vector<int> numbers;  // per list level: the next number
    for (size_t i = 0; i < blocks.size(); ++i) {
        const TextBlock& b = blocks[i];
        const bool heading = isHeading(b.kind);
        const double size = heading ? headingSize(b.kind) : (b.size > 0 ? b.size : style.bodySize);
        if (!isList(b.kind)) {
            numbers.clear();  // a numbered list starts again after other blocks
        }
        if (b.text.isEmpty()) {
            y += emptyLineHeight(style.bodySize);  // (no empty text elements: Xournal++ drops them)
            continue;
        }
        if (i > 0) {
            y += spaceBefore(b.kind, size);
        }
        const std::string font = fontName(style, heading || b.bold, b.italic);
        double x = style.leftMargin;
        if (isList(b.kind)) {
            const double markerX = style.leftMargin + b.indent * LIST_INDENT;
            QString markerText = QStringLiteral("•");
            if (b.kind == Kind::Numbered) {
                numbers.resize(static_cast<size_t>(b.indent) + 1, 1);
                markerText = QString::number(numbers[static_cast<size_t>(b.indent)]++) + '.';
            }
            if (static_cast<size_t>(b.indent) + 1 < numbers.size()) {
                numbers.resize(static_cast<size_t>(b.indent) + 1);  // deeper levels start again
            }
            elements.push_back(makeText(markerText, font, size, b.color, markerX, y, Text::NO_WRAP));
            x = markerX + size * (b.kind == Kind::Numbered ? 1.8 : 1.2);
        }
        auto t = makeText(b.text, font, size, b.color, x, y, std::max(size * 4, right - x));
        breakIntoLines(*t);
        const double height = t->getBoundingBox().height;
        elements.push_back(std::move(t));
        y += height + spacingAfter(size);
    }
    if (overflow) {
        *overflow = std::max(0.0, y - (pageHeight - MARGIN));
    }
    return elements;
}
}  // namespace TextFlow

namespace {
/// The texts of the text layer before / after an edit of the flow (the same action undoes and redoes: it swaps).
class TextFlowUndoAction final: public UndoAction {
public:
    TextFlowUndoAction(const PageRef& p, Layer* layer, std::vector<ElementPtr> other):
            UndoAction("TextFlowUndoAction"), layer(layer), other(std::move(other)) {
        this->page = p;
    }
    bool undo(Control* control) override {
        swap(control);
        return true;
    }
    bool redo(Control* control) override {
        swap(control);
        return true;
    }
    std::string getText() override { return "Text"; }

private:
    void swap(Control* control) {
        Document* doc = control->getDocument();
        doc->lock();
        auto& elements = layer->getElements();
        std::vector<ElementPtr> texts;
        for (auto it = elements.begin(); it != elements.end();) {
            if ((*it)->getType() == ELEMENT_TEXT) {
                texts.push_back(std::move(*it));
                it = elements.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& e: other) {
            elements.push_back(std::move(e));
        }
        other = std::move(texts);
        doc->unlock();
        page->firePageChanged();
    }
    Layer* layer;
    std::vector<ElementPtr> other;
};
}  // namespace

TextFlowSession::TextFlowSession(DocumentSession& session, QObject* parent): QObject(parent), session(session) {}

TextFlowSession::~TextFlowSession() {
    if (active()) {
        finish();
    }
}

std::vector<TextBlock> TextFlowSession::begin(size_t pageNo, const TextFlow::Style& s) {
    if (active()) {
        finish();
    }
    session.clearSelectionEndText();
    Document* doc = session.getDocument();
    style = s;
    {
        std::shared_lock lock(*doc);
        if (pageNo >= doc->getPageCount()) {
            return {};
        }
        page = doc->getPage(pageNo);
        style = TextFlow::styleFor(page, s);
        layer = TextFlow::textLayer(page);
        createdLayer = !layer;
        selectedBefore = page->getSelectedLayerId();
    }
    if (!layer) {
        // At the bottom: writing with the pen goes on top of the text, into the layer it went into before.
        layer = new Layer();
        layer->setName(TextFlow::LAYER_NAME);
        session.getLayerController()->insertLayer(page, layer, 0);  // (locks the document)
        std::unique_lock lock(*doc);
        page->setSelectedLayerId(selectedBefore + 1);
    }
    {
        std::shared_lock lock(*doc);
        original.clear();
        for (const auto& e: layer->getElementsView()) {
            if (e->getType() == ELEMENT_TEXT) {
                original.push_back(e->clone());
            }
        }
    }
    std::vector<TextBlock> blocks;
    {
        std::shared_lock lock(*doc);
        blocks = TextFlow::read(page, style);
    }
    last = blocks;
    changed = false;
    return blocks;
}

void TextFlowSession::replaceTexts(std::vector<ElementPtr> elements) {
    Document* doc = session.getDocument();
    {
        std::unique_lock lock(*doc);
        auto& all = layer->getElements();
        std::erase_if(all, [](const ElementPtr& e) { return e->getType() == ELEMENT_TEXT; });
        for (auto& e: elements) {
            all.push_back(std::move(e));
        }
    }
    page->firePageChanged();
    size_t index = npos;
    {
        std::shared_lock lock(*doc);
        index = doc->indexOf(page);
    }
    if (index != npos) {
        session.firePageChanged(index);  // thumbnails
    }
}

double TextFlowSession::update(const std::vector<TextBlock>& blocks) {
    if (!active()) {
        return 0;
    }
    double overflow = 0;
    auto elements = TextFlow::layout(blocks, page->getWidth(), page->getHeight(), style, &overflow);
    if (blocks != last) {
        if (!changed) {
            // The undo step at the first change: the document is modified now (saving, autosave, the question
            // when closing). It swaps whatever text the page has then with the text from before.
            std::vector<ElementPtr> before;
            for (const auto& e: original) {
                before.push_back(e->clone());
            }
            session.getUndoRedoHandler()->addUndoAction(
                    std::make_unique<TextFlowUndoAction>(page, layer, std::move(before)));
        }
        changed = true;
        last = blocks;
        replaceTexts(std::move(elements));
    }
    return overflow;
}

void TextFlowSession::finish() {
    if (active()) {
        end();  // (the undo step is there since the first change)
    }
}

void TextFlowSession::cancel() {
    if (!active()) {
        return;
    }
    if (changed) {
        replaceTexts(std::move(original));  // (its undo step now swaps the same text: no change)
    }
    end();
}

void TextFlowSession::end() {
    // A text layer made for nothing goes again
    if (createdLayer && layer->getElements().empty()) {
        session.getLayerController()->removeLayer(page, layer);  // (locks the document)
        {
            std::unique_lock lock(*session.getDocument());
            page->setSelectedLayerId(selectedBefore);
        }
        delete layer;
        page->firePageChanged();
    }
    page = nullptr;
    layer = nullptr;
    original.clear();
    last.clear();
    changed = false;
}

}  // namespace xqt

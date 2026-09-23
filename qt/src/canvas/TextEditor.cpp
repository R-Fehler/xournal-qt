#include "TextEditor.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QTextBoundaryFinder>

#include <pango/pangocairo.h>

#include "control/ToolHandler.h"
#include "control/layer/LayerController.h"
#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Font.h"
#include "model/Layer.h"
#include "model/MarkdownText.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Color.h"
#include "util/Matrix.h"
#include "util/Range.h"
#include "util/raii/GObjectSPtr.h"
#include "view/overlays/OverlayView.h"

#include "CanvasPage.h"
#include "MdBox.h"
#include "TextFlow.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {
/// Draws the edited text on the page (upstream: TextEditionView).
class TextEditorView final: public xoj::view::OverlayView {
public:
    TextEditorView(const TextEditor* editor, xoj::view::Repaintable* parent): OverlayView(parent), editor(editor) {}
    void draw(cairo_t* cr) const override { editor->paint(cr); }
    bool isViewOf(const OverlayBase* overlay) const override { return overlay == editor; }

private:
    const TextEditor* editor;
};

constexpr double CURSOR_WIDTH = 1.2;  // pt
constexpr double FRAME_MARGIN = 3.0;  // pt

/// Layout of the text as the renderer does it (Text::createPangoLayout), with the given content.
xoj::util::GObjectSPtr<PangoLayout> layoutFor(const Text& text, const QByteArray& utf8) {
    auto layout = text.createPangoLayout();
    pango_layout_set_text(layout.get(), utf8.constData(), static_cast<int>(utf8.size()));
    return layout;
}
}  // namespace

TextEditor::TextEditor(DocumentSession& session, CanvasPage& page, double x, double y, const NewText& how):
        session(session), page(page), pageRef(page.getPage()) {
    // Port of TextEditor::initializeEditionAt
    Text* existing = nullptr;
    {
        std::shared_lock lock(*session.getDocument());
        // xournal-qt: a Markdown text drawn here (where it is drawn, not where its source would be)
        Layer* mdLayer = md::markdownLayer(pageRef);
        if (mdLayer && mdLayer->isVisible()) {
            existing = md::boxAt(*mdLayer, x, y);
        }
        if (existing) {
            layer = mdLayer;
            markdown = true;
        } else {
            layer = pageRef->getSelectedLayer();
            markdown = how.markdown || md::isMarkdownLayer(*layer);
            for (auto&& e: layer->getElements()) {
                if (e->getType() == ELEMENT_TEXT && e->hasBoundingBoxContaining(x, y)) {
                    existing = dynamic_cast<Text*>(e.get());
                    break;
                }
            }
        }
        if (existing) {
            original = existing;
            textElement = existing->cloneText();
            textElement->setMarkdown(false);  // xournal-qt: the source is edited (the layer makes it Markdown again)
            existing->setInEditing(true);  // the renderer skips it; this editor draws the copy
            content = QString::fromStdString(existing->getText());
        }
    }
    if (!existing) {
        ToolHandler* h = session.getToolHandler();
        textElement = std::make_unique<Text>();
        textElement->setColor(h->getColor());
        textElement->setFont(session.getSettings()->getFont());
        if (markdown) {
            // A Markdown text box: its own size, and as wide as there is room (up to the right margin)
            textElement->setFont(XojFont(session.getSettings()->getFont().getName(), how.markdownSize));
            double right = TextFlow::MARGIN;
            {
                std::shared_lock lock(*session.getDocument());
                right = TextFlow::styleFor(pageRef, TextFlow::Style{}).rightMargin;
            }
            textElement->setWrap(std::max(100.0, pageRef->getWidth() - right - x));
        } else {
            textElement->setAlignment(h->getTextAlignment());
            textElement->setJustify(h->getTextJustify());
        }
        textElement->setTransformation(
                xoj::util::Matrix::TRANSLATION(x, y - textElement->getBoundingBox().height / 2));
        if (markdown && !md::isMarkdownLayer(*layer)) {
            useMarkdownLayer();
        }
    } else {
        // (a Markdown text is drawn larger than its source: all of it)
        page.rerenderRect(0, 0, pageRef->getWidth(), pageRef->getHeight());
        mousePressed(x, y);
    }
    lastArea = area();
    changed(false);
}

TextEditor::~TextEditor() { finalize(); }

void TextEditor::useMarkdownLayer() {
    // The page's layer "Markdown", made if needed: at the bottom (ink goes on top, into the layer it went into)
    layer = md::markdownLayer(pageRef);
    if (layer) {
        return;
    }
    selectedBefore = pageRef->getSelectedLayerId();
    layer = new Layer();
    layer->setName(std::string(xoj::markdown::LAYER_NAME));
    session.getLayerController()->insertLayer(pageRef, layer, 0);  // (locks the document)
    std::unique_lock lock(*session.getDocument());
    pageRef->setSelectedLayerId(selectedBefore > 0 ? selectedBefore + 1 : 0);
    createdLayer = true;
}

double TextEditor::fontSize() const { return textElement->getFontSize(); }

std::unique_ptr<xoj::view::OverlayView> TextEditor::createView() {
    return std::make_unique<TextEditorView>(this, &page);
}

int TextEditor::toUtf8(int qIndex) const {
    return static_cast<int>(content.left(std::clamp(qIndex, 0, static_cast<int>(content.size()))).toUtf8().size());
}

int TextEditor::fromUtf8(int byteIndex) const {
    const QByteArray utf8 = content.toUtf8();
    return static_cast<int>(
            QString::fromUtf8(utf8.constData(), std::clamp(byteIndex, 0, static_cast<int>(utf8.size()))).size());
}

bool TextEditor::contains(double x, double y) const {
    const auto a = area();
    return x >= a.x && x <= a.x + a.width && y >= a.y && y <= a.y + a.height;
}

int TextEditor::indexAt(double x, double y) const {
    const auto local = textElement->getTransformation().inverse() * xoj::util::Point<double>(x, y);
    auto layout = layoutFor(*textElement, content.toUtf8());
    int index = 0, trailing = 0;
    pango_layout_xy_to_index(layout.get(), static_cast<int>(local.x * PANGO_SCALE),
                             static_cast<int>(local.y * PANGO_SCALE), &index, &trailing);
    return std::min(static_cast<int>(content.size()), fromUtf8(index) + trailing);
}

void TextEditor::mousePressed(double x, double y) {
    cursor = anchor = indexAt(x, y);
    changed(false);
}

void TextEditor::mouseMoved(double x, double y) {
    cursor = indexAt(x, y);  // drag: select
    changed(false);
}

void TextEditor::moveCursor(int to, bool keepAnchor) {
    cursor = std::clamp(to, 0, static_cast<int>(content.size()));
    if (!keepAnchor) {
        anchor = cursor;
    }
}

int TextEditor::wordBoundary(int from, bool forward) const {
    int i = from;
    const int n = static_cast<int>(content.size());
    if (forward) {
        while (i < n && !content[i].isLetterOrNumber()) {
            ++i;
        }
        while (i < n && content[i].isLetterOrNumber()) {
            ++i;
        }
    } else {
        while (i > 0 && !content[i - 1].isLetterOrNumber()) {
            --i;
        }
        while (i > 0 && content[i - 1].isLetterOrNumber()) {
            --i;
        }
    }
    return i;
}

int TextEditor::lineMove(int from, int lines) const {
    auto layout = layoutFor(*textElement, content.toUtf8());
    int line = 0, x = 0;
    pango_layout_index_to_line_x(layout.get(), toUtf8(from), false, &line, &x);
    const int target = line + lines;
    if (target < 0) {
        return 0;
    }
    if (target >= pango_layout_get_line_count(layout.get())) {
        return static_cast<int>(content.size());
    }
    PangoLayoutLine* l = pango_layout_get_line_readonly(layout.get(), target);
    int index = 0, trailing = 0;
    pango_layout_line_x_to_index(l, x, &index, &trailing);
    return std::min(static_cast<int>(content.size()), fromUtf8(index) + trailing);
}

int TextEditor::lineEdge(int from, bool end) const {
    auto layout = layoutFor(*textElement, content.toUtf8());
    int line = 0, x = 0;
    pango_layout_index_to_line_x(layout.get(), toUtf8(from), false, &line, &x);
    PangoLayoutLine* l = pango_layout_get_line_readonly(layout.get(), line);
    if (!end) {
        return fromUtf8(l->start_index);
    }
    int e = fromUtf8(l->start_index + l->length);
    // (a hard line break belongs to the line: stay before it)
    return e;
}

void TextEditor::removeSelection() {
    if (!hasSelection()) {
        return;
    }
    const int from = std::min(cursor, anchor), to = std::max(cursor, anchor);
    content.remove(from, to - from);
    cursor = anchor = from;
}

void TextEditor::insert(const QString& s) {
    removeSelection();
    content.insert(cursor, s);
    cursor += static_cast<int>(s.size());
    anchor = cursor;
}

bool TextEditor::wantsKey(const QKeyEvent* e) {
    const auto m = e->modifiers();
    if (m & Qt::ControlModifier) {
        return e->key() == Qt::Key_A || e->key() == Qt::Key_C || e->key() == Qt::Key_X || e->key() == Qt::Key_V ||
               e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Backspace ||
               e->key() == Qt::Key_Delete || e->key() == Qt::Key_Home || e->key() == Qt::Key_End;
    }
    switch (e->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Backspace:
        case Qt::Key_Delete:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
        case Qt::Key_Escape:
            return true;
        default:
            return !e->text().isEmpty() && e->text().at(0).isPrint() && !(m & Qt::AltModifier);
    }
}

bool TextEditor::keyPressed(const QKeyEvent* e, bool& finish) {
    finish = false;
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    auto grapheme = [&](int from, bool forward) {
        QTextBoundaryFinder f(QTextBoundaryFinder::Grapheme, content);
        f.setPosition(from);
        const qsizetype p = forward ? f.toNextBoundary() : f.toPreviousBoundary();
        return p < 0 ? from : static_cast<int>(p);
    };
    bool textChanged = false;
    switch (e->key()) {
        case Qt::Key_Left:
            moveCursor(ctrl ? wordBoundary(cursor, false) : (hasSelection() && !shift ? std::min(cursor, anchor)
                                                                                         : grapheme(cursor, false)),
                       shift);
            break;
        case Qt::Key_Right:
            moveCursor(ctrl ? wordBoundary(cursor, true) : (hasSelection() && !shift ? std::max(cursor, anchor)
                                                                                        : grapheme(cursor, true)),
                       shift);
            break;
        case Qt::Key_Up:
            moveCursor(lineMove(cursor, -1), shift);
            break;
        case Qt::Key_Down:
            moveCursor(lineMove(cursor, 1), shift);
            break;
        case Qt::Key_Home:
            moveCursor(ctrl ? 0 : lineEdge(cursor, false), shift);
            break;
        case Qt::Key_End:
            moveCursor(ctrl ? static_cast<int>(content.size()) : lineEdge(cursor, true), shift);
            break;
        case Qt::Key_Backspace:
            if (!hasSelection() && cursor > 0) {
                anchor = ctrl ? wordBoundary(cursor, false) : grapheme(cursor, false);
            }
            removeSelection();
            textChanged = true;
            break;
        case Qt::Key_Delete:
            if (!hasSelection() && cursor < content.size()) {
                anchor = ctrl ? wordBoundary(cursor, true) : grapheme(cursor, true);
            }
            removeSelection();
            textChanged = true;
            break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            insert("\n");
            textChanged = true;
            break;
        case Qt::Key_Tab:
            insert("\t");
            textChanged = true;
            break;
        case Qt::Key_Escape:
            finish = true;
            return true;
        case Qt::Key_A:
            if (ctrl) {
                anchor = 0;
                cursor = static_cast<int>(content.size());
                break;
            }
            [[fallthrough]];
        default:
            if (ctrl && (e->key() == Qt::Key_C || e->key() == Qt::Key_X)) {
                if (hasSelection()) {
                    const int from = std::min(cursor, anchor), to = std::max(cursor, anchor);
                    QGuiApplication::clipboard()->setText(content.mid(from, to - from));
                    if (e->key() == Qt::Key_X) {
                        removeSelection();
                        textChanged = true;
                    }
                }
                break;
            }
            if (ctrl && e->key() == Qt::Key_V) {
                insert(QGuiApplication::clipboard()->text());
                textChanged = true;
                break;
            }
            if (!ctrl && !e->text().isEmpty() && e->text().at(0).isPrint()) {
                insert(e->text());
                textChanged = true;
                break;
            }
            return false;
    }
    changed(textChanged);
    return true;
}

void TextEditor::inputMethodEvent(const QInputMethodEvent* e) {
    if (e->replacementLength() > 0) {
        const int from = std::clamp(cursor + e->replacementStart(), 0, static_cast<int>(content.size()));
        const int len = std::min(e->replacementLength(), static_cast<int>(content.size()) - from);
        content.remove(from, len);
        cursor = anchor = from;
    }
    if (!e->commitString().isEmpty()) {
        insert(e->commitString());
    }
    preedit = e->preeditString();
    changed(true);
}

QVariant TextEditor::inputMethodQuery(Qt::InputMethodQuery query) const {
    switch (query) {
        case Qt::ImEnabled:
            return true;
        case Qt::ImSurroundingText:
            return content;
        case Qt::ImCursorPosition:
            return cursor;
        case Qt::ImAnchorPosition:
            return anchor;
        case Qt::ImCurrentSelection:
            return content.mid(std::min(cursor, anchor), std::abs(cursor - anchor));
        case Qt::ImHints:
            return static_cast<int>(Qt::ImhMultiLine);
        case Qt::ImEnterKeyType:
            return static_cast<int>(Qt::EnterKeyReturn);
        default:
            return {};
    }
}

QRectF TextEditor::cursorRectOnPage() const {
    QString shown = content;
    shown.insert(cursor, preedit);
    auto layout = layoutFor(*textElement, shown.toUtf8());
    PangoRectangle strong;
    pango_layout_get_cursor_pos(layout.get(),
                                static_cast<int>(shown.left(cursor + static_cast<int>(preedit.size())).toUtf8().size()),
                                &strong, nullptr);
    const auto& m = textElement->getTransformation();
    const auto p0 = m * xoj::util::Point<double>(static_cast<double>(strong.x) / PANGO_SCALE,
                                                  static_cast<double>(strong.y) / PANGO_SCALE);
    const auto p1 = m * xoj::util::Point<double>(static_cast<double>(strong.x) / PANGO_SCALE + CURSOR_WIDTH,
                                                  static_cast<double>(strong.y + strong.height) / PANGO_SCALE);
    return QRectF(QPointF(p0.x, p0.y), QPointF(p1.x, p1.y)).normalized();
}

void TextEditor::setFont(const XojFont& font) {
    textElement->setFont(font);
    changed(true);
}

void TextEditor::setColor(uint32_t argb) {
    textElement->setColor(Color(argb));
    changed(false);
}

xoj::util::Rectangle<double> TextEditor::area() const {
    const auto box = textElement->getBoundingBox();
    const QRectF c = cursorRectOnPage();
    const double x0 = std::min(box.x, c.x()), y0 = std::min(box.y, c.y());
    const double x1 = std::max(box.x + box.width, c.right()), y1 = std::max(box.y + box.height, c.bottom());
    return {x0 - FRAME_MARGIN * 2, y0 - FRAME_MARGIN * 2, x1 - x0 + FRAME_MARGIN * 4, y1 - y0 + FRAME_MARGIN * 4};
}

void TextEditor::changed(bool textChanged) {
    if (textChanged) {
        QString shown = content;
        shown.insert(cursor, preedit);
        textElement->setText(shown.toStdString());
    }
    const auto now = area();
    page.flagDirtyRegion(Range(lastArea).unite(Range(now)));
    lastArea = now;
}

void TextEditor::paint(cairo_t* cr) const {
    cairo_save(cr);
    textElement->getTransformation().transformCairo(cr);
    QString shown = content;
    shown.insert(cursor, preedit);
    const QByteArray utf8 = shown.toUtf8();
    auto layout = layoutFor(*textElement, utf8);
    pango_cairo_update_layout(cr, layout.get());
    pango_context_set_matrix(pango_layout_get_context(layout.get()), nullptr);  // like TextView::initPango

    const Color selectionColor = session.getSettings()->getSelectionColor();
    // Frame (upstream draws the text box while editing).
    PangoRectangle logical;
    pango_layout_get_extents(layout.get(), nullptr, &logical);
    const double fw = std::max(20.0, static_cast<double>(logical.width) / PANGO_SCALE);
    const double fh = static_cast<double>(logical.height) / PANGO_SCALE;
    Util::cairo_set_source_rgbi(cr, selectionColor, 0.6);
    cairo_set_line_width(cr, 0.8);
    const double dash[] = {3.0, 2.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_rectangle(cr, -FRAME_MARGIN, -FRAME_MARGIN, fw + 2 * FRAME_MARGIN, fh + 2 * FRAME_MARGIN);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // Selection
    if (hasSelection()) {
        const int from = static_cast<int>(content.left(std::min(cursor, anchor)).toUtf8().size());
        const int to = static_cast<int>(content.left(std::max(cursor, anchor)).toUtf8().size());
        PangoLayoutIter* it = pango_layout_get_iter(layout.get());
        do {
            PangoLayoutLine* line = pango_layout_iter_get_line_readonly(it);
            PangoRectangle lineRect;
            pango_layout_iter_get_line_extents(it, nullptr, &lineRect);
            int* ranges = nullptr;
            int n = 0;
            pango_layout_line_get_x_ranges(line, from, to, &ranges, &n);
            for (int i = 0; i < n; ++i) {
                cairo_rectangle(cr, static_cast<double>(ranges[2 * i]) / PANGO_SCALE,
                                static_cast<double>(lineRect.y) / PANGO_SCALE,
                                static_cast<double>(ranges[2 * i + 1] - ranges[2 * i]) / PANGO_SCALE,
                                static_cast<double>(lineRect.height) / PANGO_SCALE);
            }
            g_free(ranges);
        } while (pango_layout_iter_next_line(it));
        pango_layout_iter_free(it);
        Util::cairo_set_source_rgbi(cr, selectionColor, 0.3);
        cairo_fill(cr);
    }

    // Text
    Util::cairo_set_source_rgbi(cr, textElement->getColor());
    pango_cairo_show_layout(cr, layout.get());

    // Preedit (input method): underlined
    if (!preedit.isEmpty()) {
        const int pFrom = static_cast<int>(shown.left(cursor).toUtf8().size());
        const int pTo = static_cast<int>(shown.left(cursor + static_cast<int>(preedit.size())).toUtf8().size());
        PangoRectangle a, b;
        pango_layout_index_to_pos(layout.get(), pFrom, &a);
        pango_layout_index_to_pos(layout.get(), pTo, &b);
        const double y = static_cast<double>(a.y + a.height) / PANGO_SCALE;
        cairo_set_line_width(cr, 0.8);
        cairo_move_to(cr, static_cast<double>(a.x) / PANGO_SCALE, y);
        cairo_line_to(cr, static_cast<double>(b.x) / PANGO_SCALE, y);
        cairo_stroke(cr);
    }

    // Cursor
    PangoRectangle strong;
    pango_layout_get_cursor_pos(
            layout.get(), static_cast<int>(shown.left(cursor + static_cast<int>(preedit.size())).toUtf8().size()),
            &strong, nullptr);
    cairo_rectangle(cr, static_cast<double>(strong.x) / PANGO_SCALE, static_cast<double>(strong.y) / PANGO_SCALE,
                    CURSOR_WIDTH, static_cast<double>(strong.height) / PANGO_SCALE);
    Util::cairo_set_source_rgbi(cr, textElement->getColor());
    cairo_fill(cr);
    cairo_restore(cr);
}

void TextEditor::finalize() {
    finalizeText();
    if (markdown) {
        // A Markdown layer made for nothing goes again
        if (createdLayer && layer->getElements().empty()) {
            session.getLayerController()->removeLayer(pageRef, layer);  // (locks the document)
            {
                std::unique_lock lock(*session.getDocument());
                pageRef->setSelectedLayerId(selectedBefore);
            }
            delete layer;
        }
        pageRef->firePageChanged();  // (drawn formatted again: larger than its source)
    }
}

void TextEditor::finalizeText() {
    // Port of TextEditor::finalizeEdition
    preedit.clear();
    Document* doc = session.getDocument();
    UndoRedoHandler* undo = session.getUndoRedoHandler();
    page.flagDirtyRegion(Range(lastArea));
    if (content.isEmpty()) {
        if (original) {  // an emptied text is deleted
            auto action = std::make_unique<DeleteUndoAction>(pageRef, true);
            doc->lock();
            auto [orig, index] = layer->removeElement(original);
            doc->unlock();
            if (orig) {
                original->setInEditing(false);  // (drawn again if the deletion is undone)
                const auto box = orig->getBoundingBox();
                action->addElement(layer, std::move(orig), index);
                undo->addUndoAction(std::move(action));
                page.rerenderRect(box.x, box.y, box.width, box.height);
            }
        }
        return;
    }
    textElement->setText(content.toStdString());
    if (original) {
        if (original->getText() == textElement->getText() && original->getFont().getName() ==
                                                                    textElement->getFont().getName() &&
            original->getFont().getSize() == textElement->getFont().getSize() &&
            original->getColor() == textElement->getColor()) {
            // Nothing changed: no undo step.
            original->setInEditing(false);
            const auto box = original->getBoundingBox();
            page.rerenderRect(box.x, box.y, box.width, box.height);
            return;
        }
        doc->lock();
        auto [orig, index] = layer->removeElement(original);
        Text* ptr = textElement.get();
        layer->addElement(std::move(textElement));
        doc->unlock();
        pageRef->fireElementChanged(ptr);
        if (orig) {
            original->setInEditing(false);
            const auto box = orig->getBoundingBox();
            page.rerenderRect(box.x, box.y, box.width, box.height);
            undo->addUndoAction(std::make_unique<TextBoxUndoAction>(pageRef, layer, ptr, std::move(orig)));
        } else {
            undo->addUndoAction(std::make_unique<InsertUndoAction>(pageRef, layer, ptr));
        }
    } else {
        Text* ptr = textElement.get();
        doc->lock();
        layer->addElement(std::move(textElement));
        doc->unlock();
        pageRef->fireElementChanged(ptr);
        undo->addUndoAction(std::make_unique<InsertUndoAction>(pageRef, layer, ptr));
    }
}

}  // namespace xqt

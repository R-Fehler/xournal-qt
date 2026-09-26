#include "MarkdownEditor.h"
#include "session/DocumentLink.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <regex>
#include <shared_mutex>

#include <QClipboard>
#include <QMimeData>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>

#include <pango/pangocairo.h>

#include "control/settings/Settings.h"
#include "model/Document.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "util/Color.h"
#include "util/Range.h"
#include "view/overlays/OverlayView.h"

#include "CanvasPage.h"
#include "CanvasView.h"
#include "Grapheme.h"
#include "MdBox.h"
#include "MdDocument.h"
#include "MdTexDelimiters.h"
#include "MdText.h"
#include "TextEditor.h"

namespace xqt {

namespace {
/// Draws the box being written on its page.
class MarkdownEditorView final: public xoj::view::OverlayView {
public:
    MarkdownEditorView(const MarkdownEditor* editor, CanvasPage* page):
            OverlayView(page), editor(editor), page(page) {}
    void draw(cairo_t* cr) const override {
        if (&editor->getPage() == page) {
            editor->paint(cr);
        }
    }
    bool isViewOf(const OverlayBase* overlay) const override { return overlay == editor; }

private:
    const MarkdownEditor* editor;
    const CanvasPage* page;
};

constexpr double CURSOR_WIDTH = 1.2;  // pt
constexpr double FRAME_MARGIN = 3.0;  // pt

bool isContinuation(unsigned char c) { return (c & 0xC0) == 0x80; }
bool isWordByte(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

/// A list or quote mark at the start of a line ("  - ", "1. ", "- [ ] ", "> ") or a heading's ("## ").
const std::regex& linePrefix() {
    static const std::regex r(R"(^(\s*)(#{1,6} |[-*+] \[[ xX]\] |[-*+] |\d+[.)] |> )?)");
    return r;
}
}  // namespace

// --- start and end -----------------------------------------------------------------------------------------------

MarkdownEditor::MarkdownEditor(CanvasView& view, DocumentSession& session, size_t pageNo, bool pageText, double x,
                               double y, const md::Style& style):
        view(view), session(session), md(session) {
    if (pageText) {
        md.begin(pageNo, style);
    } else {
        md.beginBox(pageNo, style, x, y);
    }
    plain = md.boxStyle().plain || (md.text().empty() && style.plain);
    parts = md.parts();
    PageRef tapped;
    {
        std::shared_lock lock(*session.getDocument());
        tapped = session.getDocument()->getPage(pageNo);
    }
    size_t start = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (parts[i].page == tapped) {
            start = i;
            break;
        }
    }
    // The cursor where the text was tapped (as it is drawn: no cursor yet)
    current = parts.size();
    if (!parts.empty()) {
        caret = anchor = parts[start].box ? hit(start, x, y) : parts[start].part.begin;
        setCurrent(partOf(caret));
    }
    changed(false);
}

MarkdownEditor::~MarkdownEditor() {
    if (page && view.indexOf(page)) {
        page->removeOverlayViewsOf(this);
    }
    const PageRef edited = editing ? parts[std::min(current, parts.size() - 1)].page : nullptr;
    if (editing) {
        md::setWritingCursor(*editing, md::NO_SOURCE);
        std::unique_lock lock(*session.getDocument());
        editing->setInEditing(false);
    }
    md.finish();  // (one undo step)
    if (edited) {
        edited->firePageChanged();  // (drawn by the renderer again)
        drawnAsWrittenChanged(edited);
    }
}

void MarkdownEditor::cancel() {
    if (editing) {
        md::setWritingCursor(*editing, md::NO_SOURCE);
        std::unique_lock lock(*session.getDocument());
        editing->setInEditing(false);
    }
    editing = nullptr;
    md.cancel();
}

MarkdownEditor::Target MarkdownEditor::target() const {
    Target t;
    t.pageText = md.isPageText();
    t.page = md.pageIndex();
    if (!t.pageText && !parts.empty()) {
        const QPointF o = originOf(0);
        t.x = o.x() + 1;
        t.y = o.y() + 1;
    }
    return t;
}

// --- places ------------------------------------------------------------------------------------------------------

size_t MarkdownEditor::partOf(size_t offset) const {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (offset < parts[i].part.end) {
            return i;
        }
    }
    return parts.empty() ? 0 : parts.size() - 1;
}

size_t MarkdownEditor::localOf(size_t part, size_t offset) const {
    const md::Part& p = parts[part].part;
    return p.prefix + (std::clamp(offset, p.begin, p.end) - p.begin);
}

size_t MarkdownEditor::sourceOf(size_t part, size_t local) const {
    const md::Part& p = parts[part].part;
    return p.begin + std::min(p.end - p.begin, local > p.prefix ? local - p.prefix : 0);
}

std::string MarkdownEditor::shownText(size_t part) const {
    std::string s = parts[part].box ? parts[part].box->getText() : std::string();
    if (part == current && !preedit.empty()) {
        s.insert(std::min(localOf(part, caret), s.size()), preedit);
    }
    return s;
}

md::Style MarkdownEditor::styleOf(size_t part) const {
    return parts[part].box ? md::styleOf(*parts[part].box) : md.boxStyle();
}

QPointF MarkdownEditor::originOf(size_t part) const {
    if (const Text* box = parts[part].box) {
        const auto& at = box->getTransformation().shift;
        return {at.x, at.y};
    }
    return {parts[part].x, parts[part].y};
}

const md::Layout& MarkdownEditor::layoutOf(size_t part) const {
    const size_t active = part == current ? localOf(part, caret) + preedit.size() : md::NO_SOURCE;
    return md::cachedLayout(shownText(part), styleOf(part), active);
}

size_t MarkdownEditor::hit(size_t part, double x, double y) const {
    const md::Layout& layout = layoutOf(part);
    const QPointF o = originOf(part);
    const double bx = x - o.x();
    const double by = y - o.y();
    // The text drawn nearest to the point
    const md::Item* best = nullptr;
    double bestDistance = 0;
    for (const md::Item& it: layout.items) {
        if (it.kind != md::Item::Kind::Text || it.sources.empty()) {
            continue;
        }
        const double dy = by < it.y ? it.y - by : (by > it.y + it.height ? by - it.y - it.height : 0);
        const double dx = bx < it.x ? it.x - bx : (bx > it.x + it.width ? bx - it.x - it.width : 0);
        const double d = dy * 1000 + dx;  // (the line first)
        if (!best || d < bestDistance) {
            best = &it;
            bestDistance = d;
        }
    }
    if (!best) {
        return parts[part].part.begin;
    }
    int index = 0;
    int trailing = 0;
    pango_layout_xy_to_index(best->layout.get(),
                             static_cast<int>(std::clamp(bx - best->x, 0.0, best->width) * PANGO_SCALE),
                             static_cast<int>(std::clamp(by - best->y, 0.0, best->height - 0.01) * PANGO_SCALE),
                             &index, &trailing);
    const std::string_view laid = pango_layout_get_text(best->layout.get());
    // On a formula drawn: into its source (its start or end, by the half tapped), not after its closing "$"
    for (const md::SourceMap& m: best->sources) {
        if ((m.flags & md::Math) && m.source != md::NO_SOURCE && index >= m.start && index < m.start + m.length &&
            static_cast<size_t>(m.length) != m.sourceLength) {
            size_t local = trailing > 0 ? m.source + m.sourceLength : m.source;
            if (part == current && !preedit.empty() && local > localOf(part, caret)) {
                local = local >= localOf(part, caret) + preedit.size() ? local - preedit.size() : localOf(part, caret);
            }
            return sourceOf(part, local);
        }
    }
    // After the character when the point is on its second half
    for (; trailing > 0 && static_cast<size_t>(index) < laid.size(); --trailing) {
        ++index;
        while (static_cast<size_t>(index) < laid.size() && isContinuation(static_cast<unsigned char>(laid[index]))) {
            ++index;
        }
    }
    // That place in the source (through the runs of the drawn text)
    size_t local = md::NO_SOURCE;
    for (size_t k = 0; k < best->sources.size(); ++k) {
        const md::SourceMap& m = best->sources[k];
        const bool last = k + 1 == best->sources.size();
        if (index < m.start || (index >= m.start + m.length && !(last && index == m.start + m.length))) {
            continue;
        }
        if (m.source == md::NO_SOURCE) {  // (a made-up break: where the next text starts)
            for (size_t n = k + 1; n < best->sources.size() && local == md::NO_SOURCE; ++n) {
                local = best->sources[n].source;
            }
        } else if (m.sourceLength == static_cast<size_t>(m.length)) {
            local = m.source + static_cast<size_t>(index - m.start);
        } else {
            local = index > m.start ? m.source + m.sourceLength : m.source;  // (an entity: before or after it)
        }
        break;
    }
    if (local == md::NO_SOURCE) {  // (after the end of the drawn text)
        const md::SourceMap& lastRun = best->sources.back();
        local = lastRun.source != md::NO_SOURCE ? lastRun.source + lastRun.sourceLength
                                                : localOf(part, parts[part].part.end);
    }
    // (the input method's text is not in the source)
    if (part == current && !preedit.empty() && local > localOf(part, caret)) {
        local = local >= localOf(part, caret) + preedit.size() ? local - preedit.size() : localOf(part, caret);
    }
    return sourceOf(part, local);
}

QRectF MarkdownEditor::boxRect(size_t part) const {
    const md::Layout& layout = layoutOf(part);
    const md::Style s = styleOf(part);
    const QPointF o = originOf(part);
    return {o.x(), o.y(), s.width, std::max(layout.height, s.size * 1.3)};
}

QRectF MarkdownEditor::caretRect() const {
    if (parts.empty()) {
        return {};
    }
    const md::Layout& layout = layoutOf(current);
    const QPointF o = originOf(current);
    const double lineHeight = styleOf(current).size * 1.25;
    if (layout.rawItem < 0) {
        return {o.x(), o.y(), CURSOR_WIDTH, lineHeight};
    }
    const md::Item& it = layout.items[static_cast<size_t>(layout.rawItem)];
    const size_t local = localOf(current, caret) + preedit.size();
    const std::string_view laid = pango_layout_get_text(it.layout.get());
    const int byte = static_cast<int>(std::min(local > layout.rawBegin ? local - layout.rawBegin : 0, laid.size()));
    PangoRectangle strong;
    pango_layout_get_cursor_pos(it.layout.get(), byte, &strong, nullptr);
    return {o.x() + it.x + strong.x / static_cast<double>(PANGO_SCALE),
            o.y() + it.y + strong.y / static_cast<double>(PANGO_SCALE), CURSOR_WIDTH,
            std::max(1.0, strong.height / static_cast<double>(PANGO_SCALE))};
}

// --- drawing -----------------------------------------------------------------------------------------------------

void MarkdownEditor::paint(cairo_t* cr) const {
    if (parts.empty()) {
        return;
    }
    const md::Layout& layout = layoutOf(current);
    const md::Style s = styleOf(current);
    const QPointF o = originOf(current);
    const Color selectionColor = session.getSettings()->getSelectionColor();
    cairo_save(cr);
    cairo_translate(cr, o.x(), o.y());

    // The frame of the box being written (as the text tool's)
    Util::cairo_set_source_rgbi(cr, selectionColor, 0.6);
    cairo_set_line_width(cr, 0.8);
    const double dash[] = {3.0, 2.0};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_rectangle(cr, -FRAME_MARGIN, -FRAME_MARGIN, s.width + 2 * FRAME_MARGIN,
                    std::max(layout.height, s.size * 1.3) + 2 * FRAME_MARGIN);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // The selection: the drawn text of its source range
    if (hasSelection()) {
        const size_t from = localOf(current, std::min(caret, anchor));
        const size_t to = localOf(current, std::max(caret, anchor));
        for (const md::Item& it: layout.items) {
            if (it.kind != md::Item::Kind::Text) {
                continue;
            }
            for (const md::SourceMap& m: it.sources) {
                if (m.source == md::NO_SOURCE || m.source + m.sourceLength <= from || m.source >= to) {
                    continue;
                }
                int a = m.start;
                int b = m.start + m.length;
                if (m.sourceLength == static_cast<size_t>(m.length)) {
                    a = m.start + static_cast<int>(std::max(from, m.source) - m.source);
                    b = m.start + static_cast<int>(std::min(to, m.source + m.sourceLength) - m.source);
                }
                PangoLayoutIter* iter = pango_layout_get_iter(it.layout.get());
                do {
                    PangoLayoutLine* line = pango_layout_iter_get_line_readonly(iter);
                    PangoRectangle lineRect;
                    pango_layout_iter_get_line_extents(iter, nullptr, &lineRect);
                    int* ranges = nullptr;
                    int n = 0;
                    pango_layout_line_get_x_ranges(line, a, b, &ranges, &n);
                    for (int i = 0; i < n; ++i) {
                        cairo_rectangle(cr, it.x + ranges[2 * i] / static_cast<double>(PANGO_SCALE),
                                        it.y + lineRect.y / static_cast<double>(PANGO_SCALE),
                                        (ranges[2 * i + 1] - ranges[2 * i]) / static_cast<double>(PANGO_SCALE),
                                        lineRect.height / static_cast<double>(PANGO_SCALE));
                    }
                    g_free(ranges);
                } while (pango_layout_iter_next_line(iter));
                pango_layout_iter_free(iter);
            }
        }
        Util::cairo_set_source_rgbi(cr, selectionColor, 0.3);
        cairo_fill(cr);
    }

    md::draw(cr, layout);
    cairo_restore(cr);

    // The cursor
    const QRectF c = caretRect();
    cairo_rectangle(cr, c.x(), c.y(), c.width(), c.height());
    Util::cairo_set_source_rgbi(cr, s.color);
    cairo_fill(cr);
}

// --- changes -----------------------------------------------------------------------------------------------------

void MarkdownEditor::drawnAsWrittenChanged(const PageRef& p) {
    size_t index = npos;
    {
        std::shared_lock lock(*session.getDocument());
        index = session.getDocument()->indexOf(p);
    }
    if (index != npos) {
        Q_EMIT session.pageContentChanged(index);  // (the search marks what is drawn)
    }
}

void MarkdownEditor::setCurrent(size_t part) {
    part = std::min(part, parts.size() - 1);
    Text* box = parts[part].box;
    if (box != editing) {
        if (editing) {
            md::setWritingCursor(*editing, md::NO_SOURCE);
            {
                std::unique_lock lock(*session.getDocument());
                editing->setInEditing(false);
            }
            for (const Part& p: parts) {
                if (p.box == editing) {
                    p.page->firePageChanged();  // (drawn by the renderer again)
                    drawnAsWrittenChanged(p.page);
                }
            }
        }
        rawBegin = md::NO_SOURCE;
        if (box) {
            {
                std::unique_lock lock(*session.getDocument());
                box->setInEditing(true);
            }
            parts[part].page->firePageChanged();  // (drawn by this editor now)
        }
        editing = box;
    }
    current = part;
    CanvasPage* p = view.canvasPageOf(parts[part].page.get());
    if (p != page) {
        if (page && view.indexOf(page)) {  // (the page may be gone: pages added for the text go again)
            page->removeOverlayViewsOf(this);
        }
        page = p;
        if (page) {
            page->addOverlayView(std::make_unique<MarkdownEditorView>(this, page));
        }
        lastArea = {};
    }
}

void MarkdownEditor::changed(bool textChanged) {
    if (parts.empty()) {
        return;
    }
    (void)textChanged;  // (the session has the text already: edit())
    parts = md.parts();
    if (parts.empty()) {
        return;
    }
    setCurrent(partOf(caret));
    if (editing) {
        // The block with the cursor is drawn as its source: the search marks what is drawn (searched again when
        // another block is drawn as source; a change of the text is searched again anyway)
        md::setWritingCursor(*editing, localOf(current, caret));
        const size_t block = layoutOf(current).rawBegin;
        if (block != rawBegin) {
            rawBegin = block;
            drawnAsWrittenChanged(parts[current].page);
        }
    }
    // Repaint where the box was and is; show the cursor
    const QRectF now = boxRect(current).adjusted(-FRAME_MARGIN * 2, -FRAME_MARGIN * 2, FRAME_MARGIN * 2,
                                                  FRAME_MARGIN * 2);
    if (page) {
        const QRectF area = lastArea.isNull() ? now : lastArea.united(now);
        page->flagDirtyRegion(Range(area.left(), area.top(), area.right(), area.bottom()));
        if (const auto index = view.indexOf(page)) {
            const QRectF c = caretRect();
            view.getViewController().scrollToPageRect(*index, c.adjusted(-20, -20, 20, 20));
        }
    }
    lastArea = now;
    Q_EMIT view.markdownCursorChanged();
}

void MarkdownEditor::edit(size_t from, size_t to, const std::string& with, EditKind kind) {
    std::string text = md.text();
    from = std::min(from, text.size());
    to = std::clamp(to, from, text.size());
    // Undo in the text being written: typing goes together (up to a space or a line)
    const bool typing = kind == EditKind::Typing && from == to && with.find_first_of(" \n") == std::string::npos;
    if (typing && lastWasTyping && !undoStack.empty() && undoStack.back().removed.empty() &&
        undoStack.back().at + undoStack.back().inserted.size() == from) {
        undoStack.back().inserted += with;
    } else {
        undoStack.push_back({from, text.substr(from, to - from), with, caret});
    }
    lastWasTyping = typing;
    redoStack.clear();
    text.replace(from, to - from, with);
    caret = anchor = from + with.size();
    md.update(text);
    changed(false);
}

void MarkdownEditor::insert(const std::string& s, EditKind kind) {
    edit(std::min(caret, anchor), std::max(caret, anchor), s, kind);
}

void MarkdownEditor::removeSelection() {
    if (hasSelection()) {
        edit(std::min(caret, anchor), std::max(caret, anchor), "");
    }
}

void MarkdownEditor::moveCursor(size_t to, bool keepAnchor) {
    caret = std::min(to, md.text().size());
    if (!keepAnchor) {
        anchor = caret;
    }
    lastWasTyping = false;
    changed(false);
}

void MarkdownEditor::undoEdit(bool redo) {
    auto& from = redo ? redoStack : undoStack;
    auto& to = redo ? undoStack : redoStack;
    if (from.empty()) {
        return;
    }
    Change c = std::move(from.back());
    from.pop_back();
    std::string text = md.text();
    if (redo) {
        text.replace(std::min(c.at, text.size()), c.removed.size(), c.inserted);
        caret = anchor = std::min(c.at + c.inserted.size(), text.size());
    } else {
        text.replace(std::min(c.at, text.size()), c.inserted.size(), c.removed);
        caret = anchor = std::min(c.caretBefore, text.size());
    }
    to.push_back(std::move(c));
    lastWasTyping = false;
    md.update(text);
    changed(false);
}

void MarkdownEditor::setCursorPosition(size_t offset) {
    preedit.clear();
    moveCursor(offset, false);
}

void MarkdownEditor::setFontSize(double size) {
    md.setFontSize(size);
    changed(false);
}

// --- input -------------------------------------------------------------------------------------------------------

CanvasPage& MarkdownEditor::getPage() const { return *page; }

bool MarkdownEditor::contains(double x, double y) const {
    return !parts.empty() && boxRect(current).adjusted(-FRAME_MARGIN, -FRAME_MARGIN, FRAME_MARGIN, FRAME_MARGIN)
                                     .contains(x, y);
}

bool MarkdownEditor::tap(CanvasPage& onPage, double x, double y) {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (view.canvasPageOf(parts[i].page.get()) != &onPage) {
            continue;
        }
        const QRectF r = boxRect(i).adjusted(-FRAME_MARGIN, -FRAME_MARGIN, FRAME_MARGIN, FRAME_MARGIN);
        if (r.contains(x, y)) {
            preedit.clear();
            caret = anchor = hit(i, x, y);
            lastWasTyping = false;
            changed(false);
            return true;
        }
    }
    return false;
}

bool MarkdownEditor::tapAnywhere(CanvasPage& onPage, double x, double y) {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (view.canvasPageOf(parts[i].page.get()) == &onPage) {
            preedit.clear();
            caret = anchor = parts[i].box ? hit(i, x, y) : parts[i].part.begin;
            lastWasTyping = false;
            changed(false);
            return true;
        }
    }
    return false;
}

bool MarkdownEditor::toggleCheckBox(CanvasPage& onPage, double x, double y) {
    for (size_t i = 0; i < parts.size(); ++i) {
        if (view.canvasPageOf(parts[i].page.get()) != &onPage) {
            continue;
        }
        const QPointF o = originOf(i);
        const auto box = md::checkBoxAt(layoutOf(i), x - o.x(), y - o.y());
        if (!box || box->mark < parts[i].part.prefix) {
            continue;
        }
        const size_t mark = sourceOf(i, box->mark);
        const size_t keepCaret = caret;
        const size_t keepAnchor = anchor;
        preedit.clear();
        edit(mark, mark + 1, md::toggledTask(md.text(), mark).substr(mark, 1));
        caret = keepCaret;  // (the same length: the cursor stays where it was)
        anchor = keepAnchor;
        changed(false);
        return true;
    }
    return false;
}

void MarkdownEditor::mousePressed(double x, double y) {
    if (!toggleCheckBox(*page, x, y)) {
        tap(*page, x, y);
    }
}

void MarkdownEditor::mouseMoved(double x, double y) {
    if (page) {
        caret = hit(current, x, y);  // drag: select
        changed(false);
    }
}

size_t MarkdownEditor::prevChar(size_t pos) const { return text::graphemeStep(md.text(), pos, false); }

size_t MarkdownEditor::nextChar(size_t pos) const { return text::graphemeStep(md.text(), pos, true); }

size_t MarkdownEditor::wordBoundary(size_t pos, bool forward) const {
    const std::string& t = md.text();
    if (forward) {
        while (pos < t.size() && !isWordByte(static_cast<unsigned char>(t[pos]))) {
            ++pos;
        }
        while (pos < t.size() && isWordByte(static_cast<unsigned char>(t[pos]))) {
            ++pos;
        }
    } else {
        while (pos > 0 && !isWordByte(static_cast<unsigned char>(t[pos - 1]))) {
            --pos;
        }
        while (pos > 0 && isWordByte(static_cast<unsigned char>(t[pos - 1]))) {
            --pos;
        }
    }
    return pos;
}

size_t MarkdownEditor::verticalMove(bool down) const {
    // On the lines as drawn: the point above / below the cursor; past the box: the page before / after
    const QRectF c = caretRect();
    const double x = c.center().x();
    const QRectF box = boxRect(current);
    if (down) {
        const double y = c.bottom() + 2;
        if (y > box.bottom() && current + 1 < parts.size()) {
            const QPointF o = originOf(current + 1);
            return hit(current + 1, x - box.left() + o.x(), o.y() + 1);
        }
        return y > box.bottom() ? md.text().size() : hit(current, x, y);
    }
    const double y = c.top() - 2;
    if (y < box.top() && current > 0) {
        const QRectF before = boxRect(current - 1);
        return hit(current - 1, x - box.left() + before.left(), before.bottom() - 1);
    }
    return y < box.top() ? 0 : hit(current, x, y);
}

void MarkdownEditor::newLine(bool soft) {
    using namespace md::text;
    const std::string& t = md.text();
    const size_t from = std::min(caret, anchor);
    const size_t ls = lineStart(t, from);
    const std::string line = t.substr(ls, from - ls);
    if (plain) {
        // Plain text: a line, indented as this one (as Ghostwriter and most editors do)
        const size_t indent = line.find_first_not_of(" \t");
        insert("\n" + line.substr(0, indent == std::string::npos ? line.size() : indent), EditKind::Other);
        return;
    }
    // In a code block: a line, indented as this one
    const md::Document doc = md::parse(t);
    const auto spans = md::topLevelSpans(t, doc);
    // (a fence that is not closed goes on to the end of the text: the end is in the code as well)
    const auto openFence = [&](const md::BlockSpan& span) {
        const std::string fence = fenceOf(lineAt(t, span.begin));
        if (fence.empty()) {
            return false;
        }
        for (size_t l = nextLine(t, span.begin); l < span.end; l = nextLine(t, l)) {
            if (closesFence(lineAt(t, l), fence)) {
                return false;
            }
        }
        return true;
    };
    for (size_t i = 0; i < spans.size(); ++i) {
        const bool inside = from < spans[i].end || (from == t.size() && spans[i].end == t.size() && openFence(spans[i]));
        if (spans[i].begin <= from && inside && doc.root.children[i].kind == md::BlockKind::CodeBlock) {
            const size_t indent = line.find_first_not_of(' ');
            insert("\n" + std::string(indent == std::string::npos ? line.size() : indent, ' '), EditKind::Other);
            return;
        }
    }
    // In a formula block ("$$" and its lines) that is not closed yet: a line of the formula
    size_t para = ls;
    while (para > 0 && !blank(lineAt(t, lineStart(t, para - 1)))) {
        para = lineStart(t, para - 1);
    }
    size_t marks = 0;
    for (size_t i = t.find("$$", para); i != std::string::npos && i + 2 <= from; i = t.find("$$", i + 2)) {
        marks += i == 0 || t[i - 1] != '\\';
    }
    // (the same for a "\[" block, as chat apps write them: its last "\[" or "\]" before the cursor is a "\[")
    const auto lastMark = [&](const char* mark) {
        size_t last = std::string::npos;
        for (size_t i = t.find(mark, para); i != std::string::npos && i + 2 <= from; i = t.find(mark, i + 2)) {
            size_t backslashes = 0;
            while (i > backslashes && t[i - 1 - backslashes] == '\\') {
                ++backslashes;
            }
            if (backslashes % 2 == 0) {
                last = i;
            }
        }
        return last;
    };
    const size_t display = lastMark("\\[");
    const size_t displayEnd = lastMark("\\]");
    if (marks % 2 == 1 || (display != std::string::npos && (displayEnd == std::string::npos || displayEnd < display))) {
        insert("\n", EditKind::Other);
        return;
    }
    std::smatch m;
    std::regex_search(line, m, linePrefix());
    const std::string mark = m[2].matched ? m[2].str() : std::string();
    if (!mark.empty() && mark[0] != '#' && !soft) {
        std::string rest = line.substr(m[0].length());
        if (rest.find_first_not_of(" \t") == std::string::npos && from == lineEnd(t, from)) {
            edit(ls, from, "\n");  // an empty item: the list (or the quote) ends; what follows is a paragraph
            return;
        }
        std::string next = mark;
        static const std::regex number(R"(^(\d+)([.)]) $)");
        std::smatch n;
        if (std::regex_match(mark, n, number)) {
            next = std::to_string(std::stoul(n[1].str()) + 1) + n[2].str() + " ";
        } else if (mark.find("[x]") != std::string::npos || mark.find("[X]") != std::string::npos) {
            next = mark.substr(0, mark.find('[')) + "[ ] ";
        }
        insert("\n" + m[1].str() + next, EditKind::Other);
        return;
    }
    if (soft) {
        insert("\n", EditKind::Other);  // a line of the same paragraph
    } else if (blank(line) && lineAt(t, ls).find_first_not_of(" \t") == std::string_view::npos) {
        insert("\n", EditKind::Other);  // (on an empty line: one more)
    } else {
        insert("\n\n", EditKind::Other);  // a new paragraph
    }
}

size_t MarkdownEditor::emptyItemMark() const {
    using namespace md::text;
    if (plain) {
        return std::string::npos;
    }
    const std::string& t = md.text();
    const size_t ls = lineStart(t, caret);
    if (caret != ls + lineAt(t, ls).size()) {
        return std::string::npos;  // (only with the cursor at the end of the line)
    }
    const std::string line = t.substr(ls, caret - ls);
    std::smatch m;
    if (!std::regex_search(line, m, linePrefix()) || !m[2].matched || m[2].str()[0] == '#' ||
        static_cast<size_t>(m[0].length()) != line.size()) {
        return std::string::npos;
    }
    return ls + static_cast<size_t>(m[1].length());
}

void MarkdownEditor::applyEdit(const md::format::Edit& change) {
    preedit.clear();
    if (change.from != change.to || !change.with.empty()) {
        edit(change.from, change.to, change.with);
    }
    const size_t size = md.text().size();
    anchor = std::min(change.anchor, size);
    caret = std::min(change.caret, size);
    lastWasTyping = false;
    changed(false);
}

void MarkdownEditor::indent(bool in) {
    using namespace md::text;
    const std::string& t = md.text();
    const size_t ls = lineStart(t, caret);
    const size_t at = caret;
    if (plain) {
        if (in) {
            insert("\t", EditKind::Other);  // (plain text: a tab where the cursor is)
        } else if (ls < t.size() && t[ls] == '\t') {
            edit(ls, ls + 1, "");
            moveCursor(at > ls ? at - 1 : ls, false);
        } else {
            const size_t spaces = std::min<size_t>(4, lineAt(t, ls).find_first_not_of(' ') == std::string_view::npos
                                                                  ? lineAt(t, ls).size()
                                                                  : lineAt(t, ls).find_first_not_of(' '));
            if (spaces > 0) {
                edit(ls, ls + spaces, "");
                moveCursor(at >= ls + spaces ? at - spaces : ls, false);
            }
        }
        return;
    }
    if (in) {
        edit(ls, ls, "  ");
        moveCursor(at + 2, false);
    } else {
        const std::string_view line = lineAt(t, ls);
        const size_t n = std::min<size_t>(2, line.find_first_not_of(' ') == std::string_view::npos
                                                      ? line.size()
                                                      : line.find_first_not_of(' '));
        if (n > 0) {
            edit(ls, ls + n, "");
            moveCursor(at >= ls + n ? at - n : ls, false);
        }
    }
}

bool MarkdownEditor::wantsKeyEvent(const QKeyEvent* e) const {
    if (e->modifiers() & Qt::ControlModifier) {
        switch (e->key()) {
            case Qt::Key_A:
            case Qt::Key_C:
            case Qt::Key_X:
            case Qt::Key_V:
            case Qt::Key_Z:
            case Qt::Key_Y:
            case Qt::Key_B:
            case Qt::Key_I:
            case Qt::Key_E:
            case Qt::Key_K:
            case Qt::Key_0:
            case Qt::Key_1:
            case Qt::Key_2:
            case Qt::Key_3:
                return !plain;
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Home:
            case Qt::Key_End:
            case Qt::Key_Backspace:
            case Qt::Key_Delete:
                return true;
            default:
                return false;
        }
    }
    return TextEditor::wantsKey(e);
}

bool MarkdownEditor::keyPressed(const QKeyEvent* e, bool& finish) {
    finish = false;
    if (parts.empty()) {
        return false;
    }
    const bool ctrl = e->modifiers() & Qt::ControlModifier;
    const bool shift = e->modifiers() & Qt::ShiftModifier;
    const std::string& t = md.text();
    switch (e->key()) {
        case Qt::Key_Escape:
            finish = true;
            return true;
        case Qt::Key_Left:
            if (hasSelection() && !shift) {
                moveCursor(std::min(caret, anchor), false);
            } else {
                moveCursor(ctrl ? wordBoundary(caret, false) : prevChar(caret), shift);
            }
            return true;
        case Qt::Key_Right:
            if (hasSelection() && !shift) {
                moveCursor(std::max(caret, anchor), false);
            } else {
                moveCursor(ctrl ? wordBoundary(caret, true) : nextChar(caret), shift);
            }
            return true;
        case Qt::Key_Up:
        case Qt::Key_Down:
            moveCursor(verticalMove(e->key() == Qt::Key_Down), shift);
            return true;
        case Qt::Key_Home:
            moveCursor(ctrl ? 0 : md::text::lineStart(t, caret), shift);
            return true;
        case Qt::Key_End:
            moveCursor(ctrl ? t.size() : md::text::lineAt(t, md::text::lineStart(t, caret)).size() +
                                                 md::text::lineStart(t, caret),
                       shift);
            return true;
        case Qt::Key_Backspace:
            if (hasSelection()) {
                removeSelection();
            } else if (const size_t mark = emptyItemMark(); mark != std::string::npos) {
                edit(mark, caret, "");  // an empty list item or quote line: its mark goes at once (as in Ghostwriter)
            } else if (caret > 0) {
                edit(ctrl ? wordBoundary(caret, false) : prevChar(caret), caret, "");
            }
            return true;
        case Qt::Key_Delete:
            if (hasSelection()) {
                removeSelection();
            } else if (caret < t.size()) {
                edit(caret, ctrl ? wordBoundary(caret, true) : nextChar(caret), "");
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            newLine(shift);
            return true;
        case Qt::Key_Tab:
            indent(true);
            return true;
        case Qt::Key_Backtab:
            indent(false);
            return true;
        default:
            break;
    }
    if (ctrl) {
        switch (e->key()) {
            case Qt::Key_A:
                anchor = 0;
                caret = t.size();
                changed(false);
                return true;
            case Qt::Key_C:
            case Qt::Key_X:
                if (hasSelection()) {
                    const size_t from = std::min(caret, anchor);
                    QGuiApplication::clipboard()->setText(
                            QString::fromStdString(t.substr(from, std::max(caret, anchor) - from)));
                    if (e->key() == Qt::Key_X) {
                        removeSelection();
                    }
                }
                return true;
            case Qt::Key_V: {
                // A copied link (Copy link): as a Markdown link relative to this document
                if (const auto link = links::fromMime(QGuiApplication::clipboard()->mimeData())) {
                    insert(links::markdownFor(*link, session.documentFile()).toStdString(), EditKind::Other);
                    return true;
                }
                std::string pasted = QGuiApplication::clipboard()->text().toStdString();
                pasted.erase(std::remove(pasted.begin(), pasted.end(), '\r'), pasted.end());  // (the text's lines end in "\n")
                if (!plain) {
                    // Formulas as chat apps write them, \( \) and \[ \]: as $ $ and $$ $$ where they are formulas here
                    pasted = md::tex::convertPasted(t, std::min(caret, anchor), std::max(caret, anchor), pasted);
                }
                insert(pasted, EditKind::Other);
                return true;
            }
            case Qt::Key_Z:
                undoEdit(shift);
                return true;
            case Qt::Key_Y:
                undoEdit(true);
                return true;
            case Qt::Key_B:
            case Qt::Key_I:
            case Qt::Key_E:
            case Qt::Key_K:
            case Qt::Key_0:
            case Qt::Key_1:
            case Qt::Key_2:
            case Qt::Key_3:
                if (plain) {
                    return false;  // (plain text: no Markdown marks)
                }
                break;
            default:
                break;
        }
        // The formatting keys: the formatting bar's tools (md::format), each one undo step
        std::optional<md::format::Action> action;
        switch (e->key()) {
            case Qt::Key_B:
                action = md::format::Action::Bold;
                break;
            case Qt::Key_I:
                action = md::format::Action::Italic;
                break;
            case Qt::Key_E:
                action = md::format::Action::Code;
                break;
            case Qt::Key_K:
                action = md::format::Action::Link;
                break;
            case Qt::Key_0:
                action = md::format::Action::Paragraph;
                break;
            case Qt::Key_1:
                action = md::format::Action::Heading1;
                break;
            case Qt::Key_2:
                action = md::format::Action::Heading2;
                break;
            case Qt::Key_3:
                action = md::format::Action::Heading3;
                break;
            default:
                return false;
        }
        applyEdit(md::format::apply(t, anchor, caret, *action));
        return true;
    }
    const QString typed = e->text();
    if (!typed.isEmpty() && !(e->modifiers() & (Qt::AltModifier | Qt::MetaModifier)) && typed.at(0).isPrint()) {
        insert(typed.toStdString());
        return true;
    }
    return false;
}

void MarkdownEditor::inputMethodEvent(const QInputMethodEvent* e) {
    using namespace md::text;
    const std::string& t = md.text();
    if (e->replacementLength() > 0) {
        // (in the surrounding text, the line: UTF-16 around the cursor)
        const size_t ls = lineStart(t, caret);
        const QString line = QString::fromStdString(std::string(lineAt(t, ls)));
        const int cursor = static_cast<int>(QString::fromStdString(t.substr(ls, caret - ls)).size());
        const int from = std::clamp(cursor + e->replacementStart(), 0, static_cast<int>(line.size()));
        const int len = std::min(e->replacementLength(), static_cast<int>(line.size()) - from);
        const size_t a = ls + static_cast<size_t>(line.left(from).toUtf8().size());
        const size_t b = ls + static_cast<size_t>(line.left(from + len).toUtf8().size());
        preedit.clear();
        edit(a, b, e->commitString().toStdString());
    } else if (!e->commitString().isEmpty()) {
        preedit.clear();
        insert(e->commitString().toStdString());
    }
    preedit = e->preeditString().toStdString();
    changed(false);
}

QVariant MarkdownEditor::inputMethodQuery(Qt::InputMethodQuery query) const {
    using namespace md::text;
    const std::string& t = md.text();
    const size_t ls = lineStart(t, caret);
    switch (query) {
        case Qt::ImEnabled:
            return true;
        case Qt::ImSurroundingText:
            return QString::fromStdString(std::string(lineAt(t, ls)));
        case Qt::ImCursorPosition:
            return static_cast<int>(QString::fromStdString(t.substr(ls, caret - ls)).size());
        case Qt::ImAnchorPosition: {
            const size_t a = std::clamp(anchor, ls, ls + lineAt(t, ls).size());
            return static_cast<int>(QString::fromStdString(t.substr(ls, a - ls)).size());
        }
        case Qt::ImCurrentSelection: {
            const size_t from = std::min(caret, anchor);
            return QString::fromStdString(t.substr(from, std::max(caret, anchor) - from));
        }
        case Qt::ImHints:
            return static_cast<int>(Qt::ImhMultiLine);
        case Qt::ImEnterKeyType:
            return static_cast<int>(Qt::EnterKeyReturn);
        default:
            return {};
    }
}

QRectF MarkdownEditor::cursorRectOnPage() const { return caretRect(); }

std::string MarkdownEditor::textBeforeCursor() const {
    if (hasSelection()) {
        return {};
    }
    const std::string& t = md.text();
    const size_t ls = md::text::lineStart(t, caret);
    return t.substr(ls, caret - ls) + preedit;
}

void MarkdownEditor::replaceBeforeCursor(size_t bytes, const std::string& text) {
    // (the part of the input method's text before those bytes is typed; the rest goes with them)
    const size_t fromPreedit = std::min(bytes, preedit.size());
    const std::string kept = preedit.substr(0, preedit.size() - fromPreedit);
    preedit.clear();
    const size_t typed = std::min(bytes - fromPreedit, caret - md::text::lineStart(md.text(), caret));
    if (hasSelection() || typed == 0) {
        insert(kept + text, EditKind::Other);
    } else {
        edit(caret - typed, caret, kept + text, EditKind::Other);
    }
}

}  // namespace xqt

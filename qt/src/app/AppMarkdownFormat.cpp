/*
 * xournal-qt: the window's side of the Markdown formatting bar and the table editor (qt/docs/md-editor.md,
 * "Formatting bar"): the tools of md::format on the Markdown written on the page or in a .md (MarkdownEditor), and
 * on the source beside the page (its TextArea's document).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>

#include <QTextCursor>
#include <QTextDocument>

#include "AppController.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MdFormat.h"

using namespace xqt;

namespace {
/// A UTF-16 offset of a QString as a byte offset of its UTF-8 text.
size_t utf8Offset(const QString& s, int i) {
    return static_cast<size_t>(QStringView(s).left(std::clamp<qsizetype>(i, 0, s.size())).toUtf8().size());
}
/// A byte offset of a UTF-8 text as a UTF-16 offset.
int utf16Offset(const std::string& s, size_t b) {
    return static_cast<int>(QString::fromUtf8(s.data(), static_cast<qsizetype>(std::min(b, s.size()))).size());
}

QVariantMap stateMap(const md::format::State& s) {
    using List = md::format::State::List;
    return {{"bold", s.bold},
            {"italic", s.italic},
            {"strike", s.strike},
            {"code", s.code},
            {"math", s.math},
            {"link", s.link},
            {"heading", s.heading},
            {"list", s.list == List::Bullet     ? "bullet"
                     : s.list == List::Numbered ? "numbered"
                     : s.list == List::Task     ? "task"
                                                : ""},
            {"quote", s.quote},
            {"codeBlock", s.codeBlock},
            {"table", s.table}};
}

QString alignName(md::table::Align a) {
    switch (a) {
        case md::table::Align::Left:
            return "left";
        case md::table::Align::Center:
            return "center";
        case md::table::Align::Right:
            return "right";
        default:
            return "";
    }
}

QVariantMap tableMap(const std::string& text, size_t caret) {
    const auto found = md::table::at(text, caret);
    if (!found) {
        return {{"found", false}};
    }
    QVariantList cells;
    const auto row = [](const std::vector<std::string>& r) {
        QVariantList out;
        for (const std::string& c: r) {
            out.push_back(QString::fromStdString(c));
        }
        return out;
    };
    cells.push_back(row(found->table.header));
    for (const auto& r: found->table.rows) {
        cells.push_back(row(r));
    }
    QVariantList aligns;
    for (md::table::Align a: found->table.align) {
        aligns.push_back(alignName(a));
    }
    return {{"found", true},
            {"cells", cells},
            {"aligns", aligns},
            {"row", static_cast<int>(found->row)},
            {"column", static_cast<int>(found->column)}};
}

md::table::Table tableOf(const QVariantList& cells, const QStringList& aligns) {
    md::table::Table t;
    for (int r = 0; r < cells.size(); ++r) {
        std::vector<std::string> row;
        for (const QVariant& c: cells[r].toList()) {
            row.push_back(c.toString().toStdString());
        }
        if (r == 0) {
            t.header = std::move(row);
        } else {
            t.rows.push_back(std::move(row));
        }
    }
    for (const QString& a: aligns) {
        t.align.push_back(a == "left"     ? md::table::Align::Left
                          : a == "center" ? md::table::Align::Center
                          : a == "right"  ? md::table::Align::Right
                                          : md::table::Align::None);
    }
    t.align.resize(t.columns(), md::table::Align::None);
    for (auto& r: t.rows) {
        r.resize(t.columns());
    }
    return t;
}

/// A change of the source beside the page: one undo step of its document; the selection after it.
QVariantMap applyIn(QQuickTextDocument* document, const std::string& before, const md::format::Edit& e) {
    QTextDocument* d = document ? document->textDocument() : nullptr;
    if (!d) {
        return {};
    }
    std::string after = before;
    after.replace(e.from, e.to - e.from, e.with);
    if (e.from != e.to || !e.with.empty()) {
        QTextCursor c(d);
        c.setPosition(utf16Offset(before, e.from));
        c.setPosition(utf16Offset(before, e.to), QTextCursor::KeepAnchor);
        c.beginEditBlock();
        c.insertText(QString::fromStdString(e.with));
        c.endEditBlock();
    }
    return {{"anchor", utf16Offset(after, e.anchor)}, {"caret", utf16Offset(after, e.caret)}};
}
}  // namespace

QVariantMap AppController::markdownFormat() const {
    const CanvasView* v = canvas();
    const MarkdownEditor* editor = v ? v->getMarkdownEditor() : nullptr;
    if (!editor || editor->isPlain()) {
        return {};
    }
    return stateMap(md::format::stateAt(editor->text(), editor->anchorPosition(), editor->cursorPosition()));
}

bool AppController::formatMarkdown(const QString& action, const QString& arg) {
    const auto a = md::format::actionNamed(action.toStdString());
    CanvasView* v = canvas();
    if (!a || !v) {
        return false;
    }
    if (!v->getMarkdownEditor() && (textDocument() == "markdown" || v->typesIntoFlow())) {
        // (a .md or a text document of notes without a cursor yet: at the top of the page in view, as typing does)
        v->ensureTextEditor();
    }
    MarkdownEditor* editor = v->getMarkdownEditor();
    if (!editor || editor->isPlain()) {
        return false;
    }
    editor->applyEdit(md::format::apply(editor->text(), editor->anchorPosition(), editor->cursorPosition(), *a,
                                        arg.toStdString()));
    return true;
}

QVariantMap AppController::formatMarkdownIn(QQuickTextDocument* document, int anchor, int caret, const QString& action,
                                            const QString& arg) {
    const auto a = md::format::actionNamed(action.toStdString());
    if (!a || !document || !document->textDocument()) {
        return {};
    }
    const QString text = document->textDocument()->toPlainText();
    const std::string source = text.toStdString();
    return applyIn(document, source,
                   md::format::apply(source, utf8Offset(text, anchor), utf8Offset(text, caret), *a, arg.toStdString()));
}

QVariantMap AppController::markdownFormatOf(const QString& text, int anchor, int caret) const {
    return stateMap(md::format::stateAt(text.toStdString(), utf8Offset(text, anchor), utf8Offset(text, caret)));
}

QVariantMap AppController::markdownTable() const {
    const CanvasView* v = canvas();
    const MarkdownEditor* editor = v ? v->getMarkdownEditor() : nullptr;
    if (!editor || editor->isPlain()) {
        return {{"found", false}};
    }
    return tableMap(editor->text(), editor->cursorPosition());
}

QVariantMap AppController::markdownTableIn(const QString& text, int caret) const {
    return tableMap(text.toStdString(), utf8Offset(text, caret));
}

bool AppController::writeMarkdownTable(const QVariantList& cells, const QStringList& aligns) {
    CanvasView* v = canvas();
    if (!v || cells.isEmpty()) {
        return false;
    }
    if (!v->getMarkdownEditor() && textDocument() == "markdown") {
        v->ensureTextEditor();
    }
    MarkdownEditor* editor = v->getMarkdownEditor();
    if (!editor || editor->isPlain()) {
        return false;
    }
    editor->applyEdit(md::table::replaceOrInsert(editor->text(), editor->anchorPosition(), editor->cursorPosition(),
                                                 tableOf(cells, aligns)));
    return true;
}

QVariantMap AppController::writeMarkdownTableIn(QQuickTextDocument* document, int anchor, int caret,
                                                const QVariantList& cells, const QStringList& aligns) {
    if (!document || !document->textDocument() || cells.isEmpty()) {
        return {};
    }
    const QString text = document->textDocument()->toPlainText();
    const std::string source = text.toStdString();
    return applyIn(document, source,
                   md::table::replaceOrInsert(source, utf8Offset(text, anchor), utf8Offset(text, caret),
                                              tableOf(cells, aligns)));
}

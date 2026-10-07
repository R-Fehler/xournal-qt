/*
 * xournal-qt: the window's side of the Markdown formatting bar and the table editor (qt/docs/features/md-editor.md,
 * "Formatting bar"): the tools of md::format on the Markdown written on the page or in a .md (MarkdownEditor), and
 * on the source beside the page (its TextArea's document).
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>

#include <QFileInfo>
#include <QImageReader>
#include <QTextCursor>
#include <QTextDocument>
#include <QUrl>

#include "AppController.h"
#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownImages.h"
#include "shell/ContentFiles.h"
#include "MdFormat.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/TextReplace.h"

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

/// A picture file brought in (the formatting bar's picker, a drop): saved where the document keeps its pictures, its
/// link ("name.assets/…"); nullopt with a message if that failed. Links and paths that are no file URL stay as they
/// are.
std::optional<std::string> AppController::pictureLinkFor(const QString& arg) {
    const QUrl url(arg);
    if (!(url.isLocalFile() || ContentFiles::isForeign(url)) || !session()) {
        return arg.toStdString();
    }
    const QString source = ContentFiles::sourceOf(url);
    QString name = url.isLocalFile() ? url.fileName() : QFileInfo(source).fileName();
    if (!MarkdownImages::isPictureName(name)) {
        // (a picker's content:// URI may have no extension: the picture's kind from its content)
        const QByteArray kind = QImageReader::imageFormat(source);
        if (kind.isEmpty()) {
            Q_EMIT message(tr("Insert picture"), tr("\"%1\" is not a picture that can be shown.").arg(name), true);
            return std::nullopt;
        }
        name = (name.isEmpty() ? QStringLiteral("image") : name) + '.' + QString::fromLatin1(kind);
    }
    QString error;
    auto link = MarkdownImages::addPictureFile(*session(), source, name, error);
    if (!link) {
        Q_EMIT message(tr("Insert picture"), error, true);
    }
    return link;
}

bool AppController::insertMarkdownImages(const QList<QUrl>& files) {
    CanvasView* v = canvas();
    if (!v || files.isEmpty()) {
        return false;
    }
    if (!v->getMarkdownEditor() && (textDocument() == "markdown" || v->typesIntoFlow())) {
        v->ensureTextEditor();
    }
    MarkdownEditor* editor = v->getMarkdownEditor();
    if (!editor || editor->isPlain()) {
        return false;
    }
    // Each saved, all of them inserted at the cursor as one change (one undo step), one per line
    std::string markdown;
    for (const QUrl& url: files) {
        const auto link = pictureLinkFor(url.toString());
        if (!link) {
            return false;
        }
        const QString name = url.isLocalFile() ? url.fileName() : QFileInfo(ContentFiles::sourceOf(url)).fileName();
        markdown += (markdown.empty() ? "" : "\n\n") +
                    MarkdownImages::markdownFor(*link, QFileInfo(name).completeBaseName().toStdString());
    }
    const size_t from = std::min(editor->anchorPosition(), editor->cursorPosition());
    const size_t to = std::max(editor->anchorPosition(), editor->cursorPosition());
    editor->applyEdit({from, to, markdown, from + markdown.size(), from + markdown.size()});
    return true;
}

bool AppController::formatMarkdown(const QString& action, const QString& arg) {
    if (action == QLatin1String("image") && !arg.isEmpty()) {
        return insertMarkdownImages({QUrl(arg)});  // (the picker's file; a path typed in stays below)
    }
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
    std::string value = arg.toStdString();
    if (*a == md::format::Action::Image && !arg.isEmpty()) {
        const auto link = pictureLinkFor(arg);  // (a picked file: saved with the document, linked)
        if (!link) {
            return {};
        }
        value = *link;
    }
    const QString text = document->textDocument()->toPlainText();
    const std::string source = text.toStdString();
    return applyIn(document, source,
                   md::format::apply(source, utf8Offset(text, anchor), utf8Offset(text, caret), *a, value));
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

QVariantMap AppController::replaceInSource(QQuickTextDocument* document, int from, int to, const QString& with,
                                           bool all) {
    QTextDocument* d = document ? document->textDocument() : nullptr;
    const DocumentSession* s = session();
    if (!d || !s || s->search().query().isEmpty()) {
        return {};
    }
    const QString query = s->search().query();
    const replace::Options options = searchOptions();
    const QString plain = d->toPlainText();
    const std::string text = plain.toStdString();
    const auto matches = replace::find(text, query, with, options);
    if (matches.empty()) {
        return {{"count", 0}};
    }
    // text[a, b) becomes `middle`, as one undo step of the source
    const auto change = [&](size_t a, size_t b, const std::string& middle) {
        QTextCursor c(d);
        c.setPosition(utf16Offset(text, a));
        c.setPosition(utf16Offset(text, b), QTextCursor::KeepAnchor);
        c.beginEditBlock();
        c.insertText(QString::fromStdString(middle));
        c.endEditBlock();
    };
    if (all) {
        const size_t a = matches.front().begin;
        const size_t b = matches.back().end;
        const std::string after = replace::apply(text, matches);
        const std::string middle = after.substr(a, after.size() - (text.size() - b) - a);
        change(a, b, middle);
        const int at = utf16Offset(after, a + middle.size());
        return {{"count", static_cast<int>(matches.size())}, {"anchor", at}, {"caret", at}};
    }
    // The selection, if it is a match, replaced; then the match after it (or from the top again) selected
    const size_t selFrom = utf8Offset(plain, std::min(from, to));
    const size_t selTo = utf8Offset(plain, std::max(from, to));
    std::string now = text;
    size_t after = selFrom;
    bool replaced = false;
    for (const replace::Match& m: matches) {
        if (m.begin == selFrom && m.end == selTo) {
            change(m.begin, m.end, m.with);
            now = replace::apply(text, {m});
            after = m.begin + m.with.size();
            replaced = true;
            break;
        }
    }
    const auto next = replaced ? replace::find(now, query, with, options) : matches;
    QVariantMap out{{"replaced", replaced}, {"count", static_cast<int>(next.size())}};
    if (!next.empty()) {
        auto it = std::find_if(next.begin(), next.end(), [after](const replace::Match& m) { return m.begin >= after; });
        const replace::Match& m = it == next.end() ? next.front() : *it;
        out["anchor"] = utf16Offset(now, m.begin);
        out["caret"] = utf16Offset(now, m.end);
    }
    return out;
}

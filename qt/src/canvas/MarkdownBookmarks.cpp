#include "MarkdownBookmarks.h"

#include <optional>
#include <shared_mutex>
#include <vector>

#include "model/Document.h"
#include "model/Text.h"
#include "session/DocumentSession.h"
#include "session/PageBookmarks.h"
#include "session/TextDocument.h"

#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MdBookmarks.h"

namespace xqt::MarkdownBookmarks {

bool isTextPage(Document& doc, size_t page) {
    size_t end = 0;
    return TextDocument::hasTextBookmarks(doc, &end) && page < end;
}

Result edit(DocumentSession& session, CanvasView* view, size_t page, Change change, const std::string& label) {
    Result result;
    result.page = page;
    Document* doc = session.getDocument();
    std::string text;
    std::vector<md::Part> parts;
    std::optional<md::bookmarks::PageMark> mark;
    {
        std::shared_lock lock(*doc);
        if (!isTextPage(*doc, page)) {
            return result;
        }
        text = TextDocument::flowText(*doc, 0, nullptr, &parts);
        if (const Text* box = TextDocument::pageBoxOf(doc->getPage(page))) {
            mark = md::bookmarks::ofPage(box->getText());
        }
    }
    if (page >= parts.size()) {
        return result;
    }
    const md::Part& part = parts[page];
    std::optional<md::bookmarks::Edit> e;
    switch (change) {
        case Change::Add:
            e = md::bookmarks::add(text, part.begin, part.end, {}, &result.earlier);
            break;
        case Change::Remove:
            e = md::bookmarks::remove(text, part.begin, part.end);
            break;
        case Change::Rename: {
            std::string l = md::bookmarks::cleanLabel(label);
            if (PageBookmarks::isAutomaticLabel(l, page) || (mark && l == mark->automatic)) {
                l.clear();  // (the automatic label: it follows the heading, or the page)
            }
            e = md::bookmarks::rename(text, part.begin, part.end, l);
            if (e && text.compare(e->from, e->to - e->from, e->with) == 0) {
                e.reset();  // (as it is)
            }
            break;
        }
    }
    if (!e) {
        return result;
    }
    MarkdownEditor* editor = view ? view->getMarkdownEditor() : nullptr;
    if (editor && (!view->textMode() || !(editor->target().pageText && editor->target().page == 0))) {
        // (another text was being written, or the text of notes, whose undo takes back its whole edit: it ends, one
        // undo step, and the bookmark is one of its own)
        view->endTextEditing();
        editor = nullptr;
    }
    if (editor) {
        // A .md: a step of the text being written (its undo goes step by step); the cursor stays in its text
        const auto keep = [&](size_t p) {
            return p <= e->from ? p : p >= e->to ? p - (e->to - e->from) + e->with.size() : e->from + e->with.size();
        };
        editor->applyEdit({e->from, e->to, e->with, keep(editor->anchorPosition()), keep(editor->cursorPosition())});
    } else {
        text.replace(e->from, e->to - e->from, e->with);
        MarkdownFile::setText(session, text);
    }
    result.changed = true;
    if (change == Change::Add && result.earlier) {
        // (before the block this page's text goes on with: on the page that block starts on)
        std::shared_lock lock(*doc);
        std::vector<md::Part> now;
        TextDocument::flowText(*doc, 0, nullptr, &now);
        for (size_t i = 0; i < now.size(); ++i) {
            if (now[i].begin <= e->from) {
                result.page = i;
            }
        }
    }
    return result;
}

}  // namespace xqt::MarkdownBookmarks

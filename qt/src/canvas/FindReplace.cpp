#include "FindReplace.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "session/DocumentSearch.h"
#include "session/DocumentSession.h"
#include "session/FuzzyQuery.h"
#include "session/StickyNote.h"
#include "session/TextDocument.h"
#include "undo/GroupUndoAction.h"
#include "undo/UndoAction.h"
#include "undo/UndoRedoHandler.h"

#include "CanvasView.h"
#include "MarkdownEditor.h"
#include "MarkdownFile.h"
#include "MarkdownSession.h"
#include "MdBox.h"
#include "MdPaginate.h"

namespace xqt::replace {

namespace {

/// The steps of the texts changed by one "Replace all", undone in the reverse order of their making (each text may
/// add or remove pages, which moves the pages of the texts after it: the last changed is undone first).
class ReplaceUndoAction final: public UndoAction {
public:
    ReplaceUndoAction(): UndoAction("ReplaceUndoAction") {}
    void add(UndoActionPtr step) { steps.push_back(std::move(step)); }
    bool empty() const { return steps.empty(); }
    std::vector<PageRef> getPages() override {
        std::vector<PageRef> pages;
        for (const auto& s: steps) {
            for (const PageRef& p: s->getPages()) {
                if (std::find(pages.begin(), pages.end(), p) == pages.end()) {
                    pages.push_back(p);
                }
            }
        }
        return pages;
    }
    bool undo(Control* control) override {
        bool ok = true;
        for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
            ok = (*it)->undo(control) && ok;
        }
        return ok;
    }
    bool redo(Control* control) override {
        bool ok = true;
        for (auto& s: steps) {
            ok = s->redo(control) && ok;
        }
        return ok;
    }
    std::string getText() override { return "Replace"; }

private:
    std::vector<UndoActionPtr> steps;
};

/// The source of a target as the pages hold it (the page's text: its parts joined; `parts` what each page holds, from
/// its first page on). The caller holds the document's lock.
std::string sourceOf(Document& doc, const Target& t, std::vector<md::Part>* parts = nullptr, size_t* end = nullptr) {
    if (t.pageText) {
        return TextDocument::flowText(doc, t.page, end, parts);
    }
    if (end) {
        *end = t.page + 1;
    }
    return t.box ? t.box->getText() : std::string();
}

/// Start a Markdown session on a target; false if it is not there any more
bool begin(DocumentSession& session, MarkdownSession& md, const Target& t) {
    if (t.pageText) {
        // (the style of the text's boxes; new pages' boxes get MarkdownFile::setText's)
        md.begin(t.page, session.textFile() ? MarkdownFile::style(*session.textFile()) : md::Style{});
    } else {
        md.beginText(t.page, t.box);
    }
    return md.active();
}

/// The terms the search counts for this query (to tell how many of its hits a replacement holds)
std::vector<textmatch::Term> searchTerms(const QString& query, const Options& options) {
    return options.any() ? textmatch::optionTerms(query, options) : FuzzyQuery::textTerms(query, false);
}

double overlap(const QRectF& a, const QRectF& b) {
    const QRectF i = a.intersected(b);
    return i.isValid() ? i.width() * i.height() : 0;
}

}  // namespace

std::vector<Target> targets(DocumentSession& session) {
    std::vector<Target> out;
    if (session.textFile() && !session.isEditableText()) {
        return out;
    }
    Document* doc = session.getDocument();
    std::shared_lock lock(*doc);
    for (size_t i = 0; i < doc->getPageCount(); ++i) {
        const PageRef page = doc->getPage(i);
        const Layer* mdLayer = md::markdownLayer(page);
        const Text* pageBox = TextDocument::pageBoxOf(page);
        if (pageBox && mdLayer && mdLayer->isVisible() && (i == 0 || !md::continues(pageBox->getText()))) {
            out.push_back({i, true, pageBox});
        }
        for (const Layer* l: page->getLayersView()) {
            if (!l->isVisible()) {
                continue;
            }
            const bool boxes = md::isMarkdownLayer(*l);
            const bool note = !boxes && sticky::isNote(*l);
            if (!boxes && !note) {
                continue;
            }
            for (const Element* e: l->getElementsView()) {
                if (e->getType() != ELEMENT_TEXT || e == pageBox) {
                    continue;
                }
                const auto* t = static_cast<const Text*>(e);
                if (boxes || sticky::isNoteText(*l, *t)) {
                    out.push_back({i, false, t});
                }
            }
        }
    }
    return out;
}

bool canReplace(DocumentSession& session) {
    if (session.textFile()) {
        return session.isEditableText();
    }
    return !targets(session).empty();
}

int replaceAll(DocumentSession& session, CanvasView* view, const QString& query, const QString& with,
               const Options& options) {
    MarkdownEditor* editor = view ? view->getMarkdownEditor() : nullptr;
    struct Change {
        Target target;
        std::string before;
        std::vector<Match> matches;
    };
    const auto collect = [&](int& count) {
        std::vector<Change> changes;
        count = 0;
        Document* doc = session.getDocument();
        for (const Target& t: targets(session)) {
            std::string text;
            if (editor && editor->edits(t.box)) {
                text = editor->text();  // (the text being written, as the editor has it)
            } else {
                std::shared_lock lock(*doc);
                text = sourceOf(*doc, t);
            }
            auto matches = replace::find(text, query, with, options);
            if (!matches.empty()) {
                count += static_cast<int>(matches.size());
                changes.push_back({t, std::move(text), std::move(matches)});
            }
        }
        return changes;
    };
    int count = 0;
    std::vector<Change> changes = collect(count);
    if (count == 0) {
        return 0;
    }
    if (editor) {
        if (changes.size() == 1 && editor->edits(changes[0].target.box)) {
            // Only the text being written: one step of its own (Ctrl+Z in it), from the first match to the last
            const Change& c = changes[0];
            const size_t from = c.matches.front().begin;
            const size_t to = c.matches.back().end;
            const std::string after = replace::apply(c.before, c.matches);
            md::format::Edit e;
            e.from = from;
            e.to = to;
            e.with = after.substr(from, after.size() - (c.before.size() - to) - from);
            e.anchor = e.caret = from + e.with.size();
            editor->applyEdit(e);
            return count;
        }
        view->endTextEditing();  // (its edit is an undo step of its own, before this one)
        editor = nullptr;
        changes = collect(count);
    }
    auto step = std::make_unique<ReplaceUndoAction>();
    // From the last page backwards: a text that gets longer adds pages after it, which would move those of the texts
    // after it
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
        const std::string after = replace::apply(it->before, it->matches);
        if (after == it->before) {
            continue;
        }
        auto group = std::make_unique<GroupUndoAction>();
        MarkdownSession md(session);
        md.recordInto(group.get());
        if (!begin(session, md, it->target)) {
            continue;
        }
        md.update(after);
        md.finish();
        step->add(std::move(group));
    }
    if (!step->empty()) {
        session.getUndoRedoHandler()->addUndoAction(std::move(step));
    }
    return count;
}

Step replaceCurrent(DocumentSession& session, CanvasView* view, const QString& query, const QString& with,
                    const Options& options) {
    DocumentSearch& search = session.search();
    if (search.hitCount() == 0) {
        return Step::None;
    }
    if (search.currentOnPage() < 0) {
        search.jumpToFirstFromCurrentPage();
        return Step::Shown;
    }
    const size_t page = search.currentPage();
    const int index = search.currentOnPage();
    const auto* places = search.placeNow(page);
    if (!places || index >= static_cast<int>(places->size())) {
        search.next();
        return Step::Skipped;
    }
    const DocumentSearch::Place hit = (*places)[static_cast<size_t>(index)];

    // The match of the source drawn where the hit is
    struct Best {
        Target target;
        std::string text;
        Match match;
        double overlap = 0;
    } best;
    Document* doc = session.getDocument();
    for (const Target& t: targets(session)) {
        if (t.page > page) {
            break;
        }
        std::shared_lock lock(*doc);
        std::vector<md::Part> parts;
        size_t end = 0;
        const std::string text = sourceOf(*doc, t, &parts, &end);
        if (page >= end) {
            continue;
        }
        const Text* box = t.pageText ? TextDocument::pageBoxOf(doc->getPage(page)) : t.box;
        if (!box) {
            continue;
        }
        md::Part part{0, text.size(), 0};
        if (t.pageText) {
            if (page - t.page >= parts.size()) {
                continue;
            }
            part = parts[page - t.page];
        }
        for (Match& m: replace::find(text, query, with, options)) {
            const size_t a = std::max(m.begin, part.begin);
            const size_t b = std::min(m.end, part.end);
            if (a >= b) {
                continue;  // (not on this page)
            }
            double o = 0;
            for (const md::Rect& r: md::sourceRects(*box, part.prefix + a - part.begin, part.prefix + b - part.begin)) {
                const QRectF drawn(r.x, r.y, r.width, r.height);
                o += overlap(drawn, hit.rect) + (hit.more.isNull() ? 0 : overlap(drawn, hit.more));
            }
            if (o > best.overlap) {
                best = {t, text, std::move(m), o};
            }
        }
    }
    if (best.overlap <= 0) {
        search.next();  // (PDF text, a plain text, handwriting: not replaced)
        return Step::Skipped;
    }

    MarkdownEditor* editor = view ? view->getMarkdownEditor() : nullptr;
    const Match& m = best.match;
    if (editor && editor->edits(best.target.box) && editor->text() == best.text) {
        md::format::Edit e;
        e.from = m.begin;
        e.to = m.end;
        e.with = m.with;
        e.anchor = e.caret = m.begin + m.with.size();
        editor->applyEdit(e);  // (a step of the text being written)
    } else {
        if (editor) {
            view->endTextEditing();
        }
        MarkdownSession md(session);
        if (begin(session, md, best.target)) {
            md.update(replace::apply(best.text, {m}));
            md.finish();  // (one undo step)
        }
    }
    // The hit after it: the page's text read again now, past the hits the replacement itself holds
    search.textIndex().readEdits();
    const int inside = textmatch::count(QString::fromStdString(m.with).simplified(), searchTerms(query, options));
    search.jumpToHit(page, index + inside);
    return Step::Replaced;
}

}  // namespace xqt::replace

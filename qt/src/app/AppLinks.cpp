/*
 * xournal-qt: the window's side of links between documents (qt/docs/links.md): following a link to a document (a new
 * tab, the reference, or in place of the current one), and going back and forth across documents.
 *
 * @license GNU GPLv2 or later
 */
#include <shared_mutex>
#include <tuple>
#include <utility>

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>

#include "AppController.h"
#include "CanvasView.h"
#include "control/ScrollHandler.h"
#include "model/Document.h"
#include "session/DocumentLink.h"
#include "session/DocumentSession.h"
#include "shell/DocumentLinks.h"
#include "shell/Library.h"
#include "shell/LibraryModel.h"
#include "shell/ReferenceMode.h"
#include "shell/DocumentFiles.h"
#include "shell/LinkRewrite.h"
#include "MarkdownFile.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "shell/TabManager.h"
#include "util/TextLinks.h"

using namespace xqt;

namespace {
/// A tapped link's target: a Markdown link, or "[[a wiki link]]" (CanvasView marks them so).
std::optional<links::Link> linkOf(const QString& uri) {
    if (uri.startsWith(QLatin1String("[[")) && uri.endsWith(QLatin1String("]]"))) {
        return links::parseWiki(uri.mid(2, uri.size() - 4));
    }
    return links::parse(uri);
}

QString placeText(const links::Link& link) {
    if (!link.chapter.isEmpty()) {
        return AppController::tr("chapter “%1”").arg(link.chapter);
    }
    if (!link.heading.isEmpty()) {
        return AppController::tr("heading “%1”").arg(link.heading);
    }
    if (link.page > 0) {
        return AppController::tr("page %1").arg(link.page);
    }
    if (link.pdfPage > 0) {
        return AppController::tr("PDF page %1").arg(link.pdfPage);
    }
    if (link.line > 0) {
        return AppController::tr("line %1").arg(link.line);
    }
    return {};
}

/// Show a page of a document without a place to go back to in its view (the link is the way back).
void showLinkedPage(DocumentSession& s, int page) {
    const auto count = static_cast<int>(s.getDocument()->getPageCount());
    if (count == 0) {
        return;
    }
    const auto p = static_cast<size_t>(std::clamp(page, 0, count - 1));
    s.setCurrentPageNo(p);
    s.getScrollHandler()->scrollToPage(p);
}
}  // namespace

QVariantMap AppController::documentLink(const QString& uri) const {
    QVariantMap info;
    const auto link = linkOf(uri);
    info.insert("document", link.has_value());
    if (!link) {
        return info;
    }
    const fs::path from = session() ? session()->documentFile() : fs::path();
    const fs::path root = library && library->library() ? library->library()->root() : fs::path();
    const fs::path target =
            DocumentLinks::targetOf(*link, from, root, library ? library->searchIndex() : nullptr);
    const bool here = link->path.isEmpty() || (!target.empty() && target == from);
    info.insert("here", here);
    info.insert("found", here || !target.empty());
    info.insert("name", here ? QString()
                             : !target.empty() ? QString::fromStdString(target.filename().string())
                                               : link->path.section(QLatin1Char('/'), -1));
    info.insert("place", placeText(*link));
    return info;
}

bool AppController::followDocumentLink(const QString& uri, const QString& how) {
    return followDocumentLinkFrom(uri, how, session() ? session()->documentFile() : fs::path());
}

bool AppController::followDocumentLinkFrom(const QString& uri, const QString& how, const fs::path& from) {
    const auto link = linkOf(uri);
    if (!link) {
        return false;
    }
    const fs::path root = library && library->library() ? library->library()->root() : fs::path();
    fs::path target = DocumentLinks::targetOf(*link, from, root, library ? library->searchIndex() : nullptr);
    DocumentSession* source = session();
    // This document: a jump within it (Back comes back)
    if ((link->path.isEmpty() || target == from) && source && (from.empty() || source->documentFile() == from)) {
        const links::Place place = DocumentLinks::placeIn(*source, *link);
        jumpToPage(place.page);
        if (!place.note.isEmpty()) {
            Q_EMIT pageActionDone(place.note, false);
        }
        return true;
    }
    bool foundElsewhere = false;
    if (target.empty()) {
        // Moved or renamed by another program: looked for in the library (by name, by the page's text)
        const fs::path moved = library && library->searchIndex()
                                       ? DocumentLinks::findMoved(*link, from.empty() ? root / "x" : from,
                                                                  *library->searchIndex())
                                       : fs::path();
        relink = {source, uri, how, moved};
        if (moved.empty()) {
            Q_EMIT linkTargetMissing(link->path.section(QLatin1Char('/'), -1));
            return false;
        }
        target = moved;
        foundElsewhere = true;
    }
    const QString path = QString::fromStdString(target.string());
    if (how == QLatin1String("reference")) {
        if (!openAsReference(path)) {
            return false;
        }
        const int ref = tabs->referenceOf(tabs->currentIndex());
        if (DocumentSession* shown = ref >= 0 ? tabs->session(ref) : nullptr) {
            const links::Place place = DocumentLinks::placeIn(*shown, *link);
            referenceMode->goToPage(place.page);
            if (!place.note.isEmpty()) {
                Q_EMIT pageActionDone(place.note, false);
            }
        }
        if (foundElsewhere) {
            Q_EMIT linkTargetFound(QString::fromStdString(target.filename().string()),
                                   library ? library->relativeFolder(QString::fromStdString(target.parent_path().string()))
                                           : QString());
        }
        return true;
    }
    DocJump jump;
    if (source) {
        jump.from = {source, source->documentFile(), static_cast<int>(source->getCurrentPageNo())};
    }
    const bool wasOpen = tabs->indexOfFile(target) >= 0;
    if (!openPath(path) || !session()) {
        return false;
    }
    DocumentSession* opened = session();
    const links::Place place = DocumentLinks::placeIn(*opened, *link);
    showLinkedPage(*opened, place.page);
    // "Here": the document the link was in goes, unless it has unsaved changes (Back opens it again)
    jump.here = how == QLatin1String("here");
    if (jump.here && source && source != opened && !wasOpen && !jump.from.file.empty() && !source->isModified() &&
        !source->isSaving() && tabs->indexOf(source) >= 0) {
        closeTab(tabs->indexOf(source));
    }
    jump.to = {opened, target, place.page};
    jump.depth = canvas() ? canvas()->backDepth() : 0;
    if (!jump.from.file.empty() || jump.from.session) {
        docBack.push_back(std::move(jump));
        if (docBack.size() > 50) {
            docBack.erase(docBack.begin());
        }
        docForward.clear();
    }
    Q_EMIT navigationChanged();
    if (!place.note.isEmpty()) {
        Q_EMIT pageActionDone(place.note, false);
    }
    if (foundElsewhere) {
        Q_EMIT linkTargetFound(QString::fromStdString(target.filename().string()),
                               library ? library->relativeFolder(QString::fromStdString(target.parent_path().string()))
                                       : QString());
    }
    return true;
}

bool AppController::isCurrentPlace(const DocPlace& place) const {
    const DocumentSession* s = session();
    if (!s || home) {
        return false;
    }
    return (place.session && place.session == s) || (!place.file.empty() && s->documentFile() == place.file);
}

const AppController::DocJump* AppController::backJump() const {
    if (docBack.empty() || !isCurrentPlace(docBack.back().to)) {
        return nullptr;
    }
    // The view's own places since the link came in go first
    return canvas() && canvas()->backDepth() > docBack.back().depth ? nullptr : &docBack.back();
}

const AppController::DocJump* AppController::forwardJump() const {
    if (docForward.empty() || !isCurrentPlace(docForward.back().from)) {
        return nullptr;
    }
    return canvas() && canvas()->canGoForward() ? nullptr : &docForward.back();
}

bool AppController::showPlace(const DocPlace& place, bool replacing) {
    DocumentSession* current = session();
    if (place.session && tabs->indexOf(place.session) >= 0) {
        tabs->setCurrentIndex(tabs->indexOf(place.session));
        setHomeVisible(false);
        return true;  // (its view is where it was left)
    }
    if (place.file.empty() || !openPath(QString::fromStdString(place.file.string())) || !session()) {
        return false;
    }
    showLinkedPage(*session(), place.page);
    if (replacing && current && current != session() && !current->isModified() && !current->isSaving() &&
        tabs->indexOf(current) >= 0) {
        closeTab(tabs->indexOf(current));
    }
    return true;
}

bool AppController::navigateDocuments(bool back) {
    const DocJump* next = back ? backJump() : forwardJump();
    if (!next) {
        return false;
    }
    DocJump jump = *next;
    (back ? docBack : docForward).pop_back();
    // Where the reader is now is where the way back (or forward) returns to
    DocPlace& here = back ? jump.to : jump.from;
    here.session = session();
    here.page = session() ? static_cast<int>(session()->getCurrentPageNo()) : here.page;
    const bool shown = showPlace(back ? jump.from : jump.to, jump.here);
    DocPlace& there = back ? jump.from : jump.to;
    there.session = session();
    if (back) {
        jump.depth = 0;
        docForward.push_back(std::move(jump));
    } else {
        jump.depth = canvas() ? canvas()->backDepth() : 0;
        docBack.push_back(std::move(jump));
    }
    Q_EMIT navigationChanged();
    return shown;
}

// --- making links: Copy link -----------------------------------------------------------------------------------

namespace {
/// The name a link's title starts with: the document's name without its extension.
QString nameOf(const fs::path& file) { return QString::fromStdString(file.stem().string()); }

void putOnClipboard(const QString& title, const fs::path& file, const links::Link& link) {
    auto* mime = new QMimeData;
    links::toMime(*mime, title, file, link);
    QGuiApplication::clipboard()->setMimeData(mime);
}
}  // namespace

void AppController::copyPageLink(int page) {
    DocumentSession* s = session();
    const int index = page >= 0 ? page : pageNumber() - 1;
    if (!s || index < 0) {
        return;
    }
    const fs::path file = s->documentFile();
    if (file.empty()) {
        // No file yet: a link within the document, as upstream writes them
        QGuiApplication::clipboard()->setText(QString::fromStdString(xoj::util::pageLinkText(index + 1)));
        Q_EMIT pageActionDone(tr("Link to page %1 copied").arg(index + 1), false);
        return;
    }
    const links::Link link = DocumentLinks::linkTo(*s, static_cast<size_t>(index), {});
    putOnClipboard(tr("%1, page %2").arg(nameOf(file)).arg(index + 1), file, link);
    Q_EMIT pageActionDone(tr("Link to page %1 copied").arg(index + 1), false);
}

void AppController::copyChapterLink(int page, const QString& title) {
    DocumentSession* s = session();
    if (!s || page < 0 || s->documentFile().empty()) {
        copyPageLink(page);
        return;
    }
    const fs::path file = s->documentFile();
    const links::Link link = DocumentLinks::linkTo(*s, static_cast<size_t>(page), {}, title);
    putOnClipboard(tr("%1, %2").arg(nameOf(file), title), file, link);
    Q_EMIT pageActionDone(tr("Link to \u201c%1\u201d copied").arg(title), false);
}

bool AppController::copyDocumentLink(const QString& path, int page) {
    fs::path file(path.toStdString());
    if (const DocumentItem item = DocumentFiles::itemOf(file); item.valid()) {
        file = item.main();
    }
    std::error_code ec;
    if (file.empty() || !fs::exists(file, ec)) {
        return false;
    }
    links::Link link;
    QString title = nameOf(file);
    if (page >= 0) {
        title = tr("%1, page %2").arg(nameOf(file)).arg(page + 1);
        if (const int open = tabs->indexOfFile(file); open >= 0) {
            link = DocumentLinks::linkTo(*tabs->session(open), static_cast<size_t>(page), {});
        } else {
            link.page = page + 1;
            const std::vector<links::Page> pages =
                    library && library->searchIndex() ? library->searchIndex()->linkPages(file) : std::vector<links::Page>();
            if (static_cast<size_t>(page) < pages.size()) {
                const links::Page& p = pages[static_cast<size_t>(page)];
                if (p.pdfPage > 0) {
                    link.pdfPage = p.pdfPage;
                } else {
                    link.text = links::fingerprint(p.text);
                }
            }
        }
    }
    putOnClipboard(title, file, link);
    Q_EMIT pageActionDone(page >= 0 ? tr("Link to page %1 copied").arg(page + 1)
                                    : tr("Link to \u201c%1\u201d copied").arg(nameOf(file)),
                          false);
    return true;
}

QString AppController::clipboardLinkMarkdown() const {
    const auto copied = links::fromMime(QGuiApplication::clipboard()->mimeData());
    return copied ? links::markdownFor(*copied, session() ? session()->documentFile() : fs::path()) : QString();
}

// --- keeping links working: backlinks, links rewritten after moves, links whose file is gone ------------------

QVariantList AppController::backlinks() const {
    QVariantList list;
    DocumentSession* s = session();
    LibraryIndex* index = library ? library->searchIndex() : nullptr;
    if (!s || !index || s->documentFile().empty()) {
        return list;
    }
    for (const fs::path& file: DocumentLinks::backlinks(index->linkSources(), s->documentFile())) {
        const QString path = QString::fromStdString(file.string());
        list.append(QVariantMap{{"name", QString::fromStdString(file.stem().string())},
                                {"path", path},
                                {"folder", library->relativeFolder(QString::fromStdString(file.parent_path().string()))}});
    }
    return list;
}

std::vector<LinkRewrite::Change> AppController::relinkChange(const DocumentSession* source, const QString& written,
                                                            const fs::path& target) const {
    if (written.startsWith(QLatin1String("[[")) && written.endsWith(QLatin1String("]]"))) {
        const QString inner = written.mid(2, written.size() - 4);
        const qsizetype hash = inner.indexOf(QLatin1Char('#'));
        const QString name = DocumentFiles::isMarkdownFile(target) ? QString::fromStdString(target.stem().string())
                                                                   : QString::fromStdString(target.filename().string());
        return {{inner, name + (hash < 0 ? QString() : inner.mid(hash)), true}};
    }
    const fs::path from = source ? source->documentFile() : fs::path();
    return {{written, DocumentLinks::relinked(written, from, target), false}};
}

int AppController::rewriteOpenDocument(DocumentSession& s, const std::vector<LinkRewrite::Change>& changes) {
    const bool wasModified = s.isModified();
    int count = 0;
    if (s.textFile()) {
        std::string text = s.currentText();
        count = LinkRewrite::rewriteMarkdown(text, changes);
        if (count > 0) {
            MarkdownFile::setText(s, text);
        }
    } else {
        Document* doc = s.getDocument();
        std::vector<std::tuple<PageRef, Layer*, const Text*, std::string>> edits;
        {
            std::shared_lock lock(*doc);
            for (size_t i = 0; i < doc->getPageCount(); ++i) {
                const PageRef page = doc->getPage(i);
                for (Layer* layer: page->getLayers()) {
                    for (const Element* e: layer->getElementsView()) {
                        if (e->getType() != ELEMENT_TEXT) {
                            continue;
                        }
                        const auto* text = static_cast<const Text*>(e);
                        std::string content = text->getText();
                        if (const int n = LinkRewrite::rewriteMarkdown(content, changes); n > 0) {
                            edits.emplace_back(page, layer, text, std::move(content));
                            count += n;
                        }
                    }
                }
            }
        }
        for (auto& [page, layer, text, content]: edits) {
            // The text again with its new links (one undo step each, as a text edited)
            auto changed = text->cloneText();
            changed->setText(content);
            Text* now = changed.get();
            ElementPtr before;
            {
                std::unique_lock lock(*doc);
                auto [old, index] = layer->removeElement(text);
                before = std::move(old);
                layer->insertElement(std::move(changed), index);
            }
            s.getUndoRedoHandler()->addUndoAction(std::make_unique<TextBoxUndoAction>(page, layer, now, std::move(before)));
            page->firePageChanged();
            if (const size_t index = doc->indexOf(page); index != npos) {
                s.firePageChanged(index);
            }
        }
    }
    if (count > 0 && !wasModified && s.hasFilePath() && !s.isSaving()) {
        startSave(SaveWay::Save, {}, [](bool) {}, &s);  // (the file had these links: it gets the new ones)
    }
    return count;
}

void AppController::rewriteLinksAfter(const std::vector<std::pair<fs::path, fs::path>>& moves) {
    LibraryIndex* index = library ? library->searchIndex() : nullptr;
    if (moves.empty() || !index) {
        return;
    }
    std::vector<LinkRewrite::Plan> plans = LinkRewrite::plan(index->linkSources(), moves);
    int updated = 0;
    std::vector<LinkRewrite::Plan> closed;
    for (auto& plan: plans) {
        const auto open = tabsWithFile(plan.file, nullptr);
        if (open.empty()) {
            closed.push_back(std::move(plan));
            continue;
        }
        for (const auto& [window, s]: open) {
            updated += window->rewriteOpenDocument(*s, plan.changes);
        }
    }
    const auto note = [](int n) { return n == 1 ? tr("Updated 1 link") : tr("Updated %1 links").arg(n); };
    if (closed.empty()) {
        if (updated > 0) {
            Q_EMIT pageActionDone(note(updated), false);
        }
        return;
    }
    // The others in the background (a .xopp is loaded and written again)
    QPointer<AppController> self(this);
    QThreadPool::globalInstance()->start([self, closed = std::move(closed), updated, note]() {
        int n = updated;
        QStringList errors;
        for (const auto& plan: closed) {
            std::string error;
            const int done = LinkRewrite::rewriteFile(plan.file, plan.changes, error);
            if (done < 0) {
                errors << QString::fromStdString(plan.file.filename().string() + ": " + error);
            } else {
                n += done;
            }
        }
        QMetaObject::invokeMethod(qApp, [self, n, errors, note] {
            if (!self) {
                return;
            }
            if (n > 0) {
                Q_EMIT self->pageActionDone(note(n), false);
                if (self->library) {
                    self->library->refresh();  // (their index entries are read again)
                }
            }
            if (!errors.isEmpty()) {
                Q_EMIT self->message(tr("Links not updated"), errors.join(QLatin1Char('\n')), false);
            }
        });
    });
}

bool AppController::updateFoundLink() {
    Relink r = std::exchange(relink, {});
    if (!r.source || r.target.empty()) {
        return false;
    }
    const int n = rewriteOpenDocument(*r.source, relinkChange(r.source, r.written, r.target));
    if (n > 0) {
        Q_EMIT pageActionDone(tr("Link updated"), false);
    }
    return n > 0;
}

bool AppController::relinkTo(const QUrl& file) {
    Relink r = std::exchange(relink, {});
    const fs::path target(file.toLocalFile().toStdString());
    if (!r.source || target.empty() || tabs->indexOf(r.source) < 0) {
        return false;
    }
    const auto changes = relinkChange(r.source, r.written, target);
    if (rewriteOpenDocument(*r.source, changes) == 0) {
        return false;
    }
    tabs->setCurrentIndex(tabs->indexOf(r.source));
    const QString now = changes.front().wiki ? QStringLiteral("[[%1]]").arg(changes.front().to) : changes.front().to;
    return followDocumentLinkFrom(now, r.how.isEmpty() ? QStringLiteral("tab") : r.how, r.source->documentFile());
}

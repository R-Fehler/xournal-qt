/*
 * xournal-qt: the window's side of links between documents (qt/docs/links.md): following a link to a document (a new
 * tab, the reference, or in place of the current one), and going back and forth across documents.
 *
 * @license GNU GPLv2 or later
 */
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
#include "shell/TabManager.h"

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
    const fs::path target = DocumentLinks::targetOf(*link, from, root, library ? library->searchIndex() : nullptr);
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
    if (target.empty()) {
        Q_EMIT message(tr("Document not found"),
                       tr("\"%1\" was not found.").arg(link->path), false);
        return false;
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

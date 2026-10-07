/*
 * xournal-qt: bookmarks on pages and favourite documents (qt/docs/features/bookmarks.md), as the window offers them:
 * the page menu, the pages sidebar and the contents sidebar of the open document; the ⋮ menu's star; the library's
 * cards and its Bookmarks view.
 *
 * @license GNU GPLv2 or later
 */
#include <shared_mutex>

#include <QVariantMap>

#include "AppController.h"
#include "MarkdownBookmarks.h"
#include "model/Document.h"
#include "model/DocumentOutline.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/PageBookmarks.h"
#include "session/TextDocument.h"
#include "session/TextFile.h"
#include "shell/DocumentChapters.h"
#include "shell/DocumentPlaces.h"
#include "shell/LibraryBookmarks.h"
#include "shell/LibraryModel.h"
#include "shell/TabManager.h"

using namespace xqt;

namespace {
fs::path pathOf(const QString& s) { return fs::path(s.toStdString()); }

/// The file a document's star is kept for (empty: it has none yet).
fs::path starFileOf(const DocumentSession* s) {
    const fs::path file = s ? s->documentFile() : fs::path();
    return file.empty() ? file : DocumentPlaces::keyOf(file);
}
}  // namespace

QObject* AppController::libraryBookmarksModel() const { return libraryBookmarks; }

bool AppController::canBookmark() const {
    DocumentSession* s = session();
    // (a .md: its bookmarks are comments in its text; a plain text file has none)
    return s && !s->isReadOnly() && (!s->textFile() || s->textFile()->kind() == TextFile::Kind::Markdown);
}

QVariantList AppController::bookmarks() const {
    QVariantList list;
    DocumentSession* s = session();
    if (!s) {
        return list;
    }
    Document* doc = s->getDocument();
    std::shared_lock lock(*doc);
    for (const auto& m: PageBookmarks::of(*doc)) {
        list.append(QVariantMap{{"page", static_cast<int>(m.page)},
                                {"label", PageBookmarks::displayLabel(m.label, m.page)},
                                {"automatic", m.label.empty()}});
    }
    return list;
}

bool AppController::isBookmarked(int page) const { return !bookmarkOf(page).isEmpty(); }

QString AppController::bookmarkOf(int page) const {
    DocumentSession* s = session();
    if (!s || page < 0) {
        return {};
    }
    Document* doc = s->getDocument();
    std::shared_lock lock(*doc);
    if (static_cast<size_t>(page) >= doc->getPageCount()) {
        return {};
    }
    const auto& mark = doc->getPage(static_cast<size_t>(page))->getBookmark();
    return mark ? PageBookmarks::displayLabel(*mark, static_cast<size_t>(page)) : QString();
}

QString AppController::defaultBookmarkLabel(int page) const {
    DocumentSession* s = session();
    if (!s || page < 0) {
        return {};
    }
    Document* doc = s->getDocument();
    // The first heading written on the page (DocumentChapters), else the first entry of the PDF's own table of
    // contents that goes to its PDF page; else the automatic label ("Page N", which follows the page)
    for (const auto& c: DocumentChapters::find(*doc)) {
        if (c.page == static_cast<size_t>(page)) {
            return QString::fromStdString(c.title);
        }
    }
    std::shared_lock lock(*doc);
    if (static_cast<size_t>(page) >= doc->getPageCount()) {
        return {};
    }
    const PageRef p = doc->getPage(static_cast<size_t>(page));
    if (!p->getBackgroundType().isPdfPage()) {
        return {};
    }
    const size_t pdfPage = p->getPdfPageNr();
    std::function<const DocumentOutlineEntry*(const DocumentOutline&, bool)> find =
            [&](const DocumentOutline& entries, bool top) -> const DocumentOutlineEntry* {
        for (const auto& e: entries) {
            if (top && PageBookmarks::isOutlineItem(e)) {
                continue;
            }
            if (e.dest.getPdfPage() == pdfPage) {
                return &e;
            }
            if (const auto* c = find(e.children, false)) {
                return c;
            }
        }
        return nullptr;
    };
    const DocumentOutlineEntry* e = find(doc->getOutline(), true);
    return e ? QString::fromStdString(e->title) : QString();
}

bool AppController::editTextBookmark(int page, int change, const QString& label, bool& handled) {
    DocumentSession* s = session();
    {
        std::shared_lock lock(*s->getDocument());
        handled = MarkdownBookmarks::isTextPage(*s->getDocument(), static_cast<size_t>(page));
    }
    if (!handled) {
        return false;  // (a page of its own: its bookmark is the page's, PageBookmarks.h)
    }
    endMarkdown(true);  // (the source beside the page ends, kept: the text is changed on the pages)
    using MarkdownBookmarks::Change;
    const auto r = MarkdownBookmarks::edit(*s, canvas(), static_cast<size_t>(page),
                                           change > 0 ? Change::Add : change < 0 ? Change::Remove : Change::Rename,
                                           label.simplified().toStdString());
    if (change > 0 && r.changed) {
        Q_EMIT pageActionDone(r.earlier ? tr("Bookmark added on page %1, where the text of this page begins")
                                                  .arg(static_cast<int>(r.page) + 1)
                                        : tr("Bookmark added: %1").arg(bookmarkOf(page)),
                              true);
    } else if (change > 0 && r.earlier) {
        Q_EMIT pageActionDone(tr("This page goes on with the text of an earlier page, which has a bookmark"), false);
    } else if (change < 0 && r.changed) {
        Q_EMIT pageActionDone(tr("Bookmark removed"), true);
    }
    return r.changed;
}

bool AppController::toggleBookmark(int page) {
    DocumentSession* s = session();
    if (!canBookmark() || page < 0 || static_cast<size_t>(page) >= s->getDocument()->getPageCount()) {
        return false;
    }
    bool handled = false;
    const bool done = editTextBookmark(page, isBookmarked(page) ? -1 : 1, {}, handled);
    if (handled) {
        return done;  // (a page of a Markdown text: a comment in the text)
    }
    const bool on = !isBookmarked(page);
    const std::optional<std::string> label =
            on ? std::optional<std::string>(defaultBookmarkLabel(page).trimmed().toStdString()) : std::nullopt;
    if (!s->setBookmark(static_cast<size_t>(page), label)) {
        return false;
    }
    Q_EMIT pageActionDone(on ? tr("Bookmark added: %1").arg(bookmarkOf(page)) : tr("Bookmark removed"), true);
    return true;
}

bool AppController::renameBookmark(int page, const QString& label) {
    DocumentSession* s = session();
    if (!canBookmark() || page < 0 || static_cast<size_t>(page) >= s->getDocument()->getPageCount()) {
        return false;
    }
    bool handled = false;
    const bool done = editTextBookmark(page, 0, label, handled);
    if (handled) {
        return done;
    }
    std::string text = label.simplified().toStdString();
    if (PageBookmarks::isAutomaticLabel(text, static_cast<size_t>(page))) {
        text.clear();  // ("Page N": the automatic one, which follows the page)
    }
    return s->setBookmark(static_cast<size_t>(page), text);
}

bool AppController::favourite() const {
    const fs::path file = starFileOf(session());
    return !file.empty() && DocumentPlaces::favourite(file);
}

bool AppController::canFavourite() const { return !starFileOf(session()).empty(); }

void AppController::setFavourite(bool on) {
    const fs::path file = starFileOf(session());
    if (file.empty() || DocumentPlaces::favourite(file) == on) {
        return;
    }
    // Through the library, so its cards, its Favourites filter and its Bookmarks view follow
    library->setFavourite(QString::fromStdString(file.string()), on);
}

bool AppController::isFavouriteFile(const QString& path) const {
    return !path.isEmpty() && DocumentPlaces::favourite(DocumentPlaces::keyOf(pathOf(path)));
}

void AppController::setFavouriteFile(const QString& path, bool on) {
    if (!path.isEmpty()) {
        library->setFavourite(QString::fromStdString(DocumentPlaces::keyOf(pathOf(path)).string()), on);
    }
}

bool AppController::openBookmark(const QString& path, int page) { return openSearchHitAt(path, QString(), page); }

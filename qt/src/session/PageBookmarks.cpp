#include "PageBookmarks.h"

#include <map>
#include <mutex>

#include <QCoreApplication>

#include "model/Document.h"
#include "model/XojPage.h"
#include "util/Util.h"  // npos

namespace xqt::PageBookmarks {

QString displayLabel(const std::string& label, size_t page) {
    if (!label.empty()) {
        return QString::fromStdString(label);
    }
    return QCoreApplication::translate("PageBookmarks", "Page %1").arg(page + 1);
}

std::string displayLabelUtf8(const std::string& label, size_t page) {
    return label.empty() ? displayLabel(label, page).toStdString() : label;
}

bool isAutomaticLabel(const std::string& title, size_t page) {
    return title == displayLabelUtf8({}, page) || title == "Page " + std::to_string(page + 1);
}

bool isOutlineItem(const DocumentOutlineEntry& entry) {
    if (entry.title != OUTLINE_TITLE || entry.children.empty()) {
        return false;
    }
    for (const auto& child: entry.children) {
        if (!child.children.empty() || child.dest.getPdfPage() == npos) {
            return false;
        }
    }
    return true;
}

size_t adoptOutline(Document& doc) {
    std::unique_lock lock(doc);
    const DocumentOutlineEntry* item = nullptr;
    for (const auto& e: doc.getOutline()) {
        if (isOutlineItem(e)) {
            item = &e;  // (the last one: ours is written last)
        }
    }
    if (!item) {
        return 0;
    }
    std::map<size_t, size_t> firstPageOf;  // PDF page -> first document page showing it
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef p = doc.getPage(i);
        if (p->getBookmark()) {
            return 0;  // our data says it already (a PDF with notes, a .xopp)
        }
        if (p->getBackgroundType().isPdfPage()) {
            firstPageOf.emplace(p->getPdfPageNr(), i);
        }
    }
    size_t adopted = 0;
    for (const auto& child: item->children) {
        const auto it = firstPageOf.find(child.dest.getPdfPage());
        if (it == firstPageOf.end()) {
            continue;
        }
        const PageRef p = doc.getPage(it->second);
        if (p->getBookmark()) {
            continue;  // (two entries to one page: the first counts)
        }
        p->setBookmark(isAutomaticLabel(child.title, it->second) ? std::string() : child.title);
        ++adopted;
    }
    return adopted;
}

std::vector<Mark> of(const Document& doc) {
    std::vector<Mark> marks;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        if (const auto& b = doc.getPage(i)->getBookmark()) {
            marks.push_back({i, *b});
        }
    }
    return marks;
}

BookmarkUndoAction::BookmarkUndoAction(PageRef page, std::optional<std::string> before,
                                       std::optional<std::string> after, Apply apply):
        UndoAction("BookmarkUndoAction"),
        target(std::move(page)),
        before(std::move(before)),
        after(std::move(after)),
        apply(std::move(apply)) {}

bool BookmarkUndoAction::undo(Control*) {
    apply(target, before);
    return true;
}

bool BookmarkUndoAction::redo(Control*) {
    apply(target, after);
    return true;
}

std::string BookmarkUndoAction::getText() {
    return (!before  ? QCoreApplication::translate("PageBookmarks", "Add bookmark")
            : !after ? QCoreApplication::translate("PageBookmarks", "Remove bookmark")
                     : QCoreApplication::translate("PageBookmarks", "Rename bookmark"))
            .toStdString();
}

}  // namespace xqt::PageBookmarks

#include "LibraryIndex.h"
#include "LibraryIndexEntry.h"

#include <algorithm>
#include <optional>
#include <shared_mutex>

#include <QFile>

#include "model/Document.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfPage.h"
#include "session/DocumentSession.h"
#include "session/DocumentImages.h"
#include "session/PageBookmarks.h"
#include "session/PdfTitle.h"
#include "session/Vocabulary.h"

#include "MarkdownFile.h"
#include "MdBookmarks.h"
#include "MdBox.h"
#include "MdImages.h"
#include "MdPassages.h"
#include "MdTasks.h"
#include "PdfKeywords.h"
#include "Tags.h"

namespace xqt {

using namespace library_index;

namespace library_index {
int firstPdfPage(const std::vector<int>& pdfPage) {
    const auto it = std::find_if(pdfPage.begin(), pdfPage.end(), [](int p) { return p >= 0; });
    return it == pdfPage.end() ? -1 : *it;
}
}  // namespace library_index

// --- reading documents

std::shared_ptr<LibraryIndex::Entry> LibraryIndex::read(const DocumentItem& item, const EntryPtr& previous) {
    auto e = std::make_shared<Entry>();
    e->file = item.main();
    e->kind = entryKind(item);
    e->name = QString::fromStdString(item.name());
    e->xoppStamp = ownStamp(item);
    e->sample = contentSample(item.main());
    auto gone = [&] {
        std::error_code ec;
        return !fs::exists(item.main(), ec);
    };
    if (!item.md.empty()) {
        // Plain text: its passages through md4c, without the syntax
        const std::string source = MarkdownFile::read(item.md);
        const md::Document doc = md::parse(source);
        if (gone()) {
            return nullptr;
        }
        ++docsRead;
        std::string label;  // (a bookmark comment: not shown, its label is found with the block it marks)
        for (const md::Passage& p: md::passages(doc)) {
            std::string text = p.text;
            if (p.path.size() == 1 && md::bookmarks::isMark(doc.root.children[p.path[0]])) {
                label = *md::bookmarks::labelOf(p.text);
                text.clear();
            } else if (!label.empty()) {
                text = label + " " + text;
                label.clear();
            }
            e->blockText << simplified(QString::fromStdString(text));
            e->blockLevel.push_back(p.kind == md::Passage::Kind::Heading ? p.level : 0);
        }
        // Its bookmarks (qt/docs/bookmarks.md, "Markdown"): on the pages the text is laid out on as it opens
        if (md::bookmarks::mayContain(source)) {
            md::images::RootHandle root(DocumentImages::markdownRoot(item.md));  // (its pictures: their heights)
            const auto pages = MarkdownFile::document(source);
            for (const auto& m: PageBookmarks::of(*pages)) {
                e->bookmarks[static_cast<int>(m.page)] = QString::fromStdString(m.label);
            }
        }
        for (const md::LinkTarget& l: md::linksOf(doc)) {
            (l.wiki ? e->wikiLinks : e->links) << QString::fromStdString(l.target);
        }
        // Its to-dos (by their lines in the file; their pages are found when one is opened)
        addTodos(*e, source, -1, -1, nullptr, 0);
        numberTodos(*e);
        // Its tags: `#tag` in its text, its front matter's tags
        e->textTags = tags::inMarkdown(source);
        return e;
    }
    if (!item.other.empty()) {
        // A text file: its text (a big one: its name)
        QFile f(qstr(item.other));
        if (!f.open(QIODevice::ReadOnly) && gone()) {
            return nullptr;
        }
        ++docsRead;
        if (f.isOpen() && f.size() <= TEXT_LIMIT) {
            QByteArray bytes = f.readAll();
            if (bytes.startsWith("\xEF\xBB\xBF")) {
                bytes.remove(0, 3);
            }
            if (!bytes.contains('\0')) {  // (not text after all)
                e->blockText << simplified(QString::fromUtf8(bytes));
                e->blockLevel.push_back(0);
            }
        }
        return e;
    }
    if (item.xopp.empty() && item.pdf.empty()) {
        if (gone()) {
            return nullptr;
        }
        ++docsRead;
        return e;  // an image: its name
    }
    auto loaded = DocumentSession::loadFile(item.main());
    if (!loaded.document && gone()) {
        return nullptr;
    }
    ++docsRead;
    if (e->isPdf()) {
        // What it is: its marker was read to open it (remembered: not read again)
        e->pdfKind = kindOfPdf(e->file);
        e->versions = versionsOfPdf(e->file);
    }
    if (!loaded.document) {
        // unreadable: empty, not read again until it changes. A PDF protected with a password is never read (not even
        // while it is open in the app: its text stays out of the index; qt/docs/hybrid-pdf.md, "Encrypted PDFs")
        e->locked = loaded.needsPassword;
        return e;
    }
    Document& doc = *loaded.document;
    std::shared_lock lock(doc);
    // A hybrid PDF: its pages are those of the file itself (read from the clean copy in the app cache, whose name
    // changes with every version of the file)
    e->pdf = loaded.hybrid ? item.main() : doc.getPdfFilepath();
    e->pdfStamp = fileStamp(e->pdf);
    const EntryPtr donor = donorFor(*e, previous);
    fillPages(*e, doc, donor, true);
    lock.unlock();
    fillTitle(*e, donor);
    fillPdfTags(*e, donor);
    return e;
}

void LibraryIndex::fillTitle(Entry& e, const EntryPtr& donor) {
    const int page = firstPdfPage(e.pdfPage);
    if (page < 0 || e.pdf.empty()) {
        return;
    }
    if (donor && donor->pdf == e.pdf && donor->pdfStamp == e.pdfStamp &&
        firstPdfPage(donor->pdfPage) == page) {
        e.title = donor->title;
        e.heading = donor->heading;
        return;
    }
    const pdftitle::Titles t = pdftitle::read(e.pdf, page);
    e.title = t.meta;
    e.heading = t.heading;
}

LibraryIndex::EntryPtr LibraryIndex::donorFor(const Entry& e, const EntryPtr& previous) const {
    // PDF text read before: from this document's last entry, or another one with this PDF (e.g. the PDF of a
    // document that just got its .xopp, or that was moved by another program).
    if (e.pdfStamp.isEmpty()) {
        return nullptr;
    }
    if (previous && previous->pdf == e.pdf && previous->pdfStamp == e.pdfStamp) {
        return previous;
    }
    EntryPtr donor;
    std::lock_guard entriesLock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, other]: f.docs) {
            if (other->pdfStamp == e.pdfStamp && (other->pdf == e.pdf || !donor)) {
                donor = other;  // the same size and time: the same file (renamed), preferably the same path
            }
        }
    }
    return donor;
}

bool LibraryIndex::fillPages(Entry& e, Document& doc, const EntryPtr& donor, bool readMissing) {
    const size_t pdfPages = doc.getPdfPageCount();
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        PageRef page = doc.getPage(i);
        const bool pdfPage = page->getBackgroundType().isPdfPage() && page->getPdfPageNr() < pdfPages;
        const int pdfNr = pdfPage ? static_cast<int>(page->getPdfPageNr()) : -1;  // (a shorter new PDF version)
        e.pdfPage.push_back(pdfNr);
        if (pdfNr >= 0 && !e.pdfText.count(pdfNr)) {
            if (donor && donor->pdfText.count(pdfNr)) {
                e.pdfText[pdfNr] = donor->pdfText.at(pdfNr);
            } else if (!readMissing) {
                return false;
            } else if (XojPdfPageSPtr pdf = doc.getPdfPage(static_cast<size_t>(pdfNr))) {
                const XojPdfRectangle all(0, 0, pdf->getWidth(), pdf->getHeight());
                e.pdfText[pdfNr] = simplified(QString::fromStdString(pdf->selectText(all, XojPdfPageSelectionStyle::Linear)));
                ++pdfRead;
            }
        }
        QString elements;
        for (const Layer* layer: page->getLayers()) {
            for (const auto& el: layer->getElementsView()) {
                if (el->getType() == ELEMENT_TEXT) {
                    const auto* text = static_cast<const Text*>(el);
                    elements += ' ' + QString::fromStdString(text->getText());
                    // Its tags (typed text, Markdown boxes, sticky notes, the pages of a text document)
                    tags::merge(e.textTags, text->isMarkdown() ? tags::inMarkdown(text->getText())
                                                               : tags::inText(QString::fromStdString(text->getText())));
                    if (text->isMarkdown()) {
                        // Its links (Markdown boxes, link markers), for backlinks (qt/docs/links.md)
                        for (const md::LinkTarget& l: md::linksOf(md::parse(text->getText()))) {
                            QStringList& into = l.wiki ? e.wikiLinks : e.links;
                            if (const QString t = QString::fromStdString(l.target); !into.contains(t)) {
                                into << t;
                            }
                        }
                    }
                }
            }
        }
        e.elementText << simplified(elements);
        e.aspects.push_back(page->getWidth() > 0 ? page->getHeight() / page->getWidth() : 0);
        if (const auto& mark = page->getBookmark()) {
            e.bookmarks[static_cast<int>(i)] = QString::fromStdString(*mark);
        }
        // Its to-dos: the task lines of its Markdown boxes (the Markdown layer's, the sticky notes' texts)
        const std::vector<Text*> boxes = md::boxesOf(*page);
        for (size_t b = 0; b < boxes.size(); ++b) {
            addTodos(e, boxes[b]->getText(), static_cast<int>(i), static_cast<int>(b), boxes[b], page->getWidth());
        }
    }
    numberTodos(e);
    return true;
}

void LibraryIndex::fillPdfTags(Entry& e, const EntryPtr& donor) {
    e.pdfTags.clear();
    if (e.pdf.empty()) {
        return;
    }
    if (donor && donor->pdf == e.pdf && donor->pdfStamp == e.pdfStamp && !e.pdfStamp.isEmpty()) {
        e.pdfTags = donor->pdfTags;
        return;
    }
    e.pdfTags = pdfkeywords::tagsOf(e.pdf);
}

void LibraryIndex::addTodos(Entry& e, const std::string& source, int page, int box, const Text* text,
                            double pageWidth) {
    const std::vector<md::tasks::Task> tasks = md::tasks::find(source);
    if (tasks.empty()) {
        return;
    }
    const bool stamp = text && md::tasks::isStamp(source);
    for (const md::tasks::Task& task: tasks) {
        Todo t;
        t.page = page;
        t.box = box;
        t.line = task.line;
        t.text = QString::fromStdString(task.text);
        t.done = task.done;
        t.due = QString::fromStdString(md::tasks::dueDate(task.text));
        t.stamp = stamp;
        if (text) {
            t.x = text->getOrigin().x;
            t.y = text->getOrigin().y;
            t.size = text->getFontSize();
            t.pageWidth = pageWidth;
            // A stamp: where its check box is drawn (the handwriting beside it is the to-do)
            if (const auto box = stamp ? md::checkBoxRect(*text, task.mark) : std::nullopt) {
                t.x = box->x;
                t.y = box->y;
                t.size = box->width;
            }
        }
        e.todos.push_back(std::move(t));
    }
}

void LibraryIndex::numberTodos(Entry& e) {
    std::map<QString, int> seen;
    for (Todo& t: e.todos) {
        t.occurrence = seen[t.text]++;
    }
}

}  // namespace xqt

#include "DocumentLinks.h"

#include <algorithm>
#include <functional>
#include <map>
#include <shared_mutex>

#include "model/Document.h"
#include "model/DocumentOutline.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "DocumentChapters.h"
#include "DocumentFiles.h"
#include "Library.h"
#include "MarkdownFile.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"
#include "session/PageBookmarks.h"

namespace xqt::DocumentLinks {

namespace {
QString qstr(const fs::path& p) { return QString::fromStdString(p.string()); }

bool fileExists(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::exists(p, ec);
}

/// The file the library opens for `file` (a PDF with its .xopp: the .xopp).
fs::path opened(const fs::path& file) {
    const DocumentItem item = DocumentFiles::itemOf(file);
    return item.valid() ? item.main() : file;
}

/// How many folders lie between two paths (to pick the closest of several files of one name).
size_t distance(const fs::path& a, const fs::path& b) {
    auto ai = a.begin();
    auto bi = b.begin();
    while (ai != a.end() && bi != b.end() && *ai == *bi) {
        ++ai;
        ++bi;
    }
    return static_cast<size_t>(std::distance(ai, a.end()) + std::distance(bi, b.end()));
}

/// The text of a text document (a .md edited or shown), as its pages hold it.
std::string textOf(DocumentSession& s) {
    if (s.textFile()) {
        return s.currentText();
    }
    if (!s.shownFile().empty() && DocumentFiles::isMarkdownFile(s.shownFile())) {
        return MarkdownFile::read(s.shownFile());
    }
    return {};
}
}  // namespace

std::vector<links::Chapter> chaptersOf(Document& doc) {
    std::vector<links::Chapter> chapters;
    {
        std::shared_lock lock(doc);
        std::map<size_t, int> firstPageOf;  // PDF page -> the first page showing it
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            const PageRef p = doc.getPage(i);
            if (p->getBackgroundType().isPdfPage()) {
                firstPageOf.emplace(p->getPdfPageNr(), static_cast<int>(i));
            }
        }
        std::function<void(const DocumentOutline&)> walk = [&](const DocumentOutline& entries) {
            for (const auto& e: entries) {
                if (&entries == &doc.getOutline() && PageBookmarks::isOutlineItem(e)) {
                    continue;  // (our bookmarks, not a chapter)
                }
                if (auto it = firstPageOf.find(e.dest.getPdfPage()); it != firstPageOf.end()) {
                    chapters.push_back({QString::fromStdString(e.title), it->second});
                }
                walk(e.children);
            }
        };
        walk(doc.getOutline());
    }
    if (chapters.empty()) {
        for (const auto& c: DocumentChapters::find(doc)) {
            chapters.push_back({QString::fromStdString(c.title), static_cast<int>(c.page)});
        }
    }
    return chapters;
}

std::vector<links::Page> pagesOf(Document& doc) {
    std::vector<links::Page> pages;
    std::shared_lock lock(doc);
    pages.reserve(doc.getPageCount());
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef p = doc.getPage(i);
        links::Page page;
        if (p->getBackgroundType().isPdfPage()) {
            page.pdfPage = static_cast<int>(p->getPdfPageNr()) + 1;
        }
        for (const Layer* layer: p->getLayersView()) {
            for (const Element* e: layer->getElementsView()) {
                if (e->getType() == ELEMENT_TEXT) {
                    if (!page.text.isEmpty()) {
                        page.text += QLatin1Char(' ');
                    }
                    page.text += QString::fromStdString(static_cast<const Text*>(e)->getText());
                }
            }
        }
        pages.push_back(std::move(page));
    }
    return pages;
}

links::Place placeIn(DocumentSession& session, const links::Link& link) {
    Document& doc = *session.getDocument();
    const std::string text = textOf(session);
    if (!text.empty() || session.textFile()) {
        const links::TextPlace at = links::resolveInText(link, text);
        links::Place place;
        place.page = static_cast<int>(MarkdownFile::pageOf(doc, at.offset));
        place.note = at.note;
        if (link.heading.isEmpty() && link.line <= 0 && link.page > 0) {
            place = links::resolve(link, {}, pagesOf(doc));  // (a page of the text, as other viewers count them)
        }
        return place;
    }
    return links::resolve(link, chaptersOf(doc), pagesOf(doc));
}

fs::path targetOf(const links::Link& link, const fs::path& from, const fs::path& libraryRoot, const LibraryIndex* index) {
    const fs::path folder = from.empty() ? libraryRoot : from.parent_path();
    if (!link.wiki) {
        if (link.path.isEmpty()) {
            return from;  // this document
        }
        const fs::path file = links::resolvePath(folder, link.path);
        return fileExists(file) ? opened(file) : fs::path();
    }
    // A wiki link: a name, with or without its extension, next to the document or anywhere in the library
    const bool hasExtension = !fs::path(link.path.toStdString()).extension().empty();
    for (const QString& candidate: {link.path, link.path + QStringLiteral(".md")}) {
        if (const fs::path file = links::resolvePath(folder, candidate); fileExists(file) && fs::is_regular_file(file)) {
            return opened(file);
        }
    }
    if (!index) {
        return {};
    }
    const QString name = link.path.section(QLatin1Char('/'), -1);
    std::vector<fs::path> found = index->filesNamed(name, !hasExtension);
    if (!hasExtension) {
        // "note" is "note.md" first (Obsidian), then any document of that name
        std::stable_partition(found.begin(), found.end(),
                              [](const fs::path& f) { return DocumentFiles::isMarkdownFile(f); });
    }
    // A path in the wiki link ("Lectures/Kalman"): the folder must end with it
    const QString dir = link.path.contains(QLatin1Char('/')) ? link.path.section(QLatin1Char('/'), 0, -2) : QString();
    found.erase(std::remove_if(found.begin(), found.end(),
                               [&](const fs::path& f) {
                                   return !dir.isEmpty() &&
                                          !qstr(f.parent_path().generic_string()).endsWith(QLatin1Char('/') + dir,
                                                                                           Qt::CaseInsensitive);
                               }),
                found.end());
    if (found.empty()) {
        return {};
    }
    const bool markdownFirst = !hasExtension && DocumentFiles::isMarkdownFile(found.front());
    const auto closest = std::min_element(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
        if (markdownFirst && DocumentFiles::isMarkdownFile(a) != DocumentFiles::isMarkdownFile(b)) {
            return DocumentFiles::isMarkdownFile(a);
        }
        return distance(folder, a.parent_path()) < distance(folder, b.parent_path());
    });
    return opened(*closest);
}

links::Link linkToFile(const fs::path& file, const fs::path& from) {
    links::Link link;
    link.path = from.empty() ? qstr(file.generic_string()) : links::relativePath(from, file);
    return link;
}

links::Link linkTo(DocumentSession& session, size_t page, const fs::path& from, const QString& chapter) {
    links::Link link = linkToFile(session.documentFile(), from);
    Document& doc = *session.getDocument();
    const std::string text = textOf(session);
    if (!text.empty() && chapter.isEmpty()) {
        // A .md: the heading above the page's start, else its line
        const std::vector<size_t> starts = MarkdownFile::pageStarts(doc);
        const size_t offset = page < starts.size() ? starts[page] : 0;
        link.line = 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(
                                                                                   std::min(offset, text.size())),
                                                    '\n'));
        return link;
    }
    if (!text.empty()) {
        link.heading = links::slug(chapter);
        const size_t at = std::min(links::resolveInText(link, text).offset, text.size());
        link.line = 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
        return link;
    }
    link.chapter = chapter;
    link.page = static_cast<int>(page) + 1;
    const std::vector<links::Page> pages = pagesOf(doc);
    if (page < pages.size()) {
        if (pages[page].pdfPage > 0) {
            link.pdfPage = pages[page].pdfPage;
        } else if (chapter.isEmpty()) {
            link.text = links::fingerprint(pages[page].text);
        }
    }
    return link;
}

std::vector<fs::path> backlinks(const std::vector<LinkRewrite::Source>& sources, const fs::path& target) {
    std::vector<fs::path> found;
    const DocumentItem item = DocumentFiles::itemOf(target);
    const auto isTarget = [&](const fs::path& file) {
        return file == target || (item.valid() && item.has(file));
    };
    const QString stem = QString::fromStdString(target.stem().string());
    for (const LinkRewrite::Source& s: sources) {
        if (isTarget(s.file)) {
            continue;
        }
        bool links = false;
        for (const QString& written: s.links) {
            const auto link = links::parse(written);
            if (link && !link->path.isEmpty() && isTarget(links::resolvePath(s.file.parent_path(), link->path))) {
                links = true;
                break;
            }
        }
        for (const QString& written: s.wikiLinks) {
            if (links) {
                break;
            }
            const QString name = written.section(QLatin1Char('#'), 0, 0).section(QLatin1Char('/'), -1);
            links = name.compare(stem, Qt::CaseInsensitive) == 0 ||
                    name.compare(QString::fromStdString(target.filename().string()), Qt::CaseInsensitive) == 0;
        }
        if (links) {
            found.push_back(s.file);
        }
    }
    return found;
}

fs::path findMoved(const links::Link& link, const fs::path& from, const LibraryIndex& index) {
    const fs::path folder = from.parent_path();
    const auto closest = [&](const std::vector<fs::path>& files) {
        return *std::min_element(files.begin(), files.end(), [&](const fs::path& a, const fs::path& b) {
            return distance(folder, a.parent_path()) < distance(folder, b.parent_path());
        });
    };
    const QString name = link.path.section(QLatin1Char('/'), -1);
    if (!name.isEmpty()) {
        std::vector<fs::path> named = index.filesNamed(name);
        if (named.empty()) {
            // A PDF with its .xopp is indexed by the .xopp: the same name without the extension
            named = index.filesNamed(QString::fromStdString(fs::path(name.toStdString()).stem().string()), true);
        }
        if (!named.empty()) {
            return opened(closest(named));
        }
    }
    if (!link.text.isEmpty()) {
        if (const std::vector<fs::path> withText = index.filesWithPageText(link.text); !withText.empty()) {
            return opened(closest(withText));
        }
    }
    return {};
}

QString relinked(const QString& written, const fs::path& from, const fs::path& target) {
    links::Link l;
    l.path = from.empty() ? qstr(target.generic_string()) : links::relativePath(from, target);
    const qsizetype hash = written.indexOf(QLatin1Char('#'));
    return links::write(l) + (hash < 0 ? QString() : written.mid(hash));
}

}  // namespace xqt::DocumentLinks

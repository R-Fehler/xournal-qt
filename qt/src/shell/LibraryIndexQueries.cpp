#include "LibraryIndex.h"
#include "LibraryIndexEntry.h"

#include "MarkdownFile.h"
#include "Tags.h"

namespace xqt {

using namespace library_index;

std::vector<LibraryIndex::Bookmark> LibraryIndex::bookmarks() const {
    std::vector<Bookmark> out;
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            for (const auto& [page, label]: e->bookmarks) {
                const double aspect = page < e->pageCount() ? e->aspects[static_cast<size_t>(page)]
                                      : e->kind == QLatin1String("md")
                                              ? MarkdownFile::PAGE_HEIGHT / MarkdownFile::PAGE_WIDTH  // (A4)
                                              : 0;
                out.push_back({e->file, page, label, aspect});
            }
        }
    }
    return out;
}

std::vector<LibraryIndex::Todo> LibraryIndex::todos() const {
    std::vector<Todo> out;
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            for (const Todo& t: e->todos) {
                out.push_back(t);
                out.back().file = e->file;
            }
        }
    }
    return out;
}

std::vector<LibraryIndex::Tagged> LibraryIndex::tagged() const {
    std::vector<Tagged> out;
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            if (QStringList t = e->tags(); !t.isEmpty()) {
                out.push_back({e->file, std::move(t)});
            }
        }
    }
    return out;
}

QStringList LibraryIndex::tagsOf(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e ? e->tags() : QStringList();
}

QStringList LibraryIndex::textTagsOf(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e ? e->textTags : QStringList();
}

bool LibraryIndex::hasTag(const fs::path& file, QStringView query) const {
    return tags::anyMatches(tagsOf(file), query);
}

std::shared_ptr<const InkDoc> LibraryIndex::inkOf(const Entry& e) const {
    auto doc = inks->find(e.file);
    const QString stamp = e.xoppStamp.isEmpty() ? e.pdfStamp : e.xoppStamp;
    return doc && !stamp.isEmpty() && doc->stamp == stamp ? doc : nullptr;
}

std::shared_ptr<const InkDoc> LibraryIndex::inkOf(const fs::path& file) const {
    EntryPtr e;
    {
        std::lock_guard lock(mtx);
        e = find(file);
    }
    return e ? inkOf(*e) : nullptr;
}

std::vector<fs::path> LibraryIndex::inkCandidates(const QString& recognizer, bool incomplete) const {
    std::vector<EntryPtr> all;
    {
        std::lock_guard lock(mtx);
        for (const auto& [folder, f]: folders) {
            for (const auto& [name, e]: f.docs) {
                all.push_back(e);
            }
        }
    }
    std::vector<fs::path> out;
    for (const EntryPtr& e: all) {
        const bool withInk = e->kind == QLatin1String("xopp") ||
                             (e->isPdf() && e->pdfKind != PdfKind::Plain && e->pdfKind != PdfKind::Unknown);
        if (!withInk || e->pageCount() == 0) {
            continue;
        }
        const auto doc = inkOf(*e);
        if (!doc || doc->recognizer != recognizer || (incomplete && !doc->complete)) {
            out.push_back(e->file);
        }
    }
    return out;
}

std::map<int, QString> LibraryIndex::knownPdfText(const fs::path& pdf) const {
    std::map<int, QString> out;
    const QString stamp = fileStamp(pdf);
    if (stamp.isEmpty()) {
        return out;
    }
    const fs::path wanted = pdf.lexically_normal();
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            if (e->pdfStamp == stamp && e->pdf.lexically_normal() == wanted) {
                out.insert(e->pdfText.begin(), e->pdfText.end());  // (the texts are shared, not copied)
            }
        }
    }
    return out;
}

int LibraryIndex::pageCount(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e && !pageless(e->kind) ? e->pageCount() : -1;
}

bool LibraryIndex::lockedOf(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e && e->locked;
}

int LibraryIndex::versionsOf(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e ? e->versions : 0;
}

PdfKind LibraryIndex::pdfKind(const fs::path& file) const {
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    return e ? e->pdfKind : PdfKind::Unknown;
}

std::vector<fs::path> LibraryIndex::filesNamed(const QString& name, bool withoutExtension) const {
    std::vector<fs::path> found;
    const QString wanted = name.toCaseFolded();
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [fileName, e]: f.docs) {
            const fs::path file(fileName);
            const QString have = QString::fromStdString((withoutExtension ? file.stem() : file).string());
            if (have.toCaseFolded() == wanted) {
                found.push_back(e->file);
            }
        }
    }
    return found;
}

std::vector<links::Page> LibraryIndex::linkPages(const fs::path& file) const {
    std::vector<links::Page> pages;
    std::lock_guard lock(mtx);
    const EntryPtr e = find(file);
    if (!e || pageless(e->kind)) {
        return pages;
    }
    for (int i = 0; i < e->pageCount(); ++i) {
        links::Page p;
        const auto at = static_cast<size_t>(i);
        p.pdfPage = at < e->pdfPage.size() && e->pdfPage[at] >= 0 ? e->pdfPage[at] + 1 : 0;
        p.text = e->elementText.value(i);
        pages.push_back(std::move(p));
    }
    return pages;
}

std::vector<LinkRewrite::Source> LibraryIndex::linkSources() const {
    std::vector<LinkRewrite::Source> sources;
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            if (!e->links.isEmpty() || !e->wikiLinks.isEmpty()) {
                sources.push_back({e->file, e->links, e->wikiLinks});
            }
        }
    }
    return sources;
}

std::vector<fs::path> LibraryIndex::filesWithPageText(const QString& fingerprint) const {
    std::vector<fs::path> found;
    const QString wanted = links::normalised(fingerprint);
    if (wanted.isEmpty()) {
        return found;
    }
    std::lock_guard lock(mtx);
    for (const auto& [folder, f]: folders) {
        for (const auto& [name, e]: f.docs) {
            for (const QString& page: e->elementText) {
                if (!page.isEmpty() && links::normalised(page).contains(wanted)) {
                    found.push_back(e->file);
                    break;
                }
            }
        }
    }
    return found;
}

}  // namespace xqt

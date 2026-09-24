#include "LinkRewrite.h"

#include <algorithm>
#include <cctype>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "DocumentFiles.h"
#include "session/DocumentLink.h"
#include "session/DocumentSession.h"
#include "session/TextFile.h"

namespace xqt::LinkRewrite {

namespace {
fs::path remapAll(fs::path p, const Moves& moves) {
    for (const auto& [from, to]: moves) {
        p = DocumentFiles::remap(p, from, to);
    }
    return p;
}

Moves inverse(const Moves& moves) {
    Moves back;
    for (auto it = moves.rbegin(); it != moves.rend(); ++it) {
        back.emplace_back(it->second, it->first);
    }
    return back;
}

bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}

/// The path part of a link as written, encoded as links::write writes it.
QString encodedPath(const QString& path) {
    links::Link l;
    l.path = path;
    return links::write(l);
}

/// Replace `from` where it is a link target: after one of `before`, followed by one of `after` (or the end).
int replaceTargets(std::string& text, const std::string& from, const std::string& to,
                   const std::vector<std::string>& before, const std::string& after) {
    if (from.empty()) {
        return 0;
    }
    int count = 0;
    for (const std::string& prefix: before) {
        const std::string needle = prefix + from;
        size_t at = 0;
        while ((at = text.find(needle, at)) != std::string::npos) {
            const size_t end = at + needle.size();
            if (end < text.size() && after.find(text[end]) == std::string::npos) {
                at = end;
                continue;
            }
            text.replace(at + prefix.size(), from.size(), to);
            at += prefix.size() + to.size();
            ++count;
        }
    }
    return count;
}
}  // namespace

std::vector<Plan> plan(const std::vector<Source>& sources, const Moves& moves) {
    std::vector<Plan> plans;
    if (moves.empty()) {
        return plans;
    }
    const Moves back = inverse(moves);
    for (const Source& s: sources) {
        // Where the document was and is (the index may have taken over the move already, or not)
        const bool there = fileExists(s.file);
        const fs::path now = there ? s.file : remapAll(s.file, moves);
        const fs::path before = there ? remapAll(s.file, back) : s.file;
        Plan p;
        p.file = now;
        for (const QString& written: s.links) {
            const auto link = links::parse(written);
            if (!link || link->path.isEmpty()) {
                continue;
            }
            const fs::path linkPath(link->path.toStdString());
            const fs::path oldTarget = links::resolvePath(before.parent_path(), link->path);
            const fs::path newTarget = remapAll(oldTarget, moves);
            if (newTarget == oldTarget && now.parent_path() == before.parent_path()) {
                continue;  // neither the document nor what it links to moved
            }
            const qsizetype hash = written.indexOf(QLatin1Char('#'));
            const QString fragment = hash < 0 ? QString() : written.mid(hash);
            const QString newPath = linkPath.is_absolute() ? QString::fromStdString(newTarget.generic_string())
                                                           : links::relativePath(now, newTarget);
            if (newPath == link->path) {
                continue;  // (it reads the same from the new place)
            }
            QString from = written.trimmed();
            if (from.startsWith(QLatin1Char('<')) && from.endsWith(QLatin1Char('>'))) {
                from = from.mid(1, from.size() - 2);
            }
            p.changes.push_back({from, encodedPath(newPath) + fragment, false});
        }
        // Wiki links name a document: a renamed one gets its new name
        for (const QString& written: s.wikiLinks) {
            const qsizetype hash = written.indexOf(QLatin1Char('#'));
            const QString name = hash < 0 ? written : written.left(hash);
            const QString rest = hash < 0 ? QString() : written.mid(hash);
            const qsizetype slash = name.lastIndexOf(QLatin1Char('/'));
            const QString last = name.mid(slash + 1);
            for (const auto& [from, to]: moves) {
                if (from.stem() == to.stem() || !from.has_extension() || !DocumentFiles::isDocumentFile(to)) {
                    continue;
                }
                const QString stem = QString::fromStdString(from.stem().string());
                const QString file = QString::fromStdString(from.filename().string());
                QString renamed;
                if (last.compare(stem, Qt::CaseInsensitive) == 0) {
                    renamed = QString::fromStdString(to.stem().string());
                } else if (last.compare(file, Qt::CaseInsensitive) == 0) {
                    renamed = QString::fromStdString(to.filename().string());
                } else {
                    continue;
                }
                p.changes.push_back({written, name.left(slash + 1) + renamed + rest, true});
                break;
            }
        }
        if (!p.changes.empty()) {
            plans.push_back(std::move(p));
        }
    }
    return plans;
}

int rewriteMarkdown(std::string& text, const std::vector<Change>& changes) {
    int count = 0;
    for (const Change& c: changes) {
        const std::string from = c.from.toStdString();
        const std::string to = c.to.toStdString();
        if (c.wiki) {
            count += replaceTargets(text, from, to, {"[["}, "]|");
        } else {
            count += replaceTargets(text, from, to, {"](", "](<", "]: ", "]:"}, ")> \t\"'\n\r");
        }
    }
    return count;
}

int rewriteDocument(Document& doc, const std::vector<Change>& changes) {
    int count = 0;
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        const PageRef page = doc.getPage(i);
        for (Layer* layer: page->getLayers()) {
            for (const Element* e: layer->getElementsView()) {
                if (e->getType() != ELEMENT_TEXT) {
                    continue;
                }
                auto* text = const_cast<Text*>(static_cast<const Text*>(e));  // (the caller's document, locked)
                std::string content = text->getText();
                if (const int n = rewriteMarkdown(content, changes); n > 0) {
                    text->setText(content);
                    count += n;
                }
            }
        }
    }
    return count;
}

int rewriteFile(const fs::path& file, const std::vector<Change>& changes, std::string& error) {
    if (DocumentFiles::isMarkdownFile(file)) {
        TextFile t;
        if (!t.load(file, TextFile::Kind::Markdown, error)) {
            return -1;
        }
        std::string text = t.text();
        const int n = rewriteMarkdown(text, changes);
        if (n > 0 && !t.save(text, error)) {
            return -1;
        }
        return n;
    }
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".xopp") {
        return 0;  // (a hybrid PDF, an old .xoj: not written here)
    }
    auto loaded = DocumentSession::loadFile(file);
    if (!loaded.document) {
        error = loaded.error;
        return -1;
    }
    const int n = rewriteDocument(*loaded.document, changes);
    if (n > 0) {
        const auto saved = DocumentSession::writeDocument(*loaded.document, file);
        if (!saved.ok) {
            error = saved.error;
            return -1;
        }
    }
    return n;
}

}  // namespace xqt::LinkRewrite

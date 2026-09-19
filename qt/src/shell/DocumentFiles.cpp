#include "DocumentFiles.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>

#include <QFile>
#include <QString>

#include "model/Document.h"
#include "session/DocumentSession.h"

namespace xqt {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool isAttachment(const fs::path& p) { return endsWith(lower(p.filename().string()), ".xopp.bg.pdf"); }
bool isXopp(const fs::path& p) {
    const auto e = lower(p.extension().string());
    return e == ".xopp" || e == ".xoj";
}
bool isPdf(const fs::path& p) { return lower(p.extension().string()) == ".pdf" && !isAttachment(p); }
bool isHidden(const fs::path& p) {
    const auto n = p.filename().string();
    return n.empty() || n[0] == '.';
}
bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
/// `path` is `folder` or inside it (both canonical).
bool isInside(const fs::path& path, const fs::path& folder) {
    return DocumentFiles::remap(path, folder, "/") != path;
}
/// "a.pdf" for the stem "a.pdf" of "a.pdf.xopp" (older Xournal++ saved annotations like that)
std::string withoutPdfSuffix(const std::string& stem) {
    return endsWith(lower(stem), ".pdf") ? stem.substr(0, stem.size() - 4) : std::string();
}

/// Natural order ("2" before "10"), case-insensitive.
bool naturalLess(const std::string& a, const std::string& b) {
    return QString::localeAwareCompare(QString::fromStdString(a), QString::fromStdString(b)) < 0;
}

DocumentFiles::Result failure(std::string msg) {
    DocumentFiles::Result r;
    r.error = std::move(msg);
    return r;
}

/// Moves or copies one file; on a different file system a move is a copy and a delete.
bool transfer(const fs::path& from, const fs::path& to, bool copy, std::string& error) {
    std::error_code ec;
    if (fs::exists(to, ec)) {
        error = "\"" + to.filename().string() + "\" already exists.";
        return false;
    }
    if (!copy) {
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        if (ec != std::errc::cross_device_link) {
            error = "Could not move \"" + from.filename().string() + "\": " + ec.message();
            return false;
        }
        ec.clear();
    }
    fs::copy_file(from, to, ec);
    if (ec) {
        error = "Could not copy \"" + from.filename().string() + "\": " + ec.message();
        return false;
    }
    if (!copy) {
        fs::remove(from, ec);
    }
    return true;
}

/// Undo steps of a half-done operation (run in reverse order on failure).
struct Rollback {
    std::vector<std::function<void()>> steps;
    bool done = false;
    ~Rollback() {
        if (!done) {
            for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
                (*it)();
            }
        }
    }
    void transferred(const fs::path& from, const fs::path& to, bool copy) {
        steps.emplace_back([from, to, copy] {
            std::error_code ec;
            if (copy) {
                fs::remove(to, ec);
            } else {
                std::string ignored;
                transfer(to, from, false, ignored);
            }
        });
    }
};

/// Rename, move or copy a document to `folder`/`name` (the name is free).
DocumentFiles::Result relocate(const DocumentItem& item, const fs::path& folder, const std::string& name, bool copy) {
    DocumentFiles::Result r;
    const fs::path newXopp = item.xopp.empty() ? fs::path() : folder / (name + item.xopp.extension().string());
    fs::path newPdf = item.pdf.empty() ? fs::path() : folder / (name + ".pdf");
    fs::path pdfSource = item.pdf;

    // The .xopp must be rewritten if it uses a PDF by its path: the path changes (relative to the .xopp).
    std::unique_ptr<Document> doc;
    if (!item.xopp.empty() && lower(item.xopp.extension().string()) == ".xopp") {
        auto loaded = DocumentSession::loadFile(item.xopp);
        if (!loaded.document) {
            return failure(loaded.error);
        }
        const fs::path ref = loaded.document->getPdfFilepath();
        if (!ref.empty() && !loaded.document->isAttachPdf()) {
            doc = std::move(loaded.document);
            std::error_code ec;
            const bool refExists = fs::exists(ref, ec);
            if (!item.pdf.empty() && (!refExists || fs::equivalent(ref, item.pdf, ec))) {
                // Its own PDF (or a lost reference, repaired with the PDF next to it): it goes along.
                doc->setPdfAttributes(newPdf, false);
            } else if (copy && refExists) {
                // A PDF somewhere else: the copy gets its own ("<name>.pdf" next to it).
                pdfSource = ref;
                newPdf = folder / (name + ".pdf");
                doc->setPdfAttributes(newPdf, false);
            }
            // else: a PDF that stays where it is; only the (relative) path to it is written anew
        }
    }

    Rollback rollback;
    std::string error;
    const bool rewritten = doc != nullptr;
    if (!newXopp.empty()) {
        if (fileExists(newXopp)) {
            return failure("\"" + newXopp.filename().string() + "\" already exists.");
        }
        if (doc) {
            const auto saved = DocumentSession::writeDocument(*doc, newXopp);
            rollback.steps.emplace_back([newXopp] {
                std::error_code ec;
                fs::remove(newXopp, ec);
            });
            if (!saved.ok) {
                return failure(saved.error);
            }
            doc.reset();  // closes the PDF before it is moved
        } else {
            if (!transfer(item.xopp, newXopp, copy, error)) {
                return failure(error);
            }
            rollback.transferred(item.xopp, newXopp, copy);
        }
        if (const fs::path att = DocumentFiles::attachmentOf(item.xopp); fileExists(att)) {
            const fs::path newAtt = DocumentFiles::attachmentOf(newXopp);
            if (!transfer(att, newAtt, copy, error)) {
                return failure(error);
            }
            rollback.transferred(att, newAtt, copy);
            if (!copy) {
                r.moved.emplace_back(att, newAtt);
            }
        }
    }
    if (!pdfSource.empty()) {
        if (!transfer(pdfSource, newPdf, copy, error)) {
            return failure(error);
        }
        rollback.transferred(pdfSource, newPdf, copy);
        if (!copy) {
            r.moved.emplace_back(item.pdf, newPdf);
        }
    }
    if (!newXopp.empty() && !copy) {
        r.moved.emplace_back(item.xopp, newXopp);
        if (rewritten) {
            // The rewritten .xopp is a new file: the old one goes last.
            std::error_code ec;
            fs::remove(item.xopp, ec);
        }
    }
    rollback.done = true;
    r.ok = true;
    r.item = {newXopp, newPdf};
    return r;
}

}  // namespace

std::string DocumentItem::name() const {
    if (!xopp.empty()) {
        const std::string stem = xopp.stem().string();
        const std::string plain = withoutPdfSuffix(stem);
        return !plain.empty() && !pdf.empty() ? plain : stem;
    }
    return pdf.stem().string();
}

namespace DocumentFiles {

bool isDocumentFile(const fs::path& file) { return isXopp(file) || isPdf(file); }

fs::path attachmentOf(const fs::path& xopp) {
    fs::path p = xopp;
    p.replace_extension();
    p += ".xopp.bg.pdf";
    return p;
}

Listing scan(const fs::path& dir) {
    Listing l;
    std::map<std::string, fs::path> xopps, pdfs;
    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path& p = it->path();
        if (isHidden(p)) {
            continue;
        }
        std::error_code tec;
        if (it->is_directory(tec)) {
            l.folders.push_back(p);
        } else if (isXopp(p)) {
            xopps[p.stem().string()] = p;
        } else if (isPdf(p)) {
            pdfs[p.stem().string()] = p;
        }
    }
    for (auto& [stem, xopp]: xopps) {
        DocumentItem item{xopp, {}};
        auto pdf = pdfs.find(stem);
        if (pdf == pdfs.end()) {
            if (const std::string plain = withoutPdfSuffix(stem); !plain.empty()) {
                pdf = pdfs.find(plain);
            }
        }
        if (pdf != pdfs.end()) {
            item.pdf = pdf->second;
            pdfs.erase(pdf);
        }
        l.items.push_back(std::move(item));
    }
    for (auto& [stem, pdf]: pdfs) {
        l.items.push_back({{}, pdf});
    }
    std::sort(l.folders.begin(), l.folders.end(),
              [](const fs::path& a, const fs::path& b) { return naturalLess(a.filename().string(), b.filename().string()); });
    std::sort(l.items.begin(), l.items.end(),
              [](const DocumentItem& a, const DocumentItem& b) { return naturalLess(a.name(), b.name()); });
    return l;
}

std::vector<DocumentItem> scanRecursive(const fs::path& dir) {
    std::vector<DocumentItem> all;
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& d, int depth) {
        Listing l = scan(d);
        std::move(l.items.begin(), l.items.end(), std::back_inserter(all));
        if (depth < 32) {  // symbolic link loops
            for (const auto& f: l.folders) {
                walk(f, depth + 1);
            }
        }
    };
    walk(dir, 0);
    return all;
}

std::vector<fs::path> foldersRecursive(const fs::path& dir) {
    std::vector<fs::path> all;
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& d, int depth) {
        for (const auto& f: scan(d).folders) {
            all.push_back(f);
            if (depth < 32) {
                walk(f, depth + 1);
            }
        }
    };
    walk(dir, 0);
    return all;
}

DocumentItem itemOf(const fs::path& file) {
    if (!fileExists(file) || isDir(file)) {
        return {};
    }
    const fs::path dir = file.parent_path();
    const std::string stem = file.stem().string();
    if (isXopp(file)) {
        DocumentItem item{file, {}};
        if (fileExists(dir / (stem + ".pdf"))) {
            item.pdf = dir / (stem + ".pdf");
        } else if (const std::string plain = withoutPdfSuffix(stem); !plain.empty() && fileExists(dir / stem)) {
            item.pdf = dir / stem;
        }
        return item;
    }
    if (isPdf(file)) {
        DocumentItem item{{}, file};
        for (const fs::path& x: {dir / (stem + ".xopp"), dir / (stem + ".xoj"), fs::path(file) += ".xopp"}) {
            if (fileExists(x)) {
                item.xopp = x;
                break;
            }
        }
        return item;
    }
    return {};
}

bool validName(const std::string& name) {
    if (name.empty() || name == "." || name == ".." || name[0] == '.' || name.back() == ' ' || name[0] == ' ') {
        return false;
    }
    return name.find_first_of("/\\") == std::string::npos && name.find('\0') == std::string::npos;
}

std::string uniqueName(const fs::path& folder, const std::string& stem) {
    auto taken = [&](const std::string& n) {
        for (const char* ext: {"", ".xopp", ".xoj", ".pdf"}) {
            if (fileExists(folder / (n + ext))) {
                return true;
            }
        }
        return false;
    };
    if (!taken(stem)) {
        return stem;
    }
    for (int i = 2;; ++i) {
        std::string n = stem + " (" + std::to_string(i) + ")";
        if (!taken(n)) {
            return n;
        }
    }
}

Result rename(const DocumentItem& item, const std::string& newName) {
    if (!item.valid()) {
        return failure("The document does not exist.");
    }
    if (!validName(newName)) {
        return failure("\"" + newName + "\" cannot be used as a name.");
    }
    if (newName == item.name()) {
        Result r;
        r.ok = true;
        r.item = item;
        return r;
    }
    const fs::path folder = item.folder();
    for (const char* ext: {".xopp", ".xoj", ".pdf"}) {
        if (fileExists(folder / (newName + ext))) {
            return failure("A document named \"" + newName + "\" already exists here.");
        }
    }
    return relocate(item, folder, newName, false);
}

Result move(const DocumentItem& item, const fs::path& folder) {
    if (!item.valid()) {
        return failure("The document does not exist.");
    }
    if (!isDir(folder)) {
        return failure("The folder does not exist.");
    }
    std::error_code ec;
    if (fs::equivalent(item.folder(), folder, ec)) {
        Result r;
        r.ok = true;
        r.item = item;
        return r;
    }
    return relocate(item, folder, uniqueName(folder, item.name()), false);
}

Result import(const fs::path& file, const fs::path& folder) {
    if (!isDir(folder)) {
        return failure("The folder does not exist.");
    }
    if (isDir(file)) {
        // Not into itself
        if (isInside(fs::weakly_canonical(folder), fs::weakly_canonical(file))) {
            return failure("A folder cannot be imported into itself.");
        }
        Result r = createFolder(folder, uniqueName(folder, file.filename().string()));
        if (!r.ok) {
            return r;
        }
        const Listing l = scan(file);
        for (const auto& item: l.items) {
            if (Result sub = import(item.main(), r.folder); !sub.ok) {
                r.error = sub.error;  // go on with the others
            }
        }
        for (const auto& sub: l.folders) {
            if (Result s = import(sub, r.folder); !s.ok) {
                r.error = s.error;
            }
        }
        return r;
    }
    const DocumentItem item = itemOf(file);
    if (!item.valid()) {
        return failure("\"" + file.filename().string() + "\" is not a PDF or Xournal document.");
    }
    return relocate(item, folder, uniqueName(folder, item.name()), true);
}

Result trash(const DocumentItem& item) {
    if (!item.valid()) {
        return failure("The document does not exist.");
    }
    for (const fs::path& f: {item.xopp, item.xopp.empty() ? fs::path() : attachmentOf(item.xopp), item.pdf}) {
        if (!f.empty() && fileExists(f) && !QFile::moveToTrash(QString::fromStdString(f.string()))) {
            return failure("Could not move \"" + f.filename().string() + "\" to the trash.");
        }
    }
    Result r;
    r.ok = true;
    return r;
}

Result createFolder(const fs::path& parent, const std::string& name) {
    if (!validName(name)) {
        return failure("\"" + name + "\" cannot be used as a name.");
    }
    const fs::path dir = parent / name;
    if (fileExists(dir)) {
        return failure("\"" + name + "\" already exists.");
    }
    std::error_code ec;
    if (!fs::create_directory(dir, ec)) {
        return failure("Could not create the folder: " + ec.message());
    }
    Result r;
    r.ok = true;
    r.folder = dir;
    return r;
}

Result renameFolder(const fs::path& folder, const std::string& newName) {
    if (!validName(newName)) {
        return failure("\"" + newName + "\" cannot be used as a name.");
    }
    const fs::path target = folder.parent_path() / newName;
    if (target == folder) {
        Result r;
        r.ok = true;
        r.folder = folder;
        return r;
    }
    if (fileExists(target)) {
        return failure("\"" + newName + "\" already exists.");
    }
    std::error_code ec;
    fs::rename(folder, target, ec);
    if (ec) {
        return failure("Could not rename the folder: " + ec.message());
    }
    Result r;
    r.ok = true;
    r.folder = target;
    r.moved.emplace_back(folder, target);
    return r;
}

Result moveFolder(const fs::path& folder, const fs::path& target) {
    if (!isDir(folder) || !isDir(target)) {
        return failure("The folder does not exist.");
    }
    std::error_code ec;
    const fs::path src = fs::weakly_canonical(folder), dst = fs::weakly_canonical(target);
    if (fs::equivalent(src.parent_path(), dst, ec)) {
        Result r;
        r.ok = true;
        r.folder = folder;
        return r;
    }
    if (isInside(dst, src)) {
        return failure("A folder cannot be moved into itself.");
    }
    const fs::path dest = target / uniqueName(target, folder.filename().string());
    fs::rename(folder, dest, ec);
    if (ec) {
        return failure("Could not move the folder: " + ec.message());
    }
    Result r;
    r.ok = true;
    r.folder = dest;
    r.moved.emplace_back(folder, dest);
    return r;
}

Result trashFolder(const fs::path& folder) {
    if (!isDir(folder)) {
        return failure("The folder does not exist.");
    }
    if (!QFile::moveToTrash(QString::fromStdString(folder.string()))) {
        return failure("Could not move \"" + folder.filename().string() + "\" to the trash.");
    }
    Result r;
    r.ok = true;
    return r;
}

fs::path remap(const fs::path& path, const fs::path& from, const fs::path& to) {
    auto strip = [](fs::path p) {
        p = p.lexically_normal();
        if (!p.has_filename() && p.has_parent_path() && p != p.root_path()) {
            p = p.parent_path();  // "a/b/" -> "a/b"
        }
        return p;
    };
    const fs::path p = strip(path), f = strip(from);
    auto [fi, pi] = std::mismatch(f.begin(), f.end(), p.begin(), p.end());
    if (fi != f.end()) {
        return path;
    }
    fs::path result = to;
    for (; pi != p.end(); ++pi) {
        result /= *pi;
    }
    return result;
}

}  // namespace DocumentFiles

}  // namespace xqt

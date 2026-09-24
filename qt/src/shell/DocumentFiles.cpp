#include "DocumentFiles.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>

#include <QFile>
#include <QImageReader>
#include <QString>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/MergedPdf.h"

namespace xqt {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec);
}
/// "a.pdf" for the stem "a.pdf" of "a.pdf.xopp" (older Xournal++ saved annotations like that)
std::string withoutPdfSuffix(const std::string& stem) {
    return endsWith(lower(stem), ".pdf") ? stem.substr(0, stem.size() - 4) : std::string();
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
bool isMd(const fs::path& p) { return lower(p.extension().string()) == ".md"; }
/// "name.xopp.bg_1.png": a background image upstream stores with "name.xopp"
bool isImageAttachment(const fs::path& p) {
    const std::string n = lower(p.filename().string());
    return n.find(".xopp.bg_") != std::string::npos || n.find(".xoj.bg_") != std::string::npos;
}
/// The image extensions the library shows, the preferred first (an image that pairs with a .xopp: the first there)
const std::vector<std::string>& imageExtensions() {
    static const std::vector<std::string> exts = [] {
        std::vector<std::string> e{".png", ".jpg", ".jpeg"};
        const auto formats = QImageReader::supportedImageFormats();
        if (formats.contains("webp")) {
            e.emplace_back(".webp");
        }
        if (formats.contains("heic") || formats.contains("heif")) {
            e.emplace_back(".heic");
            e.emplace_back(".heif");
        }
        return e;
    }();
    return exts;
}
/// The place of the image's extension in imageExtensions() (-1: no image)
int imageRank(const fs::path& p) {
    const auto& exts = imageExtensions();
    const auto it = std::find(exts.begin(), exts.end(), lower(p.extension().string()));
    return it == exts.end() ? -1 : static_cast<int>(it - exts.begin());
}
bool isImage(const fs::path& p) { return imageRank(p) >= 0 && !isImageAttachment(p) && !isHidden(p); }
/// The ways an extension is written that pair an image with a .xopp: ".jpg", ".JPG", ".Jpg" (in the order of
/// their names, as a listing sorts them)
std::vector<std::string> spellings(const std::string& ext) {
    std::string upper = ext, capital = ext;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (capital.size() > 1) {
        capital[1] = static_cast<char>(std::toupper(static_cast<unsigned char>(capital[1])));
    }
    return {upper, capital, ext};
}
bool pairingSpelling(const fs::path& image) {
    const std::string ext = image.extension().string();
    const auto s = spellings(lower(ext));
    return std::find(s.begin(), s.end(), ext) != s.end();
}
/// The image a .xopp with this name in `dir` annotates (empty: none)
fs::path imageNamed(const fs::path& dir, const std::string& stem) {
    for (const std::string& ext: imageExtensions()) {
        for (const std::string& spelling: spellings(ext)) {
            if (const fs::path p = dir / (stem + spelling); fileExists(p)) {
                return p;
            }
        }
    }
    return {};
}
/// The PDF a .xopp belongs to (empty: none)
fs::path pdfOf(const fs::path& xopp) {
    const fs::path dir = xopp.parent_path();
    const std::string stem = xopp.stem().string();
    if (fileExists(dir / (stem + ".pdf"))) {
        return dir / (stem + ".pdf");
    }
    if (const std::string plain = withoutPdfSuffix(stem); !plain.empty() && fileExists(dir / stem)) {
        return dir / stem;
    }
    return {};
}
bool isDir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
/// `path` is `folder` or inside it (both canonical).
bool isInside(const fs::path& path, const fs::path& folder) {
    return DocumentFiles::remap(path, folder, "/") != path;
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
    const fs::path newMd = item.md.empty() ? fs::path() : folder / (name + item.md.extension().string());
    const fs::path newImage = item.image.empty() ? fs::path() : folder / (name + item.image.extension().string());

    // The .xopp must be rewritten if it uses a PDF by its path: the path changes (relative to the .xopp). The same
    // for the image it annotates.
    std::unique_ptr<Document> doc;
    if (!item.xopp.empty() && lower(item.xopp.extension().string()) == ".xopp") {
        auto loaded = DocumentSession::loadFile(item.xopp);
        if (!loaded.document) {
            return failure(loaded.error);
        }
        if (!item.image.empty()) {
            // Pages with its image (or a lost one, repaired with the image next to it) as background, by path
            std::error_code ec;
            for (size_t i = 0; i < loaded.document->getPageCount(); ++i) {
                PageRef page = loaded.document->getPage(i);
                BackgroundImage& bg = page->getBackgroundImage();
                if (!page->getBackgroundType().isImagePage() || bg.isAttached() || bg.getFilepath().empty()) {
                    continue;
                }
                if (!fileExists(bg.getFilepath()) || fs::equivalent(bg.getFilepath(), item.image, ec)) {
                    bg.setFilepath(newImage);
                    doc = std::move(loaded.document);
                    loaded.document.reset();
                    break;
                }
            }
            if (doc) {
                for (size_t i = 0; i < doc->getPageCount(); ++i) {  // (the other pages with it)
                    BackgroundImage& bg = doc->getPage(i)->getBackgroundImage();
                    if (doc->getPage(i)->getBackgroundType().isImagePage() && !bg.isAttached() &&
                        (bg.getFilepath() == item.image || !fileExists(bg.getFilepath()))) {
                        bg.setFilepath(newImage);
                    }
                }
            }
        }
        const fs::path ref = doc ? doc->getPdfFilepath() : loaded.document->getPdfFilepath();
        if (!ref.empty() && !(doc ? doc->isAttachPdf() : loaded.document->isAttachPdf())) {
            if (!doc) {
                doc = std::move(loaded.document);
            }
            std::error_code ec;
            const bool refExists = fs::exists(ref, ec);
            const fs::path pages = DocumentFiles::pagesOf(item.xopp);
            if (refExists && fileExists(pages) && fs::equivalent(ref, pages, ec)) {
                // Its merged PDF with pasted pages: it goes along (below), under the new name
                doc->setPdfAttributes(DocumentFiles::pagesOf(folder / (name + item.xopp.extension().string())), false);
            } else if (!item.pdf.empty() && (!refExists || fs::equivalent(ref, item.pdf, ec))) {
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
    const std::vector<fs::path> oldImages =
            item.xopp.empty() ? std::vector<fs::path>() : DocumentFiles::imageAttachmentsOf(item.xopp);
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
        // Background images stored with it: a .xopp written again writes its own (below the old ones go)
        if (rewritten) {
            for (const fs::path& img: DocumentFiles::imageAttachmentsOf(newXopp)) {
                rollback.steps.emplace_back([img] {
                    std::error_code ec;
                    fs::remove(img, ec);
                });
            }
        } else {
            const std::string oldName = item.xopp.filename().string();
            for (const fs::path& img: DocumentFiles::imageAttachmentsOf(item.xopp)) {
                const fs::path newImg =
                        folder / (newXopp.filename().string() + img.filename().string().substr(oldName.size()));
                if (!transfer(img, newImg, copy, error)) {
                    return failure(error);
                }
                rollback.transferred(img, newImg, copy);
                if (!copy) {
                    r.moved.emplace_back(img, newImg);
                }
            }
        }
        if (const fs::path pages = DocumentFiles::pagesOf(item.xopp); fileExists(pages)) {
            const fs::path newPages = DocumentFiles::pagesOf(newXopp);
            if (fileExists(newPages)) {
                std::error_code ec;
                fs::remove(newPages, ec);  // left behind by a document of that name that is gone (the name is free)
            }
            if (!transfer(pages, newPages, copy, error)) {
                return failure(error);
            }
            rollback.transferred(pages, newPages, copy);
            if (!copy) {
                r.moved.emplace_back(pages, newPages);  // (an open document follows)
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
    for (const auto& [from, to]: {std::pair(item.md, newMd), std::pair(item.image, newImage)}) {
        if (!from.empty()) {
            if (!transfer(from, to, copy, error)) {
                return failure(error);
            }
            rollback.transferred(from, to, copy);
            if (!copy) {
                r.moved.emplace_back(from, to);
            }
        }
    }
    if (!newXopp.empty() && !copy) {
        r.moved.emplace_back(item.xopp, newXopp);
        if (rewritten) {
            // The rewritten .xopp is a new file (with its own background images): the old one goes last.
            std::error_code ec;
            for (const fs::path& img: oldImages) {
                fs::remove(img, ec);
            }
            fs::remove(item.xopp, ec);
        }
    }
    rollback.done = true;
    r.ok = true;
    r.item = {newXopp, newPdf, newMd, newImage};
    return r;
}

}  // namespace

std::string DocumentItem::name() const {
    if (!xopp.empty()) {
        const std::string stem = xopp.stem().string();
        const std::string plain = withoutPdfSuffix(stem);
        return !plain.empty() && !pdf.empty() ? plain : stem;
    }
    return main().stem().string();
}

DocumentItem::Kind DocumentItem::kind() const {
    return !pdf.empty() ? Kind::Pdf : !image.empty() ? Kind::Image : !md.empty() ? Kind::Markdown : Kind::Notes;
}

const char* DocumentItem::kindName() const {
    switch (kind()) {
        case Kind::Pdf:
            return "pdf";
        case Kind::Image:
            return "image";
        case Kind::Markdown:
            return "md";
        case Kind::Notes:
            break;
    }
    return "notes";
}

bool DocumentItem::has(const fs::path& file) const {
    return !file.empty() && (file == xopp || file == pdf || file == md || file == image);
}

namespace DocumentFiles {

bool isDocumentFile(const fs::path& file) { return isXopp(file) || isPdf(file) || isMd(file) || isImage(file); }
bool isMarkdownFile(const fs::path& file) { return isMd(file); }
bool isImageFile(const fs::path& file) { return isImage(file); }

fs::path attachmentOf(const fs::path& xopp) {
    fs::path p = xopp;
    p.replace_extension();
    p += ".xopp.bg.pdf";
    return p;
}

fs::path pagesOf(const fs::path& xopp) { return MergedPdf::sidecarOf(xopp); }

std::vector<fs::path> imageAttachmentsOf(const fs::path& xopp) {
    std::vector<fs::path> images;
    const std::string prefix = xopp.filename().string() + ".bg_";
    std::error_code ec;
    for (auto it = fs::directory_iterator(xopp.parent_path(), ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0) {
            images.push_back(it->path());
        }
    }
    std::sort(images.begin(), images.end());
    return images;
}

std::vector<fs::path> filesOf(const DocumentItem& item) {
    std::vector<fs::path> files;
    const fs::path none;
    for (const fs::path& f: {item.xopp, item.xopp.empty() ? none : attachmentOf(item.xopp),
                             item.xopp.empty() ? none : pagesOf(item.xopp), item.pdf, item.md, item.image}) {
        if (!f.empty() && fileExists(f)) {
            files.push_back(f);
        }
    }
    if (!item.xopp.empty()) {
        for (const fs::path& img: imageAttachmentsOf(item.xopp)) {
            files.push_back(img);
        }
    }
    return files;
}

Listing scan(const fs::path& dir) {
    Listing l;
    std::map<std::string, fs::path> xopps, pdfs;
    std::map<std::string, std::vector<fs::path>> images;  ///< by name
    std::vector<fs::path> mds;
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
        } else if (isMd(p)) {
            mds.push_back(p);
        } else if (isImage(p)) {
            images[p.stem().string()].push_back(p);
        }
    }
    for (auto& [stem, list]: images) {
        // The one a .xopp of this name annotates first (as itemOf() finds it)
        std::sort(list.begin(), list.end(), [](const fs::path& a, const fs::path& b) {
            const bool pa = pairingSpelling(a), pb = pairingSpelling(b);
            if (pa != pb) {
                return pa;
            }
            const int ra = imageRank(a), rb = imageRank(b);
            return ra != rb ? ra < rb : a.filename() < b.filename();
        });
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
        } else if (auto img = images.find(stem); img != images.end() && pairingSpelling(img->second.front())) {
            item.image = img->second.front();
            img->second.erase(img->second.begin());
        }
        l.items.push_back(std::move(item));
    }
    for (auto& [stem, pdf]: pdfs) {
        l.items.push_back({{}, pdf});
    }
    for (auto& md: mds) {
        l.items.push_back({{}, {}, md});
    }
    for (auto& [stem, list]: images) {
        for (auto& img: list) {
            l.items.push_back({{}, {}, {}, img});
        }
    }
    std::sort(l.folders.begin(), l.folders.end(),
              [](const fs::path& a, const fs::path& b) { return naturalLess(a.filename().string(), b.filename().string()); });
    std::stable_sort(l.items.begin(), l.items.end(),
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
        DocumentItem item{file, pdfOf(file)};
        if (item.pdf.empty()) {
            item.image = imageNamed(dir, stem);
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
    if (isMd(file)) {
        return {{}, {}, file};
    }
    if (isImage(file)) {
        DocumentItem item{{}, {}, {}, file};
        // With the .xopp of its name, unless that belongs to a PDF or to another image of the name
        for (const fs::path& x: {dir / (stem + ".xopp"), dir / (stem + ".xoj")}) {
            if (fileExists(x)) {
                if (pdfOf(x).empty() && imageNamed(dir, stem) == file) {
                    item.xopp = x;
                }
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

namespace {
/// A document with this name is in `folder`: a .xopp, PDF, Markdown file or image (in any spelling of the extension
/// that pairs).
bool nameTaken(const fs::path& folder, const std::string& name) {
    for (const char* ext: {".xopp", ".xoj", ".pdf", ".md"}) {
        if (fileExists(folder / (name + ext))) {
            return true;
        }
    }
    return !imageNamed(folder, name).empty();
}
}  // namespace

std::string uniqueName(const fs::path& folder, const std::string& stem) {
    auto taken = [&](const std::string& n) { return fileExists(folder / n) || nameTaken(folder, n); };
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
    if (nameTaken(folder, newName)) {
        return failure("A document named \"" + newName + "\" already exists here.");
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

namespace {
/// A folder with its subfolders; `depth` against symbolic link loops.
Result importFolder(const fs::path& dir, const fs::path& folder, int depth) {
    Result r = createFolder(folder, uniqueName(folder, dir.filename().string()));
    if (!r.ok) {
        return r;
    }
    std::vector<std::string> errors;
    const Listing l = scan(dir);
    for (const auto& item: l.items) {
        Result sub = relocate(item, r.folder, uniqueName(r.folder, item.name()), true);
        if (sub.ok) {
            ++r.documents;
        } else {
            errors.push_back(sub.error);  // go on with the others
        }
    }
    for (const auto& sub: l.folders) {
        if (depth >= 32) {
            errors.push_back("\"" + sub.string() + "\" is nested too deep.");
            continue;
        }
        Result s = importFolder(sub, r.folder, depth + 1);
        r.documents += s.documents;
        if (!s.error.empty()) {
            errors.push_back(s.error);
        }
    }
    for (const auto& e: errors) {
        r.error += (r.error.empty() ? "" : "\n") + e;
    }
    return r;
}
}  // namespace

Result import(const fs::path& file, const fs::path& folder) {
    if (!isDir(folder)) {
        return failure("The folder does not exist.");
    }
    if (isDir(file)) {
        // Not into itself
        if (isInside(fs::weakly_canonical(folder), fs::weakly_canonical(file))) {
            return failure("A folder cannot be imported into itself.");
        }
        return importFolder(file, folder, 0);
    }
    const DocumentItem item = itemOf(file);
    if (!item.valid()) {
        return failure("\"" + file.filename().string() +
                       "\" is not a document the library shows (notes, PDFs, Markdown, images).");
    }
    Result r = relocate(item, folder, uniqueName(folder, item.name()), true);
    r.documents = r.ok ? 1 : 0;
    return r;
}

Result trash(const DocumentItem& item) {
    if (!item.valid()) {
        return failure("The document does not exist.");
    }
    for (const fs::path& f: filesOf(item)) {
        if (!QFile::moveToTrash(QString::fromStdString(f.string()))) {
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
    if (ec == std::errc::cross_device_link) {
        // Another disk (e.g. a library on a USB stick): copy everything, then delete the original.
        ec.clear();
        fs::copy(folder, dest, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) {
            std::error_code rec;
            fs::remove_all(dest, rec);
            return failure("Could not move the folder: " + ec.message());
        }
        fs::remove_all(folder, ec);
    }
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

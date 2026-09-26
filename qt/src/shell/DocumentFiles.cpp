#include "DocumentFiles.h"
#include "SyncConflicts.h"

#include <chrono>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include <optional>

#include <QCollator>
#include <QFile>
#include <QImageReader>
#include <QLocale>
#include <QString>

#include "model/Document.h"
#include "model/XojPage.h"
#include "session/DocumentImages.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/MergedPdf.h"
#include "session/TextFile.h"

#include "SystemApps.h"

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
/// A backup ("name.xopp~", "notes.txt~")
bool isBackup(const fs::path& p) {
    const std::string n = p.filename().string();
    return !n.empty() && n.back() == '~';
}
/// Text and code: shown as plain text (by the extension, or the name of a file without one)
bool isText(const fs::path& p) {
    static const std::set<std::string> exts{
            // text, markup, data
            ".txt", ".text", ".log", ".csv", ".tsv", ".json", ".jsonc", ".xml", ".yaml", ".yml", ".toml", ".ini",
            ".cfg", ".conf", ".org", ".rst", ".adoc", ".asciidoc", ".textile", ".bib", ".srt", ".vtt",
            // LaTeX
            ".tex", ".sty", ".cls", ".bst", ".ltx",
            // code
            ".py", ".pyw", ".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".ipp", ".m", ".mm", ".java",
            ".kt", ".kts", ".scala", ".groovy", ".gradle", ".js", ".mjs", ".cjs", ".jsx", ".ts", ".tsx", ".rs", ".go",
            ".rb", ".php", ".pl", ".pm", ".lua", ".r", ".jl", ".swift", ".cs", ".fs", ".hs", ".ml", ".mli", ".el",
            ".lisp", ".clj", ".scm", ".erl", ".ex", ".exs", ".dart", ".zig", ".nim", ".v", ".sv", ".vhd", ".vhdl",
            ".f", ".f90", ".for", ".sql", ".html", ".htm", ".css", ".scss", ".sass", ".less", ".vue", ".svelte",
            ".qml", ".cmake", ".mk", ".sh", ".bash", ".zsh", ".fish", ".bat", ".cmd", ".ps1", ".awk", ".sed",
            ".diff", ".patch", ".proto", ".graphql", ".glsl", ".hlsl", ".asm", ".s", ".mat", ".gp", ".plt"};
    static const std::set<std::string> names{"makefile", "gnumakefile", "cmakelists.txt", "dockerfile", "readme",
                                             "license", "copying", "authors", "changelog", "todo", "vagrantfile",
                                             "gemfile", "rakefile", "procfile", "justfile"};
    if (isHidden(p) || isBackup(p)) {
        return false;
    }
    const std::string ext = lower(p.extension().string());
    return ext.empty() ? names.count(lower(p.filename().string())) > 0 : exts.count(ext) > 0;
}
/// Files of the system that no one wants to see among their documents
bool isSystemFile(const fs::path& p) {
    const std::string n = lower(p.filename().string());
    return n == "thumbs.db" || n == "desktop.ini" || n == "icon\r";
}
/// The ways an extension is written that pair an image with a .xopp: ".JPG", ".jpg" (in the order of their names, as
/// a listing sorts them)
std::vector<std::string> spellings(const std::string& ext) {
    std::string upper = ext;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return {upper, ext};
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

bool naturalLess(const std::string& a, const std::string& b) {
    return DocumentFiles::compareNames(QString::fromStdString(a), QString::fromStdString(b)) < 0;
}

bool isAsciiDigit(QChar c) { return c >= u'0' && c <= u'9'; }

/// Natural order without a language: runs of digits by their value, everything else by case-folded code points.
int plainNaturalCompare(const QString& a, const QString& b) {
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isAsciiDigit(a[i]) && isAsciiDigit(b[j])) {
            qsizetype ei = i, ej = j;
            while (ei < a.size() && isAsciiDigit(a[ei])) {
                ++ei;
            }
            while (ej < b.size() && isAsciiDigit(b[ej])) {
                ++ej;
            }
            qsizetype si = i, sj = j;  // (leading zeros do not count)
            while (si + 1 < ei && a[si] == u'0') {
                ++si;
            }
            while (sj + 1 < ej && b[sj] == u'0') {
                ++sj;
            }
            if (ei - si != ej - sj) {
                return ei - si < ej - sj ? -1 : 1;
            }
            for (; si < ei; ++si, ++sj) {
                if (a[si] != b[sj]) {
                    return a[si] < b[sj] ? -1 : 1;
                }
            }
            i = ei;
            j = ej;
            continue;
        }
        const char16_t ca = a[i].toCaseFolded().unicode(), cb = b[j].toCaseFolded().unicode();
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
        ++i;
        ++j;
    }
    if (i < a.size() || j < b.size()) {
        return i < a.size() ? 1 : -1;
    }
    return 0;
}

/// The collator of the current language, if it sorts naturally and ignores case; else none.
QCollator* nameCollator() {
    struct Cached {
        QLocale locale = QLocale::c();
        std::optional<QCollator> collator;
        bool usable = false;
    };
    thread_local std::optional<Cached> cached;  // (QCollator is not thread-safe)
    const QLocale locale;
    if (!cached || cached->locale != locale) {
        cached.emplace();
        cached->locale = locale;
        if (locale.language() != QLocale::C) {
            QCollator& c = cached->collator.emplace(locale);
            c.setNumericMode(true);
            c.setCaseSensitivity(Qt::CaseInsensitive);
            // (checked: without ICU, numeric mode and case-insensitivity can be missing)
            cached->usable = c.compare(QStringLiteral("2"), QStringLiteral("10")) < 0 &&
                             c.compare(QStringLiteral("lecture"), QStringLiteral("Makefile")) < 0 &&
                             c.compare(QStringLiteral("Makefile"), QStringLiteral("notes")) < 0;
        }
    }
    return cached->usable ? &*cached->collator : nullptr;
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

/// Moves or copies a folder with everything in it (a .md's "name.assets"); on a different file system a move is a copy
/// and a delete.
bool transferFolder(const fs::path& from, const fs::path& to, bool copy, std::string& error) {
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
    fs::copy(from, to, fs::copy_options::recursive, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove_all(to, ignored);
        error = "Could not copy \"" + from.filename().string() + "\": " + ec.message();
        return false;
    }
    if (!copy) {
        fs::remove_all(from, ec);
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
    void transferredFolder(const fs::path& from, const fs::path& to, bool copy) {
        steps.emplace_back([from, to, copy] {
            std::error_code ec;
            if (copy) {
                fs::remove_all(to, ec);
            } else {
                std::string ignored;
                transferFolder(to, from, false, ignored);
            }
        });
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
    const fs::path newOther = item.other.empty() ? fs::path() : folder / name;  // (its whole file name)

    // The .xopp must be rewritten if it uses a PDF by its path: the path changes (relative to the .xopp). The same
    // for the image it annotates.
    std::unique_ptr<Document> doc;
    std::shared_ptr<md::images::RootHandle> pictures;  // (its Markdown's pictures, while it is written again)
    if (!item.xopp.empty() && lower(item.xopp.extension().string()) == ".xopp") {
        auto loaded = DocumentSession::loadFile(item.xopp);
        if (!loaded.document) {
            return failure(loaded.error);
        }
        pictures = loaded.pictures;
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
    for (const auto& [from, to]: {std::pair(item.md, newMd), std::pair(item.image, newImage), std::pair(item.other, newOther)}) {
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
    if (!item.md.empty()) {
        // Its pictures ("name.assets" next to it) go along; its links to them follow a new name
        // (qt/docs/md-images.md)
        const fs::path assets = DocumentImages::assetsFolder(item.md);
        const fs::path newAssets = DocumentImages::assetsFolder(newMd);
        if (isDir(assets) && assets != newAssets) {
            if (!transferFolder(assets, newAssets, copy, error)) {
                return failure(error);
            }
            rollback.transferredFolder(assets, newAssets, copy);
            if (!copy) {
                r.moved.emplace_back(assets, newAssets);
            }
        }
        const std::string oldName = DocumentImages::assetsName(item.md);
        const std::string newName = DocumentImages::assetsName(newMd);
        if (oldName != newName) {
            std::ifstream in(newMd, std::ios::binary);
            const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            in.close();
            const std::string renamed = DocumentImages::renamedAssetLinks(bytes, oldName, newName);
            if (renamed != bytes) {
                if (!TextFile::writeAtomically(newMd, renamed, error)) {
                    return failure(error);
                }
                rollback.steps.emplace_back([newMd, bytes] {
                    std::string ignored;
                    TextFile::writeAtomically(newMd, bytes, ignored);
                });
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
    r.item.other = newOther;
    return r;
}

}  // namespace

std::string DocumentItem::name() const {
    if (xopp.empty() && pdf.empty() && md.empty() && image.empty()) {
        return other.filename().string();  // a text or other file: its whole name ("report.docx")
    }
    if (!xopp.empty()) {
        const std::string stem = xopp.stem().string();
        const std::string plain = withoutPdfSuffix(stem);
        return !plain.empty() && !pdf.empty() ? plain : stem;
    }
    return main().stem().string();
}

DocumentItem::Kind DocumentItem::kind() const {
    if (!other.empty() && xopp.empty() && pdf.empty() && md.empty() && image.empty()) {
        return isText(other) ? Kind::Text : Kind::Other;
    }
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
        case Kind::Text:
            return "text";
        case Kind::Other:
            return "other";
        case Kind::Notes:
            break;
    }
    return "notes";
}

bool DocumentItem::has(const fs::path& file) const {
    return !file.empty() && (file == xopp || file == pdf || file == md || file == image || file == other);
}

namespace DocumentFiles {

namespace {
/// A .xopp changed this long after the hybrid PDF of its name was not written with it (an export is written right
/// after the PDF): it was edited elsewhere, e.g. in Xournal++.
constexpr auto EDITED_AFTER = std::chrono::seconds(60);

/// A .xopp next to the hybrid PDF of its name (its export for Xournal++, or the .xopp it was, kept as it is): the PDF
/// is the document, and the card opens it. Only such pairs look into the PDF (HybridPdf::isHybrid, remembered per
/// file version), lone PDFs are not. Returns true if the .xopp was changed well after the PDF: then it is not hidden
/// behind the PDF, the two are two documents (the caller splits them).
bool markHybrid(DocumentItem& item) {
    item.hybrid = false;
    if (item.xopp.empty() || item.pdf.empty() || !HybridPdf::isHybrid(item.pdf)) {
        return false;
    }
    std::error_code ex, ep;
    const auto xoppTime = fs::last_write_time(item.xopp, ex), pdfTime = fs::last_write_time(item.pdf, ep);
    if (!ex && !ep && xoppTime > pdfTime + EDITED_AFTER) {
        return true;
    }
    item.hybrid = true;
    return false;
}
}  // namespace

bool isDocumentFile(const fs::path& file) { return isXopp(file) || isPdf(file) || isMd(file) || isImage(file); }
bool isMarkdownFile(const fs::path& file) { return isMd(file); }
bool isImageFile(const fs::path& file) { return isImage(file); }
bool isTextFile(const fs::path& file) { return isText(file); }
bool isOtherFile(const fs::path& file) {
    return !file.empty() && !isHidden(file) && !isBackup(file) && !isSystemFile(file) && !isDocumentFile(file) &&
           !isAttachment(file) && !isImageAttachment(file) && !isText(file);
}

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
                             item.xopp.empty() ? none : pagesOf(item.xopp), item.pdf, item.md, item.image,
                             item.other}) {
        if (!f.empty() && fileExists(f)) {
            files.push_back(f);
        }
    }
    if (!item.xopp.empty()) {
        for (const fs::path& img: imageAttachmentsOf(item.xopp)) {
            files.push_back(img);
        }
    }
    if (!item.md.empty()) {
        if (const fs::path assets = DocumentImages::assetsFolder(item.md); isDir(assets)) {
            files.push_back(assets);  // (its pictures: one document with the .md)
        }
    }
    return files;
}

namespace {
/// Conflict copies of sync apps (SyncConflicts.h) whose document is among `items` go to its `conflicts`.
void foldConflicts(std::vector<DocumentItem>& items) {
    std::map<std::string, size_t> byFile;  ///< every file name of an item -> the item
    for (size_t i = 0; i < items.size(); ++i) {
        for (const fs::path* f: {&items[i].xopp, &items[i].pdf, &items[i].md, &items[i].image, &items[i].other}) {
            if (!f->empty()) {
                byFile.emplace(f->filename().string(), i);
            }
        }
    }
    std::vector<bool> folded(items.size(), false);
    for (size_t i = 0; i < items.size(); ++i) {
        const auto conflict = SyncConflicts::parse(items[i].main().filename().string());
        if (!conflict) {
            continue;
        }
        const auto owner = byFile.find(conflict->original);
        if (owner == byFile.end() || owner->second == i || folded[owner->second]) {
            continue;  // (its document is not here: a document of its own)
        }
        items[owner->second].conflicts.push_back(items[i].main());
        folded[i] = true;
    }
    size_t kept = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!folded[i]) {
            if (kept != i) {
                items[kept] = std::move(items[i]);  // (not onto itself: that empties it)
            }
            ++kept;
        }
    }
    items.resize(kept);
}
}  // namespace

Listing scan(const fs::path& dir, unsigned include) {
    Listing l;
    std::map<std::string, fs::path> xopps, pdfs;
    std::map<std::string, std::vector<fs::path>> images;  ///< by name
    std::vector<fs::path> mds, others;
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
        } else if (include != Documents && (isText(p) ? (include & TextFiles) != 0
                                                      : (include & OtherFiles) != 0 && isOtherFile(p))) {
            if (it->is_regular_file(tec)) {  // (not a socket, a device)
                others.push_back(p);
            }
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
            if (markHybrid(item)) {
                l.items.push_back({{}, item.pdf});  // (a .xopp edited after its hybrid PDF: both listed)
                item.pdf.clear();
            }
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
    for (auto& file: others) {
        DocumentItem item;
        item.other = file;
        l.items.push_back(std::move(item));
    }
    foldConflicts(l.items);
    // A .md's pictures ("name.assets" next to it) are part of it, not a folder of the library (qt/docs/md-images.md)
    if (!mds.empty()) {
        std::set<std::string> assets;
        for (const auto& md: mds) {
            assets.insert(DocumentImages::assetsName(md));
        }
        l.folders.erase(std::remove_if(l.folders.begin(), l.folders.end(),
                                       [&](const fs::path& f) { return assets.count(f.filename().string()) > 0; }),
                        l.folders.end());
    }
    std::sort(l.folders.begin(), l.folders.end(),
              [](const fs::path& a, const fs::path& b) { return naturalLess(a.filename().string(), b.filename().string()); });
    std::stable_sort(l.items.begin(), l.items.end(),
                     [](const DocumentItem& a, const DocumentItem& b) { return naturalLess(a.name(), b.name()); });
    return l;
}

std::vector<DocumentItem> scanRecursive(const fs::path& dir, unsigned include) {
    std::vector<DocumentItem> all;
    std::function<void(const fs::path&, int)> walk = [&](const fs::path& d, int depth) {
        Listing l = scan(d, include);
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

DocumentItem itemOf(const fs::path& file, unsigned include) {
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
        if (markHybrid(item)) {
            item.pdf.clear();  // (edited after its hybrid PDF: a document of its own)
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
        if (markHybrid(item)) {
            item.xopp.clear();  // (a .xopp edited after it: a document of its own)
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
    if (isText(file) ? (include & TextFiles) != 0 : (include & OtherFiles) != 0 && isOtherFile(file)) {
        DocumentItem item;
        item.other = file;
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
    if (isDir(folder / (name + ".assets"))) {
        return true;  // (the pictures of a .md of that name, or left behind by one)
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

namespace {
/// A free whole file name in `folder` for a text or other file: "name.ext", "name (2).ext", ...
std::string uniqueFileName(const fs::path& folder, const fs::path& file) {
    const std::string stem = file.stem().string(), ext = file.extension().string();
    if (!fileExists(folder / file.filename())) {
        return file.filename().string();
    }
    for (int i = 2;; ++i) {
        std::string n = stem + " (" + std::to_string(i) + ")" + ext;
        if (!fileExists(folder / n)) {
            return n;
        }
    }
}
bool isOtherItem(const DocumentItem& item) {
    return !item.other.empty() && item.xopp.empty() && item.pdf.empty() && item.md.empty() && item.image.empty();
}
/// The name a document gets in `folder` (free there)
std::string freeNameIn(const fs::path& folder, const DocumentItem& item) {
    return isOtherItem(item) ? uniqueFileName(folder, item.other) : uniqueName(folder, item.name());
}
}  // namespace

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
    if (isOtherItem(item) ? fileExists(folder / newName) : nameTaken(folder, newName)) {
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
    return relocate(item, folder, freeNameIn(folder, item), false);
}

namespace {
/// A folder with its subfolders; `depth` against symbolic link loops.
Result importFolder(const fs::path& dir, const fs::path& folder, int depth, unsigned include) {
    Result r = createFolder(folder, uniqueName(folder, dir.filename().string()));
    if (!r.ok) {
        return r;
    }
    std::vector<std::string> errors;
    const Listing l = scan(dir, include);
    for (const auto& item: l.items) {
        Result sub = relocate(item, r.folder, freeNameIn(r.folder, item), true);
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
        Result s = importFolder(sub, r.folder, depth + 1, include);
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

Result import(const fs::path& file, const fs::path& folder, unsigned include) {
    if (!isDir(folder)) {
        return failure("The folder does not exist.");
    }
    if (isDir(file)) {
        // Not into itself
        if (isInside(fs::weakly_canonical(folder), fs::weakly_canonical(file))) {
            return failure("A folder cannot be imported into itself.");
        }
        return importFolder(file, folder, 0, include);
    }
    const DocumentItem item = itemOf(file, include);
    if (!item.valid()) {
        return failure("\"" + file.filename().string() +
                       "\" is not a document the library shows (notes, PDFs, Markdown, images).");
    }
    Result r = relocate(item, folder, freeNameIn(folder, item), true);
    r.documents = r.ok ? 1 : 0;
    return r;
}

Result trash(const DocumentItem& item) {
    if (!item.valid()) {
        return failure("The document does not exist.");
    }
    for (const fs::path& f: filesOf(item)) {
        if (!SystemApps::instance().moveToTrash(QString::fromStdString(f.string()))) {
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
    if (!SystemApps::instance().moveToTrash(QString::fromStdString(folder.string()))) {
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

int compareNames(const QString& a, const QString& b) {
    int r = 0;
    if (QCollator* c = nameCollator()) {
        r = c->compare(a, b);
    }
    if (r == 0) {
        r = plainNaturalCompare(a, b);
    }
    return r != 0 ? r : QString::compare(a, b, Qt::CaseSensitive);
}

}  // namespace DocumentFiles

bool ShowFilter::shows(const DocumentItem& item) const {
    switch (item.kind()) {
        case DocumentItem::Kind::Notes:
            return notes;
        case DocumentItem::Kind::Pdf:
            // With notes: its .xopp, or its notes in it (a hybrid PDF; a lone one is looked into, once per version)
            return pdfs && (!onlyPdfsWithNotes || !item.xopp.empty() || item.hybrid || HybridPdf::isHybrid(item.pdf));
        case DocumentItem::Kind::Markdown:
            return markdown;
        case DocumentItem::Kind::Image:
            return images;
        case DocumentItem::Kind::Text:
            return text;
        case DocumentItem::Kind::Other:
            return other;
    }
    return false;
}

unsigned ShowFilter::include() const {
    return (text ? DocumentFiles::TextFiles : 0u) | (other ? DocumentFiles::OtherFiles : 0u);
}

}  // namespace xqt

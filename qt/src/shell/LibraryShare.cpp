#include "LibraryShare.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <regex>
#include <set>

#include <QCborArray>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QThreadPool>
#include <QUrl>

#include "audio/AudioFiles.h"
#include "control/ExportHelper.h"
#include "model/Document.h"
#include "session/DocumentSession.h"
#include "session/HybridPdf.h"
#include "session/PdfEncryption.h"
#include "session/FileIo.h"
#include "util/Util.h"

#include "InkTextStore.h"
#include "LibraryCache.h"
#include "LinkRewrite.h"
#include "DocumentCovers.h"
#include "ZipFile.h"
#include "config.h"

namespace xqt {

const QString LibraryShare::MANIFEST_PACK = QStringLiteral("share-manifest");

namespace {

QString tr(const char* text) { return QCoreApplication::translate("LibraryShare", text); }

std::string u8(const fs::path& p) {
    const auto s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}
fs::path fromU8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

fs::path canonicalOf(const fs::path& p) {
    std::error_code ec;
    const fs::path c = fs::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : c;
}

/// `inner` is `outer` or inside it.
bool within(const fs::path& inner, const fs::path& outer) {
    const fs::path rel = canonicalOf(inner).lexically_relative(canonicalOf(outer));
    return !rel.empty() && *rel.begin() != "..";
}

bool isFile(const fs::path& p) {
    std::error_code ec;
    return !p.empty() && fs::is_regular_file(p, ec);
}

std::time_t mtimeOf(const fs::path& p) {
    const QFileInfo info(QString::fromStdU16String(p.u16string()));
    return static_cast<std::time_t>(info.lastModified().toSecsSinceEpoch());
}

uint64_t sizeOf(const fs::path& p) {
    std::error_code ec;
    const auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<uint64_t>(s);
}

/// A PDF that opens only with a password (shared as it is: it cannot be read without one)
bool needsPassword(const fs::path& pdf) { return PdfEncryption::probe(pdf).isProtected(); }

/// Formats that are compressed already: stored as they are (deflating them again only costs time).
bool compressed(const fs::path& p) {
    static const std::set<std::string> known{".pdf",  ".xopp", ".xoj", ".png",  ".jpg", ".jpeg", ".webp", ".heic",
                                             ".heif", ".gif",  ".ogg", ".mp3",  ".m4a", ".opus", ".mp4",  ".zip",
                                             ".gz",   ".7z",   ".pack", ".docx", ".xlsx", ".pptx", ".odt", ".epub"};
    return known.count(lower(p.extension().string())) > 0;
}

/// "a/b/c" relative from folder "a/x" to file "a/b/c": "../b/c" (zip paths, "/" between folders)
std::string relativeFrom(const std::string& folder, const std::string& file) {
    const fs::path root("/r");
    return u8((root / fromU8(file)).lexically_normal().lexically_relative((root / fromU8(folder)).lexically_normal()));
}

std::string folderOf(const std::string& rel) {
    const auto slash = rel.rfind('/');
    return slash == std::string::npos ? std::string() : rel.substr(0, slash);
}

std::string xmlUnescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const size_t end = s.find(';', i);
        if (end == std::string::npos) {
            out += s[i];
            continue;
        }
        const std::string ent = s.substr(i + 1, end - i - 1);
        if (ent == "amp") {
            out += '&';
        } else if (ent == "lt") {
            out += '<';
        } else if (ent == "gt") {
            out += '>';
        } else if (ent == "quot") {
            out += '"';
        } else if (ent == "apos") {
            out += '\'';
        } else if (!ent.empty() && ent[0] == '#') {
            const char32_t c = static_cast<char32_t>(ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                                                             ? std::strtoul(ent.c_str() + 2, nullptr, 16)
                                                             : std::strtoul(ent.c_str() + 1, nullptr, 10));
            out += QString::fromUcs4(&c, 1).toStdString();
        } else {
            out += s.substr(i, end - i + 1);
            i = end;
            continue;
        }
        i = end;
    }
    return out;
}

std::string xmlEscape(const std::string& s) {
    std::string out;
    for (char c: s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

/// The paths a .xopp refers to (its XML, gunzipped): the PDF and the images of its page backgrounds by path, and the
/// recordings of its elements. Rewritten in a copy where they must change (never in the user's file).
struct XoppXml {
    enum class Kind { Pdf, Image, Audio };
    struct Ref {
        Kind kind;
        std::string value;  ///< unescaped
        size_t pos = 0, len = 0;  ///< of the attribute's value in the XML
    };
    std::string xml;
    bool ok = false;
    std::vector<Ref> refs;

    static XoppXml read(const fs::path& file) {
        XoppXml x;
        const std::string bytes = fileio::readFile(file);
        if (fileio::isGzip(bytes)) {
            x.xml = fileio::gunzip(bytes, x.ok);
        } else {
            x.xml = bytes;
            x.ok = !bytes.empty();
        }
        if (!x.ok) {
            return x;
        }
        auto attribute = [&](size_t from, size_t to, const char* name, size_t& pos, size_t& len) {
            const std::string key = std::string(" ") + name + "=\"";
            const size_t at = x.xml.find(key, from);
            if (at == std::string::npos || at >= to) {
                return false;
            }
            pos = at + key.size();
            const size_t end = x.xml.find('"', pos);
            if (end == std::string::npos || end > to) {
                return false;
            }
            len = end - pos;
            return true;
        };
        for (size_t at = x.xml.find("<background"); at != std::string::npos; at = x.xml.find("<background", at + 1)) {
            const size_t end = x.xml.find('>', at);
            if (end == std::string::npos) {
                break;
            }
            size_t p = 0, l = 0;
            std::string type, domain;
            if (attribute(at, end, "type", p, l)) {
                type = x.xml.substr(p, l);
            }
            if (attribute(at, end, "domain", p, l)) {
                domain = x.xml.substr(p, l);
            }
            if (domain == "absolute" && (type == "pdf" || type == "pixmap") && attribute(at, end, "filename", p, l)) {
                x.refs.push_back({type == "pdf" ? Kind::Pdf : Kind::Image, xmlUnescape(x.xml.substr(p, l)), p, l});
            }
        }
        for (size_t at = x.xml.find(" fn=\""); at != std::string::npos; at = x.xml.find(" fn=\"", at + 1)) {
            const size_t p = at + 5;
            const size_t end = x.xml.find('"', p);
            if (end == std::string::npos) {
                break;
            }
            if (end > p) {
                x.refs.push_back({Kind::Audio, xmlUnescape(x.xml.substr(p, end - p)), p, end - p});
            }
        }
        return x;
    }

    /// The XML with these references' values replaced (by index into refs).
    std::string rewritten(const std::map<size_t, std::string>& values) const {
        std::string out = xml;
        std::vector<size_t> idx;
        for (const auto& [i, v]: values) {
            idx.push_back(i);
        }
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return refs[a].pos > refs[b].pos; });
        for (size_t i: idx) {
            out.replace(refs[i].pos, refs[i].len, xmlEscape(values.at(i)));
        }
        return out;
    }
};

/// A reference as a path: relative to the .xopp's folder, or absolute.
fs::path resolve(const std::string& value, const fs::path& folder) {
    const fs::path p = fromU8(value);
    return p.is_absolute() ? p.lexically_normal() : (folder / p).lexically_normal();
}

/// The bytes of a .xopp with this XML: gzipped, as Xournal++ writes it (empty when it cannot be compressed).
std::string xoppBytes(const std::string& xml) {
    try {
        return fileio::gzip(xml);
    } catch (const std::exception&) {
        return {};
    }
}

bool writeBytes(const fs::path& file, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// Link targets of a Markdown text: (target as written, is a picture)
std::vector<std::pair<std::string, bool>> markdownLinks(const std::string& text) {
    std::vector<std::pair<std::string, bool>> out;
    static const std::regex link(R"((!?)\[[^\]\n]*\]\(\s*(<[^>\n]*>|[^)\s]+))");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), link); it != std::sregex_iterator(); ++it) {
        std::string target = (*it)[2].str();
        if (target.size() >= 2 && target.front() == '<' && target.back() == '>') {
            target = target.substr(1, target.size() - 2);
        }
        out.emplace_back(target, (*it)[1].length() > 0);
    }
    return out;
}

/// A link target that names a file (not a web address, a mail, an anchor): its path, %-escapes and the part after
/// "#" or "?" taken away; empty when it is none.
std::string linkPath(const std::string& target) {
    static const std::regex scheme(R"(^[A-Za-z][A-Za-z0-9+.\-]+:)");
    if (target.empty() || target[0] == '#' || std::regex_search(target, scheme)) {
        return {};  // (a drive letter "C:" has one letter: not a scheme)
    }
    std::string path = target.substr(0, target.find_first_of("#?"));
    return QUrl::fromPercentEncoding(QByteArray::fromStdString(path)).toStdString();
}

}  // namespace

// --- planning ------------------------------------------------------------------------------------------------------

LibraryShare::Format LibraryShare::formatNamed(const QString& name) {
    if (name == QLatin1String("xournal")) {
        return Format::Xournal;
    }
    return name == QLatin1String("pdf") ? Format::Pdf : Format::App;
}

fs::path LibraryShare::zipFor(const std::string& name) {
    return Util::getCacheSubfolder("share") / fromU8(name + ".zip");
}

LibraryShare::Plan LibraryShare::plan(const fs::path& source, const fs::path& library, const std::string& name,
                                      const fs::path& zip, const Options& options, LibraryIndex* index,
                                      std::string& error) {
    Plan p;
    std::error_code ec;
    if (source.empty() || !fs::is_directory(source, ec)) {
        error = tr("The folder cannot be read.").toStdString();
        return p;
    }
    if (!options.password.empty() && !Zip::aesAvailable()) {
        error = tr("Zips with a password cannot be written here.").toStdString();
        return p;
    }
    p.source = canonicalOf(source);
    p.library = library.empty() ? p.source : canonicalOf(library);
    p.name = name.empty() ? u8(p.source.filename()) : name;
    p.zip = zip;
    p.options = options;
    p.items = DocumentFiles::scanRecursive(p.source, DocumentFiles::AllFiles);
    p.folders = DocumentFiles::foldersRecursive(p.source);
    if (options.format == Format::App && options.readings && index) {
        for (const DocumentItem& item: p.items) {
            if (auto s = index->snapshot(item)) {
                p.cache.emplace(item.main(), std::move(*s));
            }
        }
    }
    return p;
}

LibraryShare::Survey LibraryShare::survey(const fs::path& source) {
    Survey s;
    std::set<fs::path> recordings;
    for (const DocumentItem& item: DocumentFiles::scanRecursive(source, DocumentFiles::AllFiles)) {
        ++s.documents;
        for (const fs::path& f: DocumentFiles::filesOf(item)) {
            std::error_code ec;
            if (fs::is_directory(f, ec)) {
                for (auto it = fs::recursive_directory_iterator(f, ec); !ec && it != fs::recursive_directory_iterator();
                     it.increment(ec)) {
                    if (it->is_regular_file()) {
                        ++s.files;
                        s.bytes += sizeOf(it->path());
                    }
                }
                continue;
            }
            ++s.files;
            s.bytes += sizeOf(f);
            const std::string ext = lower(f.extension().string());
            if (ext == ".xopp") {
                const XoppXml x = XoppXml::read(f);
                for (const auto& r: x.refs) {
                    if (r.kind == XoppXml::Kind::Audio) {
                        if (const fs::path a = audio::find(r.value, f); !a.empty()) {
                            recordings.insert(canonicalOf(a));
                        }
                    }
                }
            } else if (ext == ".pdf" && HybridPdf::isHybrid(f)) {
                if (const uint64_t bytes = HybridPdf::recordingBytes(f); bytes > 0) {
                    ++s.recordings;
                    s.recordingBytes += bytes;
                }
            }
        }
    }
    for (const fs::path& r: recordings) {
        ++s.recordings;
        s.recordingBytes += sizeOf(r);
    }
    return s;
}

// --- writing -------------------------------------------------------------------------------------------------------

namespace {

/// One file of the zip.
struct Out {
    std::string rel;   ///< below the zip's top folder
    fs::path source;   ///< its bytes: the user's file, or a copy in the work folder
    fs::path origin;   ///< the user's file it stands for (stamps of the cache; empty: none)
    std::time_t mtime = 0;
    bool compress = false;
};

class ShareWriter {
public:
    ShareWriter(const LibraryShare::Plan& plan, fs::path work): p(plan), work(std::move(work)) {}

    LibraryShare::Summary s;
    std::vector<Out> outs;

    std::string relOf(const fs::path& file) const { return u8(file.lexically_relative(p.source)); }

    /// A name for a new file in the zip folder `folder` (" (2)" when it is taken)
    std::string freeName(const std::string& folder, const std::string& file) {
        const fs::path f = fromU8(file);
        const std::string stem = u8(f.stem()), ext = f.extension().string();
        std::string name = file;
        for (int n = 2; taken.count(lower(join(folder, name))) && n < 10000; ++n) {
            name = stem + " (" + std::to_string(n) + ")" + ext;
        }
        return join(folder, name);
    }
    static std::string join(const std::string& folder, const std::string& name) {
        return folder.empty() ? name : folder + "/" + name;
    }

    void add(Out o) {
        if (!used.insert(o.rel).second) {
            return;  // (already in it)
        }
        taken.insert(lower(o.rel));
        if (!o.origin.empty()) {
            byOrigin[canonicalOf(o.origin)] = outs.size();
        }
        outs.push_back(std::move(o));
    }
    /// The user's file as it is
    void addAsIs(const fs::path& file) {
        add({relOf(file), file, file, mtimeOf(file), !compressed(file)});
    }
    /// A folder's files as they are (a Markdown file's pictures)
    void addFolderAsIs(const fs::path& folder) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file()) {
                addAsIs(it->path());
            } else if (it->is_directory() && fs::is_empty(it->path(), ec)) {
                emptyFolders.insert(relOf(it->path()));
            }
        }
    }
    /// A copy in the work folder standing for the user's file `origin` at `rel`
    fs::path workFile(const std::string& rel) const { return work / "files" / fromU8(rel); }

    /// A file outside the shared folder that a document needs: in "_attached/" (once per file). Its zip path.
    std::string attach(const fs::path& file, const fs::path& forDocument) {
        const fs::path c = canonicalOf(file);
        if (auto it = attachedAs.find(c); it != attachedAs.end()) {
            return it->second;
        }
        const std::string rel = freeName(LibraryShare::ATTACHED, u8(file.filename()));
        add({rel, file, file, mtimeOf(file), !compressed(file)});
        attachedAs[c] = rel;
        s.attached.push_back(rel + " (" + tr("for %1").arg(QString::fromStdString(relOf(forDocument))).toStdString() +
                             ")");
        return rel;
    }

    /// The zip path of the user's file (as is or as a copy), "" when it is not in the zip
    std::string zipPathOf(const fs::path& origin) const {
        auto it = byOrigin.find(canonicalOf(origin));
        return it == byOrigin.end() ? std::string() : outs[it->second].rel;
    }
    const Out* outOf(const fs::path& origin) const {
        auto it = byOrigin.find(canonicalOf(origin));
        return it == byOrigin.end() ? nullptr : &outs[it->second];
    }
    /// The stamp the file has once unpacked by this app (its time is set from the zip: whole seconds)
    QString stampOf(const fs::path& origin) const {
        const Out* o = outOf(origin);
        return o ? QString::number(sizeOf(o->source)) + ':' + QString::number(static_cast<qint64>(o->mtime) * 1000)
                 : QString();
    }
    QString hashOf(const Out& o) {
        auto it = hashes.find(o.source);
        if (it != hashes.end()) {
            return it->second;
        }
        return hashes[o.source] = contentHash(o.source);
    }
    QString hashOf(const fs::path& origin) {
        const Out* o = outOf(origin);
        return o ? hashOf(*o) : QString();
    }
    void knownHash(const fs::path& file, const QString& stamp, const QString& hash) {
        if (!hash.isEmpty() && fileStamp(file) == stamp) {
            hashes[file] = hash;  // (from the library's cache: not read again)
        }
    }

    // --- documents

    void xopp(const DocumentItem& item, const fs::path& file) {
        const XoppXml x = XoppXml::read(file);
        const std::string rel = relOf(file);
        if (!x.ok) {
            addAsIs(file);
            return;
        }
        const fs::path folder = file.parent_path();
        std::map<size_t, std::string> values;
        std::set<fs::path> recordings;
        for (size_t i = 0; i < x.refs.size(); ++i) {
            const auto& r = x.refs[i];
            if (r.kind == XoppXml::Kind::Audio) {
                if (const fs::path a = audio::find(r.value, file); !a.empty()) {
                    recordings.insert(a);
                }
                continue;
            }
            fs::path target = resolve(r.value, folder);
            if (!isFile(target)) {
                if (r.kind == XoppXml::Kind::Pdf && isFile(item.pdf)) {
                    target = item.pdf;  // (a lost reference: the PDF next to it, as the app repairs it)
                } else {
                    s.notes.push_back(rel + ": " + tr("its file %1 is missing").arg(QString::fromStdString(r.value))
                                                           .toStdString());
                    continue;
                }
            }
            const std::string inZip =
                    within(target, p.source) ? u8(canonicalOf(target).lexically_relative(p.source)) : attach(target, file);
            const std::string written = relativeFrom(folderOf(rel), inZip);
            if (fromU8(r.value).is_absolute() || resolve(r.value, folder) != resolve(written, folder) ||
                !isFile(resolve(r.value, folder))) {
                values[i] = written;
            }
        }
        if (values.empty()) {
            addAsIs(file);
        } else {
            const fs::path copy = workFile(rel);
            if (!writeBytes(copy, xoppBytes(x.rewritten(values)))) {
                s.failed.push_back(rel + ": " + tr("cannot be written").toStdString());
                return;
            }
            add({rel, copy, file, mtimeOf(file), false});
        }
        if (p.options.recordings) {
            // Next to it in "name.audio/", where the app finds them by name (qt/docs/features/audio.md)
            const std::string audioFolder = relOf(audio::exportFolderOf(file));
            for (const fs::path& a: recordings) {
                const std::string r = join(audioFolder, u8(a.filename()));
                if (!taken.count(lower(r))) {
                    add({r, a, {}, mtimeOf(a), false});
                }
            }
        }
    }

    void pdfWithNotes(const fs::path& pdf) {
        const std::string rel = relOf(pdf);
        if (needsPassword(pdf)) {
            s.notes.push_back(rel + ": " + tr("protected with a password, shared as it is").toStdString());
            addAsIs(pdf);
            return;
        }
        const HybridPdf::Marker m = HybridPdf::markerOf(pdf);
        if (m.history && p.options.history) {
            addAsIs(pdf);  // (with its versions: as it is)
            return;
        }
        const bool dropAudio = !p.options.recordings && HybridPdf::recordingBytes(pdf) > 0;
        if (!m.history && !dropAudio && !HybridPdf::hasEarlierRevisions(pdf)) {
            addAsIs(pdf);
            return;
        }
        // Written anew in one piece: without its versions and earlier revisions (deleted ink), as Share sends it
        const fs::path copy = workFile(rel);
        std::error_code ec;
        fs::create_directories(copy.parent_path(), ec);
        std::string error;
        if (!HybridPdf::compact(pdf, error, copy, dropAudio)) {
            s.failed.push_back(rel + ": " + error);
            return;
        }
        add({rel, copy, pdf, mtimeOf(pdf), false});
        if (m.history) {
            compactedWithoutHistory.insert(canonicalOf(pdf));
        }
    }

    void markdown(const fs::path& md) {
        const std::string rel = relOf(md);
        std::ifstream in(md, std::ios::binary);
        std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        std::vector<LinkRewrite::Change> changes;
        std::set<std::string> seen;
        for (const auto& [target, picture]: markdownLinks(text)) {
            const std::string path = linkPath(target);
            if (path.empty() || !seen.insert(target).second) {
                continue;
            }
            const fs::path file = resolve(path, md.parent_path());
            if (within(file, p.source)) {
                continue;
            }
            if (picture && isFile(file)) {
                const std::string inZip = attach(file, md);
                changes.push_back({QString::fromStdString(target),
                                   QString::fromStdString(relativeFrom(folderOf(rel), inZip)), false});
            } else {
                s.outsideLinks.push_back(rel + " → " + target);
            }
        }
        if (changes.empty() || LinkRewrite::rewriteMarkdown(text, changes) == 0) {
            addAsIs(md);
            return;
        }
        const fs::path copy = workFile(rel);
        if (!writeBytes(copy, text)) {
            s.failed.push_back(rel + ": " + tr("cannot be written").toStdString());
            return;
        }
        add({rel, copy, md, mtimeOf(md), true});
    }

    /// The links of a notes document (from the library's cache) that lead outside the folder
    void listOutsideLinks(const DocumentItem& item) {
        auto it = p.cache.find(item.main());
        if (it == p.cache.end()) {
            return;
        }
        for (const auto& v: it->second.notes.value(QStringLiteral("links")).toArray()) {
            const std::string target = v.toString().toStdString();
            const std::string path = linkPath(target);
            if (!path.empty() && !within(resolve(path, item.main().parent_path()), p.source)) {
                s.outsideLinks.push_back(relOf(item.main()) + " → " + target);
            }
        }
    }

    /// A notes document (a .xopp, a PDF with notes) loaded, for the other formats
    std::unique_ptr<Document> load(const fs::path& file) {
        auto loaded = DocumentSession::loadFile(file);
        if (!loaded.document) {
            s.failed.push_back(relOf(file) + ": " + loaded.error);
            return nullptr;
        }
        if (!loaded.missingPdf.empty()) {
            s.failed.push_back(relOf(file) + ": " +
                               tr("its PDF is missing: %1")
                                       .arg(QString::fromStdString(u8(loaded.missingPdf.filename())))
                                       .toStdString());
            return nullptr;
        }
        return std::move(loaded.document);
    }

    void forXournal(const DocumentItem& item) {
        const fs::path pdf = item.pdf;
        if (item.hybrid || (item.xopp.empty() && isFile(pdf) && HybridPdf::isHybrid(pdf))) {
            const std::string rel = relOf(pdf);
            if (needsPassword(pdf)) {
                s.notes.push_back(rel + ": " + tr("protected with a password: Xournal++ cannot open it, shared as it is")
                                                       .toStdString());
                addAsIs(pdf);
                return;
            }
            auto doc = load(pdf);
            if (!doc) {
                return;
            }
            std::string stem = u8(pdf.stem());
            if (lower(fromU8(stem).extension().string()) == ".notes") {
                stem = u8(fromU8(stem).stem());
            }
            const std::string xoppRel = freeName(folderOf(rel), stem + ".xopp");
            const fs::path xoppCopy = workFile(xoppRel), bgCopy = workFile(xoppRel + ".bg.pdf");
            std::error_code ec;
            fs::create_directories(xoppCopy.parent_path(), ec);
            const auto r = HybridPdf::exportXopp(*doc, xoppCopy, bgCopy, npos, true);
            doc.reset();
            if (!r.ok) {
                s.failed.push_back(rel + ": " + r.error);
                return;
            }
            // Its recordings: in "name.audio/" next to it, named bare (the export names them by their paths here)
            XoppXml x = XoppXml::read(xoppCopy);
            std::map<size_t, std::string> values;
            for (size_t i = 0; i < x.refs.size(); ++i) {
                if (x.refs[i].kind == XoppXml::Kind::Audio && fromU8(x.refs[i].value).is_absolute()) {
                    values[i] = u8(fromU8(x.refs[i].value).filename());
                }
            }
            if (!values.empty()) {
                writeBytes(xoppCopy, xoppBytes(x.rewritten(values)));
            }
            add({xoppRel, xoppCopy, {}, mtimeOf(pdf), false});
            if (isFile(bgCopy)) {
                add({xoppRel + ".bg.pdf", bgCopy, {}, mtimeOf(pdf), false});
            }
            const fs::path audioCopy = audio::exportFolderOf(xoppCopy);
            if (p.options.recordings && fs::is_directory(audioCopy, ec)) {
                for (auto it = fs::directory_iterator(audioCopy, ec); !ec && it != fs::directory_iterator();
                     it.increment(ec)) {
                    if (it->is_regular_file()) {
                        add({join(u8(fromU8(xoppRel).replace_extension(".audio")), u8(it->path().filename())),
                             it->path(), {}, mtimeOf(it->path()), false});
                    }
                }
            }
            return;
        }
        appFormat(item);  // (a .xopp with its PDF, a PDF, the rest: as they are)
    }

    void asPdf(const DocumentItem& item) {
        const bool notes = !item.xopp.empty() || (isFile(item.pdf) && HybridPdf::isHybrid(item.pdf));
        if (!notes) {
            appFormat(item);
            return;
        }
        const fs::path file = item.main();
        const std::string rel = relOf(file);
        if ((isFile(item.pdf) && needsPassword(item.pdf))) {
            s.notes.push_back(rel + ": " + tr("protected with a password, shared as it is").toStdString());
            addAsIs(item.pdf);
            return;
        }
        auto doc = load(file);
        if (!doc) {
            return;
        }
        std::string stem = u8(file.stem());
        if (lower(fromU8(stem).extension().string()) == ".notes") {
            stem = u8(fromU8(stem).stem());
        }
        const std::string pdfRel = freeName(folderOf(rel), stem + ".pdf");
        const fs::path copy = workFile(pdfRel);
        std::error_code ec;
        fs::create_directories(copy.parent_path(), ec);
        try {
            ExportHelper::exportPdf(doc.get(), copy, nullptr, nullptr, EXPORT_BACKGROUND_ALL, false);
        } catch (const std::exception& e) {
            s.failed.push_back(rel + ": " + e.what());
            return;
        }
        if (!isFile(copy)) {
            s.failed.push_back(rel + ": " + tr("cannot be written").toStdString());
            return;
        }
        add({pdfRel, copy, {}, mtimeOf(file), false});
    }

    void appFormat(const DocumentItem& item) {
        for (const fs::path& f: DocumentFiles::filesOf(item)) {
            std::error_code ec;
            if (fs::is_directory(f, ec)) {
                addFolderAsIs(f);
            } else if (const std::string ext = lower(f.extension().string()); ext == ".xopp" || ext == ".xoj") {
                xopp(item, f);
            } else if (ext == ".pdf" && f == item.pdf && needsPassword(f)) {
                s.notes.push_back(relOf(f) + ": " + tr("protected with a password, shared as it is").toStdString());
                addAsIs(f);
            } else if (ext == ".pdf" && f == item.pdf && HybridPdf::isHybrid(f)) {
                pdfWithNotes(f);
            } else if (f == item.md) {
                markdown(f);
            } else {
                addAsIs(f);
            }
        }
        for (const fs::path& c: item.conflicts) {
            if (isFile(c)) {
                addAsIs(c);  // (sync apps' conflict copies: the user's files too)
            }
        }
        if (item.md.empty()) {
            listOutsideLinks(item);
        }
    }

    // --- the readings (xournal-qt format)

    void readings(const DocumentItem& item) {
        auto it = p.cache.find(item.main());
        if (it == p.cache.end()) {
            return;
        }
        const LibraryIndex::Snapshot& c = it->second;
        const fs::path own = ownFileOf(item);
        const Out* mainOut = outOf(item.main());
        if (!mainOut || (!own.empty() && !outOf(own)) || (!c.pdf.empty() && !outOf(c.pdf))) {
            return;  // (not in the zip as it is cached)
        }
        knownHash(own, c.ownStamp, c.sha);
        knownHash(c.pdf, c.pdfStamp, c.pdfSha);
        const QString ownStamp = c.ownStamp.isEmpty() ? QString() : stampOf(own);
        const QString pdfStamp = c.pdf.empty() ? QString() : stampOf(c.pdf);
        const std::string folder = folderOf(mainOut->rel);
        const QString name = QString::fromStdString(u8(fromU8(mainOut->rel).filename()));
        FolderPacks& packs = byFolder[folder];

        QCborMap notes = c.notes;
        notes.insert(QStringLiteral("xopp"), ownStamp);
        notes.insert(QStringLiteral("pdfStamp"), pdfStamp);
        notes.insert(QStringLiteral("pdf"),
                     c.pdf.empty() ? QString() : QString::fromStdString(relativeFrom(folder, zipPathOf(c.pdf))));
        notes.remove(QStringLiteral("sha"));
        notes.remove(QStringLiteral("pdfSha"));
        if (!ownStamp.isEmpty()) {
            notes.insert(QStringLiteral("sha"), hashOf(own));
        }
        if (!pdfStamp.isEmpty()) {
            notes.insert(QStringLiteral("pdfSha"), hashOf(c.pdf));
        }
        if (compactedWithoutHistory.count(canonicalOf(item.main()))) {
            notes.remove(QStringLiteral("versions"));
        }
        packs.notes.insert(name, notes);
        if (p.options.pdfText && !c.pdfText.isEmpty()) {
            QCborMap text = c.pdfText;
            text.insert(QStringLiteral("stamp"), pdfStamp);
            packs.text.insert(name, text);
        }
        if (c.ink) {
            InkDoc ink = *c.ink;
            ink.stamp = ownStamp.isEmpty() ? pdfStamp : ownStamp;
            packs.ink.insert(name, InkTextStore::encode(ink));
        }
        if (auto cover = DocumentCovers::storedEntry(item);
            cover && cover->value(QStringLiteral("stamp")).toString().endsWith(QLatin1String("title=0"))) {
            const QString stamp = DocumentCovers::stampWith(
                    item, [this](const fs::path& f) { return stampOf(f); }, 0);
            packs.covers.insert(name, QCborMap{{QStringLiteral("stamp"), stamp},
                                                 {QStringLiteral("png"), cover->value(QStringLiteral("png"))}});
        }
        ++s.readings;
    }

    /// The packs written into the work folder, and the manifest; added to the zip
    void writePacks() {
        const fs::path dir = work / "packs";
        for (const auto& [folder, packs]: byFolder) {
            const fs::path cache = (folder.empty() ? dir : dir / fromU8(folder)) / DocumentFiles::META_DIR;
            Packs::write(cache, LibraryIndex::NOTES_PACK, LibraryIndex::FORMAT, packs.notes, true);
            Packs::write(cache, LibraryIndex::PDF_TEXT_PACK, LibraryIndex::FORMAT, packs.text, true);
            Packs::write(cache, InkTextStore::PACK, InkTextStore::FORMAT, packs.ink, true);
            Packs::write(cache, DocumentCovers::PACK, DocumentCovers::FORMAT, packs.covers, false);
        }
        QCborMap files;
        for (const Out& o: outs) {
            files.insert(QString::fromStdString(o.rel),
                         QCborMap{{QStringLiteral("size"), static_cast<qint64>(sizeOf(o.source))},
                                  {QStringLiteral("hash"), hashOf(o)},
                                  {QStringLiteral("mtime"), static_cast<qint64>(o.mtime)}});
        }
        const QCborMap manifest{
                {QStringLiteral("format"), LibraryShare::MANIFEST_FORMAT},
                {QStringLiteral("app"), QString::fromUtf8(PROJECT_STRING)},
                {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                {QStringLiteral("hash"), QStringLiteral("blake2b-256")},
                {QStringLiteral("readings"), p.options.readings},
                {QStringLiteral("pdfText"), p.options.pdfText},
                {QStringLiteral("history"), p.options.history},
                {QStringLiteral("recordings"), p.options.recordings},
                {QStringLiteral("files"), files}};
        Packs::write(dir / DocumentFiles::META_DIR, LibraryShare::MANIFEST_PACK, LibraryShare::MANIFEST_FORMAT,
                     QCborMap{{QStringLiteral("share"), manifest}}, true);
        std::error_code ec;
        const std::time_t now = QDateTime::currentSecsSinceEpoch();
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator();
             it.increment(ec)) {
            if (it->is_regular_file()) {
                packFiles.push_back({u8(it->path().lexically_relative(dir)), it->path(), {}, now, false});
            }
        }
    }

    std::string readme() const {
        std::string t = tr("Shared from xournal-qt (%1) on %2.")
                                .arg(QString::fromUtf8(PROJECT_STRING), QDate::currentDate().toString(Qt::ISODate))
                                .toStdString() +
                        "\n\n";
        if (p.options.format == LibraryShare::Format::Xournal) {
            t += tr("The notes are Xournal++ files (.xopp): open them in Xournal++ (xournalpp.github.io) or in "
                    "xournal-qt. A .xopp shows the PDF next to it (name.xopp.bg.pdf, or the PDF of its name). "
                    "Recordings are in the folder name.audio next to their notes: set it as Xournal++'s audio folder "
                    "(Preferences, Audio Recordings) to play them there.")
                         .toStdString() +
                 "\n\n";
        } else {
            t += tr("Every notes document is a PDF with the ink drawn into its pages: any PDF viewer shows it. Other "
                    "files are as they were.")
                         .toStdString() +
                 "\n\n";
        }
        if (!s.attached.empty()) {
            t += tr("Files from outside the folder that documents need are in the folder _attached:").toStdString() +
                 "\n";
            for (const auto& a: s.attached) {
                t += "- " + a + "\n";
            }
        }
        return t;
    }

    std::vector<Out> packFiles;
    std::set<std::string> emptyFolders;  ///< folders of the zip (also empty ones)

private:
    struct FolderPacks {
        QCborMap notes, text, ink, covers;
    };
    const LibraryShare::Plan& p;
    fs::path work;
    std::set<std::string> taken;  ///< zip paths used (lower case)
    std::set<std::string> used;   ///< zip paths used
    std::map<fs::path, size_t> byOrigin;
    std::map<fs::path, std::string> attachedAs;
    std::map<fs::path, QString> hashes;
    std::set<fs::path> compactedWithoutHistory;
    std::map<std::string, FolderPacks> byFolder;
};

/// A file in the "name.audio" folder next to "name.xopp" (where recordings are found by name,
/// qt/docs/features/audio.md)
bool isRecordingOfNotes(const fs::path& file) {
    const fs::path folder = file.parent_path();
    if (lower(folder.extension().string()) != ".audio") {
        return false;
    }
    fs::path notes = folder;
    notes.replace_extension(".xopp");
    return isFile(notes);
}

/// Remove zips in the share folder older than a day (the share folder's owner and limit)
void pruneShareFolder(const fs::path& keep) {
    const fs::path dir = keep.parent_path();
    std::error_code ec;
    const auto limit = fs::file_time_type::clock::now() - std::chrono::hours(LibraryShare::KEEP_HOURS);
    for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code tec;
        if (it->is_regular_file() && lower(it->path().extension().string()) == ".zip" && it->path() != keep &&
            it->last_write_time(tec) < limit && !tec) {
            fs::remove(it->path(), tec);
        }
    }
}

}  // namespace

LibraryShare::Summary LibraryShare::run(const Plan& plan, const std::atomic<bool>& cancel,
                                        const std::function<void(int, int, const std::string&)>& progress) {
    std::error_code ec;
    const fs::path work = Util::getCacheSubfolder("share-work") /
                          (std::to_string(Util::getPid()) + "-" +
                           std::to_string(QDateTime::currentMSecsSinceEpoch()));
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);
    ShareWriter w(plan, work);
    w.s.zip = plan.zip;
    const int prepare = static_cast<int>(plan.items.size());
    const int total = prepare + 100;
    int done = 0;
    auto step = [&](const std::string& current) {
        if (progress) {
            progress(++done, total, current);
        }
    };
    for (const fs::path& f: plan.folders) {
        w.emptyFolders.insert(w.relOf(f));  // (every folder, so the structure comes along also where nothing is shared)
    }
    for (const DocumentItem& item: plan.items) {
        if (cancel) {
            w.s.cancelled = true;
            break;
        }
        const std::string rel = w.relOf(item.main());
        if (!plan.options.recordings && isRecordingOfNotes(item.main())) {
            step(rel);
            continue;  // (recordings left out: also those already next to their notes)
        }
        switch (plan.options.format) {
            case Format::App: w.appFormat(item); break;
            case Format::Xournal: w.forXournal(item); break;
            case Format::Pdf: w.asPdf(item); break;
        }
        ++w.s.documents;
        step(rel);
    }
    if (!w.s.cancelled && plan.options.format == Format::App) {
        if (plan.options.readings) {
            for (const DocumentItem& item: plan.items) {
                w.readings(item);
            }
        }
        w.writePacks();
    }
    if (w.s.cancelled) {
        fs::remove_all(work, ec);
        return w.s;
    }
    // The zip: one top folder with the shared folder's name
    fs::create_directories(plan.zip.parent_path(), ec);
    pruneShareFolder(plan.zip);
    Zip::Writer zip(plan.zip);
    zip.setPassword(plan.options.password);
    const std::string top = plan.name + "/";
    const std::time_t now = QDateTime::currentSecsSinceEpoch();
    zip.addFolder(top, mtimeOf(plan.source));
    for (const std::string& f: w.emptyFolders) {
        zip.addFolder(top + f, now);
    }
    for (const Out& o: w.outs) {
        zip.addFile(top + o.rel, o.source, o.mtime, o.compress);
    }
    for (const Out& o: w.packFiles) {
        zip.addFile(top + o.rel, o.source, o.mtime, false);
    }
    if (plan.options.format != Format::App) {
        zip.addData(top + "README.txt", w.readme(), now);
    }
    const bool ok = zip.close([&](double f) {
        if (f >= 0 && progress) {
            progress(prepare + static_cast<int>(f * 100), total, tr("Writing the zip…").toStdString());
        }
        return !cancel.load();
    });
    if (!ok) {
        if (cancel) {
            w.s.cancelled = true;
        } else {
            w.s.error = zip.error();
        }
    } else {
        w.s.bytes = sizeOf(plan.zip);
    }
    fs::remove_all(work, ec);
    return w.s;
}

// --- in the background -----------------------------------------------------------------------------------------------

struct LibraryShare::State {
    std::atomic<bool> cancel{false};
};

LibraryShare::LibraryShare(QObject* parent): QObject(parent) {}

LibraryShare::~LibraryShare() { cancel(); }

bool LibraryShare::passwordAvailable() const { return Zip::aesAvailable(); }

void LibraryShare::cancel() {
    if (state) {
        state->cancel = true;
    }
}

void LibraryShare::startSurvey(const fs::path& source) {
    const quint64 gen = ++surveyGeneration;
    surveyed.clear();
    Q_EMIT surveyChanged();
    QPointer<LibraryShare> self(this);
    QThreadPool::globalInstance()->start([self, gen, source] {
        QThread::currentThread()->setPriority(QThread::IdlePriority);
        const Survey s = survey(source);
        QThread::currentThread()->setPriority(QThread::NormalPriority);
        QVariantMap map{{"documents", s.documents},
                        {"files", s.files},
                        {"bytes", static_cast<double>(s.bytes)},
                        {"recordings", s.recordings},
                        {"recordingBytes", static_cast<double>(s.recordingBytes)}};
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, gen, map] {
            if (self && self->surveyGeneration == gen) {
                self->surveyed = map;
                Q_EMIT self->surveyChanged();
            }
        });
    });
}

bool LibraryShare::start(const fs::path& source, const fs::path& library, const std::string& name,
                         const Options& options, LibraryIndex* index, std::string& error) {
    if (state) {
        error = tr("A zip is being written already.").toStdString();
        return false;
    }
    error.clear();
    Plan p = plan(source, library, name, zipFor(name.empty() ? u8(source.filename()) : name), options, index, error);
    if (!error.empty()) {
        return false;
    }
    state = std::make_shared<State>();
    doneCount = 0;
    totalCount = static_cast<int>(p.items.size()) + 100;
    currentName.clear();
    Q_EMIT runningChanged();
    Q_EMIT progressChanged();
    QPointer<LibraryShare> self(this);
    std::shared_ptr<State> st = state;
    QThreadPool::globalInstance()->start([self, st, p = std::move(p)] {
        QThread::currentThread()->setPriority(QThread::IdlePriority);  // (not for right now)
        const Summary s = run(p, st->cancel, [self, st](int done, int total, const std::string& current) {
            const QString name = QString::fromStdString(current);
            QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, done, total, name] {
                if (self && self->state == st) {
                    self->doneCount = done;
                    self->totalCount = total;
                    self->currentName = name;
                    Q_EMIT self->progressChanged();
                }
            });
        });
        QThread::currentThread()->setPriority(QThread::NormalPriority);
        auto list = [](const std::vector<std::string>& v) {
            QStringList out;
            for (const auto& x: v) {
                out << QString::fromStdString(x);
            }
            return out;
        };
        QVariantMap map{{"zip", QString::fromStdString(u8(s.zip))},
                        {"name", QString::fromStdString(u8(s.zip.filename()))},
                        {"bytes", static_cast<double>(s.bytes)},
                        {"documents", s.documents},
                        {"readings", s.readings},
                        {"attached", list(s.attached)},
                        {"outsideLinks", list(s.outsideLinks)},
                        {"notes", list(s.notes)},
                        {"failed", list(s.failed)},
                        {"cancelled", s.cancelled},
                        {"error", QString::fromStdString(s.error)}};
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, st, map] {
            if (!self || self->state != st) {
                return;
            }
            self->state.reset();
            Q_EMIT self->runningChanged();
            Q_EMIT self->finished(map);
        });
    });
    return true;
}

}  // namespace xqt

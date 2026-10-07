/*
 * xournal-qt: reading a PDF with notes (HybridPdf.h, HybridInternal.h): its marker (remembered per file version),
 * opening it as its embedded document over a clean copy, compacting it, the copy that keeps some of our annotations.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>

#include <glib.h>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>

#include "control/xojfile/LoadHandler.h"
#include "model/Document.h"
#include "util/InputStream.h"

#include "audio/AudioFiles.h"
#include "DocumentImages.h"
#include "MergedPdf.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

/// Gunzipped (also data that is not compressed: as it is).
std::string gunzipped(const std::string& data) {
    if (!fileio::isGzip(data)) {
        return data;
    }
    bool ok = false;
    std::string out = fileio::gunzip(data, ok);
    if (!ok) {
        throw std::runtime_error("Could not read the Xournal data");
    }
    return out;
}

/// The .xopp's XML from memory (LoadHandler).
class StringInputStream final: public xoj::util::InputStream {
public:
    explicit StringInputStream(std::string data): data(std::move(data)) {}
    ~StringInputStream() override { std::fill(data.begin(), data.end(), '\0'); }
    int read(char* buffer, unsigned int len) noexcept override {
        const size_t n = std::min<size_t>(len, data.size() - at);
        std::memcpy(buffer, data.data() + at, n);
        at += n;
        return static_cast<int>(n);
    }
    void close() override {}

private:
    std::string data;
    size_t at = 0;
};

struct Kind {
    int kind = 0;            ///< 0: no marker, 1: a hybrid PDF, 2: an archive PDF
    bool revisions = false;  ///< it has incremental updates
    bool markdown = false;   ///< its marker lists a "name.md" for other apps (a PDF text document)
    bool history = false;    ///< it keeps its versions (PdfHistory.h)
    int versions = 0;
};
std::atomic<int> kindReads{0};
/// Remembered by path, size and time. qpdf reads the trailer and the cross-reference table, then only the objects
/// asked for (the catalog, the marker): not the pages or the embedded files.
Kind kindOf(const fs::path& pdf) {
    static std::mutex m;
    static std::unordered_map<std::string, Kind> known;
    const std::string key = pdf.string() + "|" + stampOf(pdf);
    {
        std::lock_guard lock(m);
        if (auto it = known.find(key); it != known.end()) {
            return it->second;
        }
    }
    Kind kind;
    ++kindReads;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
        if (marker.isDictionary()) {
            QPDFObjectHandle archive = marker.getKey("/Archive");
            kind.kind = archive.isBool() && archive.getBoolValue() ? 2 : 1;
            // (a text document's "name.md" is listed there with the other files it carries: TextDocument.h)
            QPDFObjectHandle files = marker.getKey("/Files");
            for (int i = 0; files.isArray() && i < files.getArrayNItems() && !kind.markdown; ++i) {
                QPDFObjectHandle v = files.getArrayItem(i);
                const std::string name = v.isString() ? v.getUTF8Value() : std::string();
                constexpr std::string_view md = ".md";
                kind.markdown = name.size() > md.size() && name.find('/') == std::string::npos &&
                                name.compare(name.size() - md.size(), md.size(), md) == 0;
            }
            if (QPDFObjectHandle h = marker.getKey("/History"); h.isDictionary()) {
                kind.history = h.getKey("/On").isBool() && h.getKey("/On").getBoolValue();
                kind.versions = h.getKey("/Count").isInteger() ? static_cast<int>(h.getKey("/Count").getIntValue()) : 0;
            }
        }
        kind.revisions = q.getTrailer().hasKey("/Prev");
    } catch (const std::exception& e) {
        if (PdfEncryption::isPasswordError(e)) {
            return Kind();  // (a protected PDF: not remembered, it is read once its password is known)
        }
        kind = Kind();
    }
    std::lock_guard lock(m);
    if (known.size() > 4096) {
        known.clear();
    }
    known[key] = kind;
    return kind;
}

/// Remove the recordings a PDF with notes carries: their attachments (also from an archive's /AF) and /Audio.
void dropRecordings(QPDF& q) {
    QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    const auto list = audioListOf(marker);
    if (list.empty()) {
        return;
    }
    QPDFEmbeddedFileDocumentHelper efdh(q);
    std::set<std::string> names;
    for (const auto& [name, source]: list) {
        efdh.removeEmbeddedFile(name);
        names.insert(name);
    }
    if (QPDFObjectHandle af = q.getRoot().getKey("/AF"); af.isArray()) {
        for (int i = af.getArrayNItems() - 1; i >= 0; --i) {
            QPDFObjectHandle spec = af.getArrayItem(i);
            for (const char* key: {"/UF", "/F"}) {
                if (QPDFObjectHandle n = spec.isDictionary() ? spec.getKey(key) : QPDFObjectHandle::newNull();
                    n.isString() && names.count(n.getUTF8Value())) {
                    af.eraseItem(i);
                    break;
                }
            }
        }
    }
    marker.removeKey("/Audio");
}

/// The picture attachments of a text document (listed in the marker's /Files with a folder in their name,
/// "name.assets/…"; qt/docs/md-images.md) written into `dir`/pictures under their names. The folder is made also when
/// there are none (the cache entry has its pictures then).
void extractPictures(QPDF& q, QPDFObjectHandle marker, const fs::path& dir) {
    const fs::path pictures = dir / PICTURES_NAME;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path tmp = fileio::tempNameFor(pictures);
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    QPDFObjectHandle files = marker.getKey("/Files");
    QPDFEmbeddedFileDocumentHelper efdh(q);
    for (int i = 0; files.isArray() && i < files.getArrayNItems(); ++i) {
        QPDFObjectHandle v = files.getArrayItem(i);
        const std::string name = v.isString() ? v.getUTF8Value() : std::string();
        const fs::path target = DocumentImages::below(tmp, name);
        if (name.find('/') == std::string::npos || target.empty()) {
            continue;
        }
        auto spec = efdh.getEmbeddedFile(v.getStringValue());
        if (!spec) {
            spec = efdh.getEmbeddedFile(name);
        }
        if (!spec) {
            continue;
        }
        auto buffer = spec->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
        fs::create_directories(target.parent_path(), ec);
        writeFile(target, std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()));
    }
    fs::remove_all(pictures, ec);
    fs::rename(tmp, pictures, ec);
}

/// The recordings a PDF carries (the marker's /Audio) written into `dir`/audio under their names in the document
/// (qt/docs/audio.md). The folder is made also when there are none.
void extractAudio(QPDF& q, QPDFObjectHandle marker, const fs::path& dir) {
    const fs::path folder = dir / AUDIO_NAME;
    std::error_code ec;
    const fs::path tmp = fileio::tempNameFor(folder);
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    QPDFEmbeddedFileDocumentHelper efdh(q);
    for (const auto& [name, source]: audioListOf(marker)) {
        const std::u8string bare = fs::path(std::u8string(source.begin(), source.end())).filename().u8string();
        auto spec = efdh.getEmbeddedFile(name);
        if (bare.empty() || !spec) {
            continue;
        }
        auto buffer = spec->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
        writeFile(tmp / fs::path(bare),
                  std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize()));
    }
    fs::remove_all(folder, ec);
    fs::rename(tmp, folder, ec);
}

}  // namespace

bool isHybrid(const fs::path& pdf) { return kindOf(pdf).kind != 0; }

bool isArchive(const fs::path& pdf) { return kindOf(pdf).kind == 2; }

bool hasEarlierRevisions(const fs::path& pdf) { return kindOf(pdf).revisions; }

Marker markerOf(const fs::path& pdf) {
    const Kind k = kindOf(pdf);
    return {k.kind != 0, k.kind == 2, k.kind != 0 && k.markdown, k.kind != 0 && k.history, k.versions};
}

int markerReads() { return kindReads.load(); }

void forgetHistory(QPDF& q) {
    if (QPDFObjectHandle marker = q.getRoot().getKey(MARKER); marker.isDictionary()) {
        for (const char* key: {"/Base", "/Updates", "/History", "/Versions"}) {
            if (marker.hasKey(key)) {
                marker.removeKey(key);
            }
        }
    }
}

bool compact(const fs::path& pdf, std::string& error, const fs::path& to) { return compact(pdf, error, to, false); }

uint64_t recordingBytes(const fs::path& pdf) {
    try {
        if (!isHybrid(pdf) || PdfEncryption::probe(pdf).isProtected()) {
            return 0;
        }
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        const auto list = audioListOf(q.getRoot().getKey(MARKER));
        QPDFEmbeddedFileDocumentHelper efdh(q);
        uint64_t bytes = 0;
        for (const auto& [name, source]: list) {
            if (auto spec = efdh.getEmbeddedFile(name)) {
                QPDFObjectHandle stream = spec->getEmbeddedFileStream();
                const size_t size = QPDFEFStreamObjectHelper(stream).getSize();
                QPDFObjectHandle length = stream.isStream() ? stream.getDict().getKey("/Length") : stream;
                bytes += size > 0 ? size : (length.isInteger() ? length.getUIntValue() : 0);
            }
        }
        return bytes;
    } catch (const std::exception&) {
        return 0;
    }
}

bool compact(const fs::path& pdf, std::string& error, const fs::path& to, bool withoutRecordings) {
    const fileio::FileWriteLock lock(to.empty() ? pdf : to);
    try {
        const bool archive = isArchive(pdf);
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        forgetHistory(q);
        if (withoutRecordings) {
            dropRecordings(q);
        }
        ArchiveWrite how;
        how.on = archive;
        writePdfTo(q, to.empty() ? pdf : to, how);  // (a protected PDF stays encrypted, the same password)
        if (!to.empty()) {
            PdfEncryption::derive(to, pdf);
        }
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

Revision revisionOf(const fs::path& cleanCopy, const fs::path& pdf) {
    Revision rev;
    try {
        const std::string stamp = stampOf(pdf);
        const fs::path dir = cleanCopy.parent_path();
        std::error_code ec;
        if (stamp.empty() || cleanCopy.filename() != CLEAN_NAME ||
            dir.lexically_normal() != entryOf(pdf, stamp).lexically_normal() || !fs::exists(dir / CHECK_NAME, ec) ||
            !fileio::readFile(dir / CHECK_NAME).empty() || !fs::exists(dir / PAGES_NAME, ec)) {
            return rev;  // (another version of the file, or edited in another app: written in full)
        }
        std::istringstream in(fileio::readFile(dir / PAGES_NAME));
        int obj = 0, gen = 0;
        while (in >> obj >> gen) {
            rev.pages.emplace_back(obj, gen);
        }
        rev.stamp = stamp;
    } catch (const std::exception&) {
        rev = Revision();
    }
    return rev;
}

fs::path xoppExportOf(const fs::path& pdf) {
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
        if (!marker.isDictionary()) {
            return {};
        }
        QPDFObjectHandle name = marker.getKey("/XoppExport");
        if (!name.isString() || name.getUTF8Value().empty()) {
            return {};
        }
        const std::string utf8 = name.getUTF8Value();
        const fs::path p(std::u8string(utf8.begin(), utf8.end()));
        return p.is_absolute() ? p : (pdf.parent_path() / p).lexically_normal();
    } catch (const std::exception&) {
        return {};
    }
}

Opened open(const fs::path& pdf) {
    Opened o;
    try {
        const std::string stamp = stampOf(pdf);
        if (stamp.empty()) {
            o.error = "The file cannot be read.";
            return o;
        }
        const fs::path dir = entryOf(pdf, stamp);
        const fs::path base = dir / CLEAN_NAME, xopp = dir / DATA_NAME, check = dir / CHECK_NAME;
        // A protected PDF (PdfEncryption.h): its Xournal data is read into memory on every opening, never written into
        // the cache; the clean copy there is encrypted like the file (qt/docs/hybrid-pdf.md, "Encrypted PDFs")
        const bool secret = PdfEncryption::isProtected(pdf);
        std::vector<std::pair<std::string, std::string>> files;  // (the .xopp and its attached files, by their names)
        std::error_code ec;
        if (!fs::exists(check, ec) || secret) {
            QPDF q;
            q.setSuppressWarnings(true);
            PdfEncryption::openQpdf(q, pdf);
            QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
            if (!marker.isDictionary()) {
                o.error = "The PDF has no Xournal data.";
                return o;
            }
            if (QPDFObjectHandle v = marker.getKey("/Version"); v.isInteger() && v.getIntValue() > ARCHIVE_FORMAT_VERSION) {
                o.error = "The Xournal data of this PDF was written by a newer version of xournal-qt.";
                return o;
            }
            QPDFObjectHandle dataName = marker.getKey("/Data");
            const std::string name = dataName.isString() ? dataName.getUTF8Value() : std::string(DATA_NAME);
            QPDFEmbeddedFileDocumentHelper efdh(q);
            auto spec = efdh.getEmbeddedFile(name);
            if (!spec) {
                o.error = "The Xournal data is missing from this PDF (another app may have removed it).";
                return o;
            }
            fs::create_directories(dir, ec);
            // The embedded files first (strip() removes them)
            for (const auto& [n, s]: efdh.getEmbeddedFiles()) {
                if (n == name || n.rfind(name + ".", 0) == 0) {
                    auto buffer = s->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
                    files.emplace_back(n == name ? std::string(DATA_NAME) : std::string(DATA_NAME) + n.substr(name.size()),
                                       std::string(reinterpret_cast<const char*>(buffer->getBuffer()),
                                                   buffer->getSize()));
                }
            }
            // (a protected PDF: its pictures and recordings are taken out while it is open, the Markdown renderer and
            // the player read files; DocumentSession removes them when it is closed)
            extractPictures(q, marker, dir);  // (before strip(): it removes them)
            extractAudio(q, marker, dir);
            if (secret) {
                markUnpacked(dir);  // (removed when it is closed, or at a start after a crash)
            }
            if (!fs::exists(check, ec)) {
                std::vector<QPDFObjectHandle> pages;  // (the clean copy's page k is this page of the file)
                for (auto& p: QPDFPageDocumentHelper(q).getAllPages()) {
                    pages.push_back(p.getObjectHandle());
                }
                writeFile(dir / PAGES_NAME, pagesText(pages));
                const std::vector<std::string> changed = strip(q);
                MergedPdf::mark(q, MergedPdf::Kind::Own);  // ("Save as" .xopp puts it next to the .xopp)
                writePdfTo(q, base, {}, fileio::Sync::None);  // (encrypted as the file is; a cache entry)
                if (!secret) {
                    for (const auto& [n, data]: files) {
                        const fs::path tmp = fileio::tempNameFor(dir / n);
                        if (!writeFile(tmp, data)) {
                            throw std::runtime_error("Could not write into the cache: " + (dir / n).string());
                        }
                        fs::rename(tmp, dir / n, ec);
                    }
                }
                std::string list;
                for (const auto& c: changed) {
                    list += c + "\n";
                }
                const fs::path tmp = fileio::tempNameFor(check);
                writeFile(tmp, list);
                fs::rename(tmp, check, ec);  // last: the entry is complete
            } else {
                touch(base);
            }
        } else {
            touch(base);
            if (!fs::exists(dir / PICTURES_NAME, ec)) {  // (an entry made before pictures were carried)
                QPDF q;
                q.setSuppressWarnings(true);
                PdfEncryption::openQpdf(q, pdf);
                extractPictures(q, q.getRoot().getKey(MARKER), dir);
            }
            if (!fs::exists(dir / AUDIO_NAME, ec)) {  // (an entry made before recordings were carried)
                QPDF q;
                q.setSuppressWarnings(true);
                PdfEncryption::openQpdf(q, pdf);
                extractAudio(q, q.getRoot().getKey(MARKER), dir);
            }
        }
        PdfEncryption::derive(base, pdf);  // (the clean copy opens with the file's password)
        o.pictures = dir / PICTURES_NAME;
        o.audio = dir / AUDIO_NAME;
        audio::setExtractedFolder(pdf, o.audio);
        {
            std::istringstream in(fileio::readFile(check));
            for (std::string line; std::getline(in, line);) {
                if (!line.empty()) {
                    o.changed.push_back(line);
                }
            }
        }
        LoadHandler handler(&o.warnings);
        if (secret) {
            std::string data;
            for (auto& [n, d]: files) {
                if (n == DATA_NAME) {
                    data = gunzipped(d);
                }
            }
            auto attached = [&files](const fs::path& name) -> std::unique_ptr<std::string> {
                const std::string wanted = std::string(DATA_NAME) + "." + name.string();
                for (const auto& [n, d]: files) {
                    if (n == wanted) {
                        return std::make_unique<std::string>(d);
                    }
                }
                return nullptr;
            };
            o.document = handler.loadDocument(std::make_unique<StringInputStream>(std::move(data)), xopp, attached);
        } else {
            o.document = handler.loadDocument(xopp);
        }
        if (!o.document) {
            o.error = "The Xournal data of this PDF cannot be read.";
            return o;
        }
        {  // (also without PDF pages: the annotations of other apps on its pages are kept from it)
            o.document->setPdfPassword(PdfEncryption::passwordOf(base));
            if (!o.document->readPdf(base, /*initPages=*/false, /*attachToDocument=*/false)) {
                o.error = "The pages of this PDF cannot be read: " + o.document->getLastErrorMsg();
                o.document.reset();
                return o;
            }
        }
        o.document->setFilepath(pdf);
        o.base = base;
        prune(dir);
    } catch (const std::exception& e) {
        o.document.reset();
        o.error = e.what();
    }
    return o;
}

fs::path importCopy(const fs::path& pdf, const std::vector<std::string>& keep, std::string& error) {
    try {
        const std::string stamp = stampOf(pdf);
        const fs::path dir = entryOf(pdf, stamp);
        std::error_code ec;
        fs::create_directories(dir, ec);
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, pdf);
        strip(q, std::set<std::string>(keep.begin(), keep.end()));
        MergedPdf::mark(q, MergedPdf::Kind::Own);
        const std::string key = fileio::hex16(fileio::fnv1a(stamp + std::to_string(keep.size())));
        const fs::path target = dir / ("imported-" + key + ".pdf");
        writePdfTo(q, target, {}, fileio::Sync::None);  // (a cache entry)
        PdfEncryption::derive(target, pdf);
        return target;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return {};
}

}  // namespace xqt::HybridPdf

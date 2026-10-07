/*
 * xournal-qt: what a PDF with notes carries besides its pages, written the same way by the full write and the
 * incremental update (HybridInternal.h, ObjectSink): the marker, the document information, the text layer of the
 * handwriting and the embedded files (the .xopp, the files next to it, the recordings, the files for other apps).
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <map>
#include <set>
#include <stdexcept>

#include <QCryptographicHash>
#include <QFile>
#include <qpdf/DLL.h>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>

#include "ArchivePdf.h"
#include "config.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

/// The font of the text layer, made through `sink`.
QPDFObjectHandle makeInkFont(ObjectSink& sink) {
    auto name = QPDFObjectHandle::newName("/XqtGlyphless");
    QPDFObjectHandle file = sink.addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::glyphlessFont());
    QPDFObjectHandle descriptor = QPDFObjectHandle::newDictionary();
    descriptor.replaceKey("/Type", QPDFObjectHandle::newName("/FontDescriptor"));
    descriptor.replaceKey("/FontName", name);
    descriptor.replaceKey("/Flags", QPDFObjectHandle::newInteger(4));  // symbolic
    descriptor.replaceKey("/FontBBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(
                                               0, InkTextLayer::DESCENT, InkTextLayer::ADVANCE, InkTextLayer::ASCENT)));
    descriptor.replaceKey("/ItalicAngle", QPDFObjectHandle::newInteger(0));
    descriptor.replaceKey("/Ascent", QPDFObjectHandle::newInteger(InkTextLayer::ASCENT));
    descriptor.replaceKey("/Descent", QPDFObjectHandle::newInteger(InkTextLayer::DESCENT));
    descriptor.replaceKey("/CapHeight", QPDFObjectHandle::newInteger(InkTextLayer::ASCENT));
    descriptor.replaceKey("/StemV", QPDFObjectHandle::newInteger(80));
    descriptor.replaceKey("/FontFile2", file);
    QPDFObjectHandle info = QPDFObjectHandle::newDictionary();
    info.replaceKey("/Registry", QPDFObjectHandle::newString("Adobe"));
    info.replaceKey("/Ordering", QPDFObjectHandle::newString("Identity"));
    info.replaceKey("/Supplement", QPDFObjectHandle::newInteger(0));
    QPDFObjectHandle cid = QPDFObjectHandle::newDictionary();
    cid.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    cid.replaceKey("/Subtype", QPDFObjectHandle::newName("/CIDFontType2"));
    cid.replaceKey("/BaseFont", name);
    cid.replaceKey("/CIDSystemInfo", info);
    cid.replaceKey("/FontDescriptor", sink.add(descriptor));
    cid.replaceKey("/DW", QPDFObjectHandle::newInteger(InkTextLayer::ADVANCE));
    cid.replaceKey("/CIDToGIDMap", sink.addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::cidToGidMap()));
    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    font.replaceKey("/BaseFont", name);
    font.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    font.replaceKey("/DescendantFonts", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{sink.add(cid)}));
    font.replaceKey("/ToUnicode", sink.addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::toUnicode()));
    return sink.add(font);
}

/// The sig of a page's text layer: its words and the page's size (where it goes on the base page is not in it: a page
/// that moves or gets another space for notes is written again anyway).
std::string inkSigOf(const std::vector<InkTextLayer::Word>& words, double w, double h) {
    if (words.empty()) {
        return {};
    }
    return InkTextLayer::sigOf(InkTextLayer::contentOf(words, h, "") + std::to_string(std::lround(w * 10)) + 'x' +
                               std::to_string(std::lround(h * 10)));
}

/// The text layer's stream first in the page's content (the graphics state is the page's own there), its font in the
/// page's resources (copied: they may be shared).
void putInkText(QPDFObjectHandle page, QPDFObjectHandle stream, QPDFObjectHandle font) {
    QPDFObjectHandle old = page.getKey("/Contents");
    QPDFObjectHandle contents = QPDFObjectHandle::newArray();
    contents.appendItem(stream);
    if (old.isArray()) {
        for (int i = 0; i < old.getArrayNItems(); ++i) {
            contents.appendItem(old.getArrayItem(i));
        }
    } else if (old.isStream() || old.isDictionary()) {
        contents.appendItem(old);
    }
    page.replaceKey("/Contents", contents);
    QPDFObjectHandle res = page.getKey("/Resources");
    res = res.isDictionary() ? res.shallowCopy() : QPDFObjectHandle::newDictionary();
    QPDFObjectHandle fonts = res.getKey("/Font");
    fonts = fonts.isDictionary() ? fonts.shallowCopy() : QPDFObjectHandle::newDictionary();
    fonts.replaceKey(InkTextLayer::FONT_RESOURCE, font);
    res.replaceKey("/Font", fonts);
    page.replaceKey("/Resources", res);
}

/// Size and MD5 of a recording's file (its embedded stream's /Params), read in pieces.
bool measureFile(const fs::path& file, long long& size, std::string& md5) {
    QFile f(QString::fromStdU16String(file.u16string()));
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!hash.addData(&f)) {
        return false;
    }
    size = f.size();
    md5 = hash.result().toStdString();
    return true;
}

/// Gives a file to qpdf in pieces, when the stream is written.
std::function<void(Pipeline*)> fileProvider(const fs::path& file) {
    return [file](Pipeline* p) {
        QFile f(QString::fromStdU16String(file.u16string()));
        if (f.open(QIODevice::ReadOnly)) {
            QByteArray chunk;
            while (!(chunk = f.read(1 << 16)).isEmpty()) {
                p->write(reinterpret_cast<const unsigned char*>(chunk.constData()), static_cast<size_t>(chunk.size()));
            }
        }
        p->finish();
    };
}

/// A file a PDF with notes carries, as its embedded stream and file specification are written.
struct FileEntry {
    std::string name;
    std::string description;            ///< its /Desc ("": none)
    std::string mime;                   ///< its stream's /Subtype ("": image/png)
    const std::string* data = nullptr;  ///< its bytes, or (nullptr)
    fs::path file;                      ///< a file read when the PDF is written (a recording)
    std::string relationship;           ///< an archive PDF's associated file: its /AFRelationship ("": none)
    bool modDate = false;               ///< its /Params has a /ModDate (a recording; every file of an archive PDF)
};

/// The embedded stream of a file (as QPDFEFStreamObjectHelper makes it: its type, size and MD5 checksum). Null when
/// its file cannot be read.
QPDFObjectHandle embeddedStream(ObjectSink& sink, const FileEntry& f) {
    long long size = 0;
    std::string md5;
    if (f.data) {
        size = static_cast<long long>(f.data->size());
        md5 = fileio::md5Of(*f.data);
    } else if (!measureFile(f.file, size, md5)) {
        return QPDFObjectHandle::newNull();
    }
    QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/" + (f.mime.empty() ? std::string("image/png") : f.mime)));
    QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
    params.replaceKey("/Size", QPDFObjectHandle::newInteger(size));
    params.replaceKey("/CheckSum", QPDFObjectHandle::newString(md5));
    if (f.modDate) {
        params.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
    }
    dict.replaceKey("/Params", params);
    return f.data ? sink.addStream(dict, *f.data) : sink.addFileStream(dict, f.file);
}

/// A new file specification of the file in the name tree of the embedded files (its nodes touched already). False
/// when its file cannot be read.
bool addFile(ObjectSink& sink, QPDFEmbeddedFileDocumentHelper& efdh, const FileEntry& f) {
    QPDFObjectHandle stream = embeddedStream(sink, f);
    if (stream.isNull()) {
        return false;
    }
    QPDFObjectHandle ef = QPDFObjectHandle::newDictionary();
    ef.replaceKey("/F", stream);
    ef.replaceKey("/UF", stream);
    QPDFObjectHandle spec = QPDFObjectHandle::newDictionary();
    spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
    spec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(f.name));
    spec.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(f.name));
    spec.replaceKey("/EF", ef);
    if (!f.description.empty()) {
        spec.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(f.description));
    }
    if (!f.relationship.empty()) {
        spec.replaceKey("/AFRelationship", QPDFObjectHandle::newName(f.relationship));
    }
    efdh.replaceEmbeddedFile(f.name, QPDFFileSpecObjectHelper(sink.add(spec)));
    return true;
}

/// The file's data as a new stream in the file specification the PDF has under its name. `keepSame`: one whose data
/// did not change (by its size and checksum, as we wrote it, and its type when the file gives one) stays as it is.
/// False: the PDF has no such file.
bool replaceData(ObjectSink& sink, QPDFEmbeddedFileDocumentHelper& efdh, const FileEntry& f, bool keepSame) {
    auto spec = efdh.getEmbeddedFile(f.name);
    QPDFObjectHandle s = spec ? spec->getObjectHandle() : QPDFObjectHandle::newNull();
    QPDFObjectHandle ef = s.isDictionary() ? s.getKey("/EF") : QPDFObjectHandle::newNull();
    if (!ef.isDictionary()) {
        return false;
    }
    if (keepSame) {
        QPDFObjectHandle had = ef.getKey("/F");
        QPDFObjectHandle params = had.isStream() ? had.getDict().getKey("/Params") : QPDFObjectHandle::newNull();
        QPDFObjectHandle size = params.isDictionary() ? params.getKey("/Size") : QPDFObjectHandle::newNull();
        QPDFObjectHandle sum = params.isDictionary() ? params.getKey("/CheckSum") : QPDFObjectHandle::newNull();
        if (size.isInteger() && size.getIntValue() == static_cast<long long>(f.data->size()) && sum.isString() &&
            sum.getStringValue() == fileio::md5Of(*f.data) &&
            (f.mime.empty() || had.getDict().getKey("/Subtype").isNameAndEquals("/" + f.mime))) {
            return true;
        }
    }
    sink.touch(s);
    sink.touch(ef);
    QPDFObjectHandle stream = embeddedStream(sink, f);
    ef.replaceKey("/F", stream);
    if (ef.hasKey("/UF")) {
        ef.replaceKey("/UF", stream);
    }
    return true;
}

FileEntry documentEntry(const std::string& name, const std::string& xopp, bool archive) {
    FileEntry f;
    f.name = name;
    f.description = archive ? "The Xournal++ document of this PDF, with the ink editable (xournal-qt archive PDF)"
                            : "The Xournal++ document of this PDF (xournal-qt hybrid PDF)";
    f.mime = ArchivePdf::XOPP_MIME;
    f.data = &xopp;
    f.relationship = archive ? "/Source" : "";
    f.modDate = archive;
    return f;
}

/// The recordings (qt/docs/features/audio.md), listed in `audio` (attachment name, name in the document). Written in
/// full: each one whose file can be read. Saved again (`had`): one the file has stays as it is, renamed when its pages
/// changed (the same file specification and stream, under its new name); a new one is added. A recording the
/// document no longer has, or a new one in an archive PDF: throws (the whole file is written anew).
void embedAudio(ObjectSink& sink, QPDF& q, QPDFEmbeddedFileDocumentHelper& efdh, const Prepared& prep, bool archive,
                const QPDFObjectHandle* had, QPDFObjectHandle audio) {
    std::map<std::string, std::string> hadNames;  // source -> attachment name
    if (had) {
        for (const auto& [name, source]: audioListOf(*had)) {
            hadNames[source] = name;
        }
    }
    for (const auto& a: prep.attachments) {
        if (a.source.empty()) {
            continue;
        }
        auto it = hadNames.find(a.source);
        if (it == hadNames.end()) {
            if (had && archive) {
                throw std::runtime_error("a new recording in an archive PDF");  // (its /AF: written in full)
            }
            FileEntry f;
            f.name = a.name;
            f.description = a.description;
            f.mime = a.mime;
            f.file = a.file;
            f.relationship = archive ? a.relationship : std::string();
            f.modDate = true;
            if (!addFile(sink, efdh, f)) {
                continue;  // (unreadable: not carried, its strokes keep their names)
            }
        } else if (it->second != a.name) {
            auto spec = efdh.getEmbeddedFile(it->second);
            if (!spec) {
                throw std::runtime_error("a recording is missing from the file");
            }
            QPDFObjectHandle s = spec->getObjectHandle();
            if (!s.isIndirect()) {
                throw std::runtime_error("a recording's file specification is not an object of its own");
            }
            sink.touch(s);
            s.replaceKey("/F", QPDFObjectHandle::newUnicodeString(a.name));
            s.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(a.name));
            s.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(a.description));
            // (not removeEmbeddedFile: it turns the file specification into null)
            QPDFNameTreeObjectHelper tree(q.getRoot().getKey("/Names").getKey("/EmbeddedFiles"), q);
            tree.remove(it->second);
            efdh.replaceEmbeddedFile(a.name, QPDFFileSpecObjectHelper(s));
        }
        if (it != hadNames.end()) {
            hadNames.erase(it);
        }
        audio.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
        audio.appendItem(QPDFObjectHandle::newUnicodeString(a.source));
    }
    if (!hadNames.empty()) {
        throw std::runtime_error("a recording was removed");  // (it goes with the next full write)
    }
}

}  // namespace

namespace detail {

// --- ObjectSink ---------------------------------------------------------------------------------------------------

QPDFObjectHandle FullSink::add(QPDFObjectHandle value) { return q.makeIndirectObject(value); }

QPDFObjectHandle FullSink::addStream(QPDFObjectHandle dict, const std::string& data) {
    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&q, data);
    for (const auto& k: dict.getKeys()) {
        stream.getDict().replaceKey(k, dict.getKey(k));
    }
    return stream;
}

QPDFObjectHandle FullSink::addFileStream(QPDFObjectHandle dict, const fs::path& file) {
    QPDFObjectHandle stream = QPDFObjectHandle::newStream(&q);
    stream.replaceStreamData(fileProvider(file), QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
    for (const auto& k: dict.getKeys()) {
        stream.getDict().replaceKey(k, dict.getKey(k));
    }
    return stream;
}

QPDFObjectHandle UpdateSink::addFileStream(QPDFObjectHandle dict, const fs::path& file) {
    return u.addStream(dict, fileio::readFile(file));
}

// --- the embedded files -------------------------------------------------------------------------------------------

void touchNames(ObjectSink& sink, QPDFObjectHandle root) {
    sink.touch(root);
    QPDFObjectHandle names = root.getKey("/Names");
    if (!names.isDictionary()) {
        return;
    }
    sink.touch(names);
    std::function<void(QPDFObjectHandle, int)> walk = [&](QPDFObjectHandle node, int depth) {
        if (!node.isDictionary() || depth > 16) {
            return;
        }
        sink.touch(node);
        QPDFObjectHandle kids = node.getKey("/Kids");
        for (int i = 0; kids.isArray() && i < kids.getArrayNItems(); ++i) {
            walk(kids.getArrayItem(i), depth + 1);
        }
    };
    walk(names.getKey("/EmbeddedFiles"), 0);
}

void addDataSpec(QPDF& q, ObjectSink& sink, const std::string& name, const std::string& xopp) {
    touchNames(sink, q.getRoot());
    QPDFEmbeddedFileDocumentHelper efdh(q);
    addFile(sink, efdh, documentEntry(name, xopp, false));
}

Embedded embedFiles(ObjectSink& sink, QPDF& q, const Prepared& prep, bool archive, const QPDFObjectHandle* had,
                    bool dataMayBeMissing) {
    Embedded out;
    touchNames(sink, q.getRoot());
    QPDFEmbeddedFileDocumentHelper efdh(q);
    // The document
    if (had) {
        QPDFObjectHandle name = had->getKey("/Data");
        out.dataName = name.isString() ? name.getUTF8Value() : std::string(DATA_NAME);
    }
    const FileEntry document = documentEntry(out.dataName, prep.xopp, archive);
    if (!had) {
        addFile(sink, efdh, document);
    } else if (!replaceData(sink, efdh, document, false)) {
        if (!dataMayBeMissing) {
            throw std::runtime_error("the embedded document is missing");
        }
        // The last version's .xopp became a delta (version history): the embedded document is added again
        addFile(sink, efdh, document);
    }
    // What the file has (saved again): what the marker listed
    std::set<std::string> before;
    if (had) {
        QPDFObjectHandle listed = had->getKey("/Files");
        for (int i = 0; listed.isArray() && i < listed.getArrayNItems(); ++i) {
            QPDFObjectHandle v = listed.getArrayItem(i);
            before.insert(v.isString() ? v.getUTF8Value() : v.isName() ? v.getName() : std::string());
        }
    }
    // The files next to the .xopp (attached background images): other ones than before, the whole file is written
    // anew (rare)
    for (const auto& [name, data]: prep.extras) {
        out.files.appendItem(QPDFObjectHandle::newUnicodeString(name));
        FileEntry f;
        f.name = name;
        f.description = "A file of the Xournal++ document of this PDF";
        f.data = &data;
        f.relationship = archive ? "/Supplement" : "";
        f.modDate = archive;
        if (!had) {
            addFile(sink, efdh, f);
        } else if (!before.erase(name) || !replaceData(sink, efdh, f, true)) {
            throw std::runtime_error("the document has other background images");
        }
    }
    embedAudio(sink, q, efdh, prep, archive, had, out.audio);
    // The files for other apps (a text document's "name.md" and its pictures; listed with the images, so the clean
    // copy never carries them)
    for (const auto& a: prep.attachments) {
        if (!a.source.empty()) {
            continue;  // (a recording: embedAudio)
        }
        out.files.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
        FileEntry f;
        f.name = a.name;
        f.description = a.description;
        f.mime = a.mime;
        f.data = &a.data;
        f.relationship = archive ? a.relationship : std::string();
        f.modDate = archive;
        if (!had) {
            addFile(sink, efdh, f);
        } else if (a.fixed) {
            // A picture: the one the file has stays as it is (its data does not change under its name); a new one is
            // added (qt/docs/features/md-images.md)
            if (!before.erase(a.name)) {
                if (archive) {
                    throw std::runtime_error("a new attachment of an archive PDF");  // (its /AF: written in full)
                }
                addFile(sink, efdh, f);
            }
        } else if (!before.erase(a.name) || !replaceData(sink, efdh, f, true)) {
            throw std::runtime_error("the document has other attachments");
        }
    }
    // Pictures the text does not link to any more: kept (and listed, so the clean copy never has them) until the file
    // is written in full
    for (auto it = before.begin(); it != before.end();) {
        if (it->find('/') != std::string::npos) {
            out.files.appendItem(QPDFObjectHandle::newUnicodeString(*it));
            it = before.erase(it);
        } else {
            ++it;
        }
    }
    if (!before.empty()) {
        throw std::runtime_error("the document has other background images");
    }
    return out;
}

// --- the text layer of the handwriting (InkTextLayer.h) -----------------------------------------------------------

QPDFObjectHandle writeInkText(ObjectSink& sink, const Prepared& prep, const std::vector<QPDFObjectHandle>& order,
                              QPDFObjectHandle& font, const std::function<bool(size_t, const std::string&)>& keep) {
    QPDFObjectHandle sigs = QPDFObjectHandle::newArray();
    for (size_t i = 0; i < order.size() && i < prep.pages.size(); ++i) {
        const double w = prep.pages[i].width, h = prep.pages[i].height;
        const std::vector<InkTextLayer::Word>* words = i < prep.inkWords.size() ? &prep.inkWords[i] : nullptr;
        const std::string sig = words ? inkSigOf(*words, w, h) : std::string();
        sigs.appendItem(QPDFObjectHandle::newString(sig));
        if (keep && keep(i, sig)) {
            continue;
        }
        QPDFObjectHandle page = order[i];
        sink.touch(page);
        removeInkText(page);  // (the one before; a copy of a page may carry its original's)
        if (sig.empty()) {
            continue;
        }
        if (!font.isIndirect()) {
            font = makeInkFont(sink);
        }
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
        mark.replaceKey("/InkText", QPDFObjectHandle::newString(sig));
        dict.replaceKey(MARKER, mark);
        const std::string cm = placementOf(page, AnnotSpec{}, w, h).cm.unparse();
        putInkText(page, sink.addStream(dict, InkTextLayer::contentOf(*words, h, cm)), font);
    }
    return sigs;
}

// --- the marker and the document information ----------------------------------------------------------------------

void writeMarker(ObjectSink& sink, QPDFObjectHandle marker, const Prepared& prep, const MarkerContent& c) {
    auto drop = [&](const char* key) {
        if (marker.hasKey(key)) {
            marker.removeKey(key);
        }
    };
    marker.replaceKey("/Version", QPDFObjectHandle::newInteger(c.archive ? ARCHIVE_FORMAT_VERSION : FORMAT_VERSION));
    if (c.archive) {
        marker.replaceKey("/Archive", QPDFObjectHandle::newBool(true));
        marker.replaceKey("/Flattened", c.flattened);
    }
    marker.replaceKey("/Data", QPDFObjectHandle::newUnicodeString(c.files.dataName));
    marker.replaceKey("/Files", c.files.files);
    if (c.files.audio.getArrayNItems() > 0) {
        marker.replaceKey("/Audio", c.files.audio);
    } else {
        drop("/Audio");
    }
    marker.replaceKey("/InkText", c.inkText.isArray() ? c.inkText : QPDFObjectHandle::newArray());
    if (c.inkFont.isIndirect()) {
        marker.replaceKey("/InkFont", c.inkFont);
    }
    marker.replaceKey("/Annots", c.annots);
    QPDFObjectHandle drawnList = QPDFObjectHandle::newArray();  // (the base pages we drew: kept while the same)
    for (size_t i = 0; i < prep.pages.size(); ++i) {
        if (prep.pages[i].pdfPage == npos) {
            drawnList.appendItem(QPDFObjectHandle::newInteger(static_cast<long long>(i)));
        }
    }
    marker.replaceKey("/Drawn", drawnList);
    marker.replaceKey("/Spaces", spacesList(prep));
    marker.replaceKey("/Layers", c.layers);
    if (!c.xoppExport.empty()) {
        marker.replaceKey("/XoppExport", QPDFObjectHandle::newUnicodeString(c.xoppExport));
    } else {
        drop("/XoppExport");
    }
    if (c.base > 0) {
        marker.replaceKey("/Base", QPDFObjectHandle::newInteger(c.base));
        marker.replaceKey("/Updates", QPDFObjectHandle::newInteger(c.updates));
    }
    putHistory(marker, c.history,
               [&](const std::string& data) { return sink.addStream(QPDFObjectHandle::newDictionary(), data); });
    drop(PdfHistory::DELTA_KEY);  // (the version before stored as a delta: this one is whole)
}

QPDFObjectHandle writeInfo(ObjectSink& sink, QPDF& q, bool modDate) {
    QPDFObjectHandle trailer = q.getTrailer();
    QPDFObjectHandle info = trailer.getKey("/Info");
    if (!info.isDictionary()) {
        info = sink.add(QPDFObjectHandle::newDictionary());
        trailer.replaceKey("/Info", info);
    } else {
        sink.touch(info);
    }
    info.replaceKey("/Producer", QPDFObjectHandle::newString(std::string(PROJECT_STRING) + " + QPDF " + QPDF_VERSION));
    if (modDate) {
        info.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
    }
    return info;
}

}  // namespace detail

}  // namespace xqt::HybridPdf

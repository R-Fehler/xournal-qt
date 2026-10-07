/*
 * xournal-qt: the small helpers of the PDF with notes (HybridInternal.h): writing a QPDF, our annotations and marks,
 * the text layer of the handwriting on a page, space for notes; strip(), which takes everything of ours out of a PDF.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include <QCryptographicHash>
#include <QFile>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

namespace xqt::HybridPdf {
using namespace detail;

namespace {

std::string nameOfAnnot(QPDFObjectHandle annot) {
    QPDFObjectHandle nm = annot.getKey("/NM");
    return nm.isString() ? nm.getUTF8Value() : std::string();
}

/// Numbers as integers in these units (so that numbers another app writes again with other digits still match).
void canonical(std::ostringstream& out, QPDFObjectHandle o, double scale) {
    if (o.isNumber()) {
        out << std::llround(o.getNumericValue() * scale) << ' ';
    } else if (o.isArray()) {
        out << '[';
        for (int i = 0; i < o.getArrayNItems(); ++i) {
            canonical(out, o.getArrayItem(i), scale);
        }
        out << ']';
    } else if (o.isName()) {
        out << o.getName() << ' ';
    }
}

/// An archive PDF: remove the content streams we added to a page (marked with our key: the "q" before its content,
/// and the stream after it that draws our layers) and the Form XObjects they drew. The page's own streams, and streams
/// other apps added, stay as they are. The names of the layers found go into `found`.
void unflatten(QPDFObjectHandle page, std::set<std::string>& found) {
    QPDFObjectHandle contents = page.getKey("/Contents");
    if (!contents.isArray()) {
        return;
    }
    auto markOf = [](QPDFObjectHandle c) {
        QPDFObjectHandle mark = c.isStream() ? c.getDict().getKey(MARKER) : QPDFObjectHandle::newNull();
        return mark.isDictionary() && mark.hasKey("/InkText") ? QPDFObjectHandle::newNull() : mark;  // (not the text)
    };
    // Content another app appended after ours was drawn with the page's own content in "q ... Q": it keeps that (a
    // plain "q" and "Q" instead of ours), so it stays where it was
    int lastOurs = -1;
    for (int i = 0; i < contents.getArrayNItems(); ++i) {
        if (markOf(contents.getArrayItem(i)).isDictionary()) {
            lastOurs = i;
        }
    }
    const bool trailing = lastOurs >= 0 && lastOurs + 1 < contents.getArrayNItems();
    QPDF* owner = page.getOwningQPDF();
    QPDFObjectHandle kept = QPDFObjectHandle::newArray();
    std::vector<std::string> xobjects;
    bool removed = false;
    for (int i = 0; i < contents.getArrayNItems(); ++i) {
        QPDFObjectHandle c = contents.getArrayItem(i);
        QPDFObjectHandle mark = markOf(c);
        if (!mark.isDictionary()) {
            kept.appendItem(c);
            continue;
        }
        removed = true;
        if (trailing && owner) {
            kept.appendItem(QPDFObjectHandle::newStream(owner, mark.hasKey("/Layers") ? "Q\n" : "q\n"));
        }
        for (const char* key: {"/XObjects", "/Layers"}) {
            QPDFObjectHandle list = mark.getKey(key);
            for (int k = 0; list.isArray() && k < list.getArrayNItems(); ++k) {
                QPDFObjectHandle v = list.getArrayItem(k);
                if (v.isName()) {
                    xobjects.push_back(v.getName());
                } else if (v.isString()) {
                    found.insert(v.getUTF8Value());
                }
            }
        }
    }
    if (!removed) {
        return;
    }
    page.replaceKey("/Contents", kept);
    QPDFObjectHandle resources = page.getKey("/Resources");
    QPDFObjectHandle xobj = resources.isDictionary() ? resources.getKey("/XObject") : QPDFObjectHandle::newNull();
    if (xobj.isDictionary()) {
        for (const auto& name: xobjects) {
            if (xobj.hasKey(name)) {
                xobj.removeKey(name);
            }
        }
    }
}

// --- the text layer of the handwriting (InkTextLayer.h) ---------------------------------------------------------------

bool isInkText(QPDFObjectHandle c) {
    QPDFObjectHandle mark = c.isStream() ? c.getDict().getKey(MARKER) : QPDFObjectHandle::newNull();
    return mark.isDictionary() && mark.hasKey("/InkText");
}

}  // namespace

namespace detail {

bool writeFile(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

void writePdfTo(QPDF& pdf, const fs::path& target, ArchiveWrite archive,
                fileio::Sync sync) {
    fileio::AtomicFile file(target);
    {
        QPDFWriter w(pdf, file.temp().string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        // The streams of the PDF as they are (decoding and compressing them again doubled the time); new streams
        // without a filter are still compressed
        w.setDecodeLevel(archive.recompress ? qpdf_dl_generalized : qpdf_dl_none);
        if (!archive.on && archive.encryption) {
            PdfEncryption::apply(w, pdf, *archive.encryption);
        }
        if (archive.on) {
            w.setPreserveEncryption(false);
            w.setMinimumPDFVersion("1.7");
            w.setNewlineBeforeEndstream(true);  // (PDF/A: an end of line before "endstream")
        }
        w.write();
    }
    if (std::string error; !file.commit(error, sync)) {
        throw std::runtime_error(error);
    }
}

std::string pdfDateNow() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "D:%Y%m%d%H%M%SZ", &tm);
    return buf;
}

bool isOurs(QPDFObjectHandle annot) {
    if (!annot.isDictionary()) {
        return false;
    }
    if (annot.hasKey(MARKER)) {
        return true;
    }
    QPDFObjectHandle nm = annot.getKey("/NM");
    return nm.isString() && nm.getUTF8Value().rfind(NAME_PREFIX, 0) == 0;
}

std::string hashOf(QPDFObjectHandle annot) {
    std::ostringstream out;
    canonical(out, annot.getKey("/Subtype"), 1);
    out << '|';
    canonical(out, annot.getKey("/Rect"), 10);
    out << '|';
    canonical(out, annot.getKey("/InkList"), 10);
    out << '|';
    canonical(out, annot.getKey("/C"), 1000);
    return fileio::hex16(fileio::fnv1a(out.str()));
}

bool restoreBoxes(QPDFObjectHandle page) {
    QPDFObjectHandle saved = page.isDictionary() ? page.getKey(BOXES) : QPDFObjectHandle::newNull();
    if (!saved.isDictionary()) {
        return false;
    }
    for (const char* key: {"/MediaBox", "/CropBox"}) {
        QPDFObjectHandle box = saved.getKey(key);
        if (box.isArray()) {
            page.replaceKey(key, box.shallowCopy());
        } else if (page.hasKey(key)) {
            page.removeKey(key);
        }
    }
    page.removeKey(BOXES);
    return true;
}

void setSpace(QPDFObjectHandle page, const NoteSpace& s) {
    restoreBoxes(page);
    if (s.empty()) {
        return;
    }
    QPDFPageObjectHelper helper(page);
    const QPDFObjectHandle media = helper.getMediaBox();
    const bool hadCrop = page.hasKey("/CropBox");
    QPDFObjectHandle::Rectangle crop = helper.getCropBox().getArrayAsRectangle();
    QPDFObjectHandle saved = QPDFObjectHandle::newDictionary();
    saved.replaceKey("/MediaBox", QPDFObjectHandle::newArray(media.getArrayAsRectangle()));
    if (hadCrop) {
        saved.replaceKey("/CropBox", QPDFObjectHandle::newArray(crop));
    }
    QPDFObjectHandle rot = page.getKey("/Rotate");
    int rotate = rot.isInteger() ? static_cast<int>(rot.getIntValue() % 360) : 0;
    rotate = (rotate + 360) % 360;
    // Which side of the PDF's box each side of the shown page is (PDF space: y up)
    double left = s.left, top = s.top, right = s.right, bottom = s.bottom;  // of the shown page
    switch (rotate) {
        case 90:  // shown top = the PDF's left, shown right = its top
            crop = {crop.llx - top, crop.lly - left, crop.urx + bottom, crop.ury + right};
            break;
        case 180:
            crop = {crop.llx - right, crop.lly - top, crop.urx + left, crop.ury + bottom};
            break;
        case 270:
            crop = {crop.llx - bottom, crop.lly - right, crop.urx + top, crop.ury + left};
            break;
        default:
            crop = {crop.llx - left, crop.lly - bottom, crop.urx + right, crop.ury + top};
    }
    const QPDFObjectHandle::Rectangle m = media.getArrayAsRectangle();
    const QPDFObjectHandle::Rectangle grown(std::min(m.llx, crop.llx), std::min(m.lly, crop.lly),
                                            std::max(m.urx, crop.urx), std::max(m.ury, crop.ury));
    page.replaceKey("/MediaBox", QPDFObjectHandle::newArray(grown));
    page.replaceKey("/CropBox", QPDFObjectHandle::newArray(crop));
    page.replaceKey(BOXES, saved);
}

bool removeInkText(QPDFObjectHandle page) {
    QPDFObjectHandle contents = page.getKey("/Contents");
    if (!contents.isArray()) {
        return false;
    }
    QPDFObjectHandle kept = QPDFObjectHandle::newArray();
    bool removed = false;
    for (int i = 0; i < contents.getArrayNItems(); ++i) {
        QPDFObjectHandle c = contents.getArrayItem(i);
        if (isInkText(c)) {
            removed = true;
        } else {
            kept.appendItem(c);
        }
    }
    if (!removed) {
        return false;
    }
    page.replaceKey("/Contents", kept);
    QPDFObjectHandle res = page.getKey("/Resources");
    if (res.isDictionary() && res.getKey("/Font").isDictionary() && res.getKey("/Font").hasKey(InkTextLayer::FONT_RESOURCE)) {
        res = res.shallowCopy();
        QPDFObjectHandle fonts = res.getKey("/Font").shallowCopy();
        fonts.removeKey(InkTextLayer::FONT_RESOURCE);
        res.replaceKey("/Font", fonts);
        page.replaceKey("/Resources", res);
    }
    return true;
}

std::string inkSigOf(const std::vector<InkTextLayer::Word>& words, double w, double h) {
    if (words.empty()) {
        return {};
    }
    return InkTextLayer::sigOf(InkTextLayer::contentOf(words, h, "") + std::to_string(std::lround(w * 10)) + 'x' +
                               std::to_string(std::lround(h * 10)));
}

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

std::vector<std::pair<std::string, std::string>> audioListOf(QPDFObjectHandle marker) {
    std::vector<std::pair<std::string, std::string>> out;
    QPDFObjectHandle list = marker.isDictionary() ? marker.getKey("/Audio") : QPDFObjectHandle::newNull();
    for (int i = 0; list.isArray() && i + 1 < list.getArrayNItems(); i += 2) {
        QPDFObjectHandle a = list.getArrayItem(i), b = list.getArrayItem(i + 1);
        if (a.isString() && b.isString()) {
            out.emplace_back(a.getUTF8Value(), b.getUTF8Value());
        }
    }
    return out;
}

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

std::vector<std::string> strip(QPDF& pdf, const std::set<std::string>& keep) {
    QPDFObjectHandle root = pdf.getRoot();
    QPDFObjectHandle marker = root.getKey(MARKER);
    std::map<std::string, std::string> expected;
    std::set<std::string> files;
    if (marker.isDictionary()) {
        QPDFObjectHandle annots = marker.getKey("/Annots");
        if (annots.isDictionary()) {
            for (const auto& key: annots.getKeys()) {
                QPDFObjectHandle v = annots.getKey(key);
                expected[key.substr(1)] = v.isString() ? v.getUTF8Value() : std::string();
            }
        }
        QPDFObjectHandle data = marker.getKey("/Data");
        files.insert(data.isString() ? data.getUTF8Value() : std::string(DATA_NAME));
        QPDFObjectHandle more = marker.getKey("/Files");
        if (more.isArray()) {
            for (int i = 0; i < more.getArrayNItems(); ++i) {
                if (more.getArrayItem(i).isString()) {
                    files.insert(more.getArrayItem(i).getUTF8Value());
                }
            }
        }
        for (const auto& [name, source]: audioListOf(marker)) {  // (the recordings: qt/docs/audio.md)
            files.insert(name);
        }
    }
    std::set<std::string> flattened;  // an archive PDF: the layers merged into the page content
    bool archive = false;
    if (marker.isDictionary()) {
        archive = marker.getKey("/Archive").isBool() && marker.getKey("/Archive").getBoolValue();
        QPDFObjectHandle flat = marker.getKey("/Flattened");
        if (flat.isArray()) {
            for (int i = 0; i < flat.getArrayNItems(); ++i) {
                if (flat.getArrayItem(i).isString()) {
                    flattened.insert(flat.getArrayItem(i).getUTF8Value());
                }
            }
        }
    }
    std::vector<std::string> changed;
    std::set<std::string> seen;
    for (auto& page: QPDFPageDocumentHelper(pdf).getAllPages()) {
        QPDFObjectHandle p = page.getObjectHandle();
        removeInkText(p);  // (the app searches the handwriting itself)
        unflatten(p, seen);
        restoreBoxes(p);  // (space for notes: the clean copy has the page's own size)
        QPDFObjectHandle annots = p.getKey("/Annots");
        if (!annots.isArray()) {
            continue;
        }
        QPDFObjectHandle kept = QPDFObjectHandle::newArray();
        bool removed = false;
        for (int i = 0; i < annots.getArrayNItems(); ++i) {
            QPDFObjectHandle a = annots.getArrayItem(i);
            if (!isOurs(a)) {
                kept.appendItem(a);
                continue;
            }
            const std::string nm = nameOfAnnot(a);
            seen.insert(nm);
            auto it = expected.find(nm);
            const bool link = a.getKey("/Subtype").isName() && a.getKey("/Subtype").getName() == "/Link";
            if (!link && (it == expected.end() || it->second != hashOf(a))) {  // (links are written from the text)
                changed.push_back(nm);
            }
            if (keep.count(nm)) {
                a.removeKey(MARKER);
                a.replaceKey("/NM", QPDFObjectHandle::newUnicodeString("imported:" + nm));
                kept.appendItem(a);
            } else {
                removed = true;
            }
        }
        if (removed || !keep.empty()) {
            p.replaceKey("/Annots", kept);
        }
    }
    for (const auto& [nm, hash]: expected) {
        if (!seen.count(nm) && nm.find("-link") == std::string::npos) {
            changed.push_back(nm);  // deleted in another app
        }
    }
    for (const auto& nm: flattened) {
        if (!seen.count(nm)) {
            changed.push_back(nm);  // its content stream is gone (another app rewrote the page)
        }
    }
    if (root.hasKey(MARKER)) {
        root.removeKey(MARKER);
    }
    if (archive) {
        // What the archive added for PDF/A: the associated files of our data, the output intent, the metadata
        QPDFObjectHandle af = root.getKey("/AF");
        if (af.isArray()) {
            QPDFObjectHandle kept = QPDFObjectHandle::newArray();
            for (int i = 0; i < af.getArrayNItems(); ++i) {
                QPDFObjectHandle spec = af.getArrayItem(i);
                QPDFObjectHandle name = spec.isDictionary() ? spec.getKey("/UF") : QPDFObjectHandle::newNull();
                if (!(name.isString() && files.count(name.getUTF8Value()))) {
                    kept.appendItem(spec);
                }
            }
            if (kept.getArrayNItems() > 0) {
                root.replaceKey("/AF", kept);
            } else {
                root.removeKey("/AF");
            }
        }
        for (const char* key: {"/OutputIntents", "/Metadata"}) {
            if (root.hasKey(key)) {
                root.removeKey(key);
            }
        }
    }
    QPDFEmbeddedFileDocumentHelper efdh(pdf);
    for (const auto& f: files) {
        efdh.removeEmbeddedFile(f);
    }
    if (!files.empty() && efdh.getEmbeddedFiles().empty()) {  // (no empty name tree left behind)
        QPDFObjectHandle names = root.getKey("/Names");
        if (names.isDictionary() && names.hasKey("/EmbeddedFiles")) {
            names.removeKey("/EmbeddedFiles");
            if (names.getKeys().empty()) {
                root.removeKey("/Names");
            }
        }
    }
    std::sort(changed.begin(), changed.end());
    return changed;
}

}  // namespace detail

std::string nameOf(size_t page, size_t layer) {
    return std::string(NAME_PREFIX) + "p" + std::to_string(page + 1) + "-l" + std::to_string(layer + 1);
}

bool parseName(const std::string& name, size_t& page, size_t& layer) {
    unsigned long p = 0, l = 0;
    char tail = 0;
    const std::string prefix = std::string(NAME_PREFIX) + "p";
    if (name.rfind(prefix, 0) != 0 ||
        std::sscanf(name.c_str() + prefix.size(), "%lu-l%lu%c", &p, &l, &tail) != 2 || p == 0 || l == 0) {
        return false;
    }
    page = p - 1;
    layer = l - 1;
    return true;
}

}  // namespace xqt::HybridPdf

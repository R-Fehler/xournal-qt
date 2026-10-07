#include "HybridPdf.h"
#include "FileIo.h"

#include "InkText.h"
#include "InkTextLayer.h"

#include <algorithm>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include <qpdf/DLL.h>
#include <cairo-pdf.h>
#include <cairo.h>
#include <QCryptographicHash>
#include <QtGlobal>
#include <glib.h>
#include <qpdf/Buffer.hh>
#include <qpdf/Pipeline.hh>
#include <QFile>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFNameTreeObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <zlib.h>

#include "control/xml/XmlNode.h"
#include "control/xojfile/LoadHandler.h"
#include "control/xojfile/SaveHandler.h"
#include "control/xojfile/XmlAttrs.h"
#include "control/xojfile/XmlTags.h"
#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "util/InputStream.h"
#include "util/OutputStream.h"
#include "util/PathUtil.h"
#include "util/Util.h"
#include "view/LayerView.h"
#include "view/PaperTone.h"
#include "view/View.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/BackgroundView.h"

#include "ArchivePdf.h"
#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "DocumentImages.h"
#include "DocumentLink.h"
#include "IncrementalPdf.h"
#include "MdBox.h"
#include "MergedPdf.h"
#include "PdfEncryption.h"
#include "PdfKeywords.h"
#include "PageBookmarks.h"
#include "ByteDelta.h"
#include "PdfHistory.h"
#include "PdfRevisions.h"
#include "PdfBookmarks.h"
#include "StickyNote.h"
#include "TextDocument.h"
#include "config.h"

namespace xqt::HybridPdf {

namespace {

constexpr const char* MARKER = "/XournalQt";  ///< in the catalog, and the private key of our annotations
constexpr const char* CLEAN_NAME = "base.pdf";
constexpr const char* CHECK_NAME = "changed.txt";
constexpr const char* AUDIO_NAME = "audio";  ///< the recordings it carries (qt/docs/audio.md)
constexpr const char* PICTURES_NAME = "pictures";  ///< the pictures a text document carries (qt/docs/md-images.md)
constexpr const char* PAGES_NAME = "pages.txt";  ///< the file's page objects the clean copy's pages are
/// A protected PDF's pictures and recordings are taken out (by this process): "unpacked-<pid>" (removeProtectedLeftovers)
constexpr const char* UNPACKED_PREFIX = "unpacked-";
constexpr double MARGIN = 2.0;  ///< around a layer's elements (pt)
/// A base page with space for notes (qt/docs/note-space.md): its boxes as the PDF had them (/MediaBox, /CropBox)
constexpr const char* BOXES = "/XournalQtBoxes";

// --- small helpers ------------------------------------------------------------------------------------------------

/// XQT_HYBRID_TIMES=1: the time of each step of writing and opening, on stderr (for measuring).
struct Steps {
    Steps(): on(qEnvironmentVariableIsSet("XQT_HYBRID_TIMES")), last(std::chrono::steady_clock::now()) {}
    void operator()(const char* step) {
        if (on) {
            const auto now = std::chrono::steady_clock::now();
            std::fprintf(stderr, "hybrid-pdf: %-28s %8.1f ms\n", step,
                         std::chrono::duration<double, std::milli>(now - last).count());
            last = now;
        }
    }
    bool on;
    std::chrono::steady_clock::time_point last;
};

using fileio::stampOf;
uint64_t fnv(const std::string& s) { return fileio::fnv1a(s); }
std::string hex(uint64_t v) { return fileio::hex16(v); }
std::string bytesOf(const fs::path& p) { return fileio::readFile(p); }

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

/// The .xopp written into memory (SaveHandler), not into a file.
class StringOutputStream final: public OutputStream {
public:
    void write(const char* d, size_t len) override { data.append(d, len); }
    void close() override {}
    std::string data;
};

bool writeFile(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

/// A unique temporary name next to `target` (several threads may make the same clean copy at once).
fs::path partOf(const fs::path& target) { return fileio::tempNameFor(target); }

/// How an archive PDF is written (PDF/A): never encrypted, at least PDF 1.7, streams with a forbidden filter decoded.
struct ArchiveWrite {
    bool on = false;
    bool recompress = false;
    /// Not an archive: how it is encrypted (nullptr: as the PDF written has it). An archive PDF never is.
    const PdfEncryption::Encryption* encryption = nullptr;
};

/// Written whole or not at all (fileio::AtomicFile); a file of the user's made durable, a cache entry (`sync` None)
/// not. Throws.
void writePdfTo(QPDF& pdf, const fs::path& target, ArchiveWrite archive = {},
                fileio::Sync sync = fileio::Sync::Durable) {
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

/// Whether an annotation is one of ours.
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

/// The hash of what another app may change of an annotation: its kind, place, ink and colour (not its appearance
/// stream, which some apps write again when they save).
std::string hashOf(QPDFObjectHandle annot) {
    std::ostringstream out;
    canonical(out, annot.getKey("/Subtype"), 1);
    out << '|';
    canonical(out, annot.getKey("/Rect"), 10);
    out << '|';
    canonical(out, annot.getKey("/InkList"), 10);
    out << '|';
    canonical(out, annot.getKey("/C"), 1000);
    return hex(fnv(out.str()));
}

double round1(double v) { return std::round(v * 10) / 10; }

QPDFObjectHandle real1(double v) { return QPDFObjectHandle::newReal(round1(v), 1); }

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

// --- space for notes: larger boxes -----------------------------------------------------------------------------------

/// A base page as the PDF has it: the boxes it had before we gave it space for notes. True if it had others.
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

/// Give a base page space for notes: its crop box grows by the amounts (as the page is shown, its /Rotate undone), its
/// media box with it. The content stays as it is, so it lands at the offset, and its text, links and the annotations
/// of other apps stay where they are on it. Starts from the page's own boxes (restoreBoxes).
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

// --- the text layer of the handwriting (InkTextLayer.h) ---------------------------------------------------------------

bool isInkText(QPDFObjectHandle c) {
    QPDFObjectHandle mark = c.isStream() ? c.getDict().getKey(MARKER) : QPDFObjectHandle::newNull();
    return mark.isDictionary() && mark.hasKey("/InkText");
}

/// Remove the text layer from a page (its stream and its font in the page's resources). Whether it had one.
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

/// The font of the text layer, made with `add` (an indirect object of a direct one) and `addStream` (a stream).
template <typename Add, typename AddStream>
QPDFObjectHandle makeInkFont(Add add, AddStream addStream) {
    auto name = QPDFObjectHandle::newName("/XqtGlyphless");
    QPDFObjectHandle file = addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::glyphlessFont());
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
    cid.replaceKey("/FontDescriptor", add(descriptor));
    cid.replaceKey("/DW", QPDFObjectHandle::newInteger(InkTextLayer::ADVANCE));
    cid.replaceKey("/CIDToGIDMap", addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::cidToGidMap()));
    QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
    font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
    font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type0"));
    font.replaceKey("/BaseFont", name);
    font.replaceKey("/Encoding", QPDFObjectHandle::newName("/Identity-H"));
    font.replaceKey("/DescendantFonts", QPDFObjectHandle::newArray(std::vector<QPDFObjectHandle>{add(cid)}));
    font.replaceKey("/ToUnicode", addStream(QPDFObjectHandle::newDictionary(), InkTextLayer::toUnicode()));
    return add(font);
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

/// The recordings a marker lists (/Audio: attachment name, then the recording's name in the document, for each).
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

/// Remove our annotations from every page (except `keep`, which lose our mark), our marker and our embedded files.
/// Returns the /NM of our annotations whose hash differs from the marker's, or that are missing.
std::vector<std::string> strip(QPDF& pdf, const std::set<std::string>& keep = {}) {
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

// --- writing --------------------------------------------------------------------------------------------------------

/// Hashes what is written into it (FNV-1a).
class HashStream: public OutputStream {
public:
    using OutputStream::write;
    void write(const char* data, size_t len) override { h = fileio::fnv1a(std::string_view(data, len), h); }
    void close() override {}
    uint64_t h = fileio::FNV_OFFSET;
};

struct XmlAccess: XmlNode {
    using XmlNode::children;
};

/// The hash of an XML node as the .xopp holds it.
uint64_t hashOfNode(XmlNode* node) {
    HashStream out;
    node->writeOut(&out);
    return out.h;
}

/// The embedded .xopp: its PDF pages refer to the base pages of the hybrid PDF (page i shows base page i), and its
/// PDF is `pdfName` next to it (`attach`: upstream's attached PDF "name.xopp.bg.pdf", domain "attach", file name
/// "bg.pdf"). Everything else as upstream's SaveHandler writes it.
/// It also hashes each layer and each page background as written (their XML, and an image background's pixels): what
/// an incremental save compares to know which drawings it can keep (layerHash, backgroundHash).
class HybridSaveHandler: public SaveHandler {
public:
    HybridSaveHandler(std::string pdfName, bool attach): pdfName(std::move(pdfName)), attach(attach) {}

    uint64_t layerHash(size_t page, size_t layer) const {
        return page < layers.size() && layer < layers[page].size() ? layers[page][layer] : 0;
    }
    uint64_t backgroundHash(size_t page) const { return page < backgrounds.size() ? backgrounds[page] : 0; }
    /// The recordings' names written instead of theirs (Export for Xournal++: absolute paths; qt/docs/audio.md).
    const std::map<std::string, std::string>* audioNames = nullptr;

protected:
    void writeAudio(XmlNode* node, const AudioContent& content) override {
        if (audioNames) {
            if (auto it = audioNames->find(audio::nameOf(content)); it != audioNames->end()) {
                AudioContent renamed = content;
                audio::stamp(renamed, it->second, content.getTimestamp());
                SaveHandler::writeAudio(node, renamed);
                return;
            }
        }
        SaveHandler::writeAudio(node, content);
    }

    void visitLayer(XmlNode* page, const Layer* l) override {
        SaveHandler::visitLayer(page, l);
        if (current < layers.size()) {
            layers[current].push_back(hashOfNode((page->*&XmlAccess::children).back().get()));
        }
    }

    void visitPage(XmlNode* root, ConstPageRef p, const Document* doc, int id, const fs::path& target) override {
        constexpr auto children = &XmlAccess::children;
        const bool pdf = p->getBackgroundType().isPdfPage();
        const bool first = pdf && !firstPdfPageVisited;
        if (pdf) {
            // (the base class would write the document's own PDF, and save an attached one next to the document)
            firstPdfPageVisited = true;
        }
        current = static_cast<size_t>(id);
        if (layers.size() <= current) {
            layers.resize(current + 1);
            backgrounds.resize(current + 1);
        }
        layers[current].clear();
        SaveHandler::visitPage(root, p, doc, id, target);
        XmlNode* page = (root->*children).back().get();
        std::unique_ptr<XmlNode>& bg = (page->*children).front();
        using xoj::xml_attrs::BackgroundType;
        using xoj::xml_attrs::Domain;
        if (pdf) {
            std::unique_ptr<XmlNode> node(new XmlNode(xoj::xml_tags::NAMES[xoj::xml_tags::Type::BACKGROUND]));
            writeBackgroundName(node.get(), p);
            // (the order of the attributes matters to the original Xournal)
            node->setAttrib(xoj::xml_attrs::TYPE_STR, BackgroundType::NAMES[BackgroundType::PDF]);
            if (first) {
                node->setAttrib(xoj::xml_attrs::DOMAIN_STR, Domain::NAMES[attach ? Domain::ATTACH : Domain::ABSOLUTE]);
                node->setAttrib(xoj::xml_attrs::FILENAME_STR, attach ? std::string("bg.pdf") : pdfName);
            }
            node->setAttrib(xoj::xml_attrs::PAGE_NUMBER_STR, static_cast<size_t>(id) + 1);
            bg = std::move(node);
        } else if (p->getBackgroundType().isImagePage() && p->getBackgroundImage().getCloneId() == -1 &&
                   !(p->getBackgroundImage().isAttached() && p->getBackgroundImage().getPixbuf()) &&
                   !p->getBackgroundImage().getFilepath().empty()) {
            // An image file of the user's: by its absolute path (the .xopp is opened from the cache)
            std::error_code ec;
            const fs::path abs = fs::absolute(p->getBackgroundImage().getFilepath(), ec);
            bg->setAttrib(xoj::xml_attrs::FILENAME_STR, (ec ? p->getBackgroundImage().getFilepath() : abs).u8string());
        }
        if (!pdf) {
            HashStream h;
            bg->writeOut(&h);
            if (p->getBackgroundType().isImagePage()) {
                // (the pixels: an attached image's name says nothing about them, a file may change)
                if (const GdkPixbuf* pixbuf = p->getBackgroundImage().getPixbuf()) {
                    const int rows = gdk_pixbuf_get_height(pixbuf);
                    const int stride = gdk_pixbuf_get_rowstride(pixbuf);
                    const int rowBytes = gdk_pixbuf_get_width(pixbuf) * gdk_pixbuf_get_n_channels(pixbuf) *
                                         gdk_pixbuf_get_bits_per_sample(pixbuf) / 8;
                    const guchar* pixels = gdk_pixbuf_read_pixels(pixbuf);
                    for (int y = 0; y < rows; ++y) {
                        h.write(reinterpret_cast<const char*>(pixels) + static_cast<size_t>(y) * stride,
                                static_cast<size_t>(rowBytes));
                    }
                    h.write(std::to_string(gdk_pixbuf_get_width(pixbuf)) + "x" + std::to_string(rows));
                }
            }
            backgrounds[current] = h.h;
        }
    }

private:
    std::string pdfName;
    bool attach;
    size_t current = 0;
    std::vector<std::vector<uint64_t>> layers;
    std::vector<uint64_t> backgrounds;
};

cairo_status_t appendTo(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

struct PageSpec {
    double width = 0, height = 0;
    size_t pdfPage = npos;    ///< base page from the background PDF
    size_t drawnPage = npos;  ///< base page drawn by cairo (page of `drawn`; npos: the file has it, see Reuse)
    size_t annotsFrom = npos; ///< a drawn page: the annotations of other apps on this page of the background PDF
    std::string sig;          ///< a drawn page: what it shows (its background as saved, its size)
    NoteSpace space;          ///< a page of the background PDF: its space for notes (qt/docs/note-space.md)
    std::string bookmark;     ///< the title of its bookmark in the outline (qt/docs/bookmarks.md); "": none
};

struct AnnotSpec {
    size_t page = 0, layer = 0;
    size_t drawnPage = npos;                       ///< its appearance (page of `drawn`; npos: the file has it)
    std::string sig;                               ///< what the layer shows (as saved, and the page's size)
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;         ///< its box on the page (page coordinates, y down)
    std::vector<std::vector<double>> strokes;      ///< x, y, x, y, ... (page coordinates)
    Color color{};
    double width = 0;
    std::string text;
};

/// A link of a Markdown box as a PDF /Link (qt/docs/links.md): a web address (/URI), or another PDF at a page (/GoToR).
struct LinkSpec {
    size_t page = 0;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  ///< where it is drawn (page coordinates, y down)
    std::string uri;                         ///< a web or mail address, or
    std::string file;                        ///< a PDF, relative to the PDF written
    int destPage = 0;                        ///< its page (0-based)
};

/// Where a link of a Markdown box leads for other PDF viewers, from the PDF written in `folder`: a web address, or a
/// PDF and its page (a .xopp with its PDF next to it: that PDF, at the linked PDF page). False: nothing a viewer can
/// open (a .md, a lone .xopp, a place in this document).
/// `map`: the links of a document archived into another folder (read from its own folder; archived documents linked
/// in the archive).
bool linkFor(const md::LinkHit& hit, const fs::path& folder, LinkSpec& spec, const LinkMap* map = nullptr) {
    const QString target = QString::fromStdString(hit.target);
    if (!hit.wiki && (target.startsWith(QLatin1String("http://")) || target.startsWith(QLatin1String("https://")) ||
                      target.startsWith(QLatin1String("mailto:")))) {
        spec.uri = hit.target;
        return true;
    }
    const auto link = hit.wiki ? std::optional<links::Link>() : links::parse(target);
    if (!link || link->path.isEmpty()) {
        return false;
    }
    fs::path file = links::resolvePath(map && !map->from.empty() ? map->from : folder, link->path);
    if (map && map->archived) {
        if (const fs::path archived = map->archived(file); !archived.empty()) {
            // (its pages are the pages of its document)
            const int page = link->page > 0 ? link->page : link->pdfPage > 0 ? link->pdfPage : 1;
            spec.file = links::relativePath(folder / "x.pdf", archived).toStdString();
            spec.destPage = page - 1;
            return true;
        }
    }
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::error_code ec;
    bool pdfPages = false;  // the page is a page of that PDF (not of a document of notes over it)
    if (ext == ".xopp" || ext == ".xoj") {
        file.replace_extension(".pdf");
        pdfPages = true;
    } else if (ext != ".pdf") {
        return false;
    }
    if (!fs::exists(file, ec)) {
        return false;
    }
    const bool hybrid = !pdfPages && isHybrid(file);  // (its pages are the pages of its document)
    int page = link->page > 0 ? link->page : 1;
    if (link->pdfPage > 0 && !hybrid) {
        page = link->pdfPage;
    }
    spec.file = links::relativePath(folder / "x.pdf", file).toStdString();
    spec.destPage = page - 1;
    return true;
}

/// The drawings an existing file has already (by their sig): prepare() does not draw them again.
struct Reuse {
    std::set<std::string> layers, backgrounds;
};

/// What a drawing of a layer or background of a page of this size shows, as a key (its hash as saved in the .xopp;
/// the version of the app, whose drawing may change).
std::string sigOf(uint64_t saved, double width, double height) {
    std::ostringstream key;
    key << PROJECT_STRING << '|' << std::llround(width * 100) << 'x' << std::llround(height * 100) << '|' << saved;
    return hex(fnv(key.str()));
}

struct Prepared {
    fs::path bg;
    std::vector<PageSpec> pages;
    std::vector<AnnotSpec> annots;
    std::vector<LinkSpec> links;
    std::string drawn;  ///< a PDF (cairo): the generated base pages and the appearance of each annotation
    std::string xopp;
    std::vector<std::pair<std::string, std::string>> extras;  ///< files next to the .xopp (attached images)
    std::vector<TextDocument::Attachment> attachments;        ///< files for other apps (a text document's "name.md")
    std::vector<std::vector<InkTextLayer::Word>> inkWords;    ///< per page: its text layer (InkTextLayer.h)
    std::string error;
    PdfEncryption::Encryption encryption;  ///< how the file is encrypted when written in full (WriteOptions)
};

/// The words of the text layer of each page (`pages`: the recognised handwriting per page; may be null).
void addInkWords(Prepared& prep, const std::vector<std::shared_ptr<const ink::PageText>>* pages) {
    prep.inkWords.assign(prep.pages.size(), {});
    for (size_t i = 0; pages && i < pages->size() && i < prep.pages.size(); ++i) {
        if (const auto& page = (*pages)[i]) {
            prep.inkWords[i] = InkTextLayer::wordsOf(*page);
        }
    }
}

/// Everything that needs the document: under its shared lock (and briefly its lock).
Prepared prepare(Document& doc, const std::string& pdfName, const fs::path& work, const BasePageOf& baseOf,
                 size_t pdfPageCount, bool attach = false, const fs::path& linkFolder = {},
                 const LinkMap* linkMap = nullptr, const Reuse* reuse = nullptr,
                 const std::map<std::string, std::string>* audioNames = nullptr) {
    Prepared out;
    // The .xopp first (not written yet): its hashes say what each drawing shows
    const fs::path xopp = work / DATA_NAME;
    HybridSaveHandler h(pdfName, attach);
    h.audioNames = audioNames;
    std::vector<audio::Recording> recordings;  // (qt/docs/audio.md: their files are found after the lock)
    fs::path docFile;
    {
        std::shared_lock lock(doc);
        h.prepareSave(&doc, xopp);
    }
    {
        std::shared_lock lock(doc);
        out.attachments = TextDocument::attachments(doc, pdfName);  // (qt/docs/md-pdf.md)
        recordings = audio::recordingsOf(doc);
        docFile = doc.getFilepath();
        out.bg = doc.getPdfFilepath();
        const size_t bgPages = pdfPageCount != npos ? pdfPageCount : doc.getPdfPageCount();
        cairo_surface_t* surface = cairo_pdf_surface_create_for_stream(appendTo, &out.drawn, 1, 1);
        cairo_t* cr = cairo_create(surface);
        cairo_font_options_t* fontOptions = cairo_font_options_create();  // as upstream's PDF export
        cairo_font_options_set_hint_metrics(fontOptions, CAIRO_HINT_METRICS_ON);
        cairo_set_font_options(cr, fontOptions);
        cairo_font_options_destroy(fontOptions);
        size_t drawn = 0;
        for (size_t i = 0; i < doc.getPageCount(); ++i) {
            PageRef p = doc.getPage(i);
            PageSpec spec;
            spec.width = p->getWidth();
            spec.height = p->getHeight();
            if (const auto& b = p->getBookmark()) {
                spec.bookmark = PageBookmarks::displayLabelUtf8(*b, i);
            }
            if (!out.bg.empty() && p->getBackgroundType().isPdfPage() && p->getPdfPageNr() < bgPages) {
                spec.pdfPage = p->getPdfPageNr();
                spec.space = p->getNoteSpace();
            } else {
                spec.sig = sigOf(h.backgroundHash(i), spec.width, spec.height);
                if (!reuse || !reuse->backgrounds.count(spec.sig)) {
                    cairo_pdf_surface_set_size(surface, spec.width, spec.height);
                    cairo_save(cr);
                    xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
                    flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
                    xoj::view::BackgroundView::createForPage(p, flags, nullptr)->draw(cr);
                    cairo_restore(cr);
                    cairo_show_page(cr);
                    spec.drawnPage = drawn++;
                }
                if (baseOf && !out.bg.empty()) {
                    if (const size_t b = baseOf(p.get()); b < bgPages) {
                        spec.annotsFrom = b;
                    }
                }
            }
            out.pages.push_back(spec);
            size_t layerNo = 0;
            for (const Layer* layer: p->getLayersView()) {
                const size_t li = layerNo++;
                if (!layer->isVisible() || layer->getElementsView().begin() == layer->getElementsView().end()) {
                    continue;
                }
                AnnotSpec a;
                a.page = i;
                a.layer = li;
                a.x0 = a.y0 = std::numeric_limits<double>::max();
                a.x1 = a.y1 = std::numeric_limits<double>::lowest();
                bool colored = false;
                // A sticky note: a /Stamp as big as the note and its shadow (its content is drawn clipped to it), no
                // /InkList (a viewer that draws ink from it would draw the paper as a line)
                const Element* notePaper = sticky::paperOf(*layer);
                for (const auto& e: layer->getElementsView()) {
                    if (!notePaper || e == notePaper) {
                        const auto box = notePaper ? sticky::drawnRect(sticky::lookOf(*layer)->rect)
                                                   : e->getBoundingBox();
                        a.x0 = std::min(a.x0, box.x);
                        a.y0 = std::min(a.y0, box.y);
                        a.x1 = std::max(a.x1, box.x + box.width);
                        a.y1 = std::max(a.y1, box.y + box.height);
                    }
                    if (e->getType() == ELEMENT_STROKE && !notePaper) {
                        const auto* s = static_cast<const Stroke*>(e);
                        std::vector<double> pts;
                        pts.reserve(s->getPointCount() * 2);
                        for (const Point& pt: s->getPointVector()) {
                            pts.push_back(pt.x);
                            pts.push_back(pt.y);
                        }
                        if (pts.size() == 2) {  // (a dot: /InkList wants a line)
                            pts.push_back(pts[0]);
                            pts.push_back(pts[1]);
                        }
                        if (!pts.empty()) {
                            a.strokes.push_back(std::move(pts));
                        }
                        if (a.width == 0) {
                            a.width = s->getWidth();
                        }
                    } else if (e->getType() == ELEMENT_TEXT) {
                        const auto* text = static_cast<const Text*>(e);
                        if (!a.text.empty()) {
                            a.text += "\n";
                        }
                        a.text += text->getText();
                        if (text->isMarkdown() && !linkFolder.empty()) {
                            // Its links, for other viewers (/Link annotations)
                            for (const md::LinkHit& hit: md::linkBoxes(*text)) {
                                LinkSpec link;
                                if (linkFor(hit, linkFolder, link, linkMap)) {
                                    link.page = i;
                                    link.x0 = hit.x;
                                    link.y0 = hit.y;
                                    link.x1 = hit.x + hit.width;
                                    link.y1 = hit.y + hit.height;
                                    out.links.push_back(std::move(link));
                                }
                            }
                        }
                    }
                    if (!colored) {
                        a.color = e->getColor();
                        colored = true;
                    }
                }
                a.x0 = std::max(0.0, a.x0 - MARGIN);
                a.y0 = std::max(0.0, a.y0 - MARGIN);
                a.x1 = std::min(spec.width, a.x1 + MARGIN);
                a.y1 = std::min(spec.height, a.y1 + MARGIN);
                if (a.x1 <= a.x0 || a.y1 <= a.y0) {
                    continue;  // (nothing on the page)
                }
                a.sig = sigOf(h.layerHash(i, li), spec.width, spec.height);
                if (!reuse || !reuse->layers.count(a.sig)) {
                    cairo_pdf_surface_set_size(surface, spec.width, spec.height);
                    cairo_save(cr);
                    // (a highlighter on dark paper lightens: view/PaperTone.h)
                    const xoj::view::PaperToneScope tone(!p->getBackgroundType().isSpecial() &&
                                                         xoj::view::isDarkPaper(p->getBackgroundColor()));
                    xoj::view::LayerView(layer).draw(xoj::view::Context::createDefault(cr));
                    cairo_restore(cr);
                    cairo_show_page(cr);
                    a.drawnPage = drawn++;
                }
                out.annots.push_back(std::move(a));
            }
        }
        cairo_destroy(cr);
        cairo_surface_finish(surface);
        const bool ok = cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
        if (!ok) {
            out.error = std::string("Cairo: ") + cairo_status_to_string(cairo_surface_status(surface));
        }
        cairo_surface_destroy(surface);
        if (drawn == 0) {
            out.drawn.clear();
        }
        if (!ok) {
            return out;
        }
    }
    // The .xopp (of a protected document: in memory, never in a file; PdfEncryption.h)
    const bool secret = PdfEncryption::isProtected(docFile) || PdfEncryption::isProtected(out.bg);
    std::string inMemory;
    if (secret) {
        StringOutputStream mem;
        h.saveTo(&mem, xopp);  // (attached background images still go next to it, into the work folder)
        inMemory = fileio::gzip(mem.data);
        std::fill(mem.data.begin(), mem.data.end(), '\0');
    } else {
        h.saveTo(xopp);
    }
    {
        std::unique_lock lock(doc);
        h.updateDocumentInfo(&doc);
    }
    if (!h.getErrorMessage().empty()) {
        out.error = h.getErrorMessage();
        return out;
    }
    out.xopp = secret ? std::move(inMemory) : bytesOf(xopp);
    std::error_code ec;
    const std::string prefix = std::string(DATA_NAME) + ".";
    for (auto it = fs::directory_iterator(work, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.rfind(prefix, 0) == 0) {
            out.extras.emplace_back(n, bytesOf(it->path()));
        }
    }
    // The recordings, for other apps too (qt/docs/audio.md): "audio-p012-…ogg" with their pages. One whose file is
    // nowhere is left out (its strokes keep their names).
    for (const auto& rec: recordings) {
        TextDocument::Attachment a;
        a.file = audio::find(rec.name, docFile);
        if (a.file.empty()) {
            g_warning("Recording %s not found: not in the PDF", rec.name.c_str());
            continue;
        }
        a.source = rec.name;
        a.name = audio::attachmentName(rec.name, rec.pages);
        a.description = audio::attachmentDescription(rec.name, rec.pages);
        a.mime = audio::MIME;
        a.relationship = "/Supplement";
        a.fixed = true;
        out.attachments.push_back(std::move(a));
    }
    return out;
}

/// A folder for the files of one write, removed with it.
struct WorkDir {
    WorkDir() {
        static std::atomic<unsigned> counter{0};
        path = cacheFolder() / ("write-" + std::to_string(Util::getPid()) + "-" + std::to_string(++counter));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~WorkDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    fs::path path;
};

/// `relationship`: an archive PDF's associated file (PDF/A-3): how it relates to the PDF (/Source, /Supplement).
void addEmbedded(QPDF& pdf, const std::string& name, const std::string& data, const std::string& description,
                 const char* relationship = nullptr, const std::string& mime = {}) {
    auto stream = QPDFEFStreamObjectHelper::createEFStream(pdf, data);
    stream.setSubtype(!mime.empty() ? mime : name == DATA_NAME ? ArchivePdf::XOPP_MIME : "image/png");
    if (relationship) {
        stream.setModDate(pdfDateNow());
    }
    auto spec = QPDFFileSpecObjectHelper::createFileSpec(pdf, name, stream);
    spec.setDescription(description);
    if (relationship) {
        spec.getObjectHandle().replaceKey("/AFRelationship", QPDFObjectHandle::newName(relationship));
    }
    QPDFEmbeddedFileDocumentHelper(pdf).replaceEmbeddedFile(name, spec);
}

/// A recording as an embedded file (its data read from its file when the PDF is written). False if it cannot be read.
bool addEmbeddedFile(QPDF& pdf, const TextDocument::Attachment& a, bool archive) {
    long long size = 0;
    std::string md5;
    if (!measureFile(a.file, size, md5)) {
        return false;
    }
    auto stream = QPDFEFStreamObjectHelper::createEFStream(pdf, fileProvider(a.file));
    stream.setSubtype(a.mime);
    stream.setModDate(pdfDateNow());
    QPDFObjectHandle params = stream.getObjectHandle().getDict().getKey("/Params");
    if (!params.isDictionary()) {
        params = QPDFObjectHandle::newDictionary();
    }
    params.replaceKey("/Size", QPDFObjectHandle::newInteger(size));
    params.replaceKey("/CheckSum", QPDFObjectHandle::newString(md5));
    params.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
    stream.getObjectHandle().getDict().replaceKey("/Params", params);
    auto spec = QPDFFileSpecObjectHelper::createFileSpec(pdf, a.name, stream);
    spec.setDescription(a.description);
    if (archive && !a.relationship.empty()) {
        spec.getObjectHandle().replaceKey("/AFRelationship", QPDFObjectHandle::newName(a.relationship));
    }
    QPDFEmbeddedFileDocumentHelper(pdf).replaceEmbeddedFile(a.name, spec);
    return true;
}

/// A base page we drew: what it shows (an incremental save keeps it while that stays the same).
void markDrawn(QPDFObjectHandle page, const std::string& sig) {
    QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
    mark.replaceKey("/Bg", QPDFObjectHandle::newString(sig));
    page.replaceKey(MARKER, mark);
}

/// The sig of a base page we drew (empty: not one).
std::string drawnSigOf(QPDFObjectHandle page) {
    QPDFObjectHandle mark = page.isDictionary() ? page.getKey(MARKER) : QPDFObjectHandle::newNull();
    QPDFObjectHandle sig = mark.isDictionary() ? mark.getKey("/Bg") : QPDFObjectHandle::newNull();
    return sig.isString() ? sig.getStringValue() : std::string();
}

/// The base pages in document order, in `out` (the background PDF, or an empty one).
/// `withSpace`: the pages with space for notes get larger boxes (not the base pages exported for Xournal++, whose
/// .xopp says where the PDF goes).
std::vector<QPDFObjectHandle> basePages(QPDF& out, QPDF& drawn, const Prepared& prep, bool withSpace) {
    QPDFPageDocumentHelper helper(out);
    helper.pushInheritedAttributesToPage();
    const std::vector<QPDFPageObjectHelper> bgPages = helper.getAllPages();
    const std::vector<QPDFPageObjectHelper> drawnPages =
            prep.drawn.empty() ? std::vector<QPDFPageObjectHelper>() : QPDFPageDocumentHelper(drawn).getAllPages();
    std::vector<QPDFObjectHandle> order;
    std::vector<bool> used(bgPages.size(), false);
    for (const PageSpec& spec: prep.pages) {
        if (spec.pdfPage != npos && spec.pdfPage < bgPages.size()) {
            QPDFPageObjectHelper page = bgPages[spec.pdfPage];
            QPDFObjectHandle h;
            if (!used[spec.pdfPage]) {
                used[spec.pdfPage] = true;
                h = page.getObjectHandle();
            } else {
                h = page.shallowCopyPage().getObjectHandle();  // (a page shown twice)
            }
            if (h.getObjGen() != page.getObjectHandle().getObjGen()) {
                // A page shown twice: its own copies of the other annotations
                QPDFObjectHandle annots = h.getKey("/Annots");
                if (annots.isArray()) {
                    QPDFObjectHandle mine = QPDFObjectHandle::newArray();
                    for (int i = 0; i < annots.getArrayNItems(); ++i) {
                        QPDFObjectHandle a = annots.getArrayItem(i);
                        if (a.isDictionary()) {
                            a = out.makeIndirectObject(a.shallowCopy());
                            a.replaceKey("/P", h);
                        }
                        mine.appendItem(a);
                    }
                    h.replaceKey("/Annots", mine);
                }
            }
            if (withSpace) {
                setSpace(h, spec.space);
            } else {
                restoreBoxes(h);
            }
            order.push_back(h);
        } else {
            helper.addPage(drawnPages.at(spec.drawnPage), false);
            QPDFObjectHandle h = QPDFPageDocumentHelper(out).getAllPages().back().getObjectHandle();
            markDrawn(h, spec.sig);
            if (spec.annotsFrom < bgPages.size()) {
                // A page with a generated background: the annotations other apps put on it stay
                QPDFPageObjectHelper from = bgPages[spec.annotsFrom];
                QPDFObjectHandle annots = from.getObjectHandle().getKey("/Annots");
                if (annots.isArray() && annots.getArrayNItems() > 0) {
                    QPDFObjectHandle mine = QPDFObjectHandle::newArray();
                    for (int i = 0; i < annots.getArrayNItems(); ++i) {
                        QPDFObjectHandle a = annots.getArrayItem(i);
                        if (a.isDictionary()) {
                            a = out.makeIndirectObject(a.shallowCopy());
                            a.replaceKey("/P", h);
                        }
                        mine.appendItem(a);
                    }
                    h.replaceKey("/Annots", mine);
                }
            }
            order.push_back(h);
        }
    }
    QPDFObjectHandle pagesRoot = out.getRoot().getKey("/Pages");
    QPDFObjectHandle kids = QPDFObjectHandle::newArray();
    for (QPDFObjectHandle& h: order) {
        h.replaceKey("/Parent", pagesRoot);
        kids.appendItem(h);
    }
    pagesRoot.replaceKey("/Kids", kids);
    pagesRoot.replaceKey("/Count", QPDFObjectHandle::newInteger(static_cast<long long>(order.size())));
    for (size_t i = 0; i < bgPages.size(); ++i) {
        if (!used[i]) {
            // Bookmarks and links may still point at it: it stays in the file then, without its content
            QPDFObjectHandle page = bgPages[i].getObjectHandle();
            for (const char* key: {"/Contents", "/Resources", "/Annots", "/Thumb"}) {
                if (page.hasKey(key)) {
                    page.removeKey(key);
                }
            }
        }
    }
    out.updateAllPagesCache();
    return order;
}

QPDFObjectHandle colorArray(Color c) {
    QPDFObjectHandle a = QPDFObjectHandle::newArray();
    for (uint8_t v: {c.red, c.green, c.blue}) {
        a.appendItem(QPDFObjectHandle::newReal(std::round(v / 255.0 * 1000) / 1000, 3));
    }
    return a;
}

/// A link of a Markdown box as our /Link annotation on its base page (a direct dictionary), and its name.
QPDFObjectHandle linkDict(const LinkSpec& l, int number, QPDFObjectHandle pageObj, const Prepared& prep, std::string& nm) {
    QPDFPageObjectHelper page(pageObj);
    const double h = prep.pages[l.page].height;
    // Page coordinates (y down) onto the base page, as the drawing is placed (its crop box)
    const QPDFObjectHandle::Rectangle crop = page.getCropBox().getArrayAsRectangle();
    const QPDFObjectHandle::Rectangle r(crop.llx + l.x0, crop.lly + (h - l.y1), crop.llx + l.x1, crop.lly + (h - l.y0));
    QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
    annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    annot.replaceKey("/Subtype", QPDFObjectHandle::newName("/Link"));
    QPDFObjectHandle rect = QPDFObjectHandle::newArray();
    for (double v: {r.llx, r.lly, r.urx, r.ury}) {
        rect.appendItem(real1(v));
    }
    annot.replaceKey("/Rect", rect);
    annot.replaceKey("/Border", QPDFObjectHandle::parse("[0 0 0]"));
    annot.replaceKey("/F", QPDFObjectHandle::newInteger(4));  // print (PDF/A wants it on every annotation)
    nm = std::string(NAME_PREFIX) + "p" + std::to_string(l.page + 1) + "-link" + std::to_string(number);
    annot.replaceKey("/NM", QPDFObjectHandle::newUnicodeString(nm));
    annot.replaceKey("/P", pageObj);
    QPDFObjectHandle action = QPDFObjectHandle::newDictionary();
    if (!l.uri.empty()) {
        action.replaceKey("/S", QPDFObjectHandle::newName("/URI"));
        action.replaceKey("/URI", QPDFObjectHandle::newString(l.uri));
    } else {
        action.replaceKey("/S", QPDFObjectHandle::newName("/GoToR"));
        action.replaceKey("/F", QPDFObjectHandle::newUnicodeString(l.file));
        QPDFObjectHandle dest = QPDFObjectHandle::newArray();
        dest.appendItem(QPDFObjectHandle::newInteger(l.destPage));
        dest.appendItem(QPDFObjectHandle::newName("/Fit"));
        action.replaceKey("/D", dest);
        action.replaceKey("/NewWindow", QPDFObjectHandle::newBool(true));
    }
    annot.replaceKey("/A", action);
    annot.replaceKey(MARKER, QPDFObjectHandle::newDictionary());
    return annot;
}

/// The marker's record of a layer: what its drawing shows, the drawing, and its annotation (null in an archive PDF). An
/// incremental save keeps what did not change without reading the pages (see Appending).
QPDFObjectHandle layerRecord(const std::string& sig, QPDFObjectHandle form, QPDFObjectHandle annot) {
    QPDFObjectHandle r = QPDFObjectHandle::newArray();
    r.appendItem(QPDFObjectHandle::newString(sig));
    r.appendItem(form);
    r.appendItem(annot);
    return r;
}

/// A new array of the page's annotations with this one added (its old one may be shared with another page).
void appendAnnot(QPDFObjectHandle pageObj, QPDFObjectHandle annot) {
    QPDFObjectHandle old = pageObj.getKey("/Annots");
    QPDFObjectHandle annots = QPDFObjectHandle::newArray();
    if (old.isArray()) {
        for (int i = 0; i < old.getArrayNItems(); ++i) {
            annots.appendItem(old.getArrayItem(i));
        }
    }
    annots.appendItem(annot);
    pageObj.replaceKey("/Annots", annots);
}

/// The links of the Markdown boxes as /Link annotations (ours: removed and written again with the rest), with their
/// hashes in `hashes`.
void annotateLinks(QPDF& out, const Prepared& prep, const std::vector<QPDFObjectHandle>& order, QPDFObjectHandle hashes) {
    std::map<size_t, int> perPage;
    for (const LinkSpec& l: prep.links) {
        QPDFObjectHandle pageObj = order.at(l.page);
        std::string nm;
        QPDFObjectHandle annot = linkDict(l, ++perPage[l.page], pageObj, prep, nm);
        hashes.replaceKey("/" + nm, QPDFObjectHandle::newString(hashOf(annot)));
        appendAnnot(pageObj, out.makeIndirectObject(annot));
    }
}

/// A layer's drawing as a Form XObject of `out`, placed on its base page by its /Matrix (the drawn page is in PDF space,
/// y up; the base page's crop box, its rotation undone), with the box it covers there.
struct Placed {
    QPDFObjectHandle form;
    QPDFObjectHandle::Rectangle rect;
    QPDFMatrix cm;
    QPDFObjectHandle::Rectangle box;  ///< the form's /BBox
};

/// Where a drawing of a page of this size goes on the base page `pageObj` (see Placed), and the box of the layer.
Placed placementOf(QPDFObjectHandle pageObj, const AnnotSpec& a, double w, double h) {
    // (as for the Form XObject of a drawn page of this size, whose /BBox is its media box and without a /Matrix)
    static thread_local std::unique_ptr<QPDF> scratch;
    if (!scratch) {
        scratch = std::make_unique<QPDF>();
        scratch->emptyPDF();
    }
    QPDFObjectHandle fake = QPDFObjectHandle::newStream(scratch.get(), "");
    fake.getDict().replaceKey("/BBox", QPDFObjectHandle::newArray(QPDFObjectHandle::Rectangle(0, 0, w, h)));
    QPDFPageObjectHelper page(pageObj);
    const QPDFObjectHandle::Rectangle crop = page.getCropBox().getArrayAsRectangle();
    Placed p;
    p.cm = page.getMatrixForFormXObjectPlacement(fake, crop, true, true, true);
    p.box = QPDFObjectHandle::Rectangle(std::floor(a.x0 * 10) / 10, std::floor((h - a.y1) * 10) / 10,
                                        std::ceil(a.x1 * 10) / 10, std::ceil((h - a.y0) * 10) / 10);
    p.rect = p.cm.transformRectangle(p.box);
    return p;
}

/// The drawing of a layer (a page of `drawn`) as a Form XObject of `out`, placed as `p` says.
QPDFObjectHandle formOf(QPDF& out, std::vector<QPDFPageObjectHelper>& drawnPages, const AnnotSpec& a, const Placed& p) {
    QPDFObjectHandle form = drawnPages.at(a.drawnPage).getFormXObjectForPage(false);
    QPDFObjectHandle group = form.getDict().getKey("/Group");
    if (group.isDictionary() && group.hasKey("/I")) {
        group.removeKey("/I");  // (not isolated: the highlighter multiplies with the page, as upstream's export)
    }
    QPDFObjectHandle local = out.copyForeignObject(form);
    local.getDict().replaceKey("/BBox", QPDFObjectHandle::newArray(p.box));
    local.getDict().replaceKey("/Matrix", QPDFObjectHandle::newArray(p.cm));
    return local;
}

Placed placeLayer(QPDF& out, std::vector<QPDFPageObjectHelper>& drawnPages, const AnnotSpec& a,
                  QPDFObjectHandle pageObj, double w, double h) {
    Placed p = placementOf(pageObj, a, w, h);
    p.form = formOf(out, drawnPages, a, p);
    return p;
}

/// An archive PDF: our layers merged into the content of their base pages. The page's own content streams stay as
/// they are; a stream "q" goes before them and a stream after them draws our layers (each a Form XObject "/XqtInkN"
/// in the page's resources, the same drawing as the /AP of a hybrid PDF's annotation). Both streams carry our key
/// with the XObjects and layers they add, so the reader removes exactly them (unflatten). Returns the layers' names.
std::vector<std::string> flatten(QPDF& out, QPDF& drawn, const Prepared& prep, const std::vector<QPDFObjectHandle>& order,
                                 QPDFObjectHandle record) {
    std::vector<std::string> names;
    if (prep.annots.empty()) {
        return names;
    }
    std::vector<QPDFPageObjectHelper> drawnPages = QPDFPageDocumentHelper(drawn).getAllPages();
    std::map<size_t, std::vector<const AnnotSpec*>> byPage;
    for (const AnnotSpec& a: prep.annots) {
        byPage[a.page].push_back(&a);
    }
    for (const auto& [pageNo, specs]: byPage) {
        QPDFObjectHandle pageObj = order.at(pageNo);
        const double h = prep.pages[pageNo].height;
        // The page's own resources and XObjects (the dictionaries may be shared with other pages)
        QPDFObjectHandle res = pageObj.getKey("/Resources");
        res = res.isDictionary() ? res.shallowCopy() : QPDFObjectHandle::newDictionary();
        QPDFObjectHandle xobjects = res.getKey("/XObject");
        xobjects = xobjects.isDictionary() ? xobjects.shallowCopy() : QPDFObjectHandle::newDictionary();
        res.replaceKey("/XObject", xobjects);
        pageObj.replaceKey("/Resources", res);
        std::string draw = "Q\n";
        QPDFObjectHandle xnames = QPDFObjectHandle::newArray();
        QPDFObjectHandle layers = QPDFObjectHandle::newArray();
        QPDFObjectHandle sigs = QPDFObjectHandle::newArray();
        for (const AnnotSpec* a: specs) {
            const Placed placed = placeLayer(out, drawnPages, *a, pageObj, prep.pages[pageNo].width, h);
            int suffix = 1;
            const std::string name = res.getUniqueResourceName("/XqtInk", suffix);
            xobjects.replaceKey(name, placed.form);
            draw += "q " + name + " Do Q\n";
            xnames.appendItem(QPDFObjectHandle::newName(name));
            const std::string nm = nameOf(a->page, a->layer);
            layers.appendItem(QPDFObjectHandle::newUnicodeString(nm));
            sigs.appendItem(QPDFObjectHandle::newString(a->sig));
            names.push_back(nm);
            record.replaceKey("/" + nm, layerRecord(a->sig, placed.form, QPDFObjectHandle::newNull()));
        }
        QPDFObjectHandle before = QPDFObjectHandle::newStream(&out, "q\n");
        before.getDict().replaceKey(MARKER, QPDFObjectHandle::newDictionary());
        QPDFObjectHandle after = QPDFObjectHandle::newStream(&out, draw);
        QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
        mark.replaceKey("/XObjects", xnames);
        mark.replaceKey("/Layers", layers);
        mark.replaceKey("/Sigs", sigs);
        after.getDict().replaceKey(MARKER, mark);
        QPDFObjectHandle old = pageObj.getKey("/Contents");
        QPDFObjectHandle contents = QPDFObjectHandle::newArray();
        contents.appendItem(before);
        if (old.isArray()) {
            for (int i = 0; i < old.getArrayNItems(); ++i) {
                contents.appendItem(old.getArrayItem(i));
            }
        } else if (old.isStream()) {
            contents.appendItem(old);
        }
        contents.appendItem(after);
        pageObj.replaceKey("/Contents", contents);
    }
    return names;
}

/// Our annotation of a layer on its base page (a direct dictionary; the caller adds /M), placed as `p` says, its
/// appearance `form`.
QPDFObjectHandle annotDict(const AnnotSpec& a, QPDFObjectHandle pageObj, double h, const Placed& p, QPDFObjectHandle form) {
    const QPDFMatrix& cm = p.cm;
    const QPDFObjectHandle::Rectangle r = p.rect;
    QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
    annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
    const bool ink = !a.strokes.empty();
    annot.replaceKey("/Subtype", QPDFObjectHandle::newName(ink ? "/Ink" : "/Stamp"));
    QPDFObjectHandle rect = QPDFObjectHandle::newArray();
    for (double v: {r.llx, r.lly, r.urx, r.ury}) {
        rect.appendItem(real1(v));
    }
    annot.replaceKey("/Rect", rect);
    annot.replaceKey("/NM", QPDFObjectHandle::newUnicodeString(nameOf(a.page, a.layer)));
    annot.replaceKey("/F", QPDFObjectHandle::newInteger(4));  // print
    annot.replaceKey("/P", pageObj);
    annot.replaceKey("/C", colorArray(a.color));
    if (ink) {
        QPDFObjectHandle inkList = QPDFObjectHandle::newArray();
        for (const auto& s: a.strokes) {
            QPDFObjectHandle path = QPDFObjectHandle::newArray();
            for (size_t k = 0; k + 1 < s.size(); k += 2) {
                double x = 0, y = 0;
                cm.transform(s[k], h - s[k + 1], x, y);
                path.appendItem(real1(x));
                path.appendItem(real1(y));
            }
            inkList.appendItem(path);
        }
        annot.replaceKey("/InkList", inkList);
        annot.replaceKey("/BS", QPDFObjectHandle::parse("<< /Type /Border /S /S >>"));
        annot.getKey("/BS").replaceKey("/W", QPDFObjectHandle::newReal(a.width, 2));
    } else {
        annot.replaceKey("/Name", QPDFObjectHandle::newName("/XournalQt"));
    }
    if (!a.text.empty()) {
        annot.replaceKey("/Contents", QPDFObjectHandle::newUnicodeString(a.text));
    }
    QPDFObjectHandle ap = QPDFObjectHandle::newDictionary();
    ap.replaceKey("/N", form);
    annot.replaceKey("/AP", ap);
    QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
    mark.replaceKey("/Page", QPDFObjectHandle::newInteger(static_cast<long long>(a.page + 1)));
    mark.replaceKey("/Layer", QPDFObjectHandle::newInteger(static_cast<long long>(a.layer + 1)));
    mark.replaceKey("/Sig", QPDFObjectHandle::newString(a.sig));
    annot.replaceKey(MARKER, mark);
    return annot;
}

/// Our annotations onto the base pages; returns the marker's /Annots (name -> hash).
QPDFObjectHandle annotate(QPDF& out, QPDF& drawn, const Prepared& prep, const std::vector<QPDFObjectHandle>& order,
                          QPDFObjectHandle record) {
    QPDFObjectHandle hashes = QPDFObjectHandle::newDictionary();
    if (prep.annots.empty()) {
        annotateLinks(out, prep, order, hashes);
        return hashes;
    }
    std::vector<QPDFPageObjectHelper> drawnPages = QPDFPageDocumentHelper(drawn).getAllPages();
    const std::string now = pdfDateNow();
    for (const AnnotSpec& a: prep.annots) {
        QPDFObjectHandle pageObj = order.at(a.page);
        const Placed placed = placeLayer(out, drawnPages, a, pageObj, prep.pages[a.page].width, prep.pages[a.page].height);
        QPDFObjectHandle annot = annotDict(a, pageObj, prep.pages[a.page].height, placed, placed.form);
        annot.replaceKey("/M", QPDFObjectHandle::newString(now));
        hashes.replaceKey("/" + nameOf(a.page, a.layer), QPDFObjectHandle::newString(hashOf(annot)));
        annot = out.makeIndirectObject(annot);
        appendAnnot(pageObj, annot);
        record.replaceKey("/" + nameOf(a.page, a.layer), layerRecord(a.sig, placed.form, annot));
    }
    annotateLinks(out, prep, order, hashes);
    return hashes;
}

enum class Mode {
    Plain,    ///< the base pages only (the export for Xournal++)
    Hybrid,   ///< and our annotations, data and marker
    Archive,  ///< and our layers merged into the pages, links, data as the source, marker; PDF/A-3b
};

/// The pages (their places) whose boxes we made larger for space for notes (an incremental save reads only those
/// again).
QPDFObjectHandle spacesList(const Prepared& prep) {
    QPDFObjectHandle list = QPDFObjectHandle::newArray();
    for (size_t i = 0; i < prep.pages.size(); ++i) {
        if (prep.pages[i].pdfPage != npos && !prep.pages[i].space.empty()) {
            list.appendItem(QPDFObjectHandle::newInteger(static_cast<long long>(i)));
        }
    }
    return list;
}

/// The document's bookmarks for the outline (qt/docs/bookmarks.md): the base page of each bookmarked page.
std::vector<PdfBookmarks::Entry> bookmarksOf(const Prepared& prep, const std::vector<QPDFObjectHandle>& order) {
    std::vector<PdfBookmarks::Entry> entries;
    for (size_t i = 0; i < prep.pages.size() && i < order.size(); ++i) {
        if (!prep.pages[i].bookmark.empty()) {
            entries.push_back({order[i], prep.pages[i].bookmark});
        }
    }
    return entries;
}

/// The PDF with the base pages (and, by `mode`, our drawing, data and marker), written to `target`.
/// Version history (PdfHistory.h): what our marker says about it after this save.
struct HistoryMark {
    std::vector<PdfHistory::Version> versions;  ///< oldest first; the last one is the version written
    uint64_t start = 0;                         ///< where the revision with this marker begins in the file
};

/// Put the history into our marker (`mark` null: none, the keys go); `stream` makes the /Versions stream.
void putHistory(QPDFObjectHandle marker, const HistoryMark* mark,
                const std::function<QPDFObjectHandle(const std::string&)>& stream) {
    for (const char* key: {"/History", "/Versions"}) {
        if (marker.hasKey(key)) {
            marker.removeKey(key);
        }
    }
    if (!mark || mark->versions.empty()) {
        return;
    }
    QPDFObjectHandle h = QPDFObjectHandle::newDictionary();
    h.replaceKey("/On", QPDFObjectHandle::newBool(true));
    h.replaceKey("/Count", QPDFObjectHandle::newInteger(static_cast<long long>(mark->versions.size())));
    h.replaceKey("/Latest", QPDFObjectHandle::newString(mark->versions.back().date));
    h.replaceKey("/Start", QPDFObjectHandle::newInteger(static_cast<long long>(mark->start)));
    marker.replaceKey("/History", h);
    marker.replaceKey("/Versions", stream(PdfHistory::toJsonLines(mark->versions)));
}

/// `writeTo`: written there instead of `target` (whose keywords it keeps; a whole document appended to it).
Result assemble(const Prepared& prep, const fs::path& target, Mode mode, const std::string& xoppExport = {},
                const std::string& title = {}, const HistoryMark* history = nullptr, const fs::path& writeTo = {}) {
    const bool hybrid = mode != Mode::Plain;
    const bool archive = mode == Mode::Archive;
    Result r;
    Steps step;
    QPDF out;
    out.setSuppressWarnings(true);
    const bool fromBg = std::any_of(prep.pages.begin(), prep.pages.end(),
                                    [](auto& p) { return p.pdfPage != npos || p.annotsFrom != npos; });
    if (fromBg) {
        PdfEncryption::openQpdf(out, prep.bg);
        if (out.getRoot().hasKey(MARKER)) {
            strip(out);  // (a hybrid PDF as the background; a PDF without our marker has no annotations of ours)
        }
    } else {
        out.emptyPDF();
    }
    step("open and strip the PDF");
    // A hybrid PDF is never a merged PDF the app may rewrite; the base pages exported for Xournal++ are one (Own: the
    // next export replaces it)
    MergedPdf::mark(out, hybrid ? MergedPdf::Kind::None : MergedPdf::Kind::Own);
    QPDF drawn;
    drawn.setSuppressWarnings(true);
    if (!prep.drawn.empty()) {
        drawn.processMemoryFile("drawn by xournal-qt", prep.drawn.data(), prep.drawn.size());
        QPDFPageDocumentHelper(drawn).pushInheritedAttributesToPage();
    }
    const std::vector<QPDFObjectHandle> order = basePages(out, drawn, prep, hybrid);
    r.pages = order.size();
    step("base pages");
    PdfBookmarks::write(out, bookmarksOf(prep, order));  // (also the base pages for Xournal++: as they are now)
    if (hybrid) {
        QPDFObjectHandle hashes;
        QPDFObjectHandle flattened = QPDFObjectHandle::newArray();
        QPDFObjectHandle record = QPDFObjectHandle::newDictionary();
        if (archive) {
            for (const auto& nm: flatten(out, drawn, prep, order, record)) {
                flattened.appendItem(QPDFObjectHandle::newUnicodeString(nm));
            }
            r.flattened = static_cast<size_t>(flattened.getArrayNItems());
            hashes = QPDFObjectHandle::newDictionary();
            annotateLinks(out, prep, order, hashes);
        } else {
            hashes = annotate(out, drawn, prep, order, record);
            r.annotations = prep.annots.size();
        }
        step("annotations");
        addEmbedded(out, DATA_NAME, prep.xopp,
                    archive ? "The Xournal++ document of this PDF, with the ink editable (xournal-qt archive PDF)"
                            : "The Xournal++ document of this PDF (xournal-qt hybrid PDF)",
                    archive ? "/Source" : nullptr);
        QPDFObjectHandle files = QPDFObjectHandle::newArray();
        for (const auto& [name, data]: prep.extras) {
            addEmbedded(out, name, data, "A file of the Xournal++ document of this PDF", archive ? "/Supplement" : nullptr);
            files.appendItem(QPDFObjectHandle::newUnicodeString(name));
        }
        QPDFObjectHandle audioList = QPDFObjectHandle::newArray();
        for (const auto& a: prep.attachments) {  // (for other apps: a text document's "name.md"; listed with the
            if (!a.source.empty()) {             // images, so the clean copy never carries them)
                if (addEmbeddedFile(out, a, archive)) {  // (a recording: listed in /Audio with its name in the document)
                    audioList.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
                    audioList.appendItem(QPDFObjectHandle::newUnicodeString(a.source));
                }
                continue;
            }
            addEmbedded(out, a.name, a.data, a.description,
                        archive && !a.relationship.empty() ? a.relationship.c_str() : nullptr, a.mime);
            files.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
        }
        // The handwriting as invisible text, for other PDF viewers (InkTextLayer.h)
        QPDFObjectHandle inkSigs = QPDFObjectHandle::newArray();
        QPDFObjectHandle inkFont = QPDFObjectHandle::newNull();
        for (size_t i = 0; i < order.size() && i < prep.pages.size(); ++i) {
            const double w = prep.pages[i].width, h = prep.pages[i].height;
            const std::vector<InkTextLayer::Word>* words = i < prep.inkWords.size() ? &prep.inkWords[i] : nullptr;
            const std::string sig = words ? inkSigOf(*words, w, h) : std::string();
            inkSigs.appendItem(QPDFObjectHandle::newString(sig));
            if (sig.empty()) {
                continue;
            }
            if (!inkFont.isIndirect()) {
                inkFont = makeInkFont([&](QPDFObjectHandle o) { return out.makeIndirectObject(o); },
                                      [&](QPDFObjectHandle dict, const std::string& data) {
                                          QPDFObjectHandle stream = QPDFObjectHandle::newStream(&out, data);
                                          for (const auto& k: dict.getKeys()) {
                                              stream.getDict().replaceKey(k, dict.getKey(k));
                                          }
                                          return stream;
                                      });
            }
            const std::string cm = placementOf(order[i], AnnotSpec{}, w, h).cm.unparse();
            QPDFObjectHandle stream = QPDFObjectHandle::newStream(&out, InkTextLayer::contentOf(*words, h, cm));
            QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
            mark.replaceKey("/InkText", QPDFObjectHandle::newString(sig));
            stream.getDict().replaceKey(MARKER, mark);
            putInkText(order[i], stream, inkFont);
        }
        QPDFObjectHandle marker = QPDFObjectHandle::newDictionary();
        marker.replaceKey("/Version", QPDFObjectHandle::newInteger(archive ? ARCHIVE_FORMAT_VERSION : FORMAT_VERSION));
        marker.replaceKey("/InkText", inkSigs);
        if (inkFont.isIndirect()) {
            marker.replaceKey("/InkFont", inkFont);
        }
        if (archive) {
            marker.replaceKey("/Archive", QPDFObjectHandle::newBool(true));
            marker.replaceKey("/Flattened", flattened);
        }
        marker.replaceKey("/Data", QPDFObjectHandle::newUnicodeString(DATA_NAME));
        marker.replaceKey("/Files", files);
        if (audioList.getArrayNItems() > 0) {
            marker.replaceKey("/Audio", audioList);
        }
        marker.replaceKey("/Annots", hashes);
        QPDFObjectHandle drawnList = QPDFObjectHandle::newArray();  // (the base pages we drew: kept while the same)
        for (size_t i = 0; i < prep.pages.size(); ++i) {
            if (prep.pages[i].pdfPage == npos) {
                drawnList.appendItem(QPDFObjectHandle::newInteger(static_cast<long long>(i)));
            }
        }
        marker.replaceKey("/Drawn", drawnList);
        marker.replaceKey("/Spaces", spacesList(prep));
        marker.replaceKey("/Layers", record);
        if (!xoppExport.empty()) {
            marker.replaceKey("/XoppExport", QPDFObjectHandle::newUnicodeString(xoppExport));
        }
        putHistory(marker, history, [&](const std::string& data) { return QPDFObjectHandle::newStream(&out, data); });
        out.getRoot().replaceKey(MARKER, out.makeIndirectObject(marker));
    }
    QPDFObjectHandle trailer = out.getTrailer();
    QPDFObjectHandle info = trailer.getKey("/Info");
    if (!info.isDictionary()) {
        info = out.makeIndirectObject(QPDFObjectHandle::newDictionary());
        trailer.replaceKey("/Info", info);
    }
    info.replaceKey("/Producer", QPDFObjectHandle::newString(std::string(PROJECT_STRING) + " + QPDF " + QPDF_VERSION));
    info.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
    if (hybrid) {
        // The file written over keeps its keywords: its tags (qt/docs/tags.md; given to the file, not to the document,
        // so a background without them would drop them)
        if (std::error_code ec; fs::exists(target, ec) && !fs::equivalent(target, prep.bg, ec)) {
            const QString keywords = pdfkeywords::read(target, /*session=*/true).info;
            if (!keywords.isEmpty()) {
                info.replaceKey("/Keywords", QPDFObjectHandle::newUnicodeString(keywords.toStdString()));
            }
        }
    }
    step("annotations, data, marker");
    ArchiveWrite how;
    how.encryption = &prep.encryption;
    if (!archive) {
        ArchivePdf::dropPdfAClaim(out);  // (a hybrid PDF is never PDF/A, even when its source PDF was)
    }
    if (archive) {
        ArchivePdf::Metadata meta;
        meta.fallbackTitle = title;
        const ArchivePdf::Report report = ArchivePdf::conform(out, meta);
        r.pdfa = report.pdfa;
        r.notPdfA = report.problems;
        r.adjusted = report.adjusted;
        how.on = true;
        how.recompress = report.recompress;
        step("PDF/A");
    }
    writePdfTo(out, writeTo.empty() ? target : writeTo, how);
    step("write");
    r.ok = true;
    return r;
}

// --- saving again: an incremental update ----------------------------------------------------------------------------

/// The existing hybrid PDF an incremental save appends to (opened before the document is prepared: what it has
/// already need not be drawn again).
struct Existing {
    std::unique_ptr<QPDF> q = std::make_unique<QPDF>();
    IncrementalPdf::Tail tail;
    std::unique_ptr<IncrementalPdf::Update> update;
    QPDFObjectHandle root, pagesRoot, marker;
    std::vector<QPDFObjectHandle> tree;  ///< its pages, in order
    std::map<std::string, std::vector<QPDFObjectHandle>> drawnBySig;  ///< base pages we drew, by what they show
    std::map<std::string, QPDFObjectHandle> formBySig;                ///< drawings of layers, by what they show
    std::set<QPDFObjGen> ours;  ///< the pages our annotations or layers are on (by the marker's names)
    /// The marker's record of our layers (name -> what it shows, its record): pages whose layers are as recorded are
    /// not read at all
    struct Recorded {
        std::string sig;
        QPDFObjectHandle record;
    };
    std::map<std::string, Recorded> layers;
    Reuse reuse;
    long long base = 0;     ///< its size when it was last written in full
    long long updates = 0;  ///< incremental updates since
};

/// Whether an annotation belongs to another app (not ours).
bool isForeign(QPDFObjectHandle a) { return !(a.isDictionary() && isOurs(a)); }

std::vector<QPDFObjectHandle> annotsOf(QPDFObjectHandle page) {
    QPDFObjectHandle annots = page.getKey("/Annots");
    return annots.isArray() ? annots.getArrayAsVector() : std::vector<QPDFObjectHandle>();
}

std::string sigOfAnnot(QPDFObjectHandle a) {
    QPDFObjectHandle mark = a.isDictionary() ? a.getKey(MARKER) : QPDFObjectHandle::newNull();
    QPDFObjectHandle sig = mark.isDictionary() ? mark.getKey("/Sig") : QPDFObjectHandle::newNull();
    return sig.isString() ? sig.getStringValue() : std::string();
}

/// Our marked content streams of an archive PDF's page: the "q" before its content and the one drawing our layers.
struct InkStreams {
    int before = -1, after = -1;  ///< their places in /Contents
    QPDFObjectHandle mark;       ///< the mark of `after`
};
InkStreams inkStreamsOf(const std::vector<QPDFObjectHandle>& contents) {
    InkStreams ink;
    for (int i = 0; i < static_cast<int>(contents.size()); ++i) {
        QPDFObjectHandle c = contents[static_cast<size_t>(i)];
        QPDFObjectHandle mark = c.isStream() ? c.getDict().getKey(MARKER) : QPDFObjectHandle::newNull();
        if (!mark.isDictionary() || mark.hasKey("/InkText")) {
            continue;  // (the text layer of the handwriting is not the drawing)
        }
        if (mark.hasKey("/Layers")) {
            ink.after = i;
            ink.mark = mark;
        } else {
            ink.before = i;
        }
    }
    return ink;
}

std::vector<QPDFObjectHandle> contentsOf(QPDFObjectHandle page) {
    QPDFObjectHandle c = page.getKey("/Contents");
    if (c.isArray()) {
        return c.getArrayAsVector();
    }
    return c.isStream() ? std::vector<QPDFObjectHandle>{c} : std::vector<QPDFObjectHandle>();
}

std::vector<std::string> stringsOf(QPDFObjectHandle array) {
    std::vector<std::string> out;
    for (int i = 0; array.isArray() && i < array.getArrayNItems(); ++i) {
        QPDFObjectHandle v = array.getArrayItem(i);
        out.push_back(v.isString() ? v.getStringValue() : v.isName() ? v.getName() : std::string());
    }
    return out;
}

/// Open `target` for an incremental update from `rev` (nullptr and `why`: it cannot be appended to).
std::unique_ptr<Existing> openExisting(const fs::path& target, const Revision& rev, bool archive, std::string& why) {
    if (stampOf(target) != rev.stamp) {
        why = "the file is not the version last written or opened";
        return nullptr;
    }
    auto e = std::make_unique<Existing>();
    if (!IncrementalPdf::readTail(target, e->tail, why)) {
        return nullptr;
    }
    QPDF& q = *e->q;
    Steps step;
    q.setSuppressWarnings(true);
    PdfEncryption::openQpdf(q, target);
    step("  read the file");
    e->update = std::make_unique<IncrementalPdf::Update>(q);  // (before anything is changed)
    step("  count its objects");
    if (PdfEncryption::Encrypter enc(q); enc.encrypted() && !enc.supported()) {
        why = "the file is encrypted: " + enc.why();  // (AES-256 files are appended to, encrypted: PdfEncryption.h)
        return nullptr;
    }
    e->root = q.getRoot();
    e->marker = e->root.getKey(MARKER);
    if (!e->marker.isDictionary()) {
        why = "the file has no marker";
        return nullptr;
    }
    QPDFObjectHandle isArchive = e->marker.getKey("/Archive");
    if ((isArchive.isBool() && isArchive.getBoolValue()) != archive) {
        why = "the file is of the other kind";
        return nullptr;
    }
    if (QPDFObjectHandle v = e->marker.getKey("/Version");
        !v.isInteger() || v.getIntValue() != (archive ? ARCHIVE_FORMAT_VERSION : FORMAT_VERSION)) {
        why = "the file's format version differs";
        return nullptr;
    }
    // A flat page tree, as we write it: its root lists every page and passes nothing on to them
    e->pagesRoot = e->root.getKey("/Pages");
    QPDFObjectHandle kids = e->pagesRoot.isDictionary() ? e->pagesRoot.getKey("/Kids") : QPDFObjectHandle::newNull();
    if (!e->pagesRoot.isIndirect() || !kids.isArray()) {
        why = "the page tree is not ours";
        return nullptr;
    }
    for (const char* key: {"/Resources", "/MediaBox", "/CropBox", "/Rotate"}) {
        if (e->pagesRoot.hasKey(key)) {
            why = "the page tree passes attributes on";
            return nullptr;
        }
    }
    // (the pages themselves are not read: in a long PDF written with object streams that would mean reading most of
    // the file; only the ones of ours are)
    std::set<QPDFObjGen> seen;
    for (QPDFObjectHandle kid: kids.getArrayAsVector()) {
        if (!kid.isIndirect() || !seen.insert(kid.getObjGen()).second) {
            why = "the page tree is not flat";
            return nullptr;
        }
        e->tree.push_back(kid);
    }
    if (QPDFObjectHandle count = e->pagesRoot.getKey("/Count");
        !count.isInteger() || count.getIntValue() != static_cast<long long>(e->tree.size())) {
        why = "the page tree is not flat";
        return nullptr;
    }
    step("  its page tree");
    QPDFObjectHandle base = e->marker.getKey("/Base");
    e->base = base.isInteger() && base.getIntValue() > 0 ? base.getIntValue() : static_cast<long long>(e->tail.size);
    QPDFObjectHandle updates = e->marker.getKey("/Updates");
    e->updates = updates.isInteger() ? updates.getIntValue() : 0;
    // The pages with our annotations or layers: by their names in the marker ("xopp:p3-l1": page 3 of the page tree
    // as written). Only those are looked into (a long PDF has many annotations of its own, links mostly)
    {
        std::vector<std::string> names;
        if (QPDFObjectHandle hashes = e->marker.getKey("/Annots"); hashes.isDictionary()) {
            for (const auto& k: hashes.getKeys()) {
                names.push_back(k.substr(1));
            }
        }
        for (const auto& n: stringsOf(e->marker.getKey("/Flattened"))) {
            names.push_back(n);
        }
        const std::string prefix = std::string(NAME_PREFIX) + "p";
        for (const auto& n: names) {
            unsigned long page = 0;
            if (n.rfind(prefix, 0) == 0 && std::sscanf(n.c_str() + prefix.size(), "%lu", &page) == 1 && page >= 1 &&
                page <= e->tree.size()) {
                e->ours.insert(e->tree[page - 1].getObjGen());
            }
        }
    }
    // What it has already: the base pages we drew (the marker lists them), the drawings of the layers
    {
        QPDFObjectHandle drawnPages = e->marker.getKey("/Drawn");
        for (int i = 0; drawnPages.isArray() && i < drawnPages.getArrayNItems(); ++i) {
            QPDFObjectHandle n = drawnPages.getArrayItem(i);
            if (n.isInteger() && n.getIntValue() >= 0 && n.getIntValue() < static_cast<long long>(e->tree.size())) {
                QPDFObjectHandle page = e->tree[static_cast<size_t>(n.getIntValue())];
                if (const std::string sig = drawnSigOf(page); !sig.empty()) {
                    e->drawnBySig[sig].push_back(page);
                    e->reuse.backgrounds.insert(sig);
                }
            }
        }
    }
    step("  its drawn pages");
    QPDFObjectHandle record = e->marker.getKey("/Layers");
    if (!record.isDictionary()) {
        why = "no record of our layers";
        return nullptr;
    }
    for (const auto& k: record.getKeys()) {
        QPDFObjectHandle r = record.getKey(k);
        if (!r.isArray() || r.getArrayNItems() < 3) {
            continue;
        }
        QPDFObjectHandle sig = r.getArrayItem(0), form = r.getArrayItem(1);
        if (!sig.isString() || !form.isIndirect() || !e->update->has(form.getObjGen())) {
            continue;
        }
        e->formBySig.emplace(sig.getStringValue(), form);  // (not read yet)
        e->reuse.layers.insert(sig.getStringValue());
        e->layers[k.substr(1)] = {sig.getStringValue(), r};
    }
    step("  our layers");
    return e;
}

bool near(QPDFObjectHandle array, const std::vector<double>& values) {
    if (!array.isArray() || array.getArrayNItems() != static_cast<int>(values.size())) {
        return false;
    }
    for (size_t i = 0; i < values.size(); ++i) {
        QPDFObjectHandle v = array.getArrayItem(static_cast<int>(i));
        if (!v.isNumber() || std::abs(v.getNumericValue() - values[i]) > 1e-3) {
            return false;
        }
    }
    return true;
}

bool placedAs(QPDFObjectHandle dict, const Placed& p) {
    const QPDFMatrix& m = p.cm;
    return near(dict.getKey("/Matrix"), {m.a, m.b, m.c, m.d, m.e, m.f}) &&
           near(dict.getKey("/BBox"), {p.box.llx, p.box.lly, p.box.urx, p.box.ury});
}

/// A drawing the file has, placed as `p` says: itself, or (placed elsewhere) a copy of it.
QPDFObjectHandle placedForm(IncrementalPdf::Update& u, QPDFObjectHandle form, const Placed& p) {
    if (placedAs(u.streamDictionary(form), p)) {
        return form;
    }
    QPDFObjectHandle copy = u.copyStream(form);
    u.streamDictionary(copy).replaceKey("/BBox", QPDFObjectHandle::newArray(p.box));
    u.streamDictionary(copy).replaceKey("/Matrix", QPDFObjectHandle::newArray(p.cm));
    return copy;
}

/// The dictionary `to` becomes `from` (the same object, written again).
void replaceAll(QPDFObjectHandle to, QPDFObjectHandle from) {
    for (const auto& k: to.getKeys()) {
        if (!from.hasKey(k)) {
            to.removeKey(k);
        }
    }
    for (const auto& k: from.getKeys()) {
        to.replaceKey(k, from.getKey(k));
    }
}

/// The name tree of the embedded files, and what holds it, are about to change.
void touchNames(IncrementalPdf::Update& u, QPDFObjectHandle root) {
    u.touch(root);
    QPDFObjectHandle names = root.getKey("/Names");
    if (!names.isDictionary()) {
        return;
    }
    u.touch(names);
    std::function<void(QPDFObjectHandle, int)> walk = [&](QPDFObjectHandle node, int depth) {
        if (!node.isDictionary() || depth > 16) {
            return;
        }
        u.touch(node);
        QPDFObjectHandle kids = node.getKey("/Kids");
        for (int i = 0; kids.isArray() && i < kids.getArrayNItems(); ++i) {
            walk(kids.getArrayItem(i), depth + 1);
        }
    };
    walk(names.getKey("/EmbeddedFiles"), 0);
}

/// The embedded document as a new file specification in the name tree (whose nodes are touched here): after an older
/// version's .xopp became a delta (version history), and in a version cut out of the file.
void addDataSpec(QPDF& q, IncrementalPdf::Update& u, const std::string& name, const std::string& xopp) {
    touchNames(u, q.getRoot());
    QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
    dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
    dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/" + std::string(ArchivePdf::XOPP_MIME)));
    QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
    params.replaceKey("/Size", QPDFObjectHandle::newInteger(static_cast<long long>(xopp.size())));
    params.replaceKey("/CheckSum", QPDFObjectHandle::newString(fileio::md5Of(xopp)));
    dict.replaceKey("/Params", params);
    QPDFObjectHandle stream = u.addStream(dict, xopp);
    QPDFObjectHandle ef = QPDFObjectHandle::newDictionary();
    ef.replaceKey("/F", stream);
    ef.replaceKey("/UF", stream);
    QPDFObjectHandle spec = QPDFObjectHandle::newDictionary();
    spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
    spec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(name));
    spec.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(name));
    spec.replaceKey("/EF", ef);
    spec.replaceKey("/Desc",
                    QPDFObjectHandle::newUnicodeString("The Xournal++ document of this PDF (xournal-qt hybrid PDF)"));
    QPDFEmbeddedFileDocumentHelper(q).replaceEmbeddedFile(name, QPDFFileSpecObjectHelper(u.add(spec)));
}

/// One incremental save: what changed of the document `prep` is put into the existing file `e`, then appended.
/// Pages whose layers and links are as the marker recorded are not read at all (see unchanged()).
class Appending {
public:
    Appending(Existing& e, const Prepared& prep, bool archive, const Revision& rev,
              const HistoryMark* history = nullptr):
            e(e), q(*e.q), u(*e.update), prep(prep), archive(archive), rev(rev), next(rev), history(history) {}

    /// Not ok without an error, and `why`: the policy wants the whole file written anew.
    Result run(const std::string& xoppExport, const fs::path& target, Revision* written, std::string& why) {
        Result r;
        Steps step;
        if (!prep.drawn.empty()) {
            drawn.setSuppressWarnings(true);
            drawn.processMemoryFile("drawn by xournal-qt", prep.drawn.data(), prep.drawn.size());
            QPDFPageDocumentHelper(drawn).pushInheritedAttributesToPage();
            drawnPages = QPDFPageDocumentHelper(drawn).getAllPages();
        }
        if (!placePages(why)) {
            return r;
        }
        step("base pages");
        readRecord();
        for (size_t i = 0; i < order.size(); ++i) {
            if (!unchanged(i)) {
                updatePage(i);
            }
        }
        placeInkText();
        step("annotations");
        PdfBookmarks::write(q, bookmarksOf(prep, order), &u);
        embedData();
        mark(xoppExport);
        if (archive) {
            // PDF/A: the dates of the information and its XMP metadata stay the same (the new drawings were checked
            // before they were copied)
            r.pdfa = ArchivePdf::update(q, u);
        }
        step("data, marker");

        // Appended, unless the file has grown too much since it was last written in full
        IncrementalPdf::Stats stats;
        const std::string bytes = u.serialize(e.tail, &stats);
        step("serialise");
        const double grown = static_cast<double>(e.tail.size + bytes.size()) - static_cast<double>(e.base);
        if (!history && grown > compactAbove * static_cast<double>(e.base)) {  // (versions are kept: never)
            why = "the file grew by more than " + std::to_string(static_cast<int>(compactAbove * 100)) +
                  "% since it was last written in full";
            return r;
        }
        const auto appended = IncrementalPdf::append(target, e.tail, bytes);
        step("append");
        if (!appended.ok) {
            r.error = appended.error;
            return r;
        }
        if (step.on) {
            std::fprintf(stderr, "hybrid-pdf: appended %llu bytes: %zu objects changed, %zu new (%zu streams)\n",
                         static_cast<unsigned long long>(bytes.size()), stats.changed, stats.added, stats.streams);
        }
        r.ok = true;
        r.incremental = true;
        r.appended = bytes.size();
        r.pages = order.size();
        r.annotations = archive ? 0 : annotations;
        r.flattened = archive ? static_cast<size_t>(flattened.getArrayNItems()) : 0;
        e.tree = order;  // (the pages as they are now)
        if (written) {
            *written = next;
            written->stamp = stampOf(target);
        }
        return r;
    }

private:
    // --- the base pages -------------------------------------------------------------------------------------------

    /// The page object of page k of the document's background PDF (a null object: none). It is not read: in a long
    /// PDF with object streams, reading every page means reading most of the file (isIndirect() says whether there
    /// is one).
    QPDFObjectHandle objectOf(size_t k) {
        if (k < rev.pages.size() && rev.pages[k].first > 0) {
            const QPDFObjGen og(rev.pages[k].first, rev.pages[k].second);
            if (u.has(og)) {
                return q.getObjectByObjGen(og);
            }
        }
        return QPDFObjectHandle::newNull();
    }

    /// A page of another PDF, copied without the page tree (its /Parent is set here; nothing of the tree of `q` is
    /// read).
    QPDFObjectHandle addForeign(QPDFPageObjectHelper page) {
        QPDFObjectHandle copy = u.copy(page.getObjectHandle());
        copy.replaceKey("/Parent", e.pagesRoot);
        return copy;
    }

    /// A new page with the content of `page`, without annotations (they are made for it by updatePage).
    QPDFObjectHandle newCopyOf(QPDFObjectHandle page) {
        QPDFObjectHandle copy = page.shallowCopy();
        if (copy.hasKey("/Annots")) {
            copy.removeKey("/Annots");
        }
        return u.add(copy);
    }

    void mapPage(size_t k, QPDFObjectHandle o) {
        if (next.pages.size() <= k) {
            next.pages.resize(k + 1, {0, 0});
        }
        next.pages[k] = {o.getObjectID(), o.getGeneration()};
    }

    /// The base pages in document order (`order`): those of the file, the new ones added, the page tree's root.
    /// False (and `why`): many pages were added or removed, the whole file is written anew.
    bool placePages(std::string& why) {
        u.touch(e.pagesRoot);
        std::set<QPDFObjGen> claimed, oldTree;
        for (QPDFObjectHandle p: e.tree) {
            oldTree.insert(p.getObjGen());
        }
        std::set<size_t> pdfMapped;
        size_t fresh = 0;
        for (size_t i = 0; i < prep.pages.size(); ++i) {
            const PageSpec& spec = prep.pages[i];
            QPDFObjectHandle obj = spec.pdfPage != npos ? pdfPage(i, claimed) : drawnPage(i, claimed);
            if (spec.pdfPage != npos && pdfMapped.insert(spec.pdfPage).second) {
                mapPage(spec.pdfPage, obj);
            } else if (spec.pdfPage == npos && spec.annotsFrom != npos) {
                mapPage(spec.annotsFrom, obj);
            }
            fresh += oldTree.count(obj.getObjGen()) ? 0 : 1;
            claimed.insert(obj.getObjGen());
            if (spec.pdfPage != npos) {
                placeSpace(i, obj, spec.space);
            }
            order.push_back(obj);
        }
        size_t removed = 0;
        for (QPDFObjectHandle p: e.tree) {
            removed += claimed.count(p.getObjGen()) ? 0 : 1;
        }
        if (!history && ((fresh > 1 && fresh * 4 > order.size()) || (removed > 1 && removed * 4 > e.tree.size()))) {
            why = "many pages were added or removed";
            return false;
        }
        bool same = order.size() == e.tree.size();
        for (size_t i = 0; same && i < order.size(); ++i) {
            same = order[i].getObjGen() == e.tree[i].getObjGen();
        }
        if (!same) {
            e.pagesRoot.replaceKey("/Kids", QPDFObjectHandle::newArray(order));
            e.pagesRoot.replaceKey("/Count", QPDFObjectHandle::newInteger(static_cast<long long>(order.size())));
            for (QPDFObjectHandle p: order) {
                if (u.isNew(p)) {
                    p.replaceKey("/Parent", e.pagesRoot);
                }
            }
        }
        return true;
    }

    /// Page i's boxes for its space for notes (qt/docs/note-space.md). Only pages that have or had space are read; a
    /// page of the file whose boxes change is written again, with its annotations (placed on its crop box).
    void placeSpace(size_t i, QPDFObjectHandle obj, const NoteSpace& space) {
        if (u.isNew(obj)) {
            setSpace(obj, space);
            return;
        }
        if (space.empty() && !hadSpace().count(obj.getObjGen())) {
            return;  // (not read)
        }
        QPDFObjectHandle wanted = obj.shallowCopy();
        setSpace(wanted, space);
        if (wanted.unparseResolved() != obj.unparseResolved()) {
            u.touch(obj);
            setSpace(obj, space);
            spaceChanged.insert(i);
        }
    }

    /// The page objects the last write gave space for notes (by the marker's /Spaces: their places then).
    const std::set<QPDFObjGen>& hadSpace() {
        if (!hadSpaceRead) {
            hadSpaceRead = true;
            QPDFObjectHandle list = e.marker.getKey("/Spaces");
            if (list.isArray()) {
                for (QPDFObjectHandle n: list.getArrayAsVector()) {
                    if (n.isInteger() && n.getIntValue() >= 0 && static_cast<size_t>(n.getIntValue()) < e.tree.size()) {
                        hadSpaceSet.insert(e.tree[static_cast<size_t>(n.getIntValue())].getObjGen());
                    }
                }
            }
        }
        return hadSpaceSet;
    }

    /// Page i shows a page of the background PDF: its page object in the file, a copy of it when it is shown twice,
    /// or (pasted from another PDF) copied from the background PDF.
    QPDFObjectHandle pdfPage(size_t i, const std::set<QPDFObjGen>& claimed) {
        const size_t p = prep.pages[i].pdfPage;
        QPDFObjectHandle base = objectOf(p);
        if (base.isIndirect() && !claimed.count(base.getObjGen())) {
            return base;
        }
        if (base.isIndirect()) {
            foreignFrom[i] = base;
            return newCopyOf(base);  // (a page shown twice: its own copy)
        }
        if (!source) {
            source = std::make_unique<QPDF>();
            source->setSuppressWarnings(true);
            PdfEncryption::openQpdf(*source, prep.bg);
            if (source->getRoot().hasKey(MARKER)) {  // (never: the background is a clean copy or the user's PDF)
                throw std::runtime_error("the background PDF has our annotations");
            }
            QPDFPageDocumentHelper(*source).pushInheritedAttributesToPage();
            sourcePages = QPDFPageDocumentHelper(*source).getAllPages();
        }
        if (p >= sourcePages.size()) {
            throw std::runtime_error("a page of the background PDF is missing");
        }
        return addForeign(sourcePages[p]);
    }

    /// Page i has a generated background: the page we drew for it before if it still shows the same, else another
    /// one of the file that shows the same (without annotations of other apps), else the new drawing. The page it
    /// replaces passes its annotations of other apps on.
    QPDFObjectHandle drawnPage(size_t i, const std::set<QPDFObjGen>& claimed) {
        const PageSpec& spec = prep.pages[i];
        QPDFObjectHandle was = spec.annotsFrom != npos ? objectOf(spec.annotsFrom) : QPDFObjectHandle::newNull();
        if (was.isIndirect() && !claimed.count(was.getObjGen()) && drawnSigOf(was) == spec.sig) {
            return was;
        }
        QPDFObjectHandle obj = QPDFObjectHandle::newNull();
        const auto same = e.drawnBySig.find(spec.sig);
        if (same != e.drawnBySig.end()) {
            for (QPDFObjectHandle c: same->second) {
                const auto annots = annotsOf(c);
                if (!claimed.count(c.getObjGen()) &&
                    std::none_of(annots.begin(), annots.end(), [](auto& a) { return isForeign(a); })) {
                    obj = c;
                    break;
                }
            }
        }
        if (!obj.isIndirect()) {
            if (spec.drawnPage != npos) {
                obj = addForeign(drawnPages.at(spec.drawnPage));
                markDrawn(obj, spec.sig);
            } else if (same != e.drawnBySig.end() && !same->second.empty()) {
                obj = newCopyOf(same->second.front());  // (the same background: the same content)
            } else {
                throw std::runtime_error("a background drawing is missing");
            }
        }
        if (was.isIndirect() && was.getObjGen() != obj.getObjGen()) {
            foreignFrom[i] = was;
        }
        return obj;
    }

    // --- our annotations, or our layers in the page content (archive), and links ------------------------------------

    /// What the marker recorded on each page (by its place then): its layers (name, sig) in order, how many links.
    void readRecord() {
        for (const AnnotSpec& a: prep.annots) {
            layersOn[a.page].push_back(&a);
        }
        for (const LinkSpec& l: prep.links) {
            linksOn[l.page].push_back(&l);
        }
        oldHashes = e.marker.getKey("/Annots");
        for (size_t j = 0; j < e.tree.size(); ++j) {
            oldIndex[e.tree[j].getObjGen()] = j;
        }
        for (const auto& [name, rec]: e.layers) {
            size_t p = 0, l = 0;
            if (parseName(name, p, l)) {
                recordedOn[p].emplace_back(l, std::make_pair(name, rec.sig));
            }
        }
        for (auto& [p, list]: recordedOn) {
            std::sort(list.begin(), list.end());
        }
        if (oldHashes.isDictionary()) {
            const std::string prefix = "/" + std::string(NAME_PREFIX) + "p";
            for (const auto& k: oldHashes.getKeys()) {
                unsigned long p = 0;
                if (k.rfind(prefix, 0) == 0 && k.find("-link") != std::string::npos &&
                    std::sscanf(k.c_str() + prefix.size(), "%lu", &p) == 1 && p >= 1) {
                    ++linksRecordedOn[p - 1];
                }
            }
        }
    }

    std::vector<const AnnotSpec*> layersOf(size_t i) {
        auto it = layersOn.find(i);
        return it != layersOn.end() ? it->second : std::vector<const AnnotSpec*>();
    }
    std::vector<const LinkSpec*> linksOf(size_t i) {
        auto it = linksOn.find(i);
        return it != linksOn.end() ? it->second : std::vector<const LinkSpec*>();
    }

    /// Page i is in its place with its layers and links as the marker recorded them: nothing of it is read or
    /// written, the record and hashes carry over.
    bool unchanged(size_t i) {
        QPDFObjectHandle page = order[i];
        auto was = oldIndex.find(page.getObjGen());
        if (was == oldIndex.end() || was->second != i || foreignFrom.count(i) || u.isNew(page) ||
            spaceChanged.count(i)) {
            return false;
        }
        std::vector<std::pair<size_t, std::pair<std::string, std::string>>> wanted;
        for (const AnnotSpec* a: layersOf(i)) {
            wanted.emplace_back(a->layer, std::make_pair(nameOf(a->page, a->layer), a->sig));
        }
        const auto rec = recordedOn.find(i);
        const auto links = linksOf(i);
        if (wanted != (rec != recordedOn.end() ? rec->second : decltype(wanted)()) ||
            static_cast<int>(links.size()) != (linksRecordedOn.count(i) ? linksRecordedOn[i] : 0)) {
            return false;
        }
        for (const auto& [layer, entry]: wanted) {
            if (!archive && !(oldHashes.isDictionary() && oldHashes.getKey("/" + entry.first).isString())) {
                return false;
            }
        }
        std::vector<std::pair<std::string, QPDFObjectHandle>> linkHashes;
        int number = 0;
        for (const LinkSpec* l: links) {
            std::string nm;
            const std::string hash = hashOf(linkDict(*l, ++number, page, prep, nm));
            QPDFObjectHandle old = oldHashes.isDictionary() ? oldHashes.getKey("/" + nm) : QPDFObjectHandle::newNull();
            if (!old.isString() || old.getStringValue() != hash) {
                return false;
            }
            linkHashes.emplace_back(nm, old);
        }
        for (const auto& [layer, entry]: wanted) {
            const std::string& nm = entry.first;
            if (archive) {
                flattened.appendItem(QPDFObjectHandle::newUnicodeString(nm));
            } else {
                hashes.replaceKey("/" + nm, oldHashes.getKey("/" + nm));
            }
            record.replaceKey("/" + nm, e.layers.at(nm).record);
            ++annotations;
        }
        for (const auto& [nm, hash]: linkHashes) {
            hashes.replaceKey("/" + nm, hash);
        }
        return true;
    }

    /// The drawing of a layer placed as `p` says: `mine` (the one it had), or one of the file that shows the same,
    /// or the new drawing (copied from `drawn`; in an archive PDF checked for PDF/A first).
    QPDFObjectHandle drawingFor(const AnnotSpec& a, const Placed& p, QPDFObjectHandle mine) {
        if (!mine.isStream()) {
            if (auto it = e.formBySig.find(a.sig); it != e.formBySig.end()) {
                mine = it->second;
            }
        }
        if (mine.isStream()) {
            return placedForm(u, mine, p);
        }
        if (a.drawnPage == npos) {
            throw std::runtime_error("a drawing of a layer is missing");
        }
        QPDFObjectHandle form = drawnPages.at(a.drawnPage).getFormXObjectForPage(false);
        QPDFObjectHandle group = form.getDict().getKey("/Group");
        if (group.isDictionary() && group.hasKey("/I")) {
            group.removeKey("/I");  // (not isolated, as formOf)
        }
        if (archive && !ArchivePdf::check({form}).empty()) {  // (repaired where it can be, before it is copied)
            throw std::runtime_error("a new drawing is not PDF/A");
        }
        QPDFObjectHandle local = u.copy(form);
        local.replaceKey("/BBox", QPDFObjectHandle::newArray(p.box));
        local.replaceKey("/Matrix", QPDFObjectHandle::newArray(p.cm));
        return local;
    }

    /// Page i: its annotations of ours (or its layers in its content) and links as the document has them now; the
    /// annotations of other apps stay.
    void updatePage(size_t i) {
        QPDFObjectHandle page = order[i];
        const auto layers = layersOf(i);
        const auto links = linksOf(i);
        if (!e.ours.count(page.getObjGen()) && layers.empty() && links.empty() && !foreignFrom.count(i) &&
            !u.isNew(page)) {
            return;  // (nothing of ours was or is on it: its annotations stay as they are)
        }
        QPDFObjectHandle current = page.getKey("/Annots");
        std::vector<QPDFObjectHandle> was = annotsOf(page);
        std::vector<QPDFObjectHandle> foreign, ours;
        for (QPDFObjectHandle a: was) {
            (isForeign(a) ? foreign : ours).push_back(a);
        }
        if (auto from = foreignFrom.find(i); from != foreignFrom.end()) {
            // The annotations of other apps of the page it replaces go along (copies of their own)
            for (QPDFObjectHandle a: annotsOf(from->second)) {
                if (isForeign(a) && a.isDictionary()) {
                    QPDFObjectHandle copy = a.shallowCopy();
                    copy.replaceKey("/P", page);
                    foreign.push_back(u.add(copy));
                }
            }
        }
        std::vector<bool> used(ours.size(), false);
        std::vector<QPDFObjectHandle> mine;  // ours, in order
        if (archive) {
            inkLayers(i, layers);
        } else {
            annotateLayers(i, layers, ours, used, mine);
        }
        int number = 0;
        for (const LinkSpec* l: links) {
            std::string nm;
            QPDFObjectHandle dict = linkDict(*l, ++number, page, prep, nm);
            const std::string text = dict.unparse();
            int cand = -1;
            for (size_t k = 0; k < ours.size() && cand < 0; ++k) {
                if (!used[k] && ours[k].unparseResolved() == text) {
                    cand = static_cast<int>(k);
                }
            }
            if (cand >= 0) {
                used[static_cast<size_t>(cand)] = true;
                mine.push_back(ours[static_cast<size_t>(cand)]);
            } else {
                mine.push_back(u.add(dict));
            }
            hashes.replaceKey("/" + nm, QPDFObjectHandle::newString(hashOf(dict)));
        }
        std::vector<QPDFObjectHandle> all = foreign;
        all.insert(all.end(), mine.begin(), mine.end());
        bool same = all.size() == was.size();
        for (size_t k = 0; same && k < all.size(); ++k) {
            same = all[k].isIndirect() && was[k].isIndirect() && all[k].getObjGen() == was[k].getObjGen();
        }
        if (same) {
            return;
        }
        if (current.isIndirect() && current.isArray() && !all.empty()) {
            u.touch(current);  // (an array of its own: written again, the page stays)
            current.setArrayFromVector(all);
        } else {
            u.touch(page);
            if (all.empty()) {
                page.removeKey("/Annots");
            } else {
                page.replaceKey("/Annots", QPDFObjectHandle::newArray(all));
            }
        }
    }

    /// A hybrid PDF: an annotation per layer. One of this page with the same drawing is kept (as it is when its name
    /// and place are too, else written again), else a new one is made.
    void annotateLayers(size_t i, const std::vector<const AnnotSpec*>& layers, std::vector<QPDFObjectHandle>& ours,
                        std::vector<bool>& used, std::vector<QPDFObjectHandle>& mine) {
        QPDFObjectHandle page = order[i];
        const double w = prep.pages[i].width, h = prep.pages[i].height;
        for (const AnnotSpec* a: layers) {
            const Placed placed = placementOf(page, *a, w, h);
            const std::string nm = nameOf(a->page, a->layer);
            int cand = -1;  // this page's annotation of the same drawing
            for (size_t k = 0; k < ours.size() && cand < 0; ++k) {
                if (!used[k] && sigOfAnnot(ours[k]) == a->sig) {
                    cand = static_cast<int>(k);
                }
            }
            QPDFObjectHandle own = QPDFObjectHandle::newNull();
            if (cand >= 0) {
                QPDFObjectHandle old = ours[static_cast<size_t>(cand)];
                QPDFObjectHandle ap = old.getKey("/AP");
                own = ap.isDictionary() ? ap.getKey("/N") : QPDFObjectHandle::newNull();
                // The same drawing, name, page and place: as it is (its points are not computed again)
                QPDFObjectHandle name = old.getKey("/NM"), onPage = old.getKey("/P"), mark = old.getKey(MARKER);
                QPDFObjectHandle hash = oldHashes.isDictionary() ? oldHashes.getKey("/" + nm) : QPDFObjectHandle::newNull();
                if (own.isStream() && placedAs(own.getDict(), placed) && name.isString() && name.getUTF8Value() == nm &&
                    onPage.isIndirect() && onPage.getObjGen() == page.getObjGen() && hash.isString() &&
                    mark.isDictionary() && mark.getKey("/Page").isInteger() &&
                    mark.getKey("/Page").getIntValue() == static_cast<long long>(a->page + 1)) {
                    used[static_cast<size_t>(cand)] = true;
                    mine.push_back(old);
                    hashes.replaceKey("/" + nm, hash);
                    record.replaceKey("/" + nm, layerRecord(a->sig, own, old));
                    ++annotations;
                    continue;
                }
            }
            QPDFObjectHandle form = drawingFor(*a, placed, own);
            QPDFObjectHandle dict = annotDict(*a, page, h, placed, form);
            if (cand >= 0) {
                QPDFObjectHandle old = ours[static_cast<size_t>(cand)];
                used[static_cast<size_t>(cand)] = true;
                QPDFObjectHandle m = old.getKey("/M");
                dict.replaceKey("/M", m.isString() ? m : QPDFObjectHandle::newString(now));
                if (dict.unparse() != old.unparseResolved()) {
                    u.touch(old);
                    replaceAll(old, dict);
                }
                mine.push_back(old);
            } else {
                dict.replaceKey("/M", QPDFObjectHandle::newString(now));
                mine.push_back(u.add(dict));
            }
            hashes.replaceKey("/" + nm, QPDFObjectHandle::newString(hashOf(dict)));
            record.replaceKey("/" + nm, layerRecord(a->sig, form, mine.back()));
            ++annotations;
        }
    }

    /// An archive PDF: the stream that draws our layers (and the Form XObjects it names) is replaced when they
    /// changed, where it is (content another app appended after ours stays after it); the page's own content stays.
    void inkLayers(size_t i, const std::vector<const AnnotSpec*>& layers) {
        QPDFObjectHandle page = order[i];
        const double w = prep.pages[i].width, h = prep.pages[i].height;
        std::vector<std::string> names, sigs;
        for (const AnnotSpec* a: layers) {
            names.push_back(nameOf(a->page, a->layer));
            sigs.push_back(a->sig);
            flattened.appendItem(QPDFObjectHandle::newUnicodeString(names.back()));
        }
        std::vector<QPDFObjectHandle> contents = contentsOf(page);
        InkStreams ink = inkStreamsOf(contents);
        const bool whole = ink.before >= 0 && ink.after >= 0;
        QPDFObjectHandle res = page.getKey("/Resources");
        QPDFObjectHandle xobj = res.isDictionary() ? res.getKey("/XObject") : QPDFObjectHandle::newNull();
        const auto oldNames = ink.mark.isDictionary() ? stringsOf(ink.mark.getKey("/XObjects")) : std::vector<std::string>();
        const auto oldSigs = ink.mark.isDictionary() ? stringsOf(ink.mark.getKey("/Sigs")) : std::vector<std::string>();
        if (whole ? stringsOf(ink.mark.getKey("/Layers")) == names && oldSigs == sigs
                  : names.empty() && ink.before < 0 && ink.after < 0) {
            for (size_t k = 0; k < names.size() && k < oldNames.size(); ++k) {  // (as they were: their record too)
                record.replaceKey("/" + names[k],
                                  layerRecord(sigs[k], xobj.isDictionary() ? xobj.getKey(oldNames[k]) : QPDFObjectHandle::newNull(),
                                              QPDFObjectHandle::newNull()));
            }
            return;
        }
        u.touch(page);
        // Its own resources (the dictionaries may be shared with other pages)
        res = res.isDictionary() ? res.shallowCopy() : QPDFObjectHandle::newDictionary();
        xobj = xobj.isDictionary() ? xobj.shallowCopy() : QPDFObjectHandle::newDictionary();
        std::map<std::string, QPDFObjectHandle> oldForms;
        for (size_t k = 0; k < oldNames.size(); ++k) {
            if (k < oldSigs.size() && xobj.getKey(oldNames[k]).isStream()) {
                oldForms.emplace(oldSigs[k], xobj.getKey(oldNames[k]));
            }
            if (xobj.hasKey(oldNames[k])) {
                xobj.removeKey(oldNames[k]);
            }
        }
        res.replaceKey("/XObject", xobj);
        std::vector<QPDFObjectHandle> result;
        for (int k = 0; k < static_cast<int>(contents.size()); ++k) {
            if (k != ink.before && k != ink.after) {
                result.push_back(contents[static_cast<size_t>(k)]);
            }
        }
        if (!layers.empty()) {
            std::string draw = "Q\n";
            QPDFObjectHandle xnames = QPDFObjectHandle::newArray();
            QPDFObjectHandle layerNames = QPDFObjectHandle::newArray();
            QPDFObjectHandle sigArray = QPDFObjectHandle::newArray();
            for (const AnnotSpec* a: layers) {
                const Placed placed = placementOf(page, *a, w, h);
                auto old = oldForms.find(a->sig);
                QPDFObjectHandle form =
                        drawingFor(*a, placed, old != oldForms.end() ? old->second : QPDFObjectHandle::newNull());
                int suffix = 1;
                const std::string name = res.getUniqueResourceName("/XqtInk", suffix);
                xobj.replaceKey(name, form);
                draw += "q " + name + " Do Q\n";
                xnames.appendItem(QPDFObjectHandle::newName(name));
                layerNames.appendItem(QPDFObjectHandle::newUnicodeString(nameOf(a->page, a->layer)));
                sigArray.appendItem(QPDFObjectHandle::newString(a->sig));
                record.replaceKey("/" + nameOf(a->page, a->layer), layerRecord(a->sig, form, QPDFObjectHandle::newNull()));
            }
            QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
            mark.replaceKey("/XObjects", xnames);
            mark.replaceKey("/Layers", layerNames);
            mark.replaceKey("/Sigs", sigArray);
            QPDFObjectHandle afterDict = QPDFObjectHandle::newDictionary();
            afterDict.replaceKey(MARKER, mark);
            QPDFObjectHandle after = u.addStream(afterDict, draw);
            if (whole) {
                result = contents;  // (in their places: the "q" before stays)
                result[static_cast<size_t>(ink.after)] = after;
            } else {
                QPDFObjectHandle beforeDict = QPDFObjectHandle::newDictionary();
                beforeDict.replaceKey(MARKER, QPDFObjectHandle::newDictionary());
                result.insert(result.begin(), u.addStream(beforeDict, "q\n"));
                result.push_back(after);
            }
        }
        page.replaceKey("/Contents", QPDFObjectHandle::newArray(result));
        page.replaceKey("/Resources", res);
    }

    // --- the embedded document, the marker ------------------------------------------------------------------------

    /// The embedded document (and its images) as new streams in the file specifications they had. Other images than
    /// before: the whole file is written anew (rare).
    void embedData() {
        touchNames(u, e.root);
        QPDFEmbeddedFileDocumentHelper efdh(q);
        auto replaceData = [&](const std::string& name, const std::string& data, bool xopp, const std::string& mime = {}) {
            auto spec = efdh.getEmbeddedFile(name);
            QPDFObjectHandle s = spec ? spec->getObjectHandle() : QPDFObjectHandle::newNull();
            QPDFObjectHandle ef = s.isDictionary() ? s.getKey("/EF") : QPDFObjectHandle::newNull();
            if (!ef.isDictionary()) {
                return false;
            }
            const std::string md5 = fileio::md5Of(data);
            if (!xopp) {
                // An attachment whose data did not change (an attached background image, a picture): the file has it
                // already, by its size and checksum (written by us)
                QPDFObjectHandle had = ef.getKey("/F");
                QPDFObjectHandle params = had.isStream() ? had.getDict().getKey("/Params") : QPDFObjectHandle::newNull();
                QPDFObjectHandle size = params.isDictionary() ? params.getKey("/Size") : QPDFObjectHandle::newNull();
                QPDFObjectHandle sum = params.isDictionary() ? params.getKey("/CheckSum") : QPDFObjectHandle::newNull();
                if (size.isInteger() && size.getIntValue() == static_cast<long long>(data.size()) && sum.isString() &&
                    sum.getStringValue() == md5 &&
                    (mime.empty() || had.getDict().getKey("/Subtype").isNameAndEquals("/" + mime))) {
                    return true;
                }
            }
            u.touch(s);
            u.touch(ef);
            // (as QPDFEFStreamObjectHelper makes it: its type, size and MD5 checksum)
            QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
            dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            dict.replaceKey("/Subtype",
                            QPDFObjectHandle::newName("/" + (!mime.empty() ? mime
                                                             : std::string(xopp ? ArchivePdf::XOPP_MIME : "image/png"))));
            QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
            params.replaceKey("/Size", QPDFObjectHandle::newInteger(static_cast<long long>(data.size())));
            params.replaceKey("/CheckSum", QPDFObjectHandle::newString(md5));
            if (archive) {
                params.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));
            }
            dict.replaceKey("/Params", params);
            QPDFObjectHandle stream = u.addStream(dict, data);
            ef.replaceKey("/F", stream);
            if (ef.hasKey("/UF")) {
                ef.replaceKey("/UF", stream);
            }
            return true;
        };
        QPDFObjectHandle dataName = e.marker.getKey("/Data");
        const std::string data = dataName.isString() ? dataName.getUTF8Value() : std::string(DATA_NAME);
        if (!replaceData(data, prep.xopp, true)) {
            if (!history || archive) {
                throw std::runtime_error("the embedded document is missing");
            }
            // The last version's .xopp became a delta (version history): the embedded document is added again
            addDataSpec(q, u, data, prep.xopp);
        }
        std::set<std::string> before;
        {
            QPDFObjectHandle listed = e.marker.getKey("/Files");
            for (int i = 0; listed.isArray() && i < listed.getArrayNItems(); ++i) {
                QPDFObjectHandle v = listed.getArrayItem(i);
                before.insert(v.isString() ? v.getUTF8Value() : v.isName() ? v.getName() : std::string());
            }
        }
        for (const auto& [name, data]: prep.extras) {
            files.appendItem(QPDFObjectHandle::newUnicodeString(name));
            if (!before.erase(name) || !replaceData(name, data, false)) {
                throw std::runtime_error("the document has other background images");
            }
        }
        // A picture the text links to that the file does not have yet: a new file specification in the name tree
        // (the tree's nodes are touched above)
        auto addNew = [&](const TextDocument::Attachment& a) {
            if (archive) {
                throw std::runtime_error("a new attachment of an archive PDF");  // (its /AF: written in full)
            }
            QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
            dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
            dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/" + (a.mime.empty() ? std::string("image/png") : a.mime)));
            QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
            params.replaceKey("/Size", QPDFObjectHandle::newInteger(static_cast<long long>(a.data.size())));
            params.replaceKey("/CheckSum", QPDFObjectHandle::newString(fileio::md5Of(a.data)));
            dict.replaceKey("/Params", params);
            QPDFObjectHandle stream = u.addStream(dict, a.data);
            QPDFObjectHandle ef = QPDFObjectHandle::newDictionary();
            ef.replaceKey("/F", stream);
            ef.replaceKey("/UF", stream);
            QPDFObjectHandle spec = QPDFObjectHandle::newDictionary();
            spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
            spec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(a.name));
            spec.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(a.name));
            spec.replaceKey("/EF", ef);
            if (!a.description.empty()) {
                spec.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(a.description));
            }
            efdh.replaceEmbeddedFile(a.name, QPDFFileSpecObjectHelper(u.add(spec)));
        };
        embedAudio(efdh);
        for (const auto& a: prep.attachments) {  // (the same files for other apps, their data new)
            if (!a.source.empty()) {
                continue;  // (a recording: embedAudio)
            }
            files.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
            if (a.fixed) {
                // A picture: the one the file has stays as it is (its data does not change under its name); a new one
                // is added (qt/docs/md-images.md)
                if (!before.erase(a.name)) {
                    addNew(a);
                }
                continue;
            }
            if (!before.erase(a.name) || !replaceData(a.name, a.data, false, a.mime)) {
                throw std::runtime_error("the document has other attachments");
            }
        }
        // Pictures the text does not link to any more: kept (and listed, so the clean copy never has them) until the
        // file is written in full
        for (auto it = before.begin(); it != before.end();) {
            if (it->find('/') != std::string::npos) {
                files.appendItem(QPDFObjectHandle::newUnicodeString(*it));
                it = before.erase(it);
            } else {
                ++it;
            }
        }
        if (!before.empty()) {
            throw std::runtime_error("the document has other background images");
        }
    }

    /// The recordings (qt/docs/audio.md): one the file has stays as it is, renamed when its pages changed (the same
    /// file specification and stream, under its new name); a new one is added. A recording the document no longer
    /// has, or a new one in an archive PDF: the whole file is written anew.
    void embedAudio(QPDFEmbeddedFileDocumentHelper& efdh) {
        std::map<std::string, std::string> had;  // source -> attachment name
        for (const auto& [name, source]: audioListOf(e.marker)) {
            had[source] = name;
        }
        for (const auto& a: prep.attachments) {
            if (a.source.empty()) {
                continue;
            }
            auto it = had.find(a.source);
            if (it == had.end()) {
                if (archive) {
                    throw std::runtime_error("a new recording in an archive PDF");  // (its /AF: written in full)
                }
                long long size = 0;
                std::string md5;
                if (!measureFile(a.file, size, md5)) {
                    continue;  // (unreadable: not carried, as in a full write)
                }
                QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
                dict.replaceKey("/Type", QPDFObjectHandle::newName("/EmbeddedFile"));
                dict.replaceKey("/Subtype", QPDFObjectHandle::newName("/" + a.mime));
                QPDFObjectHandle params = QPDFObjectHandle::newDictionary();
                params.replaceKey("/Size", QPDFObjectHandle::newInteger(size));
                params.replaceKey("/CheckSum", QPDFObjectHandle::newString(md5));
                params.replaceKey("/ModDate", QPDFObjectHandle::newString(now));
                dict.replaceKey("/Params", params);
                QPDFObjectHandle stream = u.addStream(dict, bytesOf(a.file));
                QPDFObjectHandle ef = QPDFObjectHandle::newDictionary();
                ef.replaceKey("/F", stream);
                ef.replaceKey("/UF", stream);
                QPDFObjectHandle spec = QPDFObjectHandle::newDictionary();
                spec.replaceKey("/Type", QPDFObjectHandle::newName("/Filespec"));
                spec.replaceKey("/F", QPDFObjectHandle::newUnicodeString(a.name));
                spec.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(a.name));
                spec.replaceKey("/EF", ef);
                spec.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(a.description));
                efdh.replaceEmbeddedFile(a.name, QPDFFileSpecObjectHelper(u.add(spec)));
            } else if (it->second != a.name) {
                auto spec = efdh.getEmbeddedFile(it->second);
                if (!spec) {
                    throw std::runtime_error("a recording is missing from the file");
                }
                QPDFObjectHandle s = spec->getObjectHandle();
                if (!s.isIndirect()) {
                    throw std::runtime_error("a recording's file specification is not an object of its own");
                }
                u.touch(s);
                s.replaceKey("/F", QPDFObjectHandle::newUnicodeString(a.name));
                s.replaceKey("/UF", QPDFObjectHandle::newUnicodeString(a.name));
                s.replaceKey("/Desc", QPDFObjectHandle::newUnicodeString(a.description));
                // (not removeEmbeddedFile: it turns the file specification into null)
                QPDFObjectHandle names = e.root.getKey("/Names");
                QPDFNameTreeObjectHelper tree(names.getKey("/EmbeddedFiles"), q);
                tree.remove(it->second);
                efdh.replaceEmbeddedFile(a.name, QPDFFileSpecObjectHelper(s));
            }
            if (it != had.end()) {
                had.erase(it);
            }
            audioList.appendItem(QPDFObjectHandle::newUnicodeString(a.name));
            audioList.appendItem(QPDFObjectHandle::newUnicodeString(a.source));
        }
        if (!had.empty()) {
            throw std::runtime_error("a recording was removed");  // (it goes with the next full write)
        }
    }

    /// The marker (the hashes, what was drawn, the record, the size of the last full write) and the document
    /// information.
    // --- the text layer of the handwriting (InkTextLayer.h) -------------------------------------------------------

    /// The pages whose text layer changed (by the marker's sigs per page), new pages, and pages whose space for notes
    /// changed get theirs anew; the others are not read.
    void placeInkText() {
        QPDFObjectHandle recorded = e.marker.getKey("/InkText");
        inkFont = e.marker.getKey("/InkFont");
        inkSigs = QPDFObjectHandle::newArray();
        for (size_t i = 0; i < order.size() && i < prep.pages.size(); ++i) {
            const double w = prep.pages[i].width, h = prep.pages[i].height;
            const std::vector<InkTextLayer::Word>* words = i < prep.inkWords.size() ? &prep.inkWords[i] : nullptr;
            const std::string sig = words ? inkSigOf(*words, w, h) : std::string();
            inkSigs.appendItem(QPDFObjectHandle::newString(sig));
            const auto n = static_cast<int>(i);
            const std::string was = recorded.isArray() && n < recorded.getArrayNItems() &&
                                                    recorded.getArrayItem(n).isString()
                                            ? recorded.getArrayItem(n).getStringValue()
                                            : std::string();
            QPDFObjectHandle page = order[i];
            const bool fresh = u.isNew(page);
            if (!fresh && sig == was && !spaceChanged.count(i)) {
                continue;  // (as it was: not read)
            }
            if (sig.empty() && !fresh && was.empty()) {
                continue;
            }
            u.touch(page);
            removeInkText(page);  // (the one before; a copy of a page may carry its original's)
            if (sig.empty()) {
                continue;
            }
            if (!inkFont.isIndirect()) {
                inkFont = makeInkFont([&](QPDFObjectHandle o) { return u.add(o); },
                                      [&](QPDFObjectHandle dict, const std::string& data) { return u.addStream(dict, data); });
            }
            QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
            QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
            mark.replaceKey("/InkText", QPDFObjectHandle::newString(sig));
            dict.replaceKey(MARKER, mark);
            const std::string cm = placementOf(page, AnnotSpec{}, w, h).cm.unparse();
            putInkText(page, u.addStream(dict, InkTextLayer::contentOf(*words, h, cm)), inkFont);
        }
    }

    void mark(const std::string& xoppExport) {
        QPDFObjectHandle marker = e.marker;
        u.touch(marker.isIndirect() ? marker : e.root);
        marker.replaceKey("/InkText", inkSigs.isArray() ? inkSigs : QPDFObjectHandle::newArray());
        if (inkFont.isIndirect()) {
            marker.replaceKey("/InkFont", inkFont);
        }
        marker.replaceKey("/Files", files);
        if (audioList.getArrayNItems() > 0) {
            marker.replaceKey("/Audio", audioList);
        } else if (marker.hasKey("/Audio")) {
            marker.removeKey("/Audio");
        }
        marker.replaceKey("/Annots", hashes);
        if (archive) {
            marker.replaceKey("/Flattened", flattened);
        }
        if (!xoppExport.empty()) {
            marker.replaceKey("/XoppExport", QPDFObjectHandle::newUnicodeString(xoppExport));
        } else if (marker.hasKey("/XoppExport")) {
            marker.removeKey("/XoppExport");
        }
        QPDFObjectHandle drawnList = QPDFObjectHandle::newArray();
        for (size_t i = 0; i < prep.pages.size(); ++i) {
            if (prep.pages[i].pdfPage == npos) {
                drawnList.appendItem(QPDFObjectHandle::newInteger(static_cast<long long>(i)));
            }
        }
        marker.replaceKey("/Drawn", drawnList);
        marker.replaceKey("/Spaces", spacesList(prep));
        marker.replaceKey("/Layers", record);
        marker.replaceKey("/Base", QPDFObjectHandle::newInteger(e.base));
        marker.replaceKey("/Updates", QPDFObjectHandle::newInteger(e.updates + 1));
        putHistory(marker, history, [&](const std::string& data) {
            return u.addStream(QPDFObjectHandle::newDictionary(), data);
        });
        if (marker.hasKey(PdfHistory::DELTA_KEY)) {
            marker.removeKey(PdfHistory::DELTA_KEY);  // (the version before stored as a delta: this one is whole)
        }
        QPDFObjectHandle trailer = q.getTrailer();
        QPDFObjectHandle info = trailer.getKey("/Info");
        if (!info.isDictionary()) {
            info = u.add(QPDFObjectHandle::newDictionary());
            trailer.replaceKey("/Info", info);
        } else {
            u.touch(info);
        }
        info.replaceKey("/Producer", QPDFObjectHandle::newString(std::string(PROJECT_STRING) + " + QPDF " + QPDF_VERSION));
        if (!archive) {
            info.replaceKey("/ModDate", QPDFObjectHandle::newString(pdfDateNow()));  // (an archive's: ArchivePdf::update)
        }
    }

    QPDFObjectHandle inkSigs = QPDFObjectHandle::newNull();  ///< the marker's /InkText (placeInkText)
    QPDFObjectHandle inkFont = QPDFObjectHandle::newNull();
    Existing& e;
    QPDF& q;
    IncrementalPdf::Update& u;
    const Prepared& prep;
    const bool archive;
    const Revision& rev;
    Revision next;  ///< the revision written
    const HistoryMark* history;  ///< version history: what the marker says (and the file is never compacted)
    QPDF drawn;     ///< the new drawings (prep.drawn)
    std::vector<QPDFPageObjectHelper> drawnPages;
    std::unique_ptr<QPDF> source;  ///< the background PDF: pages the file does not have yet (pasted ones)
    std::vector<QPDFPageObjectHelper> sourcePages;
    std::vector<QPDFObjectHandle> order;  ///< the base pages in document order
    std::map<size_t, QPDFObjectHandle> foreignFrom;  ///< a page in place of another: its annotations of other apps
    std::set<size_t> spaceChanged;                    ///< pages whose boxes changed (space for notes)
    std::set<QPDFObjGen> hadSpaceSet;
    bool hadSpaceRead = false;
    std::map<size_t, std::vector<const AnnotSpec*>> layersOn;
    std::map<size_t, std::vector<const LinkSpec*>> linksOn;
    std::map<QPDFObjGen, size_t> oldIndex;  ///< the pages' places in the file
    std::map<size_t, std::vector<std::pair<size_t, std::pair<std::string, std::string>>>> recordedOn;
    std::map<size_t, int> linksRecordedOn;
    const std::string now = pdfDateNow();
    QPDFObjectHandle oldHashes;
    QPDFObjectHandle hashes = QPDFObjectHandle::newDictionary();     ///< the marker's new /Annots
    QPDFObjectHandle flattened = QPDFObjectHandle::newArray();       ///< and /Flattened
    QPDFObjectHandle record = QPDFObjectHandle::newDictionary();     ///< and /Layers
    QPDFObjectHandle files = QPDFObjectHandle::newArray();           ///< and /Files
    QPDFObjectHandle audioList = QPDFObjectHandle::newArray();       ///< and /Audio (embedAudio)
    size_t annotations = 0;
};

// --- the cache ------------------------------------------------------------------------------------------------------

std::mutex cacheMutex;
std::map<fs::path, int>& retained() {
    static std::map<fs::path, int> r;
    return r;
}

/// The folder of the clean copy of this version of a file.
fs::path entryOf(const fs::path& pdf, const std::string& stamp) {
    std::error_code ec;
    const fs::path abs = fs::weakly_canonical(pdf, ec);
    return cacheFolder() / (hex(fnv((ec ? pdf : abs).string())) + "-" + stamp);
}

/// Remove clean copies of other versions of this file, and those not used for a day (unless retained).
void prune(const fs::path& keep) {
    const std::string prefix = keep.filename().string().substr(0, 16);
    const auto now = fs::file_time_type::clock::now();
    std::lock_guard lock(cacheMutex);
    std::error_code ec;
    for (auto it = fs::directory_iterator(cacheFolder(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path dir = it->path();
        const std::string n = dir.filename().string();
        if (dir == keep || n.rfind("write-", 0) == 0 || !it->is_directory()) {
            continue;
        }
        if (std::any_of(retained().begin(), retained().end(),
                        [&](const auto& r) { return r.first.parent_path() == dir.lexically_normal(); })) {
            continue;
        }
        std::error_code tec;
        const auto t = fs::last_write_time(dir, tec);
        const bool old = !tec && now - t > std::chrono::hours(24);
        const bool sameFile = n.rfind(prefix, 0) == 0 && now - t > std::chrono::minutes(1);
        if (old || sameFile) {
            std::error_code rec;
            fs::remove_all(dir, rec);
        }
    }
}

}  // namespace

/// The title of a PDF without one: its file name without ".pdf", ".notes.pdf", ".archive.pdf".
static std::string titleOf(const fs::path& pdf) {
    fs::path stem = pdf.filename();
    stem.replace_extension();
    for (const char* tail: {".archive", ".notes"}) {
        if (stem.extension() == tail) {
            stem.replace_extension();
        }
    }
    const auto u8 = stem.u8string();
    return std::string(u8.begin(), u8.end());
}

// --- public ---------------------------------------------------------------------------------------------------------

void markUnpacked(const fs::path& folder) {
    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
        writeFile(folder / (std::string(UNPACKED_PREFIX) + std::to_string(Util::getPid())), "");
    }
}

void dropExtracted(const fs::path& base) {
    std::error_code ec;
    fs::remove_all(base.parent_path() / PICTURES_NAME, ec);
    fs::remove_all(base.parent_path() / AUDIO_NAME, ec);
    fs::remove(base.parent_path() / (std::string(UNPACKED_PREFIX) + std::to_string(Util::getPid())), ec);
}

int removeUnpackedLeftovers(const std::vector<fs::path>& roots, const std::function<bool(int64_t)>& alive,
                            const std::function<void(const fs::path&)>& remove) {
    int removed = 0;
    const std::string prefix = UNPACKED_PREFIX;
    for (const fs::path& root: roots) {
        std::error_code ec;
        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code dec;
            if (!it->is_directory(dec)) {
                continue;
            }
            bool stale = false;
            for (auto f = fs::directory_iterator(it->path(), dec); !dec && f != fs::directory_iterator();
                 f.increment(dec)) {
                const std::string n = f->path().filename().string();
                if (n.rfind(prefix, 0) == 0) {
                    try {
                        const int64_t pid = std::stoll(n.substr(prefix.size()));
                        if (alive(pid)) {
                            stale = false;  // (a process that still runs has it open)
                            break;
                        }
                        stale = true;
                    } catch (const std::exception&) {
                        stale = true;
                    }
                }
            }
            if (stale) {
                remove(it->path());
                ++removed;
            }
        }
    }
    return removed;
}

int removeProtectedLeftovers(const std::function<bool(int64_t)>& alive) {
    // The clean copies' entries: their pictures and recordings (the encrypted clean copy stays); the documents' work
    // folders of Markdown pictures: whole
    int removed = removeUnpackedLeftovers({cacheFolder()}, alive, [](const fs::path& dir) {
        std::error_code ec;
        fs::remove_all(dir / PICTURES_NAME, ec);
        fs::remove_all(dir / AUDIO_NAME, ec);
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (it->path().filename().string().rfind(UNPACKED_PREFIX, 0) == 0) {
                std::error_code rec;
                fs::remove(it->path(), rec);
            }
        }
    });
    removed += removeUnpackedLeftovers({Util::getCacheSubfolder("md-assets")}, alive,
                                       [](const fs::path& dir) {
                                           std::error_code ec;
                                           fs::remove_all(dir, ec);
                                       });
    return removed;
}

int forgetCopies(const fs::path& pdf) {
    const std::string prefix = entryOf(pdf, "").filename().string();  // (the path's hash and "-")
    std::lock_guard lock(cacheMutex);
    std::error_code ec;
    int removed = 0;
    for (auto it = fs::directory_iterator(cacheFolder(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path dir = it->path();
        if (dir.filename().string().rfind(prefix, 0) != 0 || !it->is_directory()) {
            continue;
        }
        if (std::any_of(retained().begin(), retained().end(),
                        [&](const auto& r) { return r.first.parent_path() == dir.lexically_normal(); })) {
            continue;
        }
        std::error_code rec;
        fs::remove_all(dir, rec);
        removed += !rec;
    }
    return removed;
}

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

fs::path cacheFolder() { return Util::getCacheSubfolder("hybrid-pdf"); }

bool inCache(const fs::path& file) {
    if (file.empty()) {
        return false;
    }
    return file.lexically_normal().parent_path().parent_path() == cacheFolder().lexically_normal();
}

void retain(const fs::path& base) {
    std::lock_guard lock(cacheMutex);
    ++retained()[base.lexically_normal()];
}

void release(const fs::path& base) {
    std::lock_guard lock(cacheMutex);
    auto it = retained().find(base.lexically_normal());
    if (it != retained().end() && --it->second <= 0) {
        retained().erase(it);
    }
}

void touch(const fs::path& base) {
    std::error_code ec;
    fs::last_write_time(base.parent_path(), fs::file_time_type::clock::now(), ec);
}

double compactAbove = 0.25;

namespace {
/// What a file written in full is now, for the next incremental save: its pages by the page numbers of the document's
/// background PDF.
Revision revisionAfterFull(const fs::path& target, const Prepared& prep) {
    Revision rev;
    QPDF q;
    q.setSuppressWarnings(true);
    PdfEncryption::openQpdf(q, target);
    // (the kids of the page tree's root: written flat; reading the pages themselves reads most of a long file: 0.5 s
    // for pgfmanual with qpdf 12.4, 2.7 s with 10.6)
    QPDFObjectHandle kids = q.getRoot().getKey("/Pages").getKey("/Kids");
    const std::vector<QPDFObjectHandle> pages = kids.isArray() ? kids.getArrayAsVector() : std::vector<QPDFObjectHandle>();
    auto map = [&](size_t k, QPDFObjectHandle o) {
        if (rev.pages.size() <= k) {
            rev.pages.resize(k + 1, {0, 0});
        }
        if (rev.pages[k].first == 0) {
            rev.pages[k] = {o.getObjectID(), o.getGeneration()};
        }
    };
    for (size_t i = 0; i < prep.pages.size() && i < pages.size(); ++i) {
        const PageSpec& spec = prep.pages[i];
        const size_t k = spec.pdfPage != npos ? spec.pdfPage : spec.annotsFrom;
        if (k != npos) {
            map(k, pages[i]);
        }
    }
    rev.stamp = stampOf(target);
    return rev;
}

std::string pagesText(const std::vector<QPDFObjectHandle>& pages) {
    std::string out;
    for (QPDFObjectHandle p: pages) {
        out += std::to_string(p.getObjectID()) + " " + std::to_string(p.getGeneration()) + "\n";
    }
    return out;
}

/// After an incremental update that kept the base pages as they were (the same pages in the same order), the clean
/// copy of the previous version is the clean copy of this one too: its cache entry gets it (a hard link), with the new
/// embedded document, so opening the file again does not make it again.
void keepCleanCopy(const fs::path& target, const std::string& was, const Prepared& prep,
                   const std::vector<QPDFObjectHandle>& pages) {
    std::error_code ec;
    const fs::path from = entryOf(target, was);
    const fs::path to = entryOf(target, stampOf(target));
    if (!fs::exists(from / CHECK_NAME, ec) || !fs::exists(from / CLEAN_NAME, ec) || !bytesOf(from / CHECK_NAME).empty() ||
        fs::exists(to / CHECK_NAME, ec)) {
        return;
    }
    const std::string list = pagesText(pages);
    if (bytesOf(from / PAGES_NAME) != list) {
        return;  // (pages added, removed or moved: the next open makes a clean copy of this version)
    }
    fs::create_directories(to, ec);
    fs::create_hard_link(from / CLEAN_NAME, to / CLEAN_NAME, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(from / CLEAN_NAME, to / CLEAN_NAME, fs::copy_options::overwrite_existing, ec);
    }
    // (a protected PDF: not its Xournal data, which is read from the file at each opening)
    const bool secret = PdfEncryption::isProtected(target);
    if (ec || !writeFile(to / PAGES_NAME, list) || (!secret && !writeFile(to / DATA_NAME, prep.xopp))) {
        fs::remove_all(to, ec);
        return;
    }
    for (const auto& [name, data]: prep.extras) {
        if (!secret) {
            writeFile(to / name, data);
        }
    }
    const fs::path tmp = partOf(to / CHECK_NAME);
    writeFile(tmp, "");
    fs::rename(tmp, to / CHECK_NAME, ec);  // last: the entry is complete
}
}  // namespace

namespace {
// --- version history (PdfHistory.h; qt/docs/hybrid-pdf.md, "Version history") --------------------------------------

/// The embedded document.xopp of a PDF with notes as the file has it (gzipped); empty: none.
std::string embeddedXoppOf(QPDF& q) {
    QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    QPDFObjectHandle name = marker.isDictionary() ? marker.getKey("/Data") : QPDFObjectHandle::newNull();
    auto spec = QPDFEmbeddedFileDocumentHelper(q).getEmbeddedFile(name.isString() ? name.getUTF8Value()
                                                                                   : std::string(DATA_NAME));
    if (!spec) {
        return {};
    }
    auto buffer = spec->getEmbeddedFileStream().getStreamData();
    return std::string(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
}

/// The SHA-256 of a .xopp (of its XML, gunzipped).
std::string xoppSha(const std::string& gz) {
    bool ok = false;
    const std::string xml = fileio::gunzip(gz, ok);
    return PdfHistory::sha256(ok ? xml : gz);
}

/// The revisions of `target` after its first `from` bytes written again as one (the day's version replaced: the
/// earlier save of the day and this one become one revision; what only the earlier one used goes). `change`, when
/// given, changes the file's objects first (through the update).
bool rewriteFrom(const fs::path& target, uint64_t from, std::string& error,
                 const std::function<void(QPDF&, IncrementalPdf::Update&)>& change = {}) {
    const auto chain = PdfRevisions::read(target);
    size_t k = 0;
    while (k < chain.revisions.size() && chain.revisions[k].end != from) {
        ++k;
    }
    if (k + 1 >= chain.revisions.size() || chain.garbage) {
        error = "the file is not as expected";
        return false;
    }
    IncrementalPdf::Update::Over over;
    over.prefix = PdfRevisions::tailOf(target, chain.revisions[k]);
    for (size_t j = 0; j <= k; ++j) {
        over.highest = std::max(over.highest, static_cast<int>(chain.revisions[j].size) - 1);
        for (const auto& [num, gen]: chain.revisions[j].objects) {
            over.highest = std::max(over.highest, num);
        }
    }
    for (size_t j = k + 1; j < chain.revisions.size(); ++j) {
        const auto& objects = chain.revisions[j].objects;
        over.objects.insert(over.objects.end(), objects.begin(), objects.end());
    }
    IncrementalPdf::Tail whole;
    if (!IncrementalPdf::readTail(target, whole, error)) {
        return false;
    }
    std::string bytes;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        IncrementalPdf::Update u(q);
        if (change) {
            change(q, u);
        }
        // (the marker says where its revision begins: version history tells our revisions from other apps' by it)
        QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
        if (QPDFObjectHandle h = marker.isDictionary() ? marker.getKey("/History") : QPDFObjectHandle::newNull();
            h.isDictionary()) {
            u.touch(marker.isIndirect() ? marker : q.getRoot());
            QPDFObjectHandle copy = h.shallowCopy();
            copy.replaceKey("/Start", QPDFObjectHandle::newInteger(static_cast<long long>(from)));
            marker.replaceKey("/History", copy);
        }
        bytes = u.serializeOver(over);
    }
    const auto r = IncrementalPdf::append(target, whole, bytes, from);
    if (!r.ok) {
        error = r.error;
    }
    return r.ok;
}

/// The whole document appended to `target` as one update (instead of writing the file anew: its earlier versions
/// stay). Written in full to a file of its own first, then copied in; streams the file has already (the pages of the
/// PDF it annotates, their fonts and images) are referred to, not copied again.
Result appendWhole(const Prepared& prep, const fs::path& target, const std::string& exportName, const HistoryMark& mark,
                   Revision* written, const fs::path& work) {
    Steps step;
    const fs::path full = work / "whole.pdf";
    Result r = assemble(prep, target, Mode::Hybrid, exportName, titleOf(target), &mark, full);
    if (!r.ok) {
        return r;
    }
    PdfEncryption::derive(full, target);  // (encrypted like the file: read with its password below)
    step("the whole document written");
    IncrementalPdf::Tail tail;
    if (!IncrementalPdf::readTail(target, tail, r.error)) {
        r.ok = false;
        return r;
    }
    std::string bytes;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        if (PdfEncryption::Encrypter enc(q); enc.encrypted() && !enc.supported()) {
            throw std::runtime_error("the file is encrypted: " + enc.why());
        }
        IncrementalPdf::Update u(q);
        u.indexReuse();
        step("the file's streams");
        QPDF f;
        f.setSuppressWarnings(true);
        PdfEncryption::openQpdf(f, full);
        QPDFObjectHandle root = q.getRoot();
        u.touch(root);
        replaceAll(root, u.copyAll(f.getRoot()));
        if (QPDFObjectHandle info = f.getTrailer().getKey("/Info"); info.isDictionary()) {
            QPDFObjectHandle own = q.getTrailer().getKey("/Info");
            if (own.isIndirect() && own.isDictionary()) {
                u.touch(own);
                replaceAll(own, u.copyAll(info));
            } else {
                q.getTrailer().replaceKey("/Info", u.copyAll(info));
            }
        }
        bytes = u.serialize(tail);
        step("copied");
    }
    const auto appended = IncrementalPdf::append(target, tail, bytes);
    if (!appended.ok) {
        r.ok = false;
        r.error = appended.error;
        return r;
    }
    r.incremental = true;
    r.appended = bytes.size();
    if (written) {
        *written = revisionAfterFull(target, prep);
    }
    return r;
}

/// Before a new version is appended: the last one, still the file's last revision and an ordinary day's version, is
/// written again with its .xopp as a delta against the version before it (its pages and drawings stay as they are, so
/// any PDF viewer still shows it; its embedded document.xopp goes). Milestones, every 30th version, a version after
/// another app's revision and one whose delta would be more than half of its .xopp stay whole. False (`versions`
/// unchanged) when it stays whole.
bool storeAsDelta(const fs::path& target, const PdfHistory::Listed& listed, std::vector<PdfHistory::Version>& versions) {
    using PdfHistory::Version;
    if (versions.size() < 2 || listed.chain.garbage || !listed.chain.ok()) {
        return false;
    }
    Version& cur = versions.back();
    const Version& prev = versions[versions.size() - 2];
    const auto& last = listed.chain.revisions.back();
    if (cur.kind != PdfHistory::Kind::FULL || cur.id <= 0 || cur.start == 0 || cur.milestone() ||
        cur.id % PdfHistory::KEYFRAME_EVERY == 0 || prev.kind == PdfHistory::Kind::RECEIVED || cur.sha.empty() ||
        !listed.lastIsOurs || last.end != cur.end) {
        return false;
    }
    Steps step;
    std::string error;
    const std::string base = PdfHistory::xmlOf(target, listed, prev.id, error);
    if (base.empty()) {
        g_warning("Version %d of %s stays whole: %s", cur.id, target.string().c_str(), error.c_str());
        return false;
    }
    std::string gz;
    {
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        gz = embeddedXoppOf(q);
    }
    bool ok = false;
    const std::string xml = fileio::gunzip(gz, ok);
    if (!ok || PdfHistory::sha256(xml) != cur.sha) {
        return false;
    }
    const std::string delta = ByteDelta::encode(base, xml);
    std::string check;
    if (!ByteDelta::apply(base, delta, check) || check != xml) {
        g_warning("The delta of version %d of %s does not give it back: it stays whole", cur.id, target.string().c_str());
        return false;
    }
    if (fileio::gzip(delta).size() * 2 > gz.size()) {
        return false;  // (a keyframe: the delta would be more than half of it)
    }
    step("the last version's delta");
    const bool done = rewriteFrom(target, cur.start, error, [&](QPDF& q, IncrementalPdf::Update& u) {
        QPDFObjectHandle root = q.getRoot();
        QPDFObjectHandle marker = root.getKey(MARKER);
        QPDFObjectHandle name = marker.getKey("/Data");
        touchNames(u, root);
        QPDFObjectHandle names = root.getKey("/Names");
        QPDFNameTreeObjectHelper tree(names.getKey("/EmbeddedFiles"), q);
        tree.remove(name.isString() ? name.getUTF8Value() : std::string(DATA_NAME));
        u.touch(marker.isIndirect() ? marker : root);
        QPDFObjectHandle dict = QPDFObjectHandle::newDictionary();
        dict.replaceKey("/Type", QPDFObjectHandle::newName("/XournalQtDelta"));
        QPDFObjectHandle d = QPDFObjectHandle::newDictionary();
        d.replaceKey("/Data", u.addStream(dict, delta));
        d.replaceKey("/Base", QPDFObjectHandle::newInteger(prev.id));
        marker.replaceKey(PdfHistory::DELTA_KEY, d);
    });
    if (!done) {
        g_warning("Version %d of %s stays whole: %s", cur.id, target.string().c_str(), error.c_str());
        return false;
    }
    step("written again as a delta");
    std::error_code ec;
    cur.kind = PdfHistory::Kind::DELTA;
    cur.base = prev.id;
    cur.end = fs::file_size(target, ec);
    return true;
}

/// A save with version history on (WriteOptions::history).
Result writeKeeping(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const std::string& exportName, const WriteOptions& options, const fs::path& work) {
    Steps step;
    const std::time_t when = PdfHistory::now();
    PdfHistory::Version v;
    v.date = PdfHistory::isoUtc(when);
    v.day = PdfHistory::localDay(when);
    v.message = options.history->message;
    auto prepared = [&](const Reuse* reuse) {
        Prepared prep = prepare(doc, target.filename().string(), work, baseOf, pdfPageCount, false,
                                target.parent_path(), nullptr, reuse);
        addInkWords(prep, options.inkText);
        prep.encryption = options.encryption;
        if (!prep.error.empty()) {
            throw std::runtime_error(prep.error);
        }
        v.sha = xoppSha(prep.xopp);
        v.pages = static_cast<int>(prep.pages.size());
        return prep;
    };
    std::error_code ec;
    PdfRevisions::Chain chain;
    if (fs::exists(target, ec)) {
        chain = PdfRevisions::read(target);
    }
    if (!chain.ok() || chain.garbage) {
        // Nothing to build on (a new file, or one that does not read): the first version, written in full
        Prepared prep = prepared(nullptr);
        v.id = 1;
        HistoryMark mark{{v}, 0};
        Result r = assemble(prep, target, Mode::Hybrid, exportName, titleOf(target), &mark);
        r.version = v.id;
        if (r.ok && options.written) {
            *options.written = revisionAfterFull(target, prep);
        }
        return r;
    }
    if (!isHybrid(target)) {
        // A PDF of the user's: version 0 is the file as it was received, the document is appended on top of it
        PdfHistory::Version v0;
        v0.id = 0;
        v0.kind = PdfHistory::Kind::RECEIVED;
        v0.end = chain.end();
        v0.date = PdfRevisions::dateOf(target, v0.end);
        if (v0.date.empty()) {
            v0.date = v.date;
        }
        Prepared prep = prepared(nullptr);
        v.id = 1;
        v.start = chain.size;
        HistoryMark mark{{v0, v}, chain.size};
        Result r = appendWhole(prep, target, exportName, mark, options.written, work);
        r.version = v.id;
        return r;
    }
    const std::string stampBefore = stampOf(target);
    PdfHistory::Listed listed = PdfHistory::list(target);
    std::vector<PdfHistory::Version> versions = listed.versions;
    int last = -1;
    for (const auto& x: versions) {
        last = std::max(last, x.id);
    }
    if (versions.empty()) {
        // History begins now: version 0 is the file as it is
        PdfHistory::Version v0;
        v0.id = 0;
        v0.start = chain.revisions.back().start;
        v0.end = chain.end();
        v0.date = PdfRevisions::dateOf(target, v0.end);
        if (v0.date.empty()) {
            v0.date = v.date;
        }
        QPDF q;
        q.setSuppressWarnings(true);
        PdfEncryption::openQpdf(q, target);
        if (const std::string xopp = embeddedXoppOf(q); !xopp.empty()) {
            v0.sha = xoppSha(xopp);
        }
        versions.push_back(v0);
        last = 0;
    }
    const bool replace = !options.history->newVersion && PdfHistory::replacesLast(listed, v.day);
    uint64_t cut = 0;
    if (replace) {
        v.id = versions.back().id;
        v.start = cut = versions.back().start;
        versions.pop_back();
    } else {
        v.id = last + 1;
        v.start = chain.size;
    }
    step("the history");
    // A new version: the one before it becomes a delta (the file changes, its pages stay the same objects)
    Revision rev = options.revision ? *options.revision : Revision();
    if (!replace && storeAsDelta(target, listed, versions)) {
        v.start = versions.back().end;
        if (rev.valid() && rev.stamp == stampBefore) {
            rev.stamp = stampOf(target);
        }
    }
    auto finish = [&](Result r) {
        r.version = v.id;
        r.replaced = replace;
        if (r.ok && replace) {
            // The day's earlier save and this one: one revision
            std::string error;
            if (!rewriteFrom(target, cut, error)) {
                g_warning("Could not replace the day's version of %s: %s", target.string().c_str(), error.c_str());
            }
            if (options.written) {
                options.written->stamp = stampOf(target);
            }
            step("the day's version replaced");
        }
        return r;
    };
    std::string whyFull;
    if (rev.valid()) {
        try {
            auto existing = openExisting(target, rev, false, whyFull);
            if (existing) {
                Prepared prep = prepared(&existing->reuse);
                versions.push_back(v);
                HistoryMark mark{versions, existing->tail.size};
                const std::string was = options.revision->stamp;
                Result r = Appending(*existing, prep, false, rev, &mark).run(exportName, target, options.written, whyFull);
                if (r.ok) {
                    r = finish(r);
                    keepCleanCopy(target, was, prep, existing->tree);
                    return r;
                }
                if (!r.error.empty()) {
                    return r;  // (the file could not be written: it is as it was)
                }
                versions.pop_back();
            }
        } catch (const std::exception& e) {
            whyFull = e.what();
            if (!versions.empty() && versions.back().id == v.id) {
                versions.pop_back();
            }
        }
    } else {
        whyFull = "no revision to build on";
    }
    if (step.on) {
        std::fprintf(stderr, "hybrid-pdf: the whole document appended: %s\n", whyFull.c_str());
    }
    // The fallback that keeps the versions: the whole document appended
    Prepared prep = prepared(nullptr);
    versions.push_back(v);
    std::error_code sec;
    Result r = appendWhole(prep, target, exportName, HistoryMark{versions, fs::file_size(target, sec)}, options.written,
                           work);
    r.whyFull = whyFull;
    return finish(r);
}
}  // namespace

bool writeVersion(const fs::path& pdf, int id, const fs::path& out, std::string& error) {
    try {
        const PdfHistory::Listed listed = PdfHistory::list(pdf);
        auto it = std::find_if(listed.versions.begin(), listed.versions.end(),
                               [&](const PdfHistory::Version& v) { return v.id == id; });
        if (it == listed.versions.end()) {
            error = listed.error.empty() ? "There is no such version in the file." : listed.error;
            return false;
        }
        std::string xopp;
        if (it->kind == PdfHistory::Kind::DELTA) {
            xopp = PdfHistory::xoppOf(pdf, listed, id, error);  // (rebuilt and checked first)
            if (xopp.empty()) {
                return false;
            }
        }
        if (!PdfRevisions::extract(pdf, it->end, out, error)) {
            return false;
        }
        PdfEncryption::derive(out, pdf);  // (a prefix of a protected file: encrypted, the same password)
        if (xopp.empty()) {
            return true;  // (the file as it was saved then: its own document.xopp, or the PDF as received)
        }
        // Its .xopp was a delta: the whole one added again, so that it opens as a PDF with notes of its own
        IncrementalPdf::Tail tail;
        if (!IncrementalPdf::readTail(out, tail, error)) {
            return false;
        }
        std::string bytes;
        {
            QPDF q;
            q.setSuppressWarnings(true);
            PdfEncryption::openQpdf(q, out);
            IncrementalPdf::Update u(q);
            QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
            QPDFObjectHandle name = marker.getKey("/Data");
            addDataSpec(q, u, name.isString() ? name.getUTF8Value() : std::string(DATA_NAME), xopp);
            u.touch(marker.isIndirect() ? marker : q.getRoot());
            marker.removeKey(PdfHistory::DELTA_KEY);
            bytes = u.serialize(tail);
        }
        const auto r = IncrementalPdf::append(out, tail, bytes);
        if (!r.ok) {
            error = r.error;
        }
        return r.ok;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

namespace {
/// The file changed in a way that keeps its clean copy (only the marker): the cache entry of the version `was` serves
/// the version it is now.
void keepCacheEntry(const fs::path& pdf, const std::string& was) {
    std::error_code ec;
    const fs::path from = entryOf(pdf, was);
    const fs::path to = entryOf(pdf, stampOf(pdf));
    if (from == to || !fs::exists(from / CHECK_NAME, ec) || fs::exists(to / CHECK_NAME, ec)) {
        return;
    }
    fs::create_directories(to, ec);
    for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().filename() == CHECK_NAME || !it->is_regular_file()) {
            continue;
        }
        std::error_code lec;
        fs::create_hard_link(it->path(), to / it->path().filename(), lec);
        if (lec) {
            fs::copy_file(it->path(), to / it->path().filename(), fs::copy_options::overwrite_existing, lec);
        }
    }
    fs::copy_file(from / CHECK_NAME, to / CHECK_NAME, fs::copy_options::overwrite_existing, ec);  // (last: complete)
}
}  // namespace

namespace {
/// The marker's history written again in the update `u` of `q`, which begins at `start`: the update is the current
/// version's last revision, ours (PdfHistory::list tells our revisions from other apps' by /Start).
void markHistoryIn(QPDF& q, IncrementalPdf::Update& u, std::vector<PdfHistory::Version> versions, uint64_t start) {
    versions.back().end = 0;  // (the current version: as its own list says it)
    QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    u.touch(marker.isIndirect() ? marker : q.getRoot());
    HistoryMark mark{std::move(versions), start};
    putHistory(marker, &mark,
               [&](const std::string& data) { return u.addStream(QPDFObjectHandle::newDictionary(), data); });
}
}  // namespace

void keepHistoryIn(const fs::path& pdf, QPDF& q, IncrementalPdf::Update& u, uint64_t start) {
    const QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
    if (!marker.isDictionary() || !marker.getKey("/History").isDictionary()) {
        return;  // (not a PDF with notes, or one without history)
    }
    const PdfHistory::Listed listed = PdfHistory::list(pdf);
    if (listed.on && listed.lastIsOurs && !listed.versions.empty()) {
        markHistoryIn(q, u, listed.versions, start);
    }
}

bool setVersionMessage(const fs::path& pdf, int id, const std::string& message, std::string& error) {
    const fileio::FileWriteLock lock(pdf);
    try {
        PdfHistory::Listed listed = PdfHistory::list(pdf);
        if (!listed.on || !listed.lastIsOurs || listed.versions.empty()) {
            error = "The file was changed by another app since it was saved here: save it first.";
            return false;
        }
        bool found = false;
        for (auto& v: listed.versions) {
            if (v.id == id) {
                v.message = message;
                found = true;
            }
        }
        if (!found) {
            error = "There is no such version in the file.";
            return false;
        }
        IncrementalPdf::Tail tail;
        if (!IncrementalPdf::readTail(pdf, tail, error)) {
            return false;
        }
        std::string bytes;
        {
            QPDF q;
            q.setSuppressWarnings(true);
            PdfEncryption::openQpdf(q, pdf);
            IncrementalPdf::Update u(q);
            markHistoryIn(q, u, listed.versions, tail.size);
            bytes = u.serialize(tail);
        }
        const std::string was = stampOf(pdf);
        const auto r = IncrementalPdf::append(pdf, tail, bytes);
        if (!r.ok) {
            error = r.error;
            return false;
        }
        // (only the marker changed: the clean copy is still the clean copy of this version)
        keepCacheEntry(pdf, was);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return false;
}

Result write(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
             const fs::path& xoppExport, const WriteOptions& options) {
    const fileio::FileWriteLock lock(target);  // (its tags or a version's message are not written meanwhile)
    Result r;
    try {
        WorkDir work;
        Steps step;
        std::string exportName;
        if (!xoppExport.empty()) {
            // Relative to the PDF when it is beside it or below (it follows the PDF when both are moved)
            const fs::path rel = xoppExport.lexically_relative(target.parent_path());
            const bool inside = !rel.empty() && *rel.begin() != "..";
            const auto name = (inside ? rel : xoppExport).generic_u8string();
            exportName.assign(name.begin(), name.end());
        }
        // An archive PDF saved again stays one
        std::error_code ec;
        const bool exists = fs::exists(target, ec);
        const Mode mode = exists && isArchive(target) ? Mode::Archive : Mode::Hybrid;
        if (options.history && options.history->on && !options.compact && mode == Mode::Hybrid) {
            return writeKeeping(doc, target, baseOf, pdfPageCount, exportName, options, work.path);
        }
        std::string whyFull;
        if (options.revision && options.revision->valid() && !options.compact && exists) {
            // Only what changed, appended (qt/docs/hybrid-pdf.md, "Saving: incremental updates")
            try {
                auto existing = openExisting(target, *options.revision, mode == Mode::Archive, whyFull);
                step("open the file");
                if (existing) {
                    Prepared prep = prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount, false,
                                            target.parent_path(), nullptr, &existing->reuse);
                    addInkWords(prep, options.inkText);
        prep.encryption = options.encryption;
                    step("draw what changed and write the .xopp");
                    if (!prep.error.empty()) {
                        r.error = prep.error;
                        return r;
                    }
                    const std::string was = options.revision->stamp;
                    r = Appending(*existing, prep, mode == Mode::Archive, *options.revision)
                                .run(exportName, target, options.written, whyFull);
                    if (r.ok) {
                        keepCleanCopy(target, was, prep, existing->tree);
                        step("keep the clean copy");
                        return r;
                    }
                    if (!r.error.empty()) {
                        return r;  // (the file could not be written: it is as it was)
                    }
                    r = Result();
                }
            } catch (const std::exception& e) {
                whyFull = e.what();
                r = Result();
            }
            if (step.on) {
                std::fprintf(stderr, "hybrid-pdf: written in full: %s\n", whyFull.c_str());
            }
        } else if (options.compact) {
            whyFull = "asked to";
        } else if (exists) {
            whyFull = "no revision to build on";
        }
        Prepared prep =
                prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount, false, target.parent_path());
        addInkWords(prep, options.inkText);
        prep.encryption = options.encryption;
        step("draw and write the .xopp");
        if (!prep.error.empty()) {
            r.error = prep.error;
            return r;
        }
        r = assemble(prep, target, mode, exportName, titleOf(target));
        r.whyFull = whyFull;
        if (r.ok && options.written) {
            *options.written = revisionAfterFull(target, prep);
            step("read the pages written");
        }
        return r;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result writeArchive(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const LinkMap& links, const std::vector<std::shared_ptr<const ink::PageText>>* inkText) {
    Result r;
    try {
        WorkDir work;
        Prepared prep = prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount, false,
                                target.parent_path(), links.archived || !links.from.empty() ? &links : nullptr);
        addInkWords(prep, inkText);
        if (!prep.error.empty()) {
            r.error = prep.error;
            return r;
        }
        return assemble(prep, target, Mode::Archive, {}, titleOf(target));
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result exportXopp(Document& doc, const fs::path& xopp, const fs::path& pdf, size_t pdfPageCount, bool attached) {
    Result r;
    try {
        WorkDir work;
        // The recordings, as Xournal++ finds them wherever the copy goes: copied into "name.audio" next to it and
        // named there by their absolute paths (qt/docs/audio.md, "Export for Xournal++")
        std::map<std::string, std::string> audioNames;
        {
            std::vector<audio::Recording> recordings;
            fs::path docFile;
            {
                std::shared_lock lock(doc);
                recordings = audio::recordingsOf(doc);
                docFile = doc.getFilepath();
            }
            const fs::path folder = fs::absolute(audio::exportFolderOf(xopp));
            for (const auto& rec: recordings) {
                const fs::path file = audio::find(rec.name, docFile);
                if (file.empty()) {
                    continue;
                }
                std::error_code ec;
                fs::create_directories(folder, ec);
                const fs::path target = folder / file.filename();
                if (!fs::equivalent(file, target, ec)) {
                    fs::copy_file(file, target, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        continue;
                    }
                }
                const std::u8string abs = target.u8string();
                audioNames[rec.name] = std::string(abs.begin(), abs.end());
            }
        }
        const Prepared prep = prepare(doc, pdf.filename().string(), work.path, {}, pdfPageCount, attached, {}, nullptr,
                                      nullptr, &audioNames);
        if (!prep.error.empty()) {
            r.error = prep.error;
            return r;
        }
        bool anyPdfPage = false;
        {
            std::shared_lock lock(doc);
            for (size_t i = 0; i < doc.getPageCount() && !anyPdfPage; ++i) {
                anyPdfPage = doc.getPage(i)->getBackgroundType().isPdfPage();
            }
        }
        if (attached && !anyPdfPage) {
            r.ok = true;  // (no PDF page: the .xopp refers to no PDF, none is written)
        } else {
            r = assemble(prep, pdf, Mode::Plain);
            if (!r.ok) {
                return r;
            }
        }
        if (std::string error; !fileio::writeFileAtomically(xopp, prep.xopp, error)) {
            r.ok = false;
            r.error = error;
            return r;
        }
        for (const auto& [name, data]: prep.extras) {  // attached images: "name.xopp.bg_1.png"
            fs::path extra = xopp;
            extra += name.substr(std::string(DATA_NAME).size());
            writeFile(extra, data);
        }
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
    }
    return r;
}

namespace {
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

namespace {
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
}  // namespace

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
            !bytesOf(dir / CHECK_NAME).empty() || !fs::exists(dir / PAGES_NAME, ec)) {
            return rev;  // (another version of the file, or edited in another app: written in full)
        }
        std::istringstream in(bytesOf(dir / PAGES_NAME));
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

namespace {
/// The picture attachments of a text document (listed in the marker's /Files with a folder in their name,
/// "name.assets/…"; qt/docs/md-images.md) written into `dir`/pictures under their names. The folder is made also when
/// there are none (the cache entry has its pictures then).
void extractPictures(QPDF& q, QPDFObjectHandle marker, const fs::path& dir) {
    const fs::path pictures = dir / PICTURES_NAME;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path tmp = partOf(pictures);
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
}  // namespace

namespace {
/// The recordings a PDF carries (the marker's /Audio) written into `dir`/audio under their names in the document
/// (qt/docs/audio.md). The folder is made also when there are none.
void extractAudio(QPDF& q, QPDFObjectHandle marker, const fs::path& dir) {
    const fs::path folder = dir / AUDIO_NAME;
    std::error_code ec;
    const fs::path tmp = partOf(folder);
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
                        const fs::path tmp = partOf(dir / n);
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
                const fs::path tmp = partOf(check);
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
            std::istringstream in(bytesOf(check));
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
        const fs::path target = dir / ("imported-" + hex(fnv(stamp + std::to_string(keep.size()))) + ".pdf");
        writePdfTo(q, target, {}, fileio::Sync::None);  // (a cache entry)
        PdfEncryption::derive(target, pdf);
        return target;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return {};
}

}  // namespace xqt::HybridPdf

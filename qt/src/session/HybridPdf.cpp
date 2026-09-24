#include "HybridPdf.h"

#include <algorithm>
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
#include <system_error>
#include <unordered_map>

#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4  // as upstream's QPdfExport
#endif
#include <cairo-pdf.h>
#include <cairo.h>
#include <QtGlobal>
#include <glib.h>
#include <qpdf/Buffer.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

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
#include "util/PathUtil.h"
#include "util/Util.h"
#include "view/LayerView.h"
#include "view/View.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/BackgroundView.h"

#include "ArchivePdf.h"
#include "DocumentLink.h"
#include "MdBox.h"
#include "MergedPdf.h"
#include "config.h"

namespace xqt::HybridPdf {

namespace {

constexpr const char* MARKER = "/XournalQt";  ///< in the catalog, and the private key of our annotations
constexpr const char* CLEAN_NAME = "base.pdf";
constexpr const char* CHECK_NAME = "changed.txt";
constexpr double MARGIN = 2.0;  ///< around a layer's elements (pt)

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

std::string stampOf(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) {
        return {};
    }
    const auto time = fs::last_write_time(p, ec);
    return std::to_string(size) + "-" + std::to_string(static_cast<long long>(time.time_since_epoch().count()));
}

uint64_t fnv(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c: s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

std::string hex(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string bytesOf(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool writeFile(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

/// A unique temporary name next to `target` (several threads may make the same clean copy at once).
fs::path partOf(const fs::path& target) {
    static std::atomic<unsigned> counter{0};
    return target.parent_path() / ("." + target.filename().string() + "." + std::to_string(Util::getPid()) + "-" +
                                   std::to_string(++counter) + ".part");
}

/// How an archive PDF is written (PDF/A): never encrypted, at least PDF 1.7, streams with a forbidden filter decoded.
struct ArchiveWrite {
    bool on = false;
    bool recompress = false;
};

void writePdfTo(QPDF& pdf, const fs::path& target, ArchiveWrite archive = {}) {
    const fs::path tmp = partOf(target);
    std::error_code ec;
    try {
        QPDFWriter w(pdf, tmp.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        // The streams of the PDF as they are (decoding and compressing them again doubled the time); new streams
        // without a filter are still compressed
        w.setDecodeLevel(archive.recompress ? qpdf_dl_generalized : qpdf_dl_none);
        if (archive.on) {
            w.setPreserveEncryption(false);
            w.setMinimumPDFVersion("1.7");
            w.setNewlineBeforeEndstream(true);  // (PDF/A: an end of line before "endstream")
        }
        w.write();
    } catch (...) {
        fs::remove(tmp, ec);
        throw;
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("Could not write \"" + target.string() + "\": " + ec.message());
    }
}

std::string pdfDateNow() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);
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
        return c.isStream() ? c.getDict().getKey(MARKER) : QPDFObjectHandle::newNull();
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
        unflatten(p, seen);
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

/// The embedded .xopp: its PDF pages refer to the base pages of the hybrid PDF (page i shows base page i), and its
/// PDF is `pdfName` next to it (`attach`: upstream's attached PDF "name.xopp.bg.pdf", domain "attach", file name
/// "bg.pdf"). Everything else as upstream's SaveHandler writes it.
class HybridSaveHandler: public SaveHandler {
public:
    HybridSaveHandler(std::string pdfName, bool attach): pdfName(std::move(pdfName)), attach(attach) {}

protected:
    void visitPage(XmlNode* root, ConstPageRef p, const Document* doc, int id, const fs::path& target) override {
        struct Access: XmlNode {
            using XmlNode::children;
        };
        constexpr auto children = &Access::children;
        const bool pdf = p->getBackgroundType().isPdfPage();
        const bool first = pdf && !firstPdfPageVisited;
        if (pdf) {
            // (the base class would write the document's own PDF, and save an attached one next to the document)
            firstPdfPageVisited = true;
        }
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
    }

private:
    std::string pdfName;
    bool attach;
};

cairo_status_t appendTo(void* closure, const unsigned char* data, unsigned int length) {
    static_cast<std::string*>(closure)->append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

struct PageSpec {
    double width = 0, height = 0;
    size_t pdfPage = npos;    ///< base page from the background PDF
    size_t drawnPage = npos;  ///< base page drawn by cairo (page of `drawn`)
    size_t annotsFrom = npos; ///< a drawn page: the annotations of other apps on this page of the background PDF
};

struct AnnotSpec {
    size_t page = 0, layer = 0;
    size_t drawnPage = 0;                          ///< its appearance (page of `drawn`)
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

struct Prepared {
    fs::path bg;
    std::vector<PageSpec> pages;
    std::vector<AnnotSpec> annots;
    std::vector<LinkSpec> links;
    std::string drawn;  ///< a PDF (cairo): the generated base pages and the appearance of each annotation
    std::string xopp;
    std::vector<std::pair<std::string, std::string>> extras;  ///< files next to the .xopp (attached images)
    std::string error;
};

/// Everything that needs the document: under its shared lock (and briefly its lock).
Prepared prepare(Document& doc, const std::string& pdfName, const fs::path& work, const BasePageOf& baseOf,
                 size_t pdfPageCount, bool attach = false, const fs::path& linkFolder = {},
                 const LinkMap* linkMap = nullptr) {
    Prepared out;
    {
        std::shared_lock lock(doc);
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
            if (!out.bg.empty() && p->getBackgroundType().isPdfPage() && p->getPdfPageNr() < bgPages) {
                spec.pdfPage = p->getPdfPageNr();
            } else {
                cairo_pdf_surface_set_size(surface, spec.width, spec.height);
                cairo_save(cr);
                xoj::view::BackgroundFlags flags = xoj::view::BACKGROUND_SHOW_ALL;
                flags.showPDF = xoj::view::HIDE_PDF_BACKGROUND;
                xoj::view::BackgroundView::createForPage(p, flags, nullptr)->draw(cr);
                cairo_restore(cr);
                cairo_show_page(cr);
                spec.drawnPage = drawn++;
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
                for (const auto& e: layer->getElementsView()) {
                    const auto& box = e->getBoundingBox();
                    a.x0 = std::min(a.x0, box.x);
                    a.y0 = std::min(a.y0, box.y);
                    a.x1 = std::max(a.x1, box.x + box.width);
                    a.y1 = std::max(a.y1, box.y + box.height);
                    if (e->getType() == ELEMENT_STROKE) {
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
                cairo_pdf_surface_set_size(surface, spec.width, spec.height);
                cairo_save(cr);
                xoj::view::LayerView(layer).draw(xoj::view::Context::createDefault(cr));
                cairo_restore(cr);
                cairo_show_page(cr);
                a.drawnPage = drawn++;
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
    // The .xopp
    const fs::path xopp = work / DATA_NAME;
    HybridSaveHandler h(pdfName, attach);
    {
        std::shared_lock lock(doc);
        h.prepareSave(&doc, xopp);
    }
    h.saveTo(xopp);
    {
        std::unique_lock lock(doc);
        h.updateDocumentInfo(&doc);
    }
    if (!h.getErrorMessage().empty()) {
        out.error = h.getErrorMessage();
        return out;
    }
    out.xopp = bytesOf(xopp);
    std::error_code ec;
    const std::string prefix = std::string(DATA_NAME) + ".";
    for (auto it = fs::directory_iterator(work, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.rfind(prefix, 0) == 0) {
            out.extras.emplace_back(n, bytesOf(it->path()));
        }
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
                 const char* relationship = nullptr) {
    auto stream = QPDFEFStreamObjectHelper::createEFStream(pdf, data);
    stream.setSubtype(name == DATA_NAME ? ArchivePdf::XOPP_MIME : "image/png");
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

/// The base pages in document order, in `out` (the background PDF, or an empty one).
std::vector<QPDFObjectHandle> basePages(QPDF& out, QPDF& drawn, const Prepared& prep) {
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
            order.push_back(h);
        } else {
            helper.addPage(drawnPages.at(spec.drawnPage), false);
            QPDFObjectHandle h = QPDFPageDocumentHelper(out).getAllPages().back().getObjectHandle();
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

/// The links of the Markdown boxes as /Link annotations (ours: removed and written again with the rest), with their
/// hashes in `hashes`.
void annotateLinks(QPDF& out, const Prepared& prep, const std::vector<QPDFObjectHandle>& order, QPDFObjectHandle hashes) {
    std::map<size_t, int> perPage;
    for (const LinkSpec& l: prep.links) {
        QPDFObjectHandle pageObj = order.at(l.page);
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
        const std::string nm = std::string(NAME_PREFIX) + "p" + std::to_string(l.page + 1) + "-link" +
                               std::to_string(++perPage[l.page]);
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
        hashes.replaceKey("/" + nm, QPDFObjectHandle::newString(hashOf(annot)));
        annot = out.makeIndirectObject(annot);
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
}

/// A layer's drawing as a Form XObject of `out`, placed on its base page by its /Matrix (the drawn page is in PDF space,
/// y up; the base page's crop box, its rotation undone), with the box it covers there.
struct Placed {
    QPDFObjectHandle form;
    QPDFObjectHandle::Rectangle rect;
    QPDFMatrix cm;
};
Placed placeLayer(QPDF& out, std::vector<QPDFPageObjectHelper>& drawnPages, const AnnotSpec& a,
                  QPDFObjectHandle pageObj, double h) {
    QPDFPageObjectHelper page(pageObj);
    QPDFObjectHandle form = drawnPages.at(a.drawnPage).getFormXObjectForPage(false);
    QPDFObjectHandle group = form.getDict().getKey("/Group");
    if (group.isDictionary() && group.hasKey("/I")) {
        group.removeKey("/I");  // (not isolated: the highlighter multiplies with the page, as upstream's export)
    }
    const QPDFObjectHandle::Rectangle crop = page.getCropBox().getArrayAsRectangle();
    Placed p;
    p.cm = page.getMatrixForFormXObjectPlacement(form, crop, true, true, true);
    p.form = out.copyForeignObject(form);
    const QPDFObjectHandle::Rectangle box(std::floor(a.x0 * 10) / 10, std::floor((h - a.y1) * 10) / 10,
                                          std::ceil(a.x1 * 10) / 10, std::ceil((h - a.y0) * 10) / 10);
    p.form.getDict().replaceKey("/BBox", QPDFObjectHandle::newArray(box));
    p.form.getDict().replaceKey("/Matrix", QPDFObjectHandle::newArray(p.cm));
    p.rect = p.cm.transformRectangle(box);
    return p;
}

/// An archive PDF: our layers merged into the content of their base pages. The page's own content streams stay as
/// they are; a stream "q" goes before them and a stream after them draws our layers (each a Form XObject "/XqtInkN"
/// in the page's resources, the same drawing as the /AP of a hybrid PDF's annotation). Both streams carry our key
/// with the XObjects and layers they add, so the reader removes exactly them (unflatten). Returns the layers' names.
std::vector<std::string> flatten(QPDF& out, QPDF& drawn, const Prepared& prep, const std::vector<QPDFObjectHandle>& order) {
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
        for (const AnnotSpec* a: specs) {
            const Placed placed = placeLayer(out, drawnPages, *a, pageObj, h);
            int suffix = 1;
            const std::string name = res.getUniqueResourceName("/XqtInk", suffix);
            xobjects.replaceKey(name, placed.form);
            draw += "q " + name + " Do Q\n";
            xnames.appendItem(QPDFObjectHandle::newName(name));
            const std::string nm = nameOf(a->page, a->layer);
            layers.appendItem(QPDFObjectHandle::newUnicodeString(nm));
            names.push_back(nm);
        }
        QPDFObjectHandle before = QPDFObjectHandle::newStream(&out, "q\n");
        before.getDict().replaceKey(MARKER, QPDFObjectHandle::newDictionary());
        QPDFObjectHandle after = QPDFObjectHandle::newStream(&out, draw);
        QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
        mark.replaceKey("/XObjects", xnames);
        mark.replaceKey("/Layers", layers);
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

/// Our annotations onto the base pages; returns the marker's /Annots (name -> hash).
QPDFObjectHandle annotate(QPDF& out, QPDF& drawn, const Prepared& prep, const std::vector<QPDFObjectHandle>& order) {
    QPDFObjectHandle hashes = QPDFObjectHandle::newDictionary();
    if (prep.annots.empty()) {
        annotateLinks(out, prep, order, hashes);
        return hashes;
    }
    std::vector<QPDFPageObjectHelper> drawnPages = QPDFPageDocumentHelper(drawn).getAllPages();
    const std::string now = pdfDateNow();
    for (const AnnotSpec& a: prep.annots) {
        QPDFObjectHandle pageObj = order.at(a.page);
        const double h = prep.pages[a.page].height;
        const Placed placed = placeLayer(out, drawnPages, a, pageObj, h);
        const QPDFMatrix& cm = placed.cm;
        QPDFObjectHandle local = placed.form;
        const QPDFObjectHandle::Rectangle r = placed.rect;

        QPDFObjectHandle annot = QPDFObjectHandle::newDictionary();
        annot.replaceKey("/Type", QPDFObjectHandle::newName("/Annot"));
        const bool ink = !a.strokes.empty();
        annot.replaceKey("/Subtype", QPDFObjectHandle::newName(ink ? "/Ink" : "/Stamp"));
        QPDFObjectHandle rect = QPDFObjectHandle::newArray();
        for (double v: {r.llx, r.lly, r.urx, r.ury}) {
            rect.appendItem(real1(v));
        }
        annot.replaceKey("/Rect", rect);
        const std::string nm = nameOf(a.page, a.layer);
        annot.replaceKey("/NM", QPDFObjectHandle::newUnicodeString(nm));
        annot.replaceKey("/F", QPDFObjectHandle::newInteger(4));  // print
        annot.replaceKey("/M", QPDFObjectHandle::newString(now));
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
        ap.replaceKey("/N", local);
        annot.replaceKey("/AP", ap);
        QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
        mark.replaceKey("/Page", QPDFObjectHandle::newInteger(static_cast<long long>(a.page + 1)));
        mark.replaceKey("/Layer", QPDFObjectHandle::newInteger(static_cast<long long>(a.layer + 1)));
        annot.replaceKey(MARKER, mark);
        hashes.replaceKey("/" + nm, QPDFObjectHandle::newString(hashOf(annot)));
        annot = out.makeIndirectObject(annot);
        // A new array of the page's annotations (its old one may be shared with another page)
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
    annotateLinks(out, prep, order, hashes);
    return hashes;
}

enum class Mode {
    Plain,    ///< the base pages only (the export for Xournal++)
    Hybrid,   ///< and our annotations, data and marker
    Archive,  ///< and our layers merged into the pages, links, data as the source, marker; PDF/A-3b
};

/// The PDF with the base pages (and, by `mode`, our drawing, data and marker), written to `target`.
Result assemble(const Prepared& prep, const fs::path& target, Mode mode, const std::string& xoppExport = {},
                const std::string& title = {}) {
    const bool hybrid = mode != Mode::Plain;
    const bool archive = mode == Mode::Archive;
    Result r;
    Steps step;
    QPDF out;
    out.setSuppressWarnings(true);
    const bool fromBg = std::any_of(prep.pages.begin(), prep.pages.end(),
                                    [](auto& p) { return p.pdfPage != npos || p.annotsFrom != npos; });
    if (fromBg) {
        out.processFile(prep.bg.string().c_str());
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
    const std::vector<QPDFObjectHandle> order = basePages(out, drawn, prep);
    r.pages = order.size();
    step("base pages");
    if (hybrid) {
        QPDFObjectHandle hashes;
        QPDFObjectHandle flattened = QPDFObjectHandle::newArray();
        if (archive) {
            for (const auto& nm: flatten(out, drawn, prep, order)) {
                flattened.appendItem(QPDFObjectHandle::newUnicodeString(nm));
            }
            r.flattened = static_cast<size_t>(flattened.getArrayNItems());
            hashes = QPDFObjectHandle::newDictionary();
            annotateLinks(out, prep, order, hashes);
        } else {
            hashes = annotate(out, drawn, prep, order);
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
        QPDFObjectHandle marker = QPDFObjectHandle::newDictionary();
        marker.replaceKey("/Version", QPDFObjectHandle::newInteger(archive ? ARCHIVE_FORMAT_VERSION : FORMAT_VERSION));
        if (archive) {
            marker.replaceKey("/Archive", QPDFObjectHandle::newBool(true));
            marker.replaceKey("/Flattened", flattened);
        }
        marker.replaceKey("/Data", QPDFObjectHandle::newUnicodeString(DATA_NAME));
        marker.replaceKey("/Files", files);
        marker.replaceKey("/Annots", hashes);
        if (!xoppExport.empty()) {
            marker.replaceKey("/XoppExport", QPDFObjectHandle::newUnicodeString(xoppExport));
        }
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
    step("annotations, data, marker");
    ArchiveWrite how;
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
    writePdfTo(out, target, how);
    step("write");
    r.ok = true;
    return r;
}

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

Result write(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
             const fs::path& xoppExport) {
    Result r;
    try {
        WorkDir work;
        Steps step;
        const Prepared prep =
                prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount, false, target.parent_path());
        step("draw and write the .xopp");
        if (!prep.error.empty()) {
            r.error = prep.error;
            return r;
        }
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
        const Mode mode = fs::exists(target, ec) && isArchive(target) ? Mode::Archive : Mode::Hybrid;
        return assemble(prep, target, mode, exportName, titleOf(target));
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result writeArchive(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const LinkMap& links) {
    Result r;
    try {
        WorkDir work;
        const Prepared prep = prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount, false,
                                      target.parent_path(), links.archived || !links.from.empty() ? &links : nullptr);
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
        const Prepared prep = prepare(doc, pdf.filename().string(), work.path, {}, pdfPageCount, attached);
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
        const fs::path tmp = partOf(xopp);
        if (!writeFile(tmp, prep.xopp)) {
            r.ok = false;
            r.error = "Could not write \"" + xopp.string() + "\"";
            return r;
        }
        std::error_code ec;
        fs::rename(tmp, xopp, ec);
        if (ec) {
            fs::remove(tmp, ec);
            r.ok = false;
            r.error = "Could not write \"" + xopp.string() + "\": " + ec.message();
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
/// 0: no marker, 1: a hybrid PDF, 2: an archive PDF (remembered by path, size and time)
int kindOf(const fs::path& pdf) {
    static std::mutex m;
    static std::unordered_map<std::string, int> known;
    const std::string key = pdf.string() + "|" + stampOf(pdf);
    {
        std::lock_guard lock(m);
        if (auto it = known.find(key); it != known.end()) {
            return it->second;
        }
    }
    int kind = 0;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        QPDFObjectHandle marker = q.getRoot().getKey(MARKER);
        if (marker.isDictionary()) {
            QPDFObjectHandle archive = marker.getKey("/Archive");
            kind = archive.isBool() && archive.getBoolValue() ? 2 : 1;
        }
    } catch (const std::exception&) {
        kind = 0;
    }
    std::lock_guard lock(m);
    if (known.size() > 4096) {
        known.clear();
    }
    known[key] = kind;
    return kind;
}
}  // namespace

bool isHybrid(const fs::path& pdf) { return kindOf(pdf) != 0; }

bool isArchive(const fs::path& pdf) { return kindOf(pdf) == 2; }

fs::path xoppExportOf(const fs::path& pdf) {
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
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
        std::error_code ec;
        if (!fs::exists(check, ec)) {
            QPDF q;
            q.setSuppressWarnings(true);
            q.processFile(pdf.string().c_str());
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
            std::vector<std::pair<std::string, std::string>> files;
            for (const auto& [n, s]: efdh.getEmbeddedFiles()) {
                if (n == name || n.rfind(name + ".", 0) == 0) {
                    auto buffer = s->getEmbeddedFileStream().getStreamData(qpdf_dl_all);
                    files.emplace_back(n == name ? std::string(DATA_NAME) : std::string(DATA_NAME) + n.substr(name.size()),
                                       std::string(reinterpret_cast<const char*>(buffer->getBuffer()),
                                                   buffer->getSize()));
                }
            }
            const std::vector<std::string> changed = strip(q);
            MergedPdf::mark(q, MergedPdf::Kind::Own);  // ("Save as" .xopp puts it next to the .xopp)
            writePdfTo(q, base);
            for (const auto& [n, data]: files) {
                const fs::path tmp = partOf(dir / n);
                if (!writeFile(tmp, data)) {
                    throw std::runtime_error("Could not write into the cache: " + (dir / n).string());
                }
                fs::rename(tmp, dir / n, ec);
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
        {
            std::istringstream in(bytesOf(check));
            for (std::string line; std::getline(in, line);) {
                if (!line.empty()) {
                    o.changed.push_back(line);
                }
            }
        }
        LoadHandler handler(&o.warnings);
        o.document = handler.loadDocument(xopp);
        if (!o.document) {
            o.error = "The Xournal data of this PDF cannot be read.";
            return o;
        }
        {  // (also without PDF pages: the annotations of other apps on its pages are kept from it)
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
        q.processFile(pdf.string().c_str());
        strip(q, std::set<std::string>(keep.begin(), keep.end()));
        MergedPdf::mark(q, MergedPdf::Kind::Own);
        const fs::path target = dir / ("imported-" + hex(fnv(stamp + std::to_string(keep.size()))) + ".pdf");
        writePdfTo(q, target);
        return target;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return {};
}

}  // namespace xqt::HybridPdf

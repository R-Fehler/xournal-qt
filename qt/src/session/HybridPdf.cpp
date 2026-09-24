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
    return std::to_string(size) + "-" + std::to_string(time.time_since_epoch().count());
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

void writePdfTo(QPDF& pdf, const fs::path& target) {
    const fs::path tmp = partOf(target);
    std::error_code ec;
    try {
        QPDFWriter w(pdf, tmp.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);
        // The streams of the PDF as they are (decoding and compressing them again doubled the time); new streams
        // without a filter are still compressed
        w.setDecodeLevel(qpdf_dl_none);
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
    std::vector<std::string> changed;
    std::set<std::string> seen;
    for (auto& page: QPDFPageDocumentHelper(pdf).getAllPages()) {
        QPDFObjectHandle p = page.getObjectHandle();
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
            if (it == expected.end() || it->second != hashOf(a)) {
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
        if (!seen.count(nm)) {
            changed.push_back(nm);  // deleted in another app
        }
    }
    if (root.hasKey(MARKER)) {
        root.removeKey(MARKER);
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

struct Prepared {
    fs::path bg;
    std::vector<PageSpec> pages;
    std::vector<AnnotSpec> annots;
    std::string drawn;  ///< a PDF (cairo): the generated base pages and the appearance of each annotation
    std::string xopp;
    std::vector<std::pair<std::string, std::string>> extras;  ///< files next to the .xopp (attached images)
    std::string error;
};

/// Everything that needs the document: under its shared lock (and briefly its lock).
Prepared prepare(Document& doc, const std::string& pdfName, const fs::path& work, const BasePageOf& baseOf,
                 size_t pdfPageCount, bool attach = false) {
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
                        if (!a.text.empty()) {
                            a.text += "\n";
                        }
                        a.text += static_cast<const Text*>(e)->getText();
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

void addEmbedded(QPDF& pdf, const std::string& name, const std::string& data, const std::string& description) {
    auto stream = QPDFEFStreamObjectHelper::createEFStream(pdf, data);
    stream.setSubtype(name == DATA_NAME ? "application/x-xopp" : "image/png");
    auto spec = QPDFFileSpecObjectHelper::createFileSpec(pdf, name, stream);
    spec.setDescription(description);
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

/// Our annotations onto the base pages; returns the marker's /Annots (name -> hash).
QPDFObjectHandle annotate(QPDF& out, QPDF& drawn, const Prepared& prep, const std::vector<QPDFObjectHandle>& order) {
    QPDFObjectHandle hashes = QPDFObjectHandle::newDictionary();
    if (prep.annots.empty()) {
        return hashes;
    }
    std::vector<QPDFPageObjectHelper> drawnPages = QPDFPageDocumentHelper(drawn).getAllPages();
    const std::string now = pdfDateNow();
    for (const AnnotSpec& a: prep.annots) {
        QPDFObjectHandle pageObj = order.at(a.page);
        QPDFPageObjectHelper page(pageObj);
        const double h = prep.pages[a.page].height;
        QPDFObjectHandle form = drawnPages.at(a.drawnPage).getFormXObjectForPage(false);
        QPDFObjectHandle group = form.getDict().getKey("/Group");
        if (group.isDictionary() && group.hasKey("/I")) {
            group.removeKey("/I");  // (not isolated: the highlighter multiplies with the page, as upstream's export)
        }
        // From the drawn page (PDF space, y up) onto the base page: its crop box, its rotation undone
        const QPDFObjectHandle::Rectangle crop = page.getCropBox().getArrayAsRectangle();
        const QPDFMatrix cm = page.getMatrixForFormXObjectPlacement(form, crop, true, true, true);
        QPDFObjectHandle local = out.copyForeignObject(form);
        const QPDFObjectHandle::Rectangle box(std::floor(a.x0 * 10) / 10, std::floor((h - a.y1) * 10) / 10,
                                              std::ceil(a.x1 * 10) / 10, std::ceil((h - a.y0) * 10) / 10);
        local.getDict().replaceKey("/BBox", QPDFObjectHandle::newArray(box));
        local.getDict().replaceKey("/Matrix", QPDFObjectHandle::newArray(cm));
        const QPDFObjectHandle::Rectangle r = cm.transformRectangle(box);

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
    return hashes;
}

/// The PDF with the base pages (and, `hybrid`, our annotations, data and marker), written to `target`.
Result assemble(const Prepared& prep, const fs::path& target, bool hybrid, const std::string& xoppExport = {}) {
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
        QPDFObjectHandle hashes = annotate(out, drawn, prep, order);
        r.annotations = prep.annots.size();
        step("annotations");
        addEmbedded(out, DATA_NAME, prep.xopp, "The Xournal++ document of this PDF (xournal-qt hybrid PDF)");
        QPDFObjectHandle files = QPDFObjectHandle::newArray();
        for (const auto& [name, data]: prep.extras) {
            addEmbedded(out, name, data, "A file of the Xournal++ document of this PDF");
            files.appendItem(QPDFObjectHandle::newUnicodeString(name));
        }
        QPDFObjectHandle marker = QPDFObjectHandle::newDictionary();
        marker.replaceKey("/Version", QPDFObjectHandle::newInteger(FORMAT_VERSION));
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
    writePdfTo(out, target);
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
        const Prepared prep = prepare(doc, target.filename().string(), work.path, baseOf, pdfPageCount);
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
        return assemble(prep, target, true, exportName);
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
            r = assemble(prep, pdf, false);
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

bool isHybrid(const fs::path& pdf) {
    static std::mutex m;
    static std::unordered_map<std::string, bool> known;
    const std::string key = pdf.string() + "|" + stampOf(pdf);
    {
        std::lock_guard lock(m);
        if (auto it = known.find(key); it != known.end()) {
            return it->second;
        }
    }
    bool hybrid = false;
    try {
        QPDF q;
        q.setSuppressWarnings(true);
        q.processFile(pdf.string().c_str());
        hybrid = q.getRoot().getKey(MARKER).isDictionary();
    } catch (const std::exception&) {
        hybrid = false;
    }
    std::lock_guard lock(m);
    if (known.size() > 4096) {
        known.clear();
    }
    known[key] = hybrid;
    return hybrid;
}

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
            if (QPDFObjectHandle v = marker.getKey("/Version"); v.isInteger() && v.getIntValue() > FORMAT_VERSION) {
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

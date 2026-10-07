/*
 * xournal-qt: everything a write of a PDF with notes needs of the document (HybridInternal.h): the embedded .xopp and
 * the hashes of its layers and backgrounds, the generated base pages and the drawings of the layers (cairo), links,
 * attachments and recordings.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <system_error>

#include <cairo-pdf.h>
#include <cairo.h>
#include <glib.h>
#include <QString>

#include "control/xml/XmlNode.h"
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
#include "util/OutputStream.h"
#include "view/LayerView.h"
#include "view/PaperTone.h"
#include "view/View.h"
#include "view/background/BackgroundFlags.h"
#include "view/background/BackgroundView.h"

#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"
#include "DocumentLink.h"
#include "MdBox.h"
#include "PageBookmarks.h"
#include "StickyNote.h"
#include "config.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

/// The .xopp written into memory (SaveHandler), not into a file.
class StringOutputStream final: public OutputStream {
public:
    void write(const char* d, size_t len) override { data.append(d, len); }
    void close() override {}
    std::string data;
};

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

/// What a drawing of a layer or background of a page of this size shows, as a key (its hash as saved in the .xopp;
/// the version of the app, whose drawing may change).
std::string sigOf(uint64_t saved, double width, double height) {
    std::ostringstream key;
    key << PROJECT_STRING << '|' << std::llround(width * 100) << 'x' << std::llround(height * 100) << '|' << saved;
    return fileio::hex16(fileio::fnv1a(key.str()));
}


/// The words of the text layer of each page (`pages`: the recognised handwriting per page; may be null).
void addInkWords(Prepared& prep, const std::vector<std::shared_ptr<const ink::PageText>>* pages) {
    prep.inkWords.assign(prep.pages.size(), {});
    for (size_t i = 0; pages && i < pages->size() && i < prep.pages.size(); ++i) {
        if (const auto& page = (*pages)[i]) {
            prep.inkWords[i] = InkTextLayer::wordsOf(*page);
        }
    }
}

}  // namespace

namespace detail {

PrepareOptions preparing(const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                         const WriteOptions& options) {
    PrepareOptions o;
    o.baseOf = baseOf;
    o.pdfPageCount = pdfPageCount;
    o.linkFolder = target.parent_path();
    o.inkText = options.inkText;
    o.encryption = options.encryption;
    return o;
}

Prepared prepare(Document& doc, const std::string& pdfName, const fs::path& work, const PrepareOptions& options) {
    const BasePageOf& baseOf = options.baseOf;
    const size_t pdfPageCount = options.pdfPageCount;
    const fs::path& linkFolder = options.linkFolder;
    const LinkMap* linkMap = options.linkMap;
    const Reuse* reuse = options.reuse;
    Prepared out;
    out.encryption = options.encryption;
    // The .xopp first (not written yet): its hashes say what each drawing shows
    const fs::path xopp = work / DATA_NAME;
    HybridSaveHandler h(pdfName, options.attach);
    h.audioNames = options.audioNames;
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
    out.xopp = secret ? std::move(inMemory) : fileio::readFile(xopp);
    std::error_code ec;
    const std::string prefix = std::string(DATA_NAME) + ".";
    for (auto it = fs::directory_iterator(work, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.rfind(prefix, 0) == 0) {
            out.extras.emplace_back(n, fileio::readFile(it->path()));
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
    addInkWords(out, options.inkText);
    return out;
}

}  // namespace detail

}  // namespace xqt::HybridPdf

/*
 * xournal-qt: a PDF with notes written in full (HybridInternal.h): the base pages, our annotations (or, in an archive
 * PDF, our layers in the page content), links, the embedded files, the text layer of the handwriting and the marker.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <system_error>

#include <QString>
#include <qpdf/DLL.h>
#include <qpdf/QPDFEFStreamObjectHelper.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include "ArchivePdf.h"
#include "MergedPdf.h"
#include "PdfKeywords.h"
#include "config.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

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

double round1(double v) { return std::round(v * 10) / 10; }

QPDFObjectHandle real1(double v) { return QPDFObjectHandle::newReal(round1(v), 1); }

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

}  // namespace

namespace detail {

void markDrawn(QPDFObjectHandle page, const std::string& sig) {
    QPDFObjectHandle mark = QPDFObjectHandle::newDictionary();
    mark.replaceKey("/Bg", QPDFObjectHandle::newString(sig));
    page.replaceKey(MARKER, mark);
}

std::string drawnSigOf(QPDFObjectHandle page) {
    QPDFObjectHandle mark = page.isDictionary() ? page.getKey(MARKER) : QPDFObjectHandle::newNull();
    QPDFObjectHandle sig = mark.isDictionary() ? mark.getKey("/Bg") : QPDFObjectHandle::newNull();
    return sig.isString() ? sig.getStringValue() : std::string();
}

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

QPDFObjectHandle layerRecord(const std::string& sig, QPDFObjectHandle form, QPDFObjectHandle annot) {
    QPDFObjectHandle r = QPDFObjectHandle::newArray();
    r.appendItem(QPDFObjectHandle::newString(sig));
    r.appendItem(form);
    r.appendItem(annot);
    return r;
}

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

QPDFObjectHandle spacesList(const Prepared& prep) {
    QPDFObjectHandle list = QPDFObjectHandle::newArray();
    for (size_t i = 0; i < prep.pages.size(); ++i) {
        if (prep.pages[i].pdfPage != npos && !prep.pages[i].space.empty()) {
            list.appendItem(QPDFObjectHandle::newInteger(static_cast<long long>(i)));
        }
    }
    return list;
}

std::vector<PdfBookmarks::Entry> bookmarksOf(const Prepared& prep, const std::vector<QPDFObjectHandle>& order) {
    std::vector<PdfBookmarks::Entry> entries;
    for (size_t i = 0; i < prep.pages.size() && i < order.size(); ++i) {
        if (!prep.pages[i].bookmark.empty()) {
            entries.push_back({order[i], prep.pages[i].bookmark});
        }
    }
    return entries;
}

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

Result assemble(const Prepared& prep, const fs::path& target, Mode mode, const std::string& xoppExport,
                const std::string& title, const HistoryMark* history, const fs::path& writeTo) {
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

}  // namespace detail

}  // namespace xqt::HybridPdf

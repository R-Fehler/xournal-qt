/*
 * xournal-qt: a PDF with notes saved again as an incremental update (HybridInternal.h; qt/docs/features/hybrid-pdf.md,
 * "Saving: incremental updates"): only what changed is appended; pages whose layers and links are as the marker
 * recorded them are not even read.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>

#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>

#include "ArchivePdf.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

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

    /// Page i's boxes for its space for notes (qt/docs/features/note-space.md). Only pages that have or had space are
    /// read; a page of the file whose boxes change is written again, with its annotations (placed on its crop box).
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

    // --- the embedded files, the text layer of the handwriting, the marker (HybridMarker.cpp) -----------------------

    /// The embedded document (and its images) as new streams in the file specifications they had, what is new added
    /// (embedFiles). Other images than before, a recording removed: the whole file is written anew (rare).
    void embedData() { content.files = embedFiles(sink, q, prep, archive, &e.marker, history && !archive); }

    /// The pages whose text layer changed (by the marker's sigs per page), new pages, and pages whose space for notes
    /// changed get theirs anew; the others are not read.
    void placeInkText() {
        QPDFObjectHandle recorded = e.marker.getKey("/InkText");
        content.inkFont = e.marker.getKey("/InkFont");
        content.inkText = writeInkText(sink, prep, order, content.inkFont, [&](size_t i, const std::string& sig) {
            const auto n = static_cast<int>(i);
            const std::string was = recorded.isArray() && n < recorded.getArrayNItems() &&
                                                    recorded.getArrayItem(n).isString()
                                            ? recorded.getArrayItem(n).getStringValue()
                                            : std::string();
            if (u.isNew(order[i])) {
                return false;
            }
            return (sig == was && !spaceChanged.count(i)) || (sig.empty() && was.empty());  // (as it was: not read)
        });
    }

    /// The marker (the hashes, what was drawn, the record, the size of the last full write) and the document
    /// information.
    void mark(const std::string& xoppExport) {
        u.touch(e.marker.isIndirect() ? e.marker : e.root);
        content.archive = archive;
        content.annots = hashes;
        content.flattened = flattened;
        content.layers = record;
        content.xoppExport = xoppExport;
        content.history = history;
        content.base = e.base;
        content.updates = e.updates + 1;
        writeMarker(sink, e.marker, prep, content);
        writeInfo(sink, q, /*modDate=*/!archive);  // (an archive's: ArchivePdf::update)
    }

    Existing& e;
    QPDF& q;
    IncrementalPdf::Update& u;
    UpdateSink sink{u};
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
    MarkerContent content;  ///< what the marker says (embedData, placeInkText, mark)
    size_t annotations = 0;
};

}  // namespace

namespace detail {

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

Result appendChanges(Existing& e, const Prepared& prep, bool archive, const Revision& rev, const HistoryMark* history,
                     const std::string& xoppExport, const fs::path& target, Revision* written, std::string& why) {
    return Appending(e, prep, archive, rev, history).run(xoppExport, target, written, why);
}

std::optional<Result> appendIfPossible(const fs::path& target, const AppendTry& how, std::string& whyFull,
                                       Steps& step) {
    try {
        auto existing = openExisting(target, *how.rev, how.archive, whyFull);
        step("open the file");
        if (!existing) {
            return std::nullopt;
        }
        Prepared prep = how.prepared(&existing->reuse);
        step("draw what changed and write the .xopp");
        if (!prep.error.empty()) {
            Result r;
            r.error = prep.error;
            return r;
        }
        const std::optional<HistoryMark> mark = how.markOf ? std::optional(how.markOf(*existing)) : std::nullopt;
        Result r = appendChanges(*existing, prep, how.archive, *how.rev, mark ? &*mark : nullptr, how.exportName,
                                 target, how.written, whyFull);
        if (r.ok) {
            if (how.appended) {
                r = how.appended(r);
            }
            keepCleanCopy(target, how.cleanCopyOf, prep, existing->tree);
            step("keep the clean copy");
            return r;
        }
        if (!r.error.empty()) {
            return r;  // (the file could not be written: it is as it was)
        }
    } catch (const std::exception& e) {
        whyFull = e.what();
    }
    return std::nullopt;
}

}  // namespace detail

}  // namespace xqt::HybridPdf

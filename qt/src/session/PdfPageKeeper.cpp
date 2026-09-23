#include "PdfPageKeeper.h"

#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <unordered_set>

#include <glib.h>

#include "model/Document.h"
#include "model/XojPage.h"
#include "pdf/base/XojPdfDocument.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "DocumentSession.h"

namespace xqt {

namespace {
/// Size and modification time: whether a file is still the one that was looked at.
std::string stampOf(const fs::path& p) {
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) {
        return {};
    }
    const auto time = fs::last_write_time(p, ec);
    return std::to_string(size) + ":" + std::to_string(time.time_since_epoch().count());
}

bool isPdf(const PageRef& p) { return p->getBackgroundType().isPdfPage(); }

bool contains(const std::vector<size_t>& v, size_t x) { return std::find(v.begin(), v.end(), x) != v.end(); }

/// Copy a file to `target` through a temporary file next to it.
bool copyAtomically(const fs::path& from, const fs::path& target, std::string& error) {
    const fs::path tmp = target.parent_path() / ("." + target.filename().string() + ".part");
    std::error_code ec;
    fs::copy_file(from, tmp, fs::copy_options::overwrite_existing, ec);
    if (!ec) {
        fs::rename(tmp, target, ec);
    }
    if (ec) {
        error = ec.message();
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}
}  // namespace

PdfPageKeeper::PdfPageKeeper(DocumentSession& session): session(session) {
    registerListener(&session);
    trackAll();
}

PdfPageKeeper::~PdfPageKeeper() {
    discardCached();
    // Also one it did not write (a document recovered after a crash): not saved, it is not needed any more
    if (const fs::path bg = session.getDocument()->getPdfFilepath(); MergedPdf::inCache(bg)) {
        std::error_code ec;
        fs::remove(bg, ec);
    }
}

MergedPdf::Kind PdfPageKeeper::kindOf(const fs::path& pdf) {
    const std::string stamp = stampOf(pdf);
    if (pdf != knownPath || stamp != knownStamp) {
        knownPath = pdf;
        knownStamp = stamp;
        knownKind = stamp.empty() ? MergedPdf::Kind::None : MergedPdf::kindOf(pdf);
    }
    return knownKind;
}

bool PdfPageKeeper::switchTo(const fs::path& pdf, std::string& error) {
    {
        // Load it once on its own first: a PDF that does not load must not take the document's PDF away.
        XojPdfDocument probe;
        GError* e = nullptr;
        const bool ok = probe.load(pdf, "", &e);
        if (e) {
            error = e->message;
            g_error_free(e);
        }
        if (!ok) {
            return false;
        }
    }
    Document* doc = session.getDocument();
    if (!doc->readPdf(pdf, /*initPages=*/false, /*attachToDocument=*/false)) {
        error = doc->getLastErrorMsg();
        return false;
    }
    return true;
}

size_t PdfPageKeeper::add(const std::string& pdf, std::string& error) {
    Document* doc = session.getDocument();
    fs::path bg, xopp;
    size_t bgPages = 0;
    {
        std::shared_lock lock(*doc);
        bg = doc->getPdfFilepath();
        xopp = doc->getFilepath();
        bgPages = doc->getPdfPageCount();
    }
    if (!bg.empty() && bgPages == 0) {
        error = "The background PDF of the document is missing.";  // (its pages must keep their numbers)
        return npos;
    }
    const MergedPdf::Kind bgKind = bg.empty() ? MergedPdf::Kind::None : kindOf(bg);
    // Pasted pages go into the cache until the document is saved (nothing is written next to it before)
    const bool inPlace = bgKind != MergedPdf::Kind::None && MergedPdf::inCache(bg);
    fs::path target;
    MergedPdf::Kind kind = bgKind;
    if (inPlace) {
        target = bg;
    } else {
        if (kind == MergedPdf::Kind::None) {
            kind = bg.empty() ? MergedPdf::Kind::Own : MergedPdf::Kind::WithSource;
        }
        target = MergedPdf::cacheFolder() / (std::to_string(Util::getPid()) + "-" + std::to_string(session.serial()) +
                                             "-" + std::to_string(++cacheFiles) + ".pdf");
    }
    const auto r = MergedPdf::append(bg, pdf, target, kind);
    if (!r.ok) {
        error = r.error;
        return npos;
    }
    if (!switchTo(target, error)) {
        return npos;
    }
    if (!inPlace) {
        madeFrom = bgKind == MergedPdf::Kind::None ? bg : fs::path();
        grownFrom = bg;  // (its pages keep their numbers in the new file)
        createdInCache.insert(target);
    }
    knownPath = target;
    knownStamp = stampOf(target);
    knownKind = kind;
    return r.first;
}

fs::path PdfPageKeeper::annotatedPdf() const {
    fs::path bg;
    {
        Document* doc = session.getDocument();
        std::shared_lock lock(*doc);
        bg = doc->getPdfFilepath();
    }
    return MergedPdf::inCache(bg) ? madeFrom : bg;
}

void PdfPageKeeper::discardCached() {
    for (const fs::path& f: createdInCache) {
        std::error_code ec;
        fs::remove(f, ec);
    }
    createdInCache.clear();
}

// --- the pages ------------------------------------------------------------------------------------------------------

auto PdfPageKeeper::track(const PageRef& page) -> Tracked& {
    auto it = tracked.find(page.get());
    if (it != tracked.end() && it->second.page.lock() == page) {
        return it->second;
    }
    Tracked t;
    t.page = page;
    t.last = isPdf(page) ? page->getPdfPageNr() : npos;
    return tracked[page.get()] = std::move(t);
}

void PdfPageKeeper::trackAll() {
    Document* doc = session.getDocument();
    std::vector<PageRef> pages;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            pages.push_back(doc->getPage(i));
        }
    }
    for (auto it = tracked.begin(); it != tracked.end();) {  // (the ones that are gone for good)
        it = it->second.page.expired() ? tracked.erase(it) : std::next(it);
    }
    for (const auto& p: pages) {
        track(p);
    }
}

void PdfPageKeeper::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_CLEARED || type == DOCUMENT_CHANGE_COMPLETE) {
        trackAll();
    }
}

void PdfPageKeeper::pageInserted(size_t index) {
    PageRef page;
    {
        std::shared_lock lock(*session.getDocument());
        if (index >= session.getDocument()->getPageCount()) {
            return;
        }
        page = session.getDocument()->getPage(index);
    }
    Tracked& t = track(page);
    if (t.limbo && isPdf(page)) {
        restore(t.limbo);  // (before the views hear of the page: they come after this listener)
    } else if (isPdf(page)) {
        t.last = page->getPdfPageNr();
    }
}

void PdfPageKeeper::pageChanged(size_t index) {
    PageRef page;
    {
        std::shared_lock lock(*session.getDocument());
        if (index >= session.getDocument()->getPageCount()) {
            return;
        }
        page = session.getDocument()->getPage(index);
    }
    if (!isPdf(page)) {
        return;
    }
    Tracked& t = track(page);
    const size_t nr = page->getPdfPageNr();
    if (nr == t.last && !t.limbo) {
        return;  // (every edit of a page comes by here)
    }
    // An undone background change sets the number the page had then: before a save renumbered it
    if (t.limbo && (nr == t.last || contains(t.aliases, nr))) {
        restore(t.limbo);
    } else if (contains(t.aliases, nr)) {
        std::unique_lock lock(*session.getDocument());
        page->setBackgroundPdfPageNr(t.last);
    } else {
        t.last = nr;
        return;
    }
    session.firePageChanged(index);  // (drawn with the number it came with)
}

void PdfPageKeeper::restore(std::shared_ptr<Limbo> limbo) {  // (a copy: the pages' own are reset here)
    std::string error;
    const size_t first = add(limbo->pdf, error);
    if (first == npos) {
        g_warning("Could not add the PDF pages of pages that came back: %s", error.c_str());
    }
    std::unique_lock lock(*session.getDocument());
    for (auto& [ptr, t]: tracked) {
        if (t.limbo != limbo) {
            continue;
        }
        t.limbo.reset();  // (also when that failed: the pages show no PDF page then, it is not tried again)
        const PageRef p = t.page.lock();
        if (!p || first == npos) {
            continue;
        }
        if (t.last != npos) {
            t.aliases.push_back(t.last);
        }
        t.last = first + t.limboIndex;
        if (isPdf(p)) {
            p->setBackgroundPdfPageNr(t.last);
        }
    }
}

// --- saving ---------------------------------------------------------------------------------------------------------

std::function<bool(int)> PdfPageKeeper::stopSaveAt;

fs::path PdfPageKeeper::placeFor(const fs::path& xopp) {
    fs::path bg;
    {
        std::shared_lock lock(*session.getDocument());
        bg = session.getDocument()->getPdfFilepath();
    }
    const MergedPdf::Kind kind = bg.empty() ? MergedPdf::Kind::None : kindOf(bg);
    if (kind == MergedPdf::Kind::None || xopp.empty()) {
        return {};
    }
    const fs::path pair = MergedPdf::pairOf(xopp), sidecar = MergedPdf::sidecarOf(xopp);
    if (bg == pair || bg == sidecar) {
        return bg;
    }
    std::error_code ec;
    if (kind == MergedPdf::Kind::Own && (!fs::exists(pair, ec) || MergedPdf::kindOf(pair) != MergedPdf::Kind::None)) {
        return pair;  // the library shows "name.xopp" and "name.pdf" as one document (ours: it is replaced)
    }
    return sidecar;
}

namespace {
/// Where a merged PDF is written before it gets the name of the one the saved .xopp refers to.
fs::path stagingOf(const fs::path& place) {
    return place.parent_path() / ("." + place.stem().string() + ".next.pdf");
}
}  // namespace

void PdfPageKeeper::beforeSave(const fs::path& target) {
    Document* doc = session.getDocument();
    fs::path bg;
    size_t count = 0;
    std::vector<size_t> used;  // the PDF pages the document shows
    {
        std::shared_lock lock(*doc);
        bg = doc->getPdfFilepath();
        count = doc->getPdfPageCount();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (const PageRef p = doc->getPage(i); isPdf(p) && p->getPdfPageNr() < count) {
                used.push_back(p->getPdfPageNr());
            }
        }
    }
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());
    if (bg.empty() || count == 0 || used.empty()) {
        return;  // (no PDF page left: the merged PDF stays as it is, the .xopp does not refer to it)
    }
    const MergedPdf::Kind kind = kindOf(bg);
    if (kind == MergedPdf::Kind::None) {
        return;  // the user's PDF: never written
    }
    const fs::path place = placeFor(target);
    const fs::path staging = stagingOf(place);
    std::error_code ec;
    if (bg != staging) {
        fs::remove(staging, ec);  // (left by a save that did not finish, and not used)
    }
    const bool compact = used.size() < count;
    if (!compact && place == bg) {
        return;  // nothing changed
    }

    std::unordered_map<size_t, size_t> renumber;  // old -> new
    std::shared_ptr<Limbo> limbo;
    std::vector<size_t> dropped;  // PDF pages of pages that may come back (undo, redo), in `limbo`
    if (compact) {
        trackAll();
        for (auto& [ptr, t]: tracked) {
            const PageRef p = t.page.lock();
            if (!p || t.limbo) {
                continue;
            }
            const size_t n = isPdf(p) ? p->getPdfPageNr() : t.last;
            if (n != npos && n < count && !std::binary_search(used.begin(), used.end(), n)) {
                dropped.push_back(n);
            }
        }
        std::sort(dropped.begin(), dropped.end());
        dropped.erase(std::unique(dropped.begin(), dropped.end()), dropped.end());
        if (!dropped.empty()) {
            limbo = std::make_shared<Limbo>();
            if (const auto r = MergedPdf::extract(bg, dropped, limbo->pdf); !r.ok) {
                g_warning("Could not keep the PDF pages of undone pages: %s", r.error.c_str());
                used.insert(used.end(), dropped.begin(), dropped.end());  // they stay in the file then
                std::sort(used.begin(), used.end());
                dropped.clear();
                limbo.reset();
            }
        }
        if (used.size() < count) {
            for (size_t k = 0; k < used.size(); ++k) {
                renumber[used[k]] = k;
            }
        }
    }
    if (renumber.empty() && place == bg) {
        return;
    }
    // The saved .xopp may refer to `place`: replaced by a file whose pages have other numbers, it is written under
    // another name first (see DocumentSession::saveImpl). A file that only has pages added is safe to replace.
    const bool keepsNumbers = renumber.empty() && !grownFrom.empty() && fs::exists(grownFrom, ec) &&
                              fs::exists(place, ec) && fs::equivalent(grownFrom, place, ec);
    const bool stage = bg != staging && fs::exists(place, ec) && !keepsNumbers;
    const fs::path writeTo = stage ? staging : place;
    std::string error;
    if (!renumber.empty()) {
        if (const auto r = MergedPdf::keepOnly(bg, used, writeTo); !r.ok) {
            g_warning("Could not write the PDF pages of the document: %s", r.error.c_str());
            return;
        }
    } else {
        bool moved = false;
        if (MergedPdf::inCache(bg)) {
            fs::rename(bg, writeTo, ec);  // (the open PDF keeps reading it)
            moved = !ec;
        }
        if (!moved && !copyAtomically(bg, writeTo, error)) {
            g_warning("Could not write the PDF pages of the document: %s", error.c_str());
            return;
        }
    }
    {
        XojPdfDocument probe;  // (before the pages are renumbered: it must load)
        GError* e = nullptr;
        const bool ok = probe.load(writeTo, "", &e);
        if (e) {
            g_warning("Could not load the PDF pages of the document: %s", e->message);
            g_error_free(e);
        }
        if (!ok) {
            return;
        }
    }

    std::vector<size_t> changed;  // pages of the document with another number now
    if (!renumber.empty()) {
        std::unordered_set<const XojPage*> moved;
        std::unique_lock lock(*doc);
        for (auto& [ptr, t]: tracked) {
            const PageRef p = t.page.lock();
            if (!p || t.limbo) {
                continue;
            }
            const size_t n = isPdf(p) ? p->getPdfPageNr() : t.last;
            if (n == npos || n >= count) {
                continue;
            }
            if (auto it = renumber.find(n); it != renumber.end()) {
                if (it->second != n) {
                    t.aliases.push_back(n);
                    moved.insert(p.get());
                }
                t.last = it->second;
                if (isPdf(p)) {
                    p->setBackgroundPdfPageNr(it->second);
                }
            } else if (limbo) {
                t.limbo = limbo;
                const auto at = std::lower_bound(dropped.begin(), dropped.end(), n);
                t.limboIndex = static_cast<size_t>(at - dropped.begin());
            }
        }
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (moved.count(doc->getPage(i).get())) {
                changed.push_back(i);
            }
        }
        ++numberingNo;
    }
    if (!doc->readPdf(writeTo, /*initPages=*/false, /*attachToDocument=*/false)) {
        g_warning("Could not load the PDF pages of the document: %s", doc->getLastErrorMsg().c_str());
    }
    for (size_t i: changed) {
        session.revisePage(i);  // (drawn with the old PDF meanwhile, maybe)
    }
    if (MergedPdf::inCache(bg)) {
        fs::remove(bg, ec);  // now next to the document
        createdInCache.erase(bg);
        madeFrom.clear();
    }
    if (bg == staging) {
        leftStaging = bg;  // the saved .xopp refers to it until the new one is written
    }
    grownFrom.clear();
    stagedAs = stage ? place : fs::path();
    knownPath = writeTo;
    knownStamp = stampOf(writeTo);
    knownKind = kind;
}

void PdfPageKeeper::commitStaged() {
    Document* doc = session.getDocument();
    fs::path staged;
    {
        std::shared_lock lock(*doc);
        staged = doc->getPdfFilepath();
    }
    // A second name for the same file, then over the old one: the open PDF keeps reading it
    const fs::path tmp = stagedAs.parent_path() / ("." + stagedAs.filename().string() + ".part");
    std::error_code ec;
    fs::remove(tmp, ec);
    ec.clear();
    fs::create_hard_link(staged, tmp, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(staged, tmp, fs::copy_options::overwrite_existing, ec);
    }
    if (!ec) {
        fs::rename(tmp, stagedAs, ec);
    }
    if (ec) {
        g_warning("Could not give the PDF pages of the document their name: %s", ec.message().c_str());
        fs::remove(tmp, ec);
        stagedAs.clear();  // (the .xopp keeps referring to the other name)
        return;
    }
    doc->lock();
    doc->setPdfAttributes(stagedAs, false);
    doc->unlock();
    leftStaging = staged;
    stagedAs.clear();
    knownPath.clear();
}

void PdfPageKeeper::finishStaged() {
    if (!leftStaging.empty()) {
        std::error_code ec;
        fs::remove(leftStaging, ec);
        leftStaging.clear();
    }
}

}  // namespace xqt

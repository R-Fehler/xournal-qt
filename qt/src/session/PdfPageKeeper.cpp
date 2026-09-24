#include "PdfPageKeeper.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <system_error>
#include <unordered_set>

#include <cairo.h>
#include <gio/gio.h>
#include <glib.h>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/PageType.h"
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
    // (its pages are those of the document's PDF, with the same numbers, and the added ones)
    if (!session.loadPdfKeepingPictures(pdf)) {
        error = session.getDocument()->getLastErrorMsg();
        return false;
    }
    return true;
}

namespace {
std::string keyOf(const std::string& pdf) {
    uint64_t h = 1469598103934665603ULL;  // (FNV-1a)
    for (unsigned char c: pdf) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return std::to_string(h) + ":" + std::to_string(pdf.size());
}
}  // namespace

std::function<void()> PdfPageKeeper::beforeMergeWritten;

size_t PdfPageKeeper::add(const std::string& pdf, std::string& error) {
    session.waitForPdfWork();  // (a save that writes the merged PDF on a worker: that first, it may renumber)
    Document* doc = session.getDocument();
    fs::path bg;
    size_t bgPages = 0;
    {
        std::shared_lock lock(*doc);
        bg = doc->getPdfFilepath();
        bgPages = doc->getPdfPageCount();
    }
    if (!bg.empty() && bgPages == 0) {
        error = "The background PDF of the document is missing.";  // (its pages must keep their numbers)
        return npos;
    }
    if (addedNumbering != numberingNo) {
        addedAt.clear();
        addedNumbering = numberingNo;
    }
    std::string key = keyOf(pdf);
    if (auto it = addedAt.find(key); it != addedAt.end()) {
        return it->second;  // added before: the pages are there (or on their way)
    }
    auto merge = std::make_shared<Merge>();
    GError* e = nullptr;
    if (!merge->shown.load(std::make_unique<std::string>(pdf), "", &e) || merge->shown.getPageCount() == 0) {
        error = e ? e->message : "The PDF pages could not be read.";
        if (e) {
            g_error_free(e);
        }
        return npos;
    }
    merge->pdf = pdf;
    merge->pages = merge->shown.getPageCount();
    merge->key = key;
    {
        std::lock_guard lock(pendingMutex);
        merge->first = bgPages + pendingPages;  // (after the pages of the merges before it)
        pendingPages += merge->pages;
        pending.push_back(merge);
    }
    addedAt[key] = merge->first;
    session.queueMerge(merge);
    return merge->first;
}

void PdfPageKeeper::startMerge(Merge& m) {
    Document* doc = session.getDocument();
    {
        std::shared_lock lock(*doc);
        m.bg = doc->getPdfFilepath();
    }
    m.bgKind = m.bg.empty() ? MergedPdf::Kind::None : kindOf(m.bg);
    // Pasted pages go into the cache until the document is saved (nothing is written next to it before)
    m.inPlace = m.bgKind != MergedPdf::Kind::None && MergedPdf::inCache(m.bg);
    m.kind = m.bgKind;
    if (m.inPlace) {
        m.target = m.bg;
    } else {
        if (m.kind == MergedPdf::Kind::None) {
            m.kind = m.bg.empty() ? MergedPdf::Kind::Own : MergedPdf::Kind::WithSource;
        }
        m.target = MergedPdf::cacheFolder() / (std::to_string(Util::getPid()) + "-" + std::to_string(session.serial()) +
                                               "-" + std::to_string(++cacheFiles) + ".pdf");
    }
}

void PdfPageKeeper::writeMerge(Merge& m) {
    if (beforeMergeWritten) {
        beforeMergeWritten();
    }
    // XQT_PASTE_TIMES=1: the time of the steps on stderr (for measuring)
    static const bool times = qEnvironmentVariableIsSet("XQT_PASTE_TIMES");
    auto started = std::chrono::steady_clock::now();
    auto step = [&](const char* what) {
        if (times) {
            const auto now = std::chrono::steady_clock::now();
            std::fprintf(stderr, "paste: %-28s %8.1f ms\n", what,
                         std::chrono::duration<double, std::milli>(now - started).count());
            started = now;
        }
    };
    m.result = MergedPdf::append(m.bg, m.pdf, m.target, m.kind);
    step("append (qpdf, worker)");
    if (!m.result.ok) {
        return;
    }
    // Load it once on its own first: a PDF that does not load must not take the document's PDF away
    XojPdfDocument probe;
    GError* e = nullptr;
    m.loads = probe.load(m.target, "", &e);
    if (e) {
        m.result.error = e->message;
        g_error_free(e);
    }
    step("probe (poppler, worker)");
}

std::string PdfPageKeeper::finishMerge(Merge& m) {
    Document* doc = session.getDocument();
    std::string error;
    // (its pages are those of the document's PDF, with the same numbers, and the added ones)
    const auto started = std::chrono::steady_clock::now();
    const bool ok = m.result.ok && m.loads && session.loadPdfKeepingPictures(m.target);
    if (qEnvironmentVariableIsSet("XQT_PASTE_TIMES")) {
        std::fprintf(stderr, "paste: %-28s %8.1f ms\n", "load it (poppler, UI thread)",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
    }
    if (ok) {
        if (!m.inPlace) {
            madeFrom = m.bgKind == MergedPdf::Kind::None ? m.bg : fs::path();
            grownFrom = m.bg;  // (its pages keep their numbers in the new file)
            createdInCache.insert(m.target);
        }
        knownPath = m.target;
        knownStamp = stampOf(m.target);
        knownKind = m.kind;
        if (auto it = addedAt.find(m.key); it != addedAt.end()) {
            it->second = m.result.first;  // (as it came out)
        }
    } else {
        error = !m.result.error.empty() ? m.result.error : doc->getLastErrorMsg();
        if (!m.inPlace && m.result.ok) {
            std::error_code ec;
            fs::remove(m.target, ec);
        }
        addedAt.erase(m.key);
    }
    // The pages of this merge: with their numbers in it (if it came out otherwise), or as images of their PDF page
    std::unordered_set<const XojPage*> changed;
    const long delta = ok ? static_cast<long>(m.result.first) - static_cast<long>(m.first) : 0;
    {
        std::unique_lock lock(*doc);
        for (auto& [ptr, t]: tracked) {
            const PageRef p = t.page.lock();
            if (!p || !isPdf(p) || p->getPdfPageNr() < m.first || p->getPdfPageNr() >= m.first + m.pages) {
                continue;
            }
            const size_t local = p->getPdfPageNr() - m.first;
            if (ok) {
                if (delta != 0) {
                    t.last = m.result.first + local;
                    p->setBackgroundPdfPageNr(t.last);
                }
            } else {
                if (XojPdfPageSPtr page = m.shown.getPage(local); !page || !toImageBackground(*p, *page)) {
                    p->setBackgroundType(PageType(PageTypeFormat::Plain));  // (the annotations at least)
                }
                t.last = npos;
            }
            changed.insert(p.get());
        }
    }
    {
        std::lock_guard lock(pendingMutex);
        pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const auto& x) { return x.get() == &m; }),
                      pending.end());
        pendingPages -= std::min(pendingPages, m.pages);
    }
    std::vector<size_t> inDocument;
    {
        std::shared_lock lock(*doc);
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (changed.count(doc->getPage(i).get())) {
                inDocument.push_back(i);
            }
        }
    }
    for (size_t i: inDocument) {
        if (!ok || delta != 0) {
            session.firePageChanged(i);  // (drawn again)
        }
        session.revisePage(i);  // (their thumbnails were drawn without the PDF page)
    }
    return error;
}

XojPdfPageSPtr PdfPageKeeper::pendingPage(size_t number) const {
    std::lock_guard lock(pendingMutex);
    for (const auto& m: pending) {
        if (number >= m->first && number < m->first + m->pages) {
            return m->shown.getPage(number - m->first);
        }
    }
    return nullptr;
}

namespace {
cairo_status_t appendPng(void* closure, const unsigned char* data, unsigned int length) {
    auto* buffer = static_cast<std::vector<unsigned char>*>(closure);
    buffer->insert(buffer->end(), data, data + length);
    return CAIRO_STATUS_SUCCESS;
}
}  // namespace

bool PdfPageKeeper::toImageBackground(XojPage& page, const XojPdfPage& pdf, double dpi) {
    const double scale = dpi / 72.0;
    const int w = std::max(1, static_cast<int>(page.getWidth() * scale));
    const int h = std::max(1, static_cast<int>(page.getHeight() * scale));
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    pdf.render(cr);
    cairo_destroy(cr);
    std::vector<unsigned char> png;
    const bool ok = cairo_surface_write_to_png_stream(surface, appendPng, &png) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surface);
    if (!ok) {
        return false;
    }
    GBytes* bytes = g_bytes_new(png.data(), png.size());
    GInputStream* stream = g_memory_input_stream_new_from_bytes(bytes);
    g_bytes_unref(bytes);
    BackgroundImage img;
    GError* error = nullptr;
    img.loadFile(stream, fs::path("pasted-pdf-page.png"), &error);
    g_object_unref(stream);
    if (error) {
        g_warning("Could not convert the PDF page: %s", error->message);
        g_error_free(error);
        return false;
    }
    img.setAttach(true);  // stored in the .xopp
    page.setBackgroundImage(img);
    page.setBackgroundType(PageType(PageTypeFormat::Image));
    return true;
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
    const size_t first = add(limbo->pdf, error);  // (merged in the background; the numbers are known now)
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
    MergedPdf::Kind kind = bg.empty() ? MergedPdf::Kind::None : kindOf(bg);
    bool coming = false;  // the merged PDF it is going to have (pasted pages are being merged)
    if (kind == MergedPdf::Kind::None) {
        std::lock_guard lock(pendingMutex);
        if (!pending.empty()) {
            kind = bg.empty() ? MergedPdf::Kind::Own : MergedPdf::Kind::WithSource;
            coming = true;
        }
    }
    if (kind == MergedPdf::Kind::None || xopp.empty()) {
        return {};
    }
    const fs::path pair = MergedPdf::pairOf(xopp), sidecar = MergedPdf::sidecarOf(xopp);
    if (!coming && (bg == pair || bg == sidecar)) {
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

auto PdfPageKeeper::planSave(const fs::path& target) -> SavePlan {
    SavePlan plan;
    Document* doc = session.getDocument();
    {
        std::shared_lock lock(*doc);
        plan.bg = doc->getPdfFilepath();
        plan.count = doc->getPdfPageCount();
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (const PageRef p = doc->getPage(i); isPdf(p) && p->getPdfPageNr() < plan.count) {
                plan.used.push_back(p->getPdfPageNr());
            }
        }
    }
    std::sort(plan.used.begin(), plan.used.end());
    plan.used.erase(std::unique(plan.used.begin(), plan.used.end()), plan.used.end());
    if (plan.bg.empty() || plan.count == 0 || plan.used.empty()) {
        return plan;  // (no PDF page left: the merged PDF stays as it is, the .xopp does not refer to it)
    }
    plan.kind = kindOf(plan.bg);
    if (plan.kind == MergedPdf::Kind::None) {
        return plan;  // the user's PDF: never written
    }
    plan.place = placeFor(target);
    plan.staging = stagingOf(plan.place);
    plan.compact = plan.used.size() < plan.count;
    if (!plan.compact && plan.place == plan.bg) {
        // nothing changed (a staging file left by a save that did not finish, and not used, goes)
        if (plan.bg != plan.staging) {
            std::error_code ec;
            fs::remove(plan.staging, ec);
        }
        return plan;
    }
    if (plan.compact) {
        trackAll();
        for (auto& [ptr, t]: tracked) {
            const PageRef p = t.page.lock();
            if (!p || t.limbo) {
                continue;
            }
            const size_t n = isPdf(p) ? p->getPdfPageNr() : t.last;
            if (n != npos && n < plan.count && !std::binary_search(plan.used.begin(), plan.used.end(), n)) {
                plan.dropped.push_back(n);
            }
        }
        std::sort(plan.dropped.begin(), plan.dropped.end());
        plan.dropped.erase(std::unique(plan.dropped.begin(), plan.dropped.end()), plan.dropped.end());
    }
    plan.grownFrom = grownFrom;
    plan.needed = true;
    return plan;
}

void PdfPageKeeper::writePlanned(SavePlan& plan) {
    if (!plan.needed) {
        return;
    }
    if (stopSaveAt) {
        stopSaveAt(0);  // (tests: only a place to wait)
    }
    std::error_code ec;
    if (plan.bg != plan.staging) {
        fs::remove(plan.staging, ec);  // (left by a save that did not finish, and not used)
    }
    if (!plan.dropped.empty()) {
        if (const auto r = MergedPdf::extract(plan.bg, plan.dropped, plan.limbo); !r.ok) {
            g_warning("Could not keep the PDF pages of undone pages: %s", r.error.c_str());
            plan.used.insert(plan.used.end(), plan.dropped.begin(), plan.dropped.end());  // they stay in the file then
            std::sort(plan.used.begin(), plan.used.end());
            plan.dropped.clear();
            plan.limbo.clear();
        }
    }
    if (plan.compact && plan.used.size() < plan.count) {
        for (size_t k = 0; k < plan.used.size(); ++k) {
            plan.renumber[plan.used[k]] = k;
        }
    }
    if (plan.renumber.empty() && plan.place == plan.bg) {
        return;
    }
    // The saved .xopp may refer to `place`: replaced by a file whose pages have other numbers, it is written under
    // another name first (see DocumentSession's save). A file that only has pages added is safe to replace.
    const bool keepsNumbers = plan.renumber.empty() && !plan.grownFrom.empty() && fs::exists(plan.grownFrom, ec) &&
                              fs::exists(plan.place, ec) && fs::equivalent(plan.grownFrom, plan.place, ec);
    plan.stage = plan.bg != plan.staging && fs::exists(plan.place, ec) && !keepsNumbers;
    plan.writeTo = plan.stage ? plan.staging : plan.place;
    std::string error;
    if (!plan.renumber.empty()) {
        if (const auto r = MergedPdf::keepOnly(plan.bg, plan.used, plan.writeTo); !r.ok) {
            g_warning("Could not write the PDF pages of the document: %s", r.error.c_str());
            return;
        }
    } else {
        bool moved = false;
        if (MergedPdf::inCache(plan.bg)) {
            fs::rename(plan.bg, plan.writeTo, ec);  // (the open PDF keeps reading it)
            moved = !ec;
        }
        if (!moved && !copyAtomically(plan.bg, plan.writeTo, error)) {
            g_warning("Could not write the PDF pages of the document: %s", error.c_str());
            return;
        }
    }
    {
        XojPdfDocument probe;  // (before the pages are renumbered: it must load)
        GError* e = nullptr;
        const bool ok = probe.load(plan.writeTo, "", &e);
        if (e) {
            g_warning("Could not load the PDF pages of the document: %s", e->message);
            g_error_free(e);
        }
        if (!ok) {
            return;
        }
    }
    plan.written = true;
}

bool PdfPageKeeper::applySave(SavePlan& plan) {
    if (!plan.written) {
        return true;
    }
    Document* doc = session.getDocument();
    std::vector<size_t> changed;  // pages of the document with another number now
    if (!plan.renumber.empty()) {
        std::unique_lock lock(*doc);
        // Pages that came back while the PDF was written (undo, redo) may show a PDF page it dropped: plan again
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            const PageRef p = doc->getPage(i);
            if (isPdf(p) && p->getPdfPageNr() < plan.count && !plan.renumber.count(p->getPdfPageNr())) {
                return false;
            }
        }
        std::shared_ptr<Limbo> limbo;
        if (!plan.limbo.empty()) {
            limbo = std::make_shared<Limbo>();
            limbo->pdf = std::move(plan.limbo);
        }
        std::unordered_set<const XojPage*> moved;
        for (auto& [ptr, t]: tracked) {
            const PageRef p = t.page.lock();
            if (!p || t.limbo) {
                continue;
            }
            const size_t n = isPdf(p) ? p->getPdfPageNr() : t.last;
            if (n == npos || n >= plan.count) {
                continue;
            }
            if (auto it = plan.renumber.find(n); it != plan.renumber.end()) {
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
                const auto at = std::lower_bound(plan.dropped.begin(), plan.dropped.end(), n);
                t.limboIndex = static_cast<size_t>(at - plan.dropped.begin());
            }
        }
        for (size_t i = 0; i < doc->getPageCount(); ++i) {
            if (moved.count(doc->getPage(i).get())) {
                changed.push_back(i);
            }
        }
        ++numberingNo;
    }
    if (!doc->readPdf(plan.writeTo, /*initPages=*/false, /*attachToDocument=*/false)) {
        g_warning("Could not load the PDF pages of the document: %s", doc->getLastErrorMsg().c_str());
    }
    for (size_t i: changed) {
        session.revisePage(i);  // (drawn with the old PDF meanwhile, maybe)
    }
    std::error_code ec;
    if (MergedPdf::inCache(plan.bg)) {
        fs::remove(plan.bg, ec);  // now next to the document
        createdInCache.erase(plan.bg);
        madeFrom.clear();
    }
    if (plan.bg == plan.staging) {
        leftStaging = plan.bg;  // the saved .xopp refers to it until the new one is written
    }
    grownFrom.clear();
    stagedAs = plan.stage ? plan.place : fs::path();
    knownPath = plan.writeTo;
    knownStamp = stampOf(plan.writeTo);
    knownKind = plan.kind;
    return true;
}

void PdfPageKeeper::beforeSave(const fs::path& target) {
    for (int attempt = 0; attempt < 3; ++attempt) {  // (on this thread nothing changes the pages in between: once)
        SavePlan plan = planSave(target);
        writePlanned(plan);
        if (applySave(plan)) {
            return;
        }
    }
}

bool PdfPageKeeper::commitFile(const fs::path& staged, const fs::path& name, std::string& error) {
    // A second name for the same file, then over the old one: the open PDF keeps reading it
    const fs::path tmp = name.parent_path() / ("." + name.filename().string() + ".part");
    std::error_code ec;
    fs::remove(tmp, ec);
    ec.clear();
    fs::create_hard_link(staged, tmp, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(staged, tmp, fs::copy_options::overwrite_existing, ec);
    }
    if (!ec) {
        fs::rename(tmp, name, ec);
    }
    if (ec) {
        error = ec.message();
        std::error_code ignored;
        fs::remove(tmp, ignored);
        return false;
    }
    return true;
}

void PdfPageKeeper::commitApplied(const fs::path& staged, bool committed) {
    if (!committed) {
        stagedAs.clear();  // (the .xopp keeps referring to the other name)
        return;
    }
    Document* doc = session.getDocument();
    doc->lock();
    if (doc->getPdfFilepath() == staged) {  // (unless pages were pasted meanwhile: then it reads another file)
        doc->setPdfAttributes(stagedAs, false);
    }
    doc->unlock();
    leftStaging = staged;
    stagedAs.clear();
    knownPath.clear();
}

void PdfPageKeeper::commitStaged() {
    fs::path staged;
    {
        Document* doc = session.getDocument();
        std::shared_lock lock(*doc);
        staged = doc->getPdfFilepath();
    }
    std::string error;
    const bool ok = commitFile(staged, stagedAs, error);
    if (!ok) {
        g_warning("Could not give the PDF pages of the document their name: %s", error.c_str());
    }
    commitApplied(staged, ok);
}

void PdfPageKeeper::finishStaged() {
    if (!leftStaging.empty()) {
        std::error_code ec;
        fs::remove(leftStaging, ec);
        leftStaging.clear();
    }
}

}  // namespace xqt

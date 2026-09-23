#include "PdfPageKeeper.h"

#include <mutex>
#include <shared_mutex>
#include <system_error>

#include <glib.h>

#include "model/Document.h"
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
}  // namespace

PdfPageKeeper::PdfPageKeeper(DocumentSession& session): session(session) { registerListener(&session); }

PdfPageKeeper::~PdfPageKeeper() { discardCached(); }

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
    const bool inPlace = bgKind != MergedPdf::Kind::None &&
                         (MergedPdf::inCache(bg) ||
                          (!xopp.empty() && (bg == MergedPdf::sidecarOf(xopp) || bg == MergedPdf::pairOf(xopp))));
    fs::path target;
    MergedPdf::Kind kind = bgKind;
    if (inPlace) {
        target = bg;
    } else {
        if (kind == MergedPdf::Kind::None) {
            kind = bg.empty() ? MergedPdf::Kind::Own : MergedPdf::Kind::WithSource;
        }
        std::error_code ec;
        if (xopp.empty()) {
            // Not saved yet: in the cache until the first save puts it next to the document
            target = MergedPdf::cacheFolder() / (std::to_string(Util::getPid()) + "-" +
                                                 std::to_string(session.serial()) + "-" +
                                                 std::to_string(++cacheFiles) + ".pdf");
        } else if (kind == MergedPdf::Kind::Own && !fs::exists(MergedPdf::pairOf(xopp), ec)) {
            target = MergedPdf::pairOf(xopp);  // the library shows "name.xopp" and "name.pdf" as one document
        } else {
            target = MergedPdf::sidecarOf(xopp);
        }
    }
    const auto r = MergedPdf::append(bg, pdf, target, kind);
    if (!r.ok) {
        error = r.error;
        return npos;
    }
    if (!switchTo(target, error)) {
        return npos;
    }
    if (MergedPdf::inCache(target) && !MergedPdf::inCache(bg)) {
        madeFrom = bgKind == MergedPdf::Kind::None ? bg : fs::path();
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

}  // namespace xqt

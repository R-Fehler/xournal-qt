#include "MergedPdf.h"

#include <algorithm>
#include <exception>
#include <system_error>

#include <qpdf/DLL.h>
#if QPDF_MAJOR_VERSION == 11
#define POINTERHOLDER_TRANSITION 4  // as upstream's QPdfExport
#endif
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "util/PathUtil.h"

namespace xqt::MergedPdf {

namespace {
constexpr const char* MARK = "/XournalQtPages";
constexpr const char* PAGES_SUFFIX = ".pages.pdf";

void open(QPDF& pdf, const fs::path& file) {
    pdf.setSuppressWarnings(true);
    pdf.processFile(file.string().c_str());
}

QPDFObjectHandle info(QPDF& pdf) {
    QPDFObjectHandle trailer = pdf.getTrailer();
    QPDFObjectHandle i = trailer.getKey("/Info");
    if (!i.isDictionary()) {
        i = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        trailer.replaceKey("/Info", i);
    }
    return i;
}

void mark(QPDF& pdf, Kind kind) {
    info(pdf).replaceKey(MARK, QPDFObjectHandle::newName(kind == Kind::Own ? "/Own" : "/WithSource"));
    info(pdf).replaceKey("/Producer", QPDFObjectHandle::newString("xournal-qt (pages of a document)"));
}

/// Write the PDF to a temporary file next to `target`, then rename it over `target`.
void writeAtomically(QPDF& pdf, const fs::path& target) {
    fs::path tmp = target.parent_path() / ("." + target.filename().string() + ".part");
    try {
        QPDFWriter w(pdf, tmp.string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);  // smaller: object streams
        w.write();
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("Could not write \"" + target.string() + "\": " + ec.message());
    }
}
}  // namespace

fs::path sidecarOf(const fs::path& xopp) {
    fs::path stem = xopp.filename();
    Util::clearExtensions(stem);
    return xopp.parent_path() / ("." + stem.string() + PAGES_SUFFIX);
}

fs::path pairOf(const fs::path& xopp) {
    fs::path stem = xopp.filename();
    Util::clearExtensions(stem);
    return xopp.parent_path() / (stem.string() + ".pdf");
}

bool isSidecarName(const fs::path& pdf) {
    const std::string n = pdf.filename().string();
    const std::string suffix = PAGES_SUFFIX;
    return n.size() > suffix.size() + 1 && n[0] == '.' &&
           n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0;
}

fs::path cacheFolder() { return Util::getCacheSubfolder("pasted-pages"); }

bool inCache(const fs::path& pdf) {
    if (pdf.empty()) {
        return false;
    }
    const fs::path folder = cacheFolder().lexically_normal();
    return pdf.lexically_normal().parent_path() == folder;
}

Kind kindOf(const fs::path& file) {
    try {
        QPDF pdf;
        open(pdf, file);
        QPDFObjectHandle i = pdf.getTrailer().getKey("/Info");
        if (!i.isDictionary() || !i.hasKey(MARK)) {
            return Kind::None;
        }
        QPDFObjectHandle m = i.getKey(MARK);
        if (m.isName() && m.getName() == "/Own") {
            return Kind::Own;
        }
        return Kind::WithSource;
    } catch (const std::exception&) {
        return Kind::None;
    }
}

Result extract(const fs::path& file, const std::vector<size_t>& pages, std::string& out) {
    Result r;
    try {
        QPDF src;
        open(src, file);
        const std::vector<QPDFPageObjectHelper> all = QPDFPageDocumentHelper(src).getAllPages();
        QPDF dst;
        dst.emptyPDF();
        QPDFPageDocumentHelper helper(dst);
        for (size_t p: pages) {
            if (p >= all.size()) {
                r.error = "The PDF has no page " + std::to_string(p + 1) + ".";
                return r;
            }
            helper.addPage(all[p], false);
        }
        QPDFWriter w(dst);
        w.setOutputMemory();
        w.write();
        auto buffer = w.getBufferSharedPointer();
        out.assign(reinterpret_cast<const char*>(buffer->getBuffer()), buffer->getSize());
        r.pages = pages.size();
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result append(const fs::path& base, const std::string& addition, const fs::path& target, Kind kind) {
    Result r;
    try {
        QPDF pdf;
        if (base.empty()) {
            pdf.emptyPDF();
        } else {
            open(pdf, base);
        }
        QPDFPageDocumentHelper helper(pdf);
        r.first = helper.getAllPages().size();
        QPDF add;  // (must live until the file is written: its streams are copied then)
        add.setSuppressWarnings(true);
        add.processMemoryFile("pasted pages", addition.data(), addition.size());
        for (const auto& page: QPDFPageDocumentHelper(add).getAllPages()) {
            helper.addPage(page, false);
        }
        mark(pdf, kind);
        r.pages = helper.getAllPages().size();
        writeAtomically(pdf, target);
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result keepOnly(const fs::path& source, const std::vector<size_t>& pages, const fs::path& target) {
    Result r;
    try {
        QPDF pdf;
        open(pdf, source);
        QPDFPageDocumentHelper helper(pdf);
        const std::vector<QPDFPageObjectHelper> all = helper.getAllPages();
        for (size_t i = 0; i < all.size(); ++i) {
            if (std::binary_search(pages.begin(), pages.end(), i)) {
                continue;
            }
            helper.removePage(all[i]);
            // Bookmarks and links may still point at the page: it stays in the file then, but without its content.
            QPDFObjectHandle page = all[i].getObjectHandle();
            for (const char* key: {"/Contents", "/Resources", "/Annots", "/Thumb"}) {
                page.removeKey(key);
            }
        }
        r.pages = helper.getAllPages().size();
        writeAtomically(pdf, target);
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace xqt::MergedPdf

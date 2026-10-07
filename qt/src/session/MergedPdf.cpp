#include "MergedPdf.h"

#include <algorithm>
#include <exception>
#include <system_error>

#include <qpdf/DLL.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include "util/PathUtil.h"

#include "FileIo.h"
#include "PdfEncryption.h"

namespace xqt::MergedPdf {

namespace {
constexpr const char* MARK = "/XournalQtPages";
constexpr const char* PAGES_SUFFIX = ".pages.pdf";

void open(QPDF& pdf, const fs::path& file) {
    pdf.setSuppressWarnings(true);
    PdfEncryption::openQpdf(pdf, file);  // (pages of a protected PDF: with its password, and written encrypted)
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

}  // namespace

void mark(QPDF& pdf, Kind kind) {
    if (kind == Kind::None) {
        QPDFObjectHandle i = pdf.getTrailer().getKey("/Info");
        if (i.isDictionary() && i.hasKey(MARK)) {
            i.removeKey(MARK);
        }
        return;
    }
    info(pdf).replaceKey(MARK, QPDFObjectHandle::newName(kind == Kind::Own ? "/Own" : "/WithSource"));
    info(pdf).replaceKey("/Producer", QPDFObjectHandle::newString("xournal-qt (pages of a document)"));
}

void writeAtomically(QPDF& pdf, const fs::path& target) {
    fileio::AtomicFile file(target);
    {
        QPDFWriter w(pdf, file.temp().string().c_str());
        w.setObjectStreamMode(qpdf_o_generate);  // smaller: object streams
        // The streams as they are (decoding and compressing them again took half of the time: 9 s for 1,300 pages)
        w.setDecodeLevel(qpdf_dl_none);
        w.write();
    }
    if (std::string error; !file.commit(error)) {
        throw std::runtime_error(error);
    }
}

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

Result extract(const fs::path& file, const std::vector<size_t>& pages, std::string& out, int quarterTurns) {
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
        if (const int q = ((quarterTurns % 4) + 4) % 4; q != 0) {
            for (QPDFPageObjectHelper& page: helper.getAllPages()) {
                page.rotatePage(90 * q, true);  // (relative: added to the /Rotate it has, an inherited one too)
            }
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
        if (!base.empty()) {
            PdfEncryption::derive(target, base);  // (encrypted as the base is)
        }
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
        PdfEncryption::derive(target, source);
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

}  // namespace xqt::MergedPdf

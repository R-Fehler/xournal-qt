/*
 * xournal-qt: the PDF with notes (HybridPdf.h): write(), writeArchive() and exportXopp(). The parts are in the
 * Hybrid*.cpp files next to it (HybridInternal.h says which is where).
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <atomic>
#include <map>
#include <shared_mutex>
#include <system_error>

#include "model/Document.h"
#include "model/XojPage.h"
#include "util/PathUtil.h"
#include "util/Util.h"

#include "audio/AudioFiles.h"
#include "audio/DocumentAudio.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

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

}  // namespace

namespace detail {

/// The title of a PDF without one: its file name without ".pdf", ".notes.pdf", ".archive.pdf".
std::string titleOf(const fs::path& pdf) {
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

}  // namespace detail

double compactAbove = 0.25;

Result write(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
             const fs::path& xoppExport, const WriteOptions& options) {
    const fileio::FileWriteLock lock(target);  // (its tags or a version's message are not written meanwhile)
    Result r;
    try {
        WorkDir work;
        Steps step;
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
        const bool exists = fs::exists(target, ec);
        const Mode mode = exists && isArchive(target) ? Mode::Archive : Mode::Hybrid;
        if (options.history && options.history->on && !options.compact && mode == Mode::Hybrid) {
            return writeKeeping(doc, target, baseOf, pdfPageCount, exportName, options, work.path);
        }
        std::string whyFull;
        if (options.revision && options.revision->valid() && !options.compact && exists) {
            // Only what changed, appended (qt/docs/hybrid-pdf.md, "Saving: incremental updates")
            try {
                auto existing = openExisting(target, *options.revision, mode == Mode::Archive, whyFull);
                step("open the file");
                if (existing) {
                    PrepareOptions how = preparing(target, baseOf, pdfPageCount, options);
                    how.reuse = &existing->reuse;
                    Prepared prep = prepare(doc, target.filename().string(), work.path, how);
                    step("draw what changed and write the .xopp");
                    if (!prep.error.empty()) {
                        r.error = prep.error;
                        return r;
                    }
                    const std::string was = options.revision->stamp;
                    r = appendChanges(*existing, prep, mode == Mode::Archive, *options.revision, nullptr,
                                      exportName, target, options.written, whyFull);
                    if (r.ok) {
                        keepCleanCopy(target, was, prep, existing->tree);
                        step("keep the clean copy");
                        return r;
                    }
                    if (!r.error.empty()) {
                        return r;  // (the file could not be written: it is as it was)
                    }
                    r = Result();
                }
            } catch (const std::exception& e) {
                whyFull = e.what();
                r = Result();
            }
            if (step.on) {
                std::fprintf(stderr, "hybrid-pdf: written in full: %s\n", whyFull.c_str());
            }
        } else if (options.compact) {
            whyFull = "asked to";
        } else if (exists) {
            whyFull = "no revision to build on";
        }
        Prepared prep =
                prepare(doc, target.filename().string(), work.path, preparing(target, baseOf, pdfPageCount, options));
        step("draw and write the .xopp");
        if (!prep.error.empty()) {
            r.error = prep.error;
            return r;
        }
        r = assemble(prep, target, mode, exportName, titleOf(target));
        r.whyFull = whyFull;
        if (r.ok && options.written) {
            *options.written = revisionAfterFull(target, prep);
            step("read the pages written");
        }
        return r;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}

Result writeArchive(Document& doc, const fs::path& target, const BasePageOf& baseOf, size_t pdfPageCount,
                    const LinkMap& links, const std::vector<std::shared_ptr<const ink::PageText>>* inkText) {
    Result r;
    try {
        WorkDir work;
        PrepareOptions how;
        how.baseOf = baseOf;
        how.pdfPageCount = pdfPageCount;
        how.linkFolder = target.parent_path();
        how.linkMap = links.archived || !links.from.empty() ? &links : nullptr;
        how.inkText = inkText;
        Prepared prep = prepare(doc, target.filename().string(), work.path, how);
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
        // The recordings, as Xournal++ finds them wherever the copy goes: copied into "name.audio" next to it and
        // named there by their absolute paths (qt/docs/audio.md, "Export for Xournal++")
        std::map<std::string, std::string> audioNames;
        {
            std::vector<audio::Recording> recordings;
            fs::path docFile;
            {
                std::shared_lock lock(doc);
                recordings = audio::recordingsOf(doc);
                docFile = doc.getFilepath();
            }
            const fs::path folder = fs::absolute(audio::exportFolderOf(xopp));
            for (const auto& rec: recordings) {
                const fs::path file = audio::find(rec.name, docFile);
                if (file.empty()) {
                    continue;
                }
                std::error_code ec;
                fs::create_directories(folder, ec);
                const fs::path target = folder / file.filename();
                if (!fs::equivalent(file, target, ec)) {
                    fs::copy_file(file, target, fs::copy_options::overwrite_existing, ec);
                    if (ec) {
                        continue;
                    }
                }
                const std::u8string abs = target.u8string();
                audioNames[rec.name] = std::string(abs.begin(), abs.end());
            }
        }
        PrepareOptions how;
        how.pdfPageCount = pdfPageCount;
        how.attach = attached;
        how.audioNames = &audioNames;
        const Prepared prep = prepare(doc, pdf.filename().string(), work.path, how);
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
        if (std::string error; !fileio::writeFileAtomically(xopp, prep.xopp, error)) {
            r.ok = false;
            r.error = error;
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

}  // namespace xqt::HybridPdf

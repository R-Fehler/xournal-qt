/*
 * xournal-qt: the clean copies of PDFs with notes in the app cache (HybridPdf.h, HybridInternal.h): one folder per
 * version of a file (its path's hash and its stamp), retained while a document uses it, pruned when a file is opened.
 *
 * @license GNU GPLv2 or later
 */
#include "HybridInternal.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <mutex>
#include <system_error>

#include "util/PathUtil.h"
#include "util/Util.h"

namespace xqt::HybridPdf {
using namespace detail;

namespace {

std::mutex cacheMutex;
std::map<fs::path, int>& retained() {
    static std::map<fs::path, int> r;
    return r;
}

}  // namespace

namespace detail {

fs::path entryOf(const fs::path& pdf, const std::string& stamp) {
    std::error_code ec;
    const fs::path abs = fs::weakly_canonical(pdf, ec);
    return cacheFolder() / (hex(fnv((ec ? pdf : abs).string())) + "-" + stamp);
}

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

std::string pagesText(const std::vector<QPDFObjectHandle>& pages) {
    std::string out;
    for (QPDFObjectHandle p: pages) {
        out += std::to_string(p.getObjectID()) + " " + std::to_string(p.getGeneration()) + "\n";
    }
    return out;
}

void keepCleanCopy(const fs::path& target, const std::string& was, const Prepared& prep,
                   const std::vector<QPDFObjectHandle>& pages) {
    std::error_code ec;
    const fs::path from = entryOf(target, was);
    const fs::path to = entryOf(target, stampOf(target));
    if (!fs::exists(from / CHECK_NAME, ec) || !fs::exists(from / CLEAN_NAME, ec) || !bytesOf(from / CHECK_NAME).empty() ||
        fs::exists(to / CHECK_NAME, ec)) {
        return;
    }
    const std::string list = pagesText(pages);
    if (bytesOf(from / PAGES_NAME) != list) {
        return;  // (pages added, removed or moved: the next open makes a clean copy of this version)
    }
    fs::create_directories(to, ec);
    fs::create_hard_link(from / CLEAN_NAME, to / CLEAN_NAME, ec);
    if (ec) {
        ec.clear();
        fs::copy_file(from / CLEAN_NAME, to / CLEAN_NAME, fs::copy_options::overwrite_existing, ec);
    }
    // (a protected PDF: not its Xournal data, which is read from the file at each opening)
    const bool secret = PdfEncryption::isProtected(target);
    if (ec || !writeFile(to / PAGES_NAME, list) || (!secret && !writeFile(to / DATA_NAME, prep.xopp))) {
        fs::remove_all(to, ec);
        return;
    }
    for (const auto& [name, data]: prep.extras) {
        if (!secret) {
            writeFile(to / name, data);
        }
    }
    const fs::path tmp = partOf(to / CHECK_NAME);
    writeFile(tmp, "");
    fs::rename(tmp, to / CHECK_NAME, ec);  // last: the entry is complete
}

void keepCacheEntry(const fs::path& pdf, const std::string& was) {
    std::error_code ec;
    const fs::path from = entryOf(pdf, was);
    const fs::path to = entryOf(pdf, stampOf(pdf));
    if (from == to || !fs::exists(from / CHECK_NAME, ec) || fs::exists(to / CHECK_NAME, ec)) {
        return;
    }
    fs::create_directories(to, ec);
    for (auto it = fs::directory_iterator(from, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->path().filename() == CHECK_NAME || !it->is_regular_file()) {
            continue;
        }
        std::error_code lec;
        fs::create_hard_link(it->path(), to / it->path().filename(), lec);
        if (lec) {
            fs::copy_file(it->path(), to / it->path().filename(), fs::copy_options::overwrite_existing, lec);
        }
    }
    fs::copy_file(from / CHECK_NAME, to / CHECK_NAME, fs::copy_options::overwrite_existing, ec);  // (last: complete)
}

}  // namespace detail

void markUnpacked(const fs::path& folder) {
    std::error_code ec;
    if (fs::is_directory(folder, ec)) {
        writeFile(folder / (std::string(UNPACKED_PREFIX) + std::to_string(Util::getPid())), "");
    }
}

void dropExtracted(const fs::path& base) {
    std::error_code ec;
    fs::remove_all(base.parent_path() / PICTURES_NAME, ec);
    fs::remove_all(base.parent_path() / AUDIO_NAME, ec);
    fs::remove(base.parent_path() / (std::string(UNPACKED_PREFIX) + std::to_string(Util::getPid())), ec);
}

int removeUnpackedLeftovers(const std::vector<fs::path>& roots, const std::function<bool(int64_t)>& alive,
                            const std::function<void(const fs::path&)>& remove) {
    int removed = 0;
    const std::string prefix = UNPACKED_PREFIX;
    for (const fs::path& root: roots) {
        std::error_code ec;
        for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            std::error_code dec;
            if (!it->is_directory(dec)) {
                continue;
            }
            bool stale = false;
            for (auto f = fs::directory_iterator(it->path(), dec); !dec && f != fs::directory_iterator();
                 f.increment(dec)) {
                const std::string n = f->path().filename().string();
                if (n.rfind(prefix, 0) == 0) {
                    try {
                        const int64_t pid = std::stoll(n.substr(prefix.size()));
                        if (alive(pid)) {
                            stale = false;  // (a process that still runs has it open)
                            break;
                        }
                        stale = true;
                    } catch (const std::exception&) {
                        stale = true;
                    }
                }
            }
            if (stale) {
                remove(it->path());
                ++removed;
            }
        }
    }
    return removed;
}

int removeProtectedLeftovers(const std::function<bool(int64_t)>& alive) {
    // The clean copies' entries: their pictures and recordings (the encrypted clean copy stays); the documents' work
    // folders of Markdown pictures: whole
    int removed = removeUnpackedLeftovers({cacheFolder()}, alive, [](const fs::path& dir) {
        std::error_code ec;
        fs::remove_all(dir / PICTURES_NAME, ec);
        fs::remove_all(dir / AUDIO_NAME, ec);
        for (auto it = fs::directory_iterator(dir, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (it->path().filename().string().rfind(UNPACKED_PREFIX, 0) == 0) {
                std::error_code rec;
                fs::remove(it->path(), rec);
            }
        }
    });
    removed += removeUnpackedLeftovers({Util::getCacheSubfolder("md-assets")}, alive,
                                       [](const fs::path& dir) {
                                           std::error_code ec;
                                           fs::remove_all(dir, ec);
                                       });
    return removed;
}

int forgetCopies(const fs::path& pdf) {
    const std::string prefix = entryOf(pdf, "").filename().string();  // (the path's hash and "-")
    std::lock_guard lock(cacheMutex);
    std::error_code ec;
    int removed = 0;
    for (auto it = fs::directory_iterator(cacheFolder(), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const fs::path dir = it->path();
        if (dir.filename().string().rfind(prefix, 0) != 0 || !it->is_directory()) {
            continue;
        }
        if (std::any_of(retained().begin(), retained().end(),
                        [&](const auto& r) { return r.first.parent_path() == dir.lexically_normal(); })) {
            continue;
        }
        std::error_code rec;
        fs::remove_all(dir, rec);
        removed += !rec;
    }
    return removed;
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

}  // namespace xqt::HybridPdf

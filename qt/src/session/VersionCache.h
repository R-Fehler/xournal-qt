/*
 * xournal-qt: the versions of PDFs with notes cut out of their files to be shown or opened (version history,
 * PdfHistory.h): HybridPdf::writeVersion writes each once, into the app cache.
 *
 * The owner of those files. Its limit: the last 5 versions used, at most 500 MB; older ones are removed as new ones
 * come, all of this process's when the app quits (clear()), and those other processes left behind (a crash) after a
 * day. Each lives in a folder of its own, under a readable name ("lecture (version 3).pdf"): it is what a tab shows.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <list>
#include <mutex>
#include <string>

#include "filesystem.h"

namespace xqt {

class VersionCache {
public:
    static VersionCache& instance();

    /// The file of version `id` of `pdf` as it is now (made when it is not there yet; any thread). Empty: it could
    /// not be made (`error`).
    fs::path get(const fs::path& pdf, int id, std::string& error);
    /// Whether a file is one of these.
    bool contains(const fs::path& file) const;
    /// Remove every file of this process (when the app quits).
    void clear();
    /// The folder of this process's files.
    fs::path folder() const;

    size_t limitCount = 5;
    uint64_t limitBytes = 500ull << 20;

private:
    VersionCache();
    struct Entry {
        std::string key;
        fs::path file;
        uint64_t bytes = 0;
    };
    void trim();
    mutable std::mutex m;
    std::list<Entry> entries;  ///< most recently used first
};

}  // namespace xqt

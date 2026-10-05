/*
 * xournal-qt: the versions of PDFs with notes cut out of their files to be shown or opened (version history,
 * PdfHistory.h): HybridPdf::writeVersion writes each once, into the app cache.
 *
 * The owner of those files. Its limit: the last 5 versions used, at most 500 MB; older ones are removed as new ones
 * come, all of this process's when the app quits (clear()), and those other processes left behind (a crash) after a
 * day. A version that is shown (a tab holds it: beside the document, compared, opened) is pinned (pin()): the limit
 * never removes it and counts only the versions nobody shows; unpinned, it is an unused one again. Each lives in a folder of its own, under a readable name ("lecture (version 3).pdf"): it is what a tab shows.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstdint>
#include <list>
#include <map>
#include <memory>
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
    /// Keep the file of a version while what this returns is held (a tab that shows it; nothing for another file).
    /// Released, the limit applies to it again.
    using Pin = std::shared_ptr<void>;
    Pin pin(const fs::path& file);
    bool pinned(const fs::path& file) const;
    /// Whether a file is one of these (any thread; cheap: documents of versions are shown read-only, and
    /// DocumentSession::isReadOnly asks).
    bool contains(const fs::path& file) const;
    /// Remove every file of this process (when the app quits).
    void clear();
    /// Remove the versions cut out of `pdf` (it was protected with a password: no unencrypted copy of it stays).
    void forget(const fs::path& pdf);
    /// The PDF a file of these was cut out of (empty: not one of these).
    fs::path sourceOf(const fs::path& file) const;
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
        fs::path source;  ///< the PDF it was cut out of
    };
    void trim();  ///< (under `m`)
    std::map<fs::path, int> pins;  ///< files shown, how many times
    fs::path ownFolder;  ///< folder(), normalised
    mutable std::mutex m;
    std::list<Entry> entries;  ///< most recently used first
};

}  // namespace xqt

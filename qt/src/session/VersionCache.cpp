#include "VersionCache.h"

#include <chrono>
#include <system_error>

#include <QCryptographicHash>

#include "util/PathUtil.h"
#include "util/Util.h"

#include "HybridPdf.h"

namespace xqt {

namespace {
std::string keyOf(const fs::path& pdf, int id) {
    std::error_code ec;
    const auto size = fs::file_size(pdf, ec);
    const auto time = fs::last_write_time(pdf, ec);
    const std::string text = fs::absolute(pdf, ec).string() + "|" + std::to_string(size) + "|" +
                             std::to_string(static_cast<long long>(time.time_since_epoch().count()));
    return QCryptographicHash::hash(QByteArray::fromStdString(text), QCryptographicHash::Sha1).toHex().left(16).toStdString() +
           "-" + std::to_string(id);
}
}  // namespace

VersionCache& VersionCache::instance() {
    static VersionCache cache;
    return cache;
}

VersionCache::VersionCache() {
    ownFolder = (Util::getCacheSubfolder("versions") / std::to_string(Util::getPid())).lexically_normal();
    // What other processes left behind (a crash): after a day
    std::error_code ec;
    const fs::path root = Util::getCacheSubfolder("versions");
    const auto now = fs::file_time_type::clock::now();
    for (auto it = fs::directory_iterator(root, ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code tec;
        if (it->path() != folder() && now - fs::last_write_time(it->path(), tec) > std::chrono::hours(24) && !tec) {
            fs::remove_all(it->path(), tec);
        }
    }
}

fs::path VersionCache::folder() const {
    return Util::getCacheSubfolder("versions") / std::to_string(Util::getPid());
}

fs::path VersionCache::get(const fs::path& pdf, int id, std::string& error) {
    const std::string key = keyOf(pdf, id);
    {
        std::lock_guard lock(m);
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            std::error_code ec;
            if (it->key == key && fs::exists(it->file, ec)) {
                entries.splice(entries.begin(), entries, it);
                return entries.front().file;
            }
        }
    }
    const fs::path dir = folder() / key;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    fs::path name = pdf.stem();
    name += " (version " + std::to_string(id) + ").pdf";
    const fs::path file = dir / name;
    if (!HybridPdf::writeVersion(pdf, id, file, error)) {
        fs::remove_all(dir, ec);
        return {};
    }
    std::lock_guard lock(m);
    entries.push_front({key, file, fs::file_size(file, ec)});
    trim();
    return file;
}

auto VersionCache::pin(const fs::path& file) -> Pin {
    if (!contains(file)) {
        return {};
    }
    const fs::path key = file.lexically_normal();
    {
        std::lock_guard lock(m);
        ++pins[key];
    }
    return Pin(nullptr, [this, key](void*) {
        std::lock_guard lock(m);
        if (auto it = pins.find(key); it != pins.end() && --it->second <= 0) {
            pins.erase(it);
            trim();  // (unused again: over the limit, it goes)
        }
    });
}

bool VersionCache::pinned(const fs::path& file) const {
    std::lock_guard lock(m);
    return pins.count(file.lexically_normal()) > 0;
}

void VersionCache::trim() {
    uint64_t total = 0;
    size_t n = 0;
    for (auto it = entries.begin(); it != entries.end();) {
        if (pins.count(it->file.lexically_normal())) {
            ++it;  // (shown: never removed, not counted)
            continue;
        }
        total += it->bytes;
        ++n;
        if (n > 1 && (n > limitCount || total > limitBytes)) {
            std::error_code ec;
            fs::remove_all(it->file.parent_path(), ec);
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

bool VersionCache::contains(const fs::path& file) const {
    if (file.empty()) {
        return false;
    }
    const fs::path f = file.lexically_normal();
    return f.parent_path().parent_path() == ownFolder;
}

void VersionCache::clear() {
    std::lock_guard lock(m);
    entries.clear();
    std::error_code ec;
    fs::remove_all(folder(), ec);
}

}  // namespace xqt

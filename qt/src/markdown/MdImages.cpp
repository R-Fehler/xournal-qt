#include "MdImages.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <list>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include "filesystem.h"

namespace xqt::md::images {

namespace {

fs::path pathOf(const std::string& utf8) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}
std::string utf8Of(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool isDriveLetterPath(std::string_view s) {
    return s.size() >= 3 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':' && (s[2] == '/' || s[2] == '\\');
}

bool isAbsolute(std::string_view s) { return !s.empty() && (s[0] == '/' || s[0] == '\\' || isDriveLetterPath(s)); }

/// "scheme:" at the start (a Windows drive letter is none).
bool hasScheme(std::string_view s) {
    const auto colon = s.find(':');
    if (colon == std::string_view::npos || colon < 2) {
        return false;
    }
    for (size_t i = 0; i < colon; ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (!(std::isalnum(c) || c == '+' || c == '-' || c == '.')) {
            return false;
        }
    }
    return std::isalpha(static_cast<unsigned char>(s[0]));
}

bool isFile(const std::string& path) {
    std::error_code ec;
    return !path.empty() && fs::is_regular_file(pathOf(path), ec);
}

// --- roots ------------------------------------------------------------------------------------------------------

struct Roots {
    std::mutex mtx;
    std::vector<std::pair<uint64_t, Root>> list;  ///< oldest first
    uint64_t nextId = 1;
    std::string webDir;
};
Roots& roots() {
    static Roots r;
    return r;
}
std::atomic<uint64_t> generationCounter{1};

uint64_t addRoot(Root root) {
    auto& r = roots();
    std::lock_guard lock(r.mtx);
    const uint64_t id = r.nextId++;
    r.list.emplace_back(id, std::move(root));
    changed();
    return id;
}
void removeRoot(uint64_t id) {
    auto& r = roots();
    std::lock_guard lock(r.mtx);
    r.list.erase(std::remove_if(r.list.begin(), r.list.end(), [id](const auto& e) { return e.first == id; }),
                 r.list.end());
    changed();
}

// --- the decoder --------------------------------------------------------------------------------------------------

bool pixbufSize(const std::string& path, int& w, int& h) {
    return gdk_pixbuf_get_file_info(path.c_str(), &w, &h) != nullptr && w > 0 && h > 0;
}

std::shared_ptr<Pixels> pixbufDecode(const std::string& path, int w, int h) {
    GError* err = nullptr;
    GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_scale(path.c_str(), w, h, false, &err);
    if (!pb) {
        if (err) {
            g_error_free(err);
        }
        return nullptr;
    }
    auto out = std::make_shared<Pixels>();
    out->width = gdk_pixbuf_get_width(pb);
    out->height = gdk_pixbuf_get_height(pb);
    out->stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, out->width);
    out->data.resize(static_cast<size_t>(out->stride) * out->height);
    const int channels = gdk_pixbuf_get_n_channels(pb);
    const bool alpha = gdk_pixbuf_get_has_alpha(pb);
    const int inStride = gdk_pixbuf_get_rowstride(pb);
    const guchar* in = gdk_pixbuf_read_pixels(pb);
    for (int y = 0; y < out->height; ++y) {
        const guchar* p = in + static_cast<ptrdiff_t>(y) * inStride;
        auto* q = reinterpret_cast<uint32_t*>(out->data.data() + static_cast<ptrdiff_t>(y) * out->stride);
        for (int x = 0; x < out->width; ++x, p += channels) {
            const uint32_t a = alpha ? p[3] : 255;
            const auto pre = [a](uint32_t c) { return (c * a + 127) / 255; };
            q[x] = (a << 24) | (pre(p[0]) << 16) | (pre(p[1]) << 8) | pre(p[2]);
        }
    }
    g_object_unref(pb);
    return out;
}

struct DecoderSlot {
    std::mutex mtx;
    Decoder decoder{pixbufSize, pixbufDecode};
};
DecoderSlot& decoderSlot() {
    static DecoderSlot d;
    return d;
}
Decoder currentDecoder() {
    auto& d = decoderSlot();
    std::lock_guard lock(d.mtx);
    return d.decoder;
}

// --- caches -------------------------------------------------------------------------------------------------------

struct Caches {
    std::mutex mtx;
    struct Size {
        bool ok = false;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<std::string, Size> sizes;  ///< by stamp
    using Entry = std::pair<std::string, std::shared_ptr<const Pixels>>;
    std::list<Entry> lru;  ///< most recently used first
    std::unordered_map<std::string, std::list<Entry>::iterator> byKey;
    size_t bytes = 0;
    size_t hits = 0;
    size_t decoded = 0;
};
Caches& caches() {
    static Caches c;
    return c;
}

size_t bytesOf(const Pixels& p) { return p.data.size() + sizeof(Pixels); }

/// FNV-1a: the same name for the same address in every run (the web cache's file names).
uint64_t fnv1a(std::string_view s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c: s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

bool isJpegFile(const std::string& path) {
    std::FILE* f = g_fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    unsigned char head[3] = {};
    const bool jpeg = std::fread(head, 1, 3, f) == 3 && head[0] == 0xFF && head[1] == 0xD8 && head[2] == 0xFF;
    std::fclose(f);
    return jpeg;
}

std::string readFile(const std::string& path, size_t limit) {
    std::string out;
    std::FILE* f = g_fopen(path.c_str(), "rb");
    if (!f) {
        return out;
    }
    char buf[65536];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
        out.append(buf, n);
        if (out.size() > limit) {
            out.clear();
            break;
        }
    }
    std::fclose(f);
    return out;
}

const cairo_user_data_key_t PIXELS_KEY{};

}  // namespace

// --- links ------------------------------------------------------------------------------------------------------

LinkKind kindOf(std::string_view link) {
    link = trimmed(link);
    if (startsWithNoCase(link, "http://") || startsWithNoCase(link, "https://")) {
        return LinkKind::Web;
    }
    if (startsWithNoCase(link, "file:") || !hasScheme(link)) {
        return LinkKind::Local;
    }
    return LinkKind::Unsupported;
}

std::string percentDecoded(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string relativePath(std::string_view link) {
    link = trimmed(link);
    if (link.empty() || isAbsolute(link) || hasScheme(link)) {
        return {};
    }
    while (link.substr(0, 2) == "./") {
        link.remove_prefix(2);
        while (!link.empty() && link.front() == '/') {
            link.remove_prefix(1);
        }
    }
    return std::string(link);
}

RootHandle::RootHandle(Root root): id(addRoot(root)), current(std::move(root)) {}
RootHandle::~RootHandle() { reset(); }
RootHandle::RootHandle(RootHandle&& other) noexcept: id(std::exchange(other.id, 0)), current(std::move(other.current)) {}
RootHandle& RootHandle::operator=(RootHandle&& other) noexcept {
    if (this != &other) {
        reset();
        id = std::exchange(other.id, 0);
        current = std::move(other.current);
    }
    return *this;
}
void RootHandle::set(Root root) {
    reset();
    id = addRoot(root);
    current = std::move(root);
}
void RootHandle::reset() {
    if (id != 0) {
        removeRoot(id);
        id = 0;
    }
    current = {};
}

std::vector<std::string> candidates(std::string_view given) {
    std::vector<std::string> out;
    const auto add = [&out](const std::string& p) {
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) {
            out.push_back(p);
        }
    };
    const std::string_view link = trimmed(given);
    switch (kindOf(link)) {
        case LinkKind::Web:
            add(webCachePath(link));
            return out;
        case LinkKind::Unsupported:
            return out;
        case LinkKind::Local:
            break;
    }
    if (startsWithNoCase(link, "file:")) {
        std::string_view p = link.substr(5);
        if (p.substr(0, 2) == "//") {
            p.remove_prefix(2);
            if (startsWithNoCase(p, "localhost/")) {
                p.remove_prefix(9);
            }
        }
        std::string path = percentDecoded(p);
        if (path.size() >= 4 && path[0] == '/' && isDriveLetterPath(std::string_view(path).substr(1))) {
            path.erase(0, 1);  // (file:///C:/x)
        }
        add(path);
        return out;
    }
    if (isAbsolute(link)) {
        add(std::string(link));
        add(percentDecoded(link));
        return out;
    }
    const std::string rel = relativePath(link);
    if (rel.empty()) {
        return out;
    }
    const std::string decoded = percentDecoded(rel);
    std::vector<Root> list;
    {
        auto& r = roots();
        std::lock_guard lock(r.mtx);
        for (auto it = r.list.rbegin(); it != r.list.rend(); ++it) {
            list.push_back(it->second);
        }
    }
    for (const Root& root: list) {
        for (const std::string* p: {&rel, &decoded}) {
            const auto slash = p->find('/');
            if (!root.assetsDir.empty() && !root.assetsName.empty() && slash != std::string::npos &&
                p->compare(0, slash, root.assetsName) == 0) {
                add(utf8Of(pathOf(root.assetsDir) / pathOf(p->substr(slash + 1))));
            }
            if (!root.baseDir.empty()) {
                add(utf8Of(pathOf(root.baseDir) / pathOf(*p)));
            }
        }
    }
    return out;
}

std::string resolve(std::string_view link) {
    for (const std::string& c: candidates(link)) {
        if (isFile(c)) {
            return c;
        }
    }
    return {};
}

uint64_t generation() { return generationCounter.load(std::memory_order_acquire); }
void changed() { generationCounter.fetch_add(1, std::memory_order_acq_rel); }

// --- pictures -----------------------------------------------------------------------------------------------------

Info info(std::string_view link) {
    Info i;
    const LinkKind kind = kindOf(link);
    if (kind == LinkKind::Unsupported) {
        i.state = Info::State::Unsupported;
        return i;
    }
    i.path = resolve(link);
    if (i.path.empty()) {
        i.state = kind == LinkKind::Web ? Info::State::Web : Info::State::Missing;
        return i;
    }
    std::error_code ec;
    const fs::path p = pathOf(i.path);
    const auto size = fs::file_size(p, ec);
    const auto time = fs::last_write_time(p, ec);
    i.stamp = i.path + '\n' + std::to_string(static_cast<long long>(time.time_since_epoch().count())) + '\n' + std::to_string(size);
    Caches& c = caches();
    {
        std::lock_guard lock(c.mtx);
        if (auto it = c.sizes.find(i.stamp); it != c.sizes.end()) {
            i.state = it->second.ok ? Info::State::Ok : Info::State::Unreadable;
            i.width = it->second.width;
            i.height = it->second.height;
            return i;
        }
    }
    Caches::Size s;
    const Decoder d = currentDecoder();
    s.ok = d.size && d.size(i.path, s.width, s.height) && s.width > 0 && s.height > 0;
    {
        std::lock_guard lock(c.mtx);
        if (c.sizes.size() > 4096) {
            c.sizes.clear();
        }
        c.sizes[i.stamp] = s;
    }
    i.state = s.ok ? Info::State::Ok : Info::State::Unreadable;
    i.width = s.width;
    i.height = s.height;
    return i;
}

void setDecoder(Decoder decoder) {
    auto& d = decoderSlot();
    {
        std::lock_guard lock(d.mtx);
        d.decoder = std::move(decoder);
    }
    clearCache();
}

std::shared_ptr<const Pixels> pixels(const Info& info, int width, int height) {
    if (info.state != Info::State::Ok || width <= 0 || height <= 0) {
        return nullptr;
    }
    const std::string key = info.stamp + '@' + std::to_string(width) + 'x' + std::to_string(height);
    Caches& c = caches();
    {
        std::lock_guard lock(c.mtx);
        if (auto it = c.byKey.find(key); it != c.byKey.end()) {
            c.lru.splice(c.lru.begin(), c.lru, it->second);
            ++c.hits;
            return it->second->second;
        }
    }
    const Decoder d = currentDecoder();
    std::shared_ptr<const Pixels> decoded = d.decode ? d.decode(info.path, width, height) : nullptr;
    if (!decoded || decoded->width <= 0 || decoded->height <= 0 ||
        decoded->data.size() < static_cast<size_t>(decoded->stride) * decoded->height) {
        return nullptr;
    }
    std::lock_guard lock(c.mtx);
    ++c.decoded;
    if (auto it = c.byKey.find(key); it != c.byKey.end()) {
        return it->second->second;  // (another thread decoded it meanwhile)
    }
    c.lru.emplace_front(key, decoded);
    c.byKey[key] = c.lru.begin();
    c.bytes += bytesOf(*decoded);
    while (c.bytes > CACHE_LIMIT && c.lru.size() > 1) {
        c.bytes -= bytesOf(*c.lru.back().second);
        c.byKey.erase(c.lru.back().first);
        c.lru.pop_back();
    }
    return decoded;
}

void draw(cairo_t* cr, const Info& info, double x, double y, double w, double h) {
    if (info.state != Info::State::Ok || w <= 0 || h <= 0 || info.width <= 0 || info.height <= 0) {
        return;
    }
    cairo_surface_t* target = cairo_get_target(cr);
    const cairo_surface_type_t type = cairo_surface_get_type(target);
    const bool vector = type == CAIRO_SURFACE_TYPE_PDF || type == CAIRO_SURFACE_TYPE_PS ||
                        type == CAIRO_SURFACE_TYPE_SVG || type == CAIRO_SURFACE_TYPE_SCRIPT ||
                        type == CAIRO_SURFACE_TYPE_RECORDING;
    int dw = info.width;
    int dh = info.height;
    bool jpeg = false;
    if (vector) {
        jpeg = isJpegFile(info.path);
        constexpr int MAX_SIDE = 4096;  // (print quality for a page; a JPEG goes in as it is)
        if (!jpeg && std::max(dw, dh) > MAX_SIDE) {
            const double f = static_cast<double>(MAX_SIDE) / std::max(dw, dh);
            dw = std::max(1, static_cast<int>(std::lround(dw * f)));
            dh = std::max(1, static_cast<int>(std::lround(dh * f)));
        }
    } else {
        // The pixels it covers on the target (any rotation), rounded up to a halving of the natural size
        double ax = w;
        double ay = 0;
        double bx = 0;
        double by = h;
        cairo_user_to_device_distance(cr, &ax, &ay);
        cairo_user_to_device_distance(cr, &bx, &by);
        const double needW = std::hypot(ax, ay);
        const double needH = std::hypot(bx, by);
        for (int k = 1; k < 12; ++k) {
            const int hw = static_cast<int>(std::ceil(info.width / std::ldexp(1.0, k)));
            const int hh = static_cast<int>(std::ceil(info.height / std::ldexp(1.0, k)));
            if (hw < needW || hh < needH || hw < 1 || hh < 1) {
                break;
            }
            dw = hw;
            dh = hh;
        }
    }
    const std::shared_ptr<const Pixels> px = pixels(info, dw, dh);
    if (!px) {
        return;
    }
    cairo_surface_t* s = cairo_image_surface_create_for_data(const_cast<unsigned char*>(px->data.data()),
                                                             CAIRO_FORMAT_ARGB32, px->width, px->height, px->stride);
    // (the pixels live as long as the surface: a PDF surface may keep it until the page is finished)
    cairo_surface_set_user_data(s, &PIXELS_KEY, new std::shared_ptr<const Pixels>(px),
                                [](void* p) { delete static_cast<std::shared_ptr<const Pixels>*>(p); });
    if (vector) {
        const std::string id = info.stamp + '@' + std::to_string(px->width) + 'x' + std::to_string(px->height);
        auto* idData = static_cast<unsigned char*>(g_memdup2(id.data(), id.size()));
        cairo_surface_set_mime_data(s, CAIRO_MIME_TYPE_UNIQUE_ID, idData, id.size(), g_free, idData);
        if (jpeg && px->width == info.width && px->height == info.height) {
            const std::string bytes = readFile(info.path, 64 * 1024 * 1024);
            if (!bytes.empty()) {
                auto* data = static_cast<unsigned char*>(g_memdup2(bytes.data(), bytes.size()));
                cairo_surface_set_mime_data(s, CAIRO_MIME_TYPE_JPEG, data, bytes.size(), g_free, data);
            }
        }
    }
    cairo_save(cr);
    cairo_rectangle(cr, x, y, w, h);
    cairo_clip(cr);
    cairo_translate(cr, x, y);
    cairo_scale(cr, w / px->width, h / px->height);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    if (!vector) {
        // (no faded edges when scaled; a PDF would get a padded copy instead of the JPEG as it is)
        cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);
    }
    cairo_paint(cr);
    cairo_restore(cr);
    cairo_surface_destroy(s);
}

CacheStats cacheStats() {
    Caches& c = caches();
    std::lock_guard lock(c.mtx);
    return {c.lru.size(), c.bytes, c.hits, c.decoded};
}

void clearCache() {
    Caches& c = caches();
    std::lock_guard lock(c.mtx);
    c.sizes.clear();
    c.lru.clear();
    c.byKey.clear();
    c.bytes = 0;
    c.hits = 0;
    c.decoded = 0;
}

void setWebCacheDir(std::string dir) {
    {
        auto& r = roots();
        std::lock_guard lock(r.mtx);
        r.webDir = std::move(dir);
    }
    changed();
}

std::string webCachePath(std::string_view url) {
    std::string dir;
    {
        auto& r = roots();
        std::lock_guard lock(r.mtx);
        dir = r.webDir;
    }
    if (dir.empty()) {
        return {};
    }
    char name[32];
    std::snprintf(name, sizeof name, "%016llx.img", static_cast<unsigned long long>(fnv1a(trimmed(url))));
    return utf8Of(pathOf(dir) / name);
}

}  // namespace xqt::md::images

/*
 * xournal-qt: the pictures of the Markdown text (![alt](path)), qt/docs/md-images.md.
 *
 * A link is a path relative to the document's folder (or to where its "name.assets/" is), an absolute path, a
 * file:// address or a web address. Page boxes are drawn without knowing their document (upstream's TextView gives
 * the text element only), so every open document registers a Root while it is open, and a relative link is looked
 * for in the roots, newest first.
 *
 * Qt-free and thread-safe: the render threads, the thumbnails and the PDF export draw pictures at the same time. The
 * pictures are read by a Decoder (the app's reads with Qt: QImageReader, SVG through its plugin; the default one with
 * gdk-pixbuf). The decoded pixels are cached here, with a limit.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cairo.h>

namespace xqt::md::images {

// --- links ----------------------------------------------------------------------------------------------------

enum class LinkKind {
    Local,        ///< a path (relative or absolute) or a file:// address
    Web,          ///< http:// or https://
    Unsupported,  ///< another scheme (data:, ftp:, …)
};
LinkKind kindOf(std::string_view link);
/// "%20" and the like decoded (invalid escapes stay as they are).
std::string percentDecoded(std::string_view s);
/// The path of a relative link without "./" at its start and with "/" between its parts ("a\b" stays: a file name
/// may have a backslash on Linux). Empty for a link that is not relative.
std::string relativePath(std::string_view link);

/// Where the relative links of an open document point (qt/docs/md-images.md).
struct Root {
    std::string baseDir;     ///< the folder relative links are relative to (a .md's folder); may be empty
    std::string assetsName;  ///< "name.assets": links starting with it are looked for in assetsDir
    std::string assetsDir;   ///< where "name.assets" is (next to the .md, or in the app cache); may be empty
};
/// A root, registered while it lives (move-only).
class RootHandle {
public:
    RootHandle() = default;
    explicit RootHandle(Root root);
    ~RootHandle();
    RootHandle(RootHandle&& other) noexcept;
    RootHandle& operator=(RootHandle&& other) noexcept;
    RootHandle(const RootHandle&) = delete;
    RootHandle& operator=(const RootHandle&) = delete;
    /// Point it elsewhere (the document was renamed, saved as another file).
    void set(Root root);
    void reset();
    bool active() const { return id != 0; }
    const Root& root() const { return current; }

private:
    uint64_t id = 0;
    Root current;
};

/// The files a link may be, in order (roots newest first; each also with its %-escapes decoded). A web link: its
/// copy in the web cache. Unsupported: none.
std::vector<std::string> candidates(std::string_view link);
/// The first of them that exists (a file), or "".
std::string resolve(std::string_view link);

/// Changes whenever what links resolve to may have changed: a root came or went, a picture was written or fetched.
/// Caches of laid out texts are keyed by it.
uint64_t generation();
/// Say that pictures changed (a file written by the app: a paste, an unpacked attachment).
void changed();

// --- pictures -------------------------------------------------------------------------------------------------

struct Info {
    enum class State {
        Ok,          ///< drawn
        Missing,     ///< no such file
        Unreadable,  ///< a file that is no picture the decoder reads
        Web,         ///< a web address not loaded (never fetched unasked)
        Unsupported, ///< another kind of address
    };
    State state = State::Missing;
    std::string path;    ///< the file (Ok, Unreadable)
    int width = 0;       ///< pixels (natural size)
    int height = 0;
    std::string stamp;   ///< path, modification time and size: the picture's identity for caches
};
/// What a link shows now (its size read from the file's header; cached by stamp). Any thread.
Info info(std::string_view link);

/// Decoded pixels: ARGB32, premultiplied (Cairo's CAIRO_FORMAT_ARGB32).
struct Pixels {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<unsigned char> data;
};
struct Decoder {
    /// The natural size of a picture file in pixels (after its orientation); false if it is none.
    std::function<bool(const std::string& path, int& width, int& height)> size;
    /// The picture scaled to width × height; nullptr if it cannot be read.
    std::function<std::shared_ptr<Pixels>(const std::string& path, int width, int height)> decode;
};
/// The app's decoder (Qt). Without one, gdk-pixbuf reads the pictures.
void setDecoder(Decoder decoder);
/// The pixels of a picture at (about) this size, from the cache or decoded now. Any thread.
std::shared_ptr<const Pixels> pixels(const Info& info, int width, int height);

/// Draw a picture into the rectangle (x, y, w, h) of the current user space. On a raster target it is decoded at the
/// pixels needed there (rounded up to a halving of its natural size); on a vector target (PDF, print) at its natural
/// size, a JPEG as the file itself.
void draw(cairo_t* cr, const Info& info, double x, double y, double w, double h);

struct CacheStats {
    size_t entries = 0;
    size_t bytes = 0;
    size_t hits = 0;
    size_t decoded = 0;
};
CacheStats cacheStats();
/// Forget every decoded picture and size (tests).
void clearCache();
/// The pixel cache's limit: the least recently used pictures go beyond it.
constexpr size_t CACHE_LIMIT = 64 * 1024 * 1024;

// --- web pictures (never fetched here: the app fetches one when asked, see qt/docs/md-images.md) -----------------

/// The folder of the fetched pictures (the app cache). Empty: web pictures are never shown.
void setWebCacheDir(std::string dir);
/// Where a web picture is kept once fetched ("" without a web cache).
std::string webCachePath(std::string_view url);

// --- sizes ------------------------------------------------------------------------------------------------------

/// Points per pixel of a picture's natural size (96 dpi).
constexpr double POINTS_PER_PIXEL = 0.75;
/// A block image is no higher than this times the column's width (it fits an A4 page's text).
constexpr double MAX_BLOCK_ASPECT = 1.4;

}  // namespace xqt::md::images

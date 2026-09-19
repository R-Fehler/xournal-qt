/*
 * xoj-imgdiff: pixel comparison of two PNG files (used by the golden-image tests).
 *
 *   xoj-imgdiff A.png B.png [--tolerance N] [--max-pixels M] [--diff OUT.png]
 *
 * A pixel "differs" if any ARGB channel differs by more than N (default 0). Exit code 0 if at most M pixels
 * differ (default 0), 1 if more differ, 2 on I/O or size mismatch.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <cairo.h>

namespace {
cairo_surface_t* load(const char* path) {
    cairo_surface_t* s = cairo_image_surface_create_from_png(path);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
        std::fprintf(stderr, "cannot read %s: %s\n", path, cairo_status_to_string(cairo_surface_status(s)));
        cairo_surface_destroy(s);
        return nullptr;
    }
    // Normalise RGB24 / A8 etc. to ARGB32 so that the comparison is format independent.
    const int w = cairo_image_surface_get_width(s), h = cairo_image_surface_get_height(s);
    cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(out);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, s, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
    cairo_surface_flush(out);
    return out;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s A.png B.png [--tolerance N] [--max-pixels M] [--diff OUT.png]\n", argv[0]);
        return 2;
    }
    int tolerance = 0;
    long maxPixels = 0;
    const char* diffPath = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--tolerance") && i + 1 < argc) {
            tolerance = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--max-pixels") && i + 1 < argc) {
            maxPixels = std::atol(argv[++i]);
        } else if (!std::strcmp(argv[i], "--diff") && i + 1 < argc) {
            diffPath = argv[++i];
        }
    }
    cairo_surface_t* a = load(argv[1]);
    cairo_surface_t* b = load(argv[2]);
    if (!a || !b) {
        return 2;
    }
    const int w = cairo_image_surface_get_width(a), h = cairo_image_surface_get_height(a);
    if (w != cairo_image_surface_get_width(b) || h != cairo_image_surface_get_height(b)) {
        std::printf("size mismatch: %dx%d vs %dx%d\n", w, h, cairo_image_surface_get_width(b),
                    cairo_image_surface_get_height(b));
        return 2;
    }
    const int sa = cairo_image_surface_get_stride(a), sb = cairo_image_surface_get_stride(b);
    const unsigned char* pa = cairo_image_surface_get_data(a);
    const unsigned char* pb = cairo_image_surface_get_data(b);
    cairo_surface_t* diff = diffPath ? cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h) : nullptr;
    unsigned char* pd = diff ? cairo_image_surface_get_data(diff) : nullptr;
    const int sd = diff ? cairo_image_surface_get_stride(diff) : 0;

    long differing = 0;
    int maxDelta = 0;
    for (int y = 0; y < h; ++y) {
        const auto* ra = reinterpret_cast<const uint32_t*>(pa + y * sa);
        const auto* rb = reinterpret_cast<const uint32_t*>(pb + y * sb);
        auto* rd = pd ? reinterpret_cast<uint32_t*>(pd + y * sd) : nullptr;
        for (int x = 0; x < w; ++x) {
            int delta = 0;
            for (int c = 0; c < 32; c += 8) {
                delta = std::max(delta, std::abs(static_cast<int>((ra[x] >> c) & 0xff) -
                                                 static_cast<int>((rb[x] >> c) & 0xff)));
            }
            maxDelta = std::max(maxDelta, delta);
            const bool differs = delta > tolerance;
            differing += differs;
            if (rd) {
                // Grey version of A, differing pixels in red.
                const uint32_t g = ((ra[x] & 0xff) + ((ra[x] >> 8) & 0xff) + ((ra[x] >> 16) & 0xff)) / 6 + 128;
                rd[x] = differs ? 0xffff0000u : (0xff000000u | (g << 16) | (g << 8) | g);
            }
        }
    }
    if (diff) {
        cairo_surface_mark_dirty(diff);
        cairo_surface_write_to_png(diff, diffPath);
        cairo_surface_destroy(diff);
    }
    cairo_surface_destroy(a);
    cairo_surface_destroy(b);
    if (differing == 0) {
        std::printf("identical (%dx%d)\n", w, h);
    } else {
        std::printf("differ: %ld pixels (%.4f%%), max channel delta %d\n", differing,
                    100.0 * static_cast<double>(differing) / (static_cast<double>(w) * h), maxDelta);
    }
    return differing <= maxPixels ? 0 : 1;
}

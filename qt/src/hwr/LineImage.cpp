#include "LineImage.h"

#include <algorithm>
#include <cmath>

#include <cairo.h>

namespace xqt::hwr {

std::vector<LinePiece> piecesOf(const LineInput& line, int maxWords) {
    std::vector<LinePiece> out;
    const size_t n = line.words.size();
    if (n == 0) {
        return out;
    }
    const size_t max = static_cast<size_t>(std::max(1, maxWords));
    const size_t count = (n + max - 1) / max;
    size_t first = 0;
    for (size_t k = 0; k < count; ++k) {
        const size_t take = (n - first) / (count - k);  // (about equal numbers of words)
        LinePiece p;
        p.first = first;
        p.last = first + std::max<size_t>(1, take) - 1;
        p.box = line.words[p.first].box;
        for (size_t i = p.first; i <= p.last; ++i) {
            p.box = p.box.united(line.words[i].box);
            for (const uint32_t s: line.words[i].strokes) {
                if (s < line.strokes.size()) {
                    p.box = p.box.united(line.strokes[s].box);
                }
            }
        }
        out.push_back(p);
        first = p.last + 1;
    }
    return out;
}

namespace {
/// A surface of this thread, at least w x h (kept: the next line is about as big)
cairo_surface_t* surfaceFor(int w, int h) {
    struct Kept {
        cairo_surface_t* surface = nullptr;
        ~Kept() {
            if (surface) {
                cairo_surface_destroy(surface);
            }
        }
    };
    thread_local Kept kept;
    if (kept.surface && cairo_image_surface_get_width(kept.surface) >= w &&
        cairo_image_surface_get_height(kept.surface) >= h) {
        return kept.surface;
    }
    if (kept.surface) {
        cairo_surface_destroy(kept.surface);
    }
    kept.surface = cairo_image_surface_create(CAIRO_FORMAT_A8, w, h);
    return kept.surface;
}

double padOf(const LinePiece& piece) { return std::max(2.0, 0.25 * piece.box.height()); }

/// The widest picture: a whole line of an A3 page in landscape (a CTC model and the dataset export take whole lines;
/// TrOCR's pieces of 8 words are far narrower)
constexpr int MAX_PX = 128 * LINE_PX;

/// The piece's ink as coverage (A8, 255: ink) in the kept surface; its size in w, h
cairo_surface_t* draw(const LineInput& line, const LinePiece& piece, int& w, int& h) {
    const double pad = padOf(piece);
    const double scale = LINE_PX / std::max(1.0, piece.box.height() + 2 * pad);
    h = LINE_PX;
    w = std::clamp(static_cast<int>(std::ceil((piece.box.width() + 2 * pad) * scale)), 1, MAX_PX);
    cairo_surface_t* surface = surfaceFor(w, h);
    cairo_t* cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_clip(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, pad - piece.box.left(), pad - piece.box.top());
    cairo_set_source_rgba(cr, 0, 0, 0, 1);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    const double minWidth = 1.0 / scale;  // (a pixel at least)
    for (size_t i = piece.first; i <= piece.last && i < line.words.size(); ++i) {
        for (const uint32_t index: line.words[i].strokes) {
            if (index >= line.strokes.size()) {
                continue;
            }
            const InkStroke& s = line.strokes[index];
            if (s.points.empty()) {
                continue;
            }
            if (s.points.size() == 1) {
                cairo_set_line_width(cr, std::max<double>(s.width, minWidth));
                cairo_move_to(cr, s.points[0].x(), s.points[0].y());
                cairo_line_to(cr, s.points[0].x(), s.points[0].y());
                cairo_stroke(cr);
                continue;
            }
            if (s.widths.size() == s.points.size()) {
                // Pressure: each segment with its width (as upstream draws them)
                for (size_t k = 1; k < s.points.size(); ++k) {
                    cairo_set_line_width(cr, std::max<double>(s.widths[k - 1], minWidth));
                    cairo_move_to(cr, s.points[k - 1].x(), s.points[k - 1].y());
                    cairo_line_to(cr, s.points[k].x(), s.points[k].y());
                    cairo_stroke(cr);
                }
                continue;
            }
            cairo_set_line_width(cr, std::max<double>(s.width, minWidth));
            cairo_move_to(cr, s.points[0].x(), s.points[0].y());
            for (size_t k = 1; k < s.points.size(); ++k) {
                cairo_line_to(cr, s.points[k].x(), s.points[k].y());
            }
            cairo_stroke(cr);
        }
    }
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    return surface;
}
}  // namespace

std::vector<unsigned char> greyOf(const LineInput& line, const LinePiece& piece, int& width, int& height) {
    cairo_surface_t* s = draw(line, piece, width, height);
    const unsigned char* data = cairo_image_surface_get_data(s);
    const int stride = cairo_image_surface_get_stride(s);
    std::vector<unsigned char> out(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            out[static_cast<size_t>(y * width + x)] = static_cast<unsigned char>(255 - data[y * stride + x]);
        }
    }
    return out;
}

namespace {
/// One row (or column) of `n` values resampled to `m`: the average over each target pixel's span when shrinking,
/// linear between the two nearest when growing (what a smooth image filter does, the same everywhere)
void resample(const float* in, size_t n, size_t stride, float* out, size_t m, size_t outStride) {
    const double scale = static_cast<double>(n) / static_cast<double>(m);
    for (size_t j = 0; j < m; ++j) {
        if (scale > 1) {
            const double a = static_cast<double>(j) * scale, b = a + scale;
            double sum = 0;
            for (auto i = static_cast<size_t>(a); i < n && static_cast<double>(i) < b; ++i) {
                const double from = std::max(a, static_cast<double>(i)), to = std::min(b, static_cast<double>(i + 1));
                sum += in[i * stride] * (to - from);
            }
            out[j * outStride] = static_cast<float>(sum / scale);
        } else {
            const double x = std::clamp((static_cast<double>(j) + 0.5) * scale - 0.5, 0.0, static_cast<double>(n - 1));
            const auto i = static_cast<size_t>(x);
            const size_t k = std::min(i + 1, n - 1);
            const double t = x - static_cast<double>(i);
            out[j * outStride] = static_cast<float>(in[i * stride] * (1 - t) + in[k * stride] * t);
        }
    }
}
}  // namespace

std::vector<float> pixelsOf(const LineInput& line, const LinePiece& piece, int size) {
    int w = 0, h = 0;
    const std::vector<unsigned char> grey = greyOf(line, piece, w, h);
    const auto W = static_cast<size_t>(w), H = static_cast<size_t>(h), S = static_cast<size_t>(size);
    std::vector<float> source(grey.begin(), grey.end());
    std::vector<float> rows(S * H);  ///< each row resampled to `size`
    for (size_t y = 0; y < H; ++y) {
        resample(source.data() + y * W, W, 1, rows.data() + y * S, S, 1);
    }
    std::vector<float> square(S * S);
    for (size_t x = 0; x < S; ++x) {
        resample(rows.data() + x, H, S, square.data() + x, S, S);
    }
    const size_t plane = S * S;
    std::vector<float> out(3 * plane);
    for (size_t i = 0; i < plane; ++i) {
        const float v = (square[i] / 255.0f - 0.5f) / 0.5f;
        out[i] = out[plane + i] = out[2 * plane + i] = v;
    }
    return out;
}

double widthAt(const LinePiece& piece, int height) {
    const double pad = padOf(piece);
    return (piece.box.width() + 2 * pad) * height / std::max(1.0, piece.box.height() + 2 * pad);
}

std::vector<float> inkOf(const LineInput& line, const LinePiece& piece, int height, int maxWidth, int& width) {
    int w = 0, h = 0;
    const std::vector<unsigned char> grey = greyOf(line, piece, w, h);
    height = std::max(1, height);
    width = std::clamp(static_cast<int>(std::lround(static_cast<double>(w) * height / std::max(1, h))), 1,
                       std::max(1, maxWidth));
    const auto W = static_cast<size_t>(w), H = static_cast<size_t>(h);
    const auto OW = static_cast<size_t>(width), OH = static_cast<size_t>(height);
    std::vector<float> source(grey.size());
    for (size_t i = 0; i < grey.size(); ++i) {
        source[i] = 1.0f - static_cast<float>(grey[i]) / 255.0f;  // (ink 1, paper 0)
    }
    std::vector<float> rows(OW * H);  ///< each row resampled to the width
    for (size_t y = 0; y < H; ++y) {
        resample(source.data() + y * W, W, 1, rows.data() + y * OW, OW, 1);
    }
    std::vector<float> out(OW * OH);
    for (size_t x = 0; x < OW; ++x) {
        resample(rows.data() + x, H, OW, out.data() + x, OH, OW);
    }
    return out;
}

}  // namespace xqt::hwr

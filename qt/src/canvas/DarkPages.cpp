/*
 * xournal-qt: dark pages, the color mapping and its table (see DarkPages.h).
 *
 * @license GNU GPLv2 or later
 */
#include "DarkPages.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <mutex>

namespace xqt::dark {

namespace {

struct Vec3 {
    double x, y, z;
};

double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
double toEncoded(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055; }

/// OKLab (Björn Ottosson) of an sRGB color (encoded, 0..1)
Vec3 oklab(Vec3 rgb) {
    const double r = toLinear(rgb.x), g = toLinear(rgb.y), b = toLinear(rgb.z);
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return {0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}
/// Linear sRGB of an OKLab color (may be outside 0..1: out of gamut)
Vec3 linearOf(Vec3 lab) {
    const double l = std::pow(lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z, 3);
    const double m = std::pow(lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z, 3);
    const double s = std::pow(lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z, 3);
    return {4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
            -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
            -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s};
}
bool inGamut(Vec3 c) {
    constexpr double e = 1e-4;
    return c.x >= -e && c.x <= 1 + e && c.y >= -e && c.y <= 1 + e && c.z >= -e && c.z <= 1 + e;
}

Vec3 floats(QRgb c) { return {qRed(c) / 255.0, qGreen(c) / 255.0, qBlue(c) / 255.0}; }
int byte(double v) { return std::clamp(static_cast<int>(std::lround(v * 255)), 0, 255); }
QRgb rgbOf(Vec3 c) { return qRgb(byte(c.x), byte(c.y), byte(c.z)); }

/// The Dark palette's background and body ink (palettes.json: "dark")
constexpr QRgb DARK_BACKGROUND = 0xff1e1f22;
constexpr QRgb DARK_BODY = 0xffececec;

struct Anchors {
    Vec3 paper = oklab(floats(DARK_BACKGROUND));
    double inkL = oklab(floats(DARK_BODY)).x;
};
const Anchors& anchors() {
    static const Anchors a;
    return a;
}

/// Pale colors with this much chroma are lifted to a visible marker: L at least darkL + MARKER * chroma (at most
/// MARKER_MAX)
constexpr double MARKER = 1.8;
constexpr double MARKER_MAX = 0.62;
/// A color this close (sRGB, 0..1) to the way from white to a role's color is that role's color on white paper
/// (antialiased, or the highlighter's translucency). More than half a cell of the table, so that every corner around
/// a role's color in the table takes its role.
constexpr double ROLE_TOLERANCE = 0.06;

struct Table {
    std::vector<uint8_t> rgb;  ///< LUT^3 x 3, index ((b * LUT + g) * LUT + r) * 3
    QImage image;
    quint64 generation = 0;
};

struct State {
    std::mutex mtx;
    std::vector<RolePair> roles;
    std::shared_ptr<const Table> table;
    quint64 generation = 0;
};
State& state() {
    static State s;
    return s;
}

QRgb mapWith(QRgb c, const std::vector<RolePair>& roles) {
    const Vec3 p = floats(c);
    const Vec3 w{1, 1, 1};
    const RolePair* best = nullptr;
    double bestAlpha = 0;
    double bestResidual = ROLE_TOLERANCE;
    for (const RolePair& r: roles) {
        const Vec3 l = floats(r.light);
        const Vec3 s{l.x - w.x, l.y - w.y, l.z - w.z};
        const double len2 = s.x * s.x + s.y * s.y + s.z * s.z;
        if (len2 < 0.15 * 0.15) {
            continue;  // (too close to white to tell from the paper)
        }
        const Vec3 d{p.x - w.x, p.y - w.y, p.z - w.z};
        double alpha = (d.x * s.x + d.y * s.y + d.z * s.z) / len2;
        if (alpha < 0.08 || alpha > 1.1) {
            continue;
        }
        alpha = std::min(alpha, 1.0);
        const double rx = w.x + alpha * s.x - p.x, ry = w.y + alpha * s.y - p.y, rz = w.z + alpha * s.z - p.z;
        const double residual = std::sqrt(rx * rx + ry * ry + rz * rz);
        // (two roles' colors on one line from white, e.g. two greys: the one the color is, not a lighter part of
        // the darker one)
        if (residual < bestResidual - 0.004 || (best && residual < bestResidual + 0.004 && alpha > bestAlpha)) {
            bestResidual = residual;
            best = &r;
            bestAlpha = alpha;
        }
    }
    if (!best) {
        return mapGeneric(c);
    }
    // Composed as cairo composes (in sRGB values): the dark color over the dark paper
    const double a = std::min(1.0, bestAlpha * best->opacity);
    const Vec3 paper = floats(darkPaper());
    const Vec3 ink = floats(best->dark);
    return rgbOf({paper.x + a * (ink.x - paper.x), paper.y + a * (ink.y - paper.y), paper.z + a * (ink.z - paper.z)});
}

std::shared_ptr<const Table> makeTable(const std::vector<RolePair>& roles, quint64 generation) {
    auto t = std::make_shared<Table>();
    t->generation = generation;
    t->rgb.resize(static_cast<size_t>(LUT) * LUT * LUT * 3);
    t->image = QImage(LUT * LUT, LUT, QImage::Format_RGBA8888);
    for (int b = 0; b < LUT; ++b) {
        for (int g = 0; g < LUT; ++g) {
            auto* line = reinterpret_cast<uint8_t*>(t->image.scanLine(g));
            for (int r = 0; r < LUT; ++r) {
                const auto at = [](int v) { return static_cast<int>(std::lround(v * 255.0 / (LUT - 1))); };
                const QRgb m = mapWith(qRgb(at(r), at(g), at(b)), roles);
                const size_t i = ((static_cast<size_t>(b) * LUT + g) * LUT + r) * 3;
                t->rgb[i] = static_cast<uint8_t>(qRed(m));
                t->rgb[i + 1] = static_cast<uint8_t>(qGreen(m));
                t->rgb[i + 2] = static_cast<uint8_t>(qBlue(m));
                uint8_t* px = line + static_cast<size_t>(b * LUT + r) * 4;
                px[0] = static_cast<uint8_t>(qRed(m));
                px[1] = static_cast<uint8_t>(qGreen(m));
                px[2] = static_cast<uint8_t>(qBlue(m));
                px[3] = 255;
            }
        }
    }
    return t;
}

std::shared_ptr<const Table> currentTable() {
    State& s = state();
    std::lock_guard lock(s.mtx);
    if (!s.table) {
        s.table = makeTable(s.roles, s.generation);
    }
    return s.table;
}

/// Balance factors of a paper (white: 1, 1, 1)
std::array<float, 3> balanceOf(QRgb paper) {
    const auto f = [](int v) { return 255.0f / static_cast<float>(std::max(v, 16)); };
    return {f(qRed(paper)), f(qGreen(paper)), f(qBlue(paper))};
}

/// The table at a color (0..1 per channel, already balanced), trilinear: what the shader computes
void sample(const Table& t, float r, float g, float b, float out[3]) {
    constexpr float n = LUT - 1;
    const float x = std::clamp(r, 0.0f, 1.0f) * n, y = std::clamp(g, 0.0f, 1.0f) * n, z = std::clamp(b, 0.0f, 1.0f) * n;
    const int x0 = std::min(static_cast<int>(x), LUT - 2), y0 = std::min(static_cast<int>(y), LUT - 2),
              z0 = std::min(static_cast<int>(z), LUT - 2);
    const float fx = x - x0, fy = y - y0, fz = z - z0;
    const uint8_t* d = t.rgb.data();
    const auto at = [&](int xi, int yi, int zi, int c) {
        return static_cast<float>(d[((static_cast<size_t>(zi) * LUT + yi) * LUT + xi) * 3 + c]);
    };
    for (int c = 0; c < 3; ++c) {
        const float c00 = at(x0, y0, z0, c) + (at(x0 + 1, y0, z0, c) - at(x0, y0, z0, c)) * fx;
        const float c10 = at(x0, y0 + 1, z0, c) + (at(x0 + 1, y0 + 1, z0, c) - at(x0, y0 + 1, z0, c)) * fx;
        const float c01 = at(x0, y0, z0 + 1, c) + (at(x0 + 1, y0, z0 + 1, c) - at(x0, y0, z0 + 1, c)) * fx;
        const float c11 = at(x0, y0 + 1, z0 + 1, c) + (at(x0 + 1, y0 + 1, z0 + 1, c) - at(x0, y0 + 1, z0 + 1, c)) * fx;
        const float c0 = c00 + (c10 - c00) * fy;
        const float c1 = c01 + (c11 - c01) * fy;
        out[c] = c0 + (c1 - c0) * fz;
    }
}

double luminance(QRgb c) {
    const Vec3 f = floats(c);
    return 0.2126 * toLinear(f.x) + 0.7152 * toLinear(f.y) + 0.0722 * toLinear(f.z);
}

}  // namespace

void setRoles(std::vector<RolePair> pairs) {
    State& s = state();
    std::lock_guard lock(s.mtx);
    s.roles = std::move(pairs);
    ++s.generation;
    s.table.reset();  // (made anew when next asked for)
}

const std::vector<RolePair>& roles() { return state().roles; }

QRgb darkPaper() {
    static const QRgb p = mapGeneric(0xffffffff);
    return p;
}

QRgb lightInk() {
    static const QRgb p = mapGeneric(0xff000000);
    return p;
}

QRgb mapGeneric(QRgb c) {
    const Anchors& an = anchors();
    const Vec3 lab = oklab(floats(c));
    const double chroma = std::hypot(lab.y, lab.z);
    const double t = std::clamp(1.0 - lab.x, 0.0, 1.0);  // 0: white
    const double f = t * (2 - t);                        // mid tones lifted
    double light = an.paper.x + (an.inkL - an.paper.x) * f;
    // Pale colors with some chroma (a highlighter on white, a tinted box) stay a visible marker
    light = std::max(light, std::min(an.paper.x + MARKER * chroma, MARKER_MAX));
    // Hue and chroma kept; near the paper, the dark paper's tint
    const double a = lab.y + an.paper.y * (1 - f), b = lab.z + an.paper.z * (1 - f);
    // Into the gamut: as much of the chroma as fits
    double lo = 0, hi = 1;
    if (inGamut(linearOf({light, a, b}))) {
        lo = 1;
    } else {
        for (int i = 0; i < 14; ++i) {
            const double mid = (lo + hi) / 2;
            (inGamut(linearOf({light, a * mid, b * mid})) ? lo : hi) = mid;
        }
    }
    const Vec3 lin = linearOf({light, a * lo, b * lo});
    return rgbOf({toEncoded(std::clamp(lin.x, 0.0, 1.0)), toEncoded(std::clamp(lin.y, 0.0, 1.0)),
                  toEncoded(std::clamp(lin.z, 0.0, 1.0))});
}

QRgb map(QRgb c) {
    State& s = state();
    std::vector<RolePair> r;
    {
        std::lock_guard lock(s.mtx);
        r = s.roles;
    }
    return mapWith(c, r);
}

const QImage& table() { return currentTable()->image; }

quint64 tableGeneration() {
    State& s = state();
    std::lock_guard lock(s.mtx);
    return s.generation;
}

QRgb lookup(QRgb c, QRgb paper) {
    const auto t = currentTable();
    const auto k = balanceOf(paper);
    float out[3];
    sample(*t, qRed(c) / 255.0f * k[0], qGreen(c) / 255.0f * k[1], qBlue(c) / 255.0f * k[2], out);
    const auto px = [](float v) { return std::clamp(static_cast<int>(std::lround(v)), 0, 255); };
    return qRgb(px(out[0]), px(out[1]), px(out[2]));
}

bool turnsDark(QRgb paper) { return luminance(paper) >= 0.18; }

QRgb paperOfImage(const QImage& img) {
    if (img.width() < 4 || img.height() < 4) {
        return 0xffffffff;
    }
    const int ix = std::max(1, img.width() / 50), iy = std::max(1, img.height() / 50);
    const std::array<QRgb, 4> corners{img.pixel(ix, iy), img.pixel(img.width() - 1 - ix, iy),
                                      img.pixel(ix, img.height() - 1 - iy),
                                      img.pixel(img.width() - 1 - ix, img.height() - 1 - iy)};
    const auto near = [](QRgb a, QRgb b) {
        return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) + std::abs(qBlue(a) - qBlue(b)) <= 30;
    };
    int bestCount = 0;
    QRgb best = 0xffffffff;
    for (QRgb c: corners) {
        const int count =
                static_cast<int>(std::count_if(corners.begin(), corners.end(), [&](QRgb o) { return near(c, o); }));
        if (count > bestCount || (count == bestCount && luminance(c) > luminance(best))) {
            bestCount = count;
            best = c;
        }
    }
    return bestCount >= 2 ? (best | 0xff000000) : 0xffffffff;
}

void apply(QImage& img, QRgb paper, const std::vector<QRect>& keep) {
    if (img.isNull()) {
        return;
    }
    if (img.format() != QImage::Format_ARGB32_Premultiplied && img.format() != QImage::Format_RGB32) {
        img.convertTo(QImage::Format_ARGB32_Premultiplied);
    }
    const auto t = currentTable();
    const auto k = balanceOf(paper);
    const QRect bounds = img.rect();
    for (int y = 0; y < img.height(); ++y) {
        auto* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        // The kept runs of this line
        std::vector<std::pair<int, int>> kept;
        for (const QRect& r: keep) {
            const QRect c = r.intersected(bounds);
            if (!c.isEmpty() && y >= c.top() && y <= c.bottom()) {
                kept.emplace_back(c.left(), c.right());
            }
        }
        for (int x = 0; x < img.width(); ++x) {
            if (!kept.empty() &&
                std::any_of(kept.begin(), kept.end(), [x](const auto& k) { return x >= k.first && x <= k.second; })) {
                continue;
            }
            const QRgb p = line[x];
            const int a = img.format() == QImage::Format_RGB32 ? 255 : qAlpha(p);
            if (a == 0) {
                continue;
            }
            const float un = 255.0f / static_cast<float>(a);  // (premultiplied)
            float out[3];
            sample(*t, qRed(p) * un / 255.0f * k[0], qGreen(p) * un / 255.0f * k[1], qBlue(p) * un / 255.0f * k[2],
                   out);
            const auto px = [a](float v) {
                return std::clamp(static_cast<int>(std::lround(v * static_cast<float>(a) / 255.0f)), 0, 255);
            };
            line[x] = qRgba(px(out[0]), px(out[1]), px(out[2]), a);
        }
    }
}

bool takeSuffix(QString& id) {
    if (id.endsWith(QStringLiteral("~dark"))) {
        id.chop(5);
        return true;
    }
    return false;
}

std::vector<QRectF> keptPictures(std::vector<QRectF> pictures, double pageWidth, double pageHeight, size_t max) {
    const QRectF page(0, 0, pageWidth, pageHeight);
    const double area = pageWidth * pageHeight;
    std::vector<QRectF> out;
    for (const QRectF& p: pictures) {
        const QRectF r = p.normalized().intersected(page);
        if (r.isEmpty() || (area > 0 && r.width() * r.height() > SCAN_SHARE * area)) {
            continue;
        }
        if (out.size() < max) {
            out.push_back(r);
        } else {
            out.back() = out.back().united(r);
        }
    }
    return out;
}

}  // namespace xqt::dark

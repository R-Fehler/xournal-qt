#include "VersionDiff.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

#include "model/BackgroundImage.h"
#include "model/Document.h"
#include "model/Layer.h"
#include "model/PageType.h"
#include "model/XojPage.h"
#include "control/xml/XmlNode.h"
#include "control/xojfile/SaveHandler.h"
#include "control/xojfile/XmlTags.h"
#include "util/OutputStream.h"

namespace xqt::versiondiff {

namespace {
constexpr uint64_t FNV_OFFSET = 1469598103934665603ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

uint64_t fnv(std::string_view bytes, uint64_t h = FNV_OFFSET) {
    for (unsigned char c: bytes) {
        h ^= c;
        h *= FNV_PRIME;
    }
    return h;
}

uint64_t mix(uint64_t h, uint64_t v) {
    return fnv(std::string_view(reinterpret_cast<const char*>(&v), sizeof v), h);
}

/// Hashes what is written into it
class HashStream: public OutputStream {
public:
    using OutputStream::write;
    void write(const char* data, size_t len) override { h = fnv(std::string_view(data, len), h); }
    void close() override {}
    uint64_t h = FNV_OFFSET;
};

struct XmlAccess: XmlNode {
    using XmlNode::children;
};

/// Writes a layer as the .xopp holds it (upstream's SaveHandler): each element's XML, which reads back to the same
/// XML (a stroke drawn now and the same stroke loaded from a saved version give the same text, where their numbers in
/// memory may differ in the last bits)
class LayerWriter: public SaveHandler {
public:
    void elementsOf(const Layer& layer, std::vector<uint64_t>& out) {
        XmlNode page(xoj::xml_tags::NAMES[xoj::xml_tags::Type::PAGE]);
        visitLayer(&page, &layer);
        const auto& layers = page.*&XmlAccess::children;
        if (layers.empty()) {
            return;
        }
        for (const auto& element: layers.back().get()->*&XmlAccess::children) {
            HashStream hash;
            element->writeOut(&hash);
            out.push_back(hash.h);
        }
    }
};

/// The longest common subsequence of a[0..na) and b[0..nb): the matched pairs (i in a, j in b), in order
std::vector<std::pair<size_t, size_t>> commonRun(const uint64_t* a, size_t na, const uint64_t* b, size_t nb) {
    // (only for na * nb <= MAX_CELLS: the lengths are at most min(na, nb) <= 2000 and fit 16 bits)
    std::vector<uint16_t> len((na + 1) * (nb + 1), 0);
    auto at = [&](size_t i, size_t j) -> uint16_t& { return len[i * (nb + 1) + j]; };
    for (size_t i = na; i-- > 0;) {
        for (size_t j = nb; j-- > 0;) {
            at(i, j) = a[i] == b[j] ? static_cast<uint16_t>(at(i + 1, j + 1) + 1) : std::max(at(i + 1, j), at(i, j + 1));
        }
    }
    std::vector<std::pair<size_t, size_t>> pairs;
    size_t i = 0, j = 0;
    while (i < na && j < nb) {
        if (a[i] == b[j]) {
            pairs.emplace_back(i++, j++);
        } else if (at(i + 1, j) >= at(i, j + 1)) {
            ++i;
        } else {
            ++j;
        }
    }
    return pairs;
}

constexpr size_t MAX_CELLS = 4'000'000;  ///< 8 MB for the table; longer middles are compared page by page
}  // namespace

PageSig sigOf(const XojPage& page) {
    PageSig sig;
    uint64_t h = FNV_OFFSET;
    h = mix(h, static_cast<uint64_t>(std::llround(page.getWidth() * 100)));
    h = mix(h, static_cast<uint64_t>(std::llround(page.getHeight() * 100)));
    const PageType type = page.getBackgroundType();
    h = mix(h, static_cast<uint64_t>(type.format));
    h = fnv(type.config, h);
    h = mix(h, static_cast<uint64_t>(uint32_t(page.getBackgroundColor()) & 0xffffffU));  // (no alpha in the file)
    if (type.isPdfPage()) {
        h = mix(h, page.getPdfPageNr());
    }
    if (type.isImagePage()) {
        h = fnv(page.getBackgroundImage().getFilepath().string(), h);
    }
    LayerWriter writer;
    for (const Layer* layer: page.getLayersView()) {
        h = mix(h, 0x4c61796572ULL ^ (layer->isVisible() ? 1 : 0));  // (a layer begins)
        const size_t first = sig.elements.size();
        writer.elementsOf(*layer, sig.elements);
        for (size_t k = first; k < sig.elements.size(); ++k) {
            h = mix(h, sig.elements[k]);
        }
    }
    sig.whole = h;
    return sig;
}

std::vector<PageSig> sigsOf(Document& doc) {
    std::shared_lock lock(doc);
    std::vector<PageSig> sigs;
    sigs.reserve(doc.getPageCount());
    for (size_t i = 0; i < doc.getPageCount(); ++i) {
        sigs.push_back(sigOf(*doc.getPage(i)));
    }
    return sigs;
}

std::vector<uint64_t> wholeOf(const std::vector<PageSig>& sigs) {
    std::vector<uint64_t> out;
    out.reserve(sigs.size());
    for (const auto& s: sigs) {
        out.push_back(s.whole);
    }
    return out;
}

size_t Result::added() const {
    return static_cast<size_t>(std::count_if(changes.begin(), changes.end(), [](const Change& c) { return !c.inOlder; }));
}

size_t Result::removed() const {
    return static_cast<size_t>(std::count_if(changes.begin(), changes.end(), [](const Change& c) { return !c.inNewer; }));
}

Result compare(const std::vector<uint64_t>& older, const std::vector<uint64_t>& newer) {
    const size_t m = older.size(), n = newer.size();
    Result r;
    r.newerChanged.assign(n, 0);
    r.olderChanged.assign(m, 0);
    std::vector<long> matchOfNewer(n, -1);
    std::vector<char> matchedOlder(m, 0);
    auto match = [&](size_t i, size_t j) {
        matchOfNewer[i] = static_cast<long>(j);
        matchedOlder[j] = 1;
    };
    // The equal pages at both ends, then the longest common run of the middle
    size_t p = 0;
    while (p < n && p < m && newer[p] == older[p]) {
        match(p, p);
        ++p;
    }
    size_t s = 0;
    while (s < n - p && s < m - p && newer[n - 1 - s] == older[m - 1 - s]) {
        match(n - 1 - s, m - 1 - s);
        ++s;
    }
    const size_t a = n - p - s, b = m - p - s;
    if (a > 0 && b > 0) {
        if (a * b <= MAX_CELLS) {
            for (const auto& [i, j]: commonRun(newer.data() + p, a, older.data() + p, b)) {
                match(p + i, p + j);
            }
        } else {
            for (size_t k = 0; k < std::min(a, b); ++k) {
                if (newer[p + k] == older[p + k]) {
                    match(p + k, p + k);
                }
            }
        }
    }
    // A page that only moved is not a change
    std::multimap<uint64_t, size_t> leftOver;
    for (size_t j = 0; j < m; ++j) {
        if (!matchedOlder[j]) {
            leftOver.emplace(older[j], j);
        }
    }
    std::vector<char> movedOlder(m, 0), movedNewer(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (matchOfNewer[i] < 0) {
            if (auto it = leftOver.find(newer[i]); it != leftOver.end()) {
                movedNewer[i] = 1;
                movedOlder[it->second] = 1;
                leftOver.erase(it);
            }
        }
    }
    for (size_t i = 0; i < n; ++i) {
        r.newerChanged[i] = matchOfNewer[i] < 0 && !movedNewer[i];
    }
    for (size_t j = 0; j < m; ++j) {
        r.olderChanged[j] = !matchedOlder[j] && !movedOlder[j];
    }
    // Between two equal pages: the changed pages of each side paired in order, the rest added or removed
    auto clampTo = [](long v, size_t count) -> size_t {
        return count == 0 ? 0 : static_cast<size_t>(std::clamp(v, 0L, static_cast<long>(count) - 1));
    };
    long prevNewer = -1, prevOlder = -1;
    auto gap = [&](long endNewer, long endOlder) {
        std::vector<size_t> gn, go;
        for (long i = prevNewer + 1; i < endNewer; ++i) {
            if (r.newerChanged[static_cast<size_t>(i)]) {
                gn.push_back(static_cast<size_t>(i));
            }
        }
        for (long j = prevOlder + 1; j < endOlder; ++j) {
            if (r.olderChanged[static_cast<size_t>(j)]) {
                go.push_back(static_cast<size_t>(j));
            }
        }
        const size_t both = std::min(gn.size(), go.size());
        for (size_t k = 0; k < both; ++k) {
            r.changes.push_back({gn[k], go[k], true, true});
        }
        for (size_t k = both; k < gn.size(); ++k) {
            const long at = go.empty() ? prevOlder + 1 : static_cast<long>(go.back()) + 1;
            r.changes.push_back({gn[k], clampTo(at, m), true, false});
        }
        for (size_t k = both; k < go.size(); ++k) {
            const long at = gn.empty() ? prevNewer + 1 : static_cast<long>(gn.back()) + 1;
            r.changes.push_back({clampTo(at, n), go[k], false, true});
        }
    };
    for (size_t i = 0; i < n; ++i) {
        if (matchOfNewer[i] >= 0) {
            gap(static_cast<long>(i), matchOfNewer[i]);
            prevNewer = static_cast<long>(i);
            prevOlder = matchOfNewer[i];
        }
    }
    gap(static_cast<long>(n), static_cast<long>(m));
    return r;
}

}  // namespace xqt::versiondiff

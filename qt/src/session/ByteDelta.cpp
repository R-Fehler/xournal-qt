#include "ByteDelta.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace xqt::ByteDelta {

namespace {

constexpr size_t BLOCK = 16;
constexpr uint64_t PRIME = 1099511628211ULL;
constexpr char MAGIC[] = "XQD1";
constexpr unsigned char COPY = 0x01;
constexpr unsigned char INSERT = 0x02;

void putVarint(std::string& out, uint64_t v) {
    while (v >= 0x80) {
        out.push_back(static_cast<char>((v & 0x7f) | 0x80));
        v >>= 7;
    }
    out.push_back(static_cast<char>(v));
}

bool getVarint(const std::string& in, size_t& at, uint64_t& v) {
    v = 0;
    for (int shift = 0; shift < 64; shift += 7) {
        if (at >= in.size()) {
            return false;
        }
        const auto b = static_cast<unsigned char>(in[at++]);
        v |= static_cast<uint64_t>(b & 0x7f) << shift;
        if ((b & 0x80) == 0) {
            return true;
        }
    }
    return false;
}

/// The hash of BLOCK bytes from `p` (a polynomial: it rolls).
uint64_t hashOf(const unsigned char* p) {
    uint64_t h = 0;
    for (size_t k = 0; k < BLOCK; ++k) {
        h = h * PRIME + p[k];
    }
    return h;
}

}  // namespace

std::string encode(const std::string& base, const std::string& target) {
    std::string out(MAGIC, 4);
    putVarint(out, base.size());
    putVarint(out, target.size());
    const auto* b = reinterpret_cast<const unsigned char*>(base.data());
    const auto* t = reinterpret_cast<const unsigned char*>(target.data());
    // The base's blocks by their hash (the first one of a hash wins: the same bytes in the same order)
    std::unordered_map<uint64_t, size_t> index;
    if (base.size() >= BLOCK) {
        index.reserve(base.size() / BLOCK + 1);
        for (size_t at = 0; at + BLOCK <= base.size(); at += BLOCK) {
            index.emplace(hashOf(b + at), at);
        }
    }
    uint64_t top = 1;  // PRIME^(BLOCK-1): what the byte leaving the window weighs
    for (size_t k = 1; k < BLOCK; ++k) {
        top *= PRIME;
    }
    size_t pending = 0;  // the start of bytes not yet written (an insert)
    auto flushInsert = [&](size_t until) {
        if (until > pending) {
            out.push_back(static_cast<char>(INSERT));
            putVarint(out, until - pending);
            out.append(target, pending, until - pending);
        }
    };
    size_t i = 0;
    uint64_t h = 0;
    bool rolling = false;
    while (index.size() > 0 && i + BLOCK <= target.size()) {
        if (!rolling) {
            h = hashOf(t + i);
            rolling = true;
        }
        auto it = index.find(h);
        if (it != index.end() && std::equal(t + i, t + i + BLOCK, b + it->second)) {
            size_t from = it->second;
            size_t start = i;
            // Extended backwards into what would be inserted, and forwards as far as the bytes are the same
            while (start > pending && from > 0 && t[start - 1] == b[from - 1]) {
                --start;
                --from;
            }
            size_t end = i + BLOCK, fromEnd = it->second + BLOCK;
            while (end < target.size() && fromEnd < base.size() && t[end] == b[fromEnd]) {
                ++end;
                ++fromEnd;
            }
            flushInsert(start);
            out.push_back(static_cast<char>(COPY));
            putVarint(out, from);
            putVarint(out, end - start);
            pending = i = end;
            rolling = false;
            continue;
        }
        if (i + BLOCK < target.size()) {
            h = (h - t[i] * top) * PRIME + t[i + BLOCK];
        }
        ++i;
    }
    flushInsert(target.size());
    return out;
}

bool apply(const std::string& base, const std::string& delta, std::string& target) {
    target.clear();
    if (delta.size() < 4 || delta.compare(0, 4, MAGIC, 4) != 0) {
        return false;
    }
    size_t at = 4;
    uint64_t baseSize = 0, size = 0;
    if (!getVarint(delta, at, baseSize) || !getVarint(delta, at, size) || baseSize != base.size() ||
        size > (uint64_t(1) << 34)) {
        return false;
    }
    target.reserve(static_cast<size_t>(size));
    while (at < delta.size()) {
        const auto op = static_cast<unsigned char>(delta[at++]);
        uint64_t a = 0, n = 0;
        if (op == COPY) {
            if (!getVarint(delta, at, a) || !getVarint(delta, at, n) || a > base.size() || n > base.size() - a) {
                return false;
            }
            target.append(base, static_cast<size_t>(a), static_cast<size_t>(n));
        } else if (op == INSERT) {
            if (!getVarint(delta, at, n) || n > delta.size() - at) {
                return false;
            }
            target.append(delta, at, static_cast<size_t>(n));
            at += static_cast<size_t>(n);
        } else {
            return false;
        }
        if (target.size() > size) {
            return false;
        }
    }
    return target.size() == size;
}

}  // namespace xqt::ByteDelta

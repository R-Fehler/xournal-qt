/*
 * xournal-qt: the byte delta of older versions (ByteDelta.h): round trips over many random edits (inserts, deletes,
 * replaced and moved blocks), the edge cases, deterministic output, a small delta for a small change of a large
 * text, and deltas that do not fit their base or are damaged are refused.
 *
 * @license GNU GPLv2 or later
 */
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include "session/ByteDelta.h"

using namespace xqt;

namespace {
struct Random {
    uint64_t state;
    explicit Random(uint64_t seed): state(seed * 6364136223846793005ULL + 1442695040888963407ULL) {}
    uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state >> 33;
    }
    size_t below(size_t n) { return n == 0 ? 0 : static_cast<size_t>(next() % n); }
};

/// Something like the XML of a .xopp: lines of strokes with numbers.
std::string xmlLike(Random& r, size_t lines) {
    std::string out = "<?xml version=\"1.0\" standalone=\"no\"?>\n<xournal creator=\"xournal-qt\" fileversion=\"4\">\n";
    for (size_t i = 0; i < lines; ++i) {
        out += "<stroke tool=\"pen\" color=\"#000000ff\" width=\"1.41\">";
        for (int k = 0; k < 12; ++k) {
            out += std::to_string(r.below(60000) / 100.0) + " ";
        }
        out += "</stroke>\n";
    }
    return out + "</xournal>\n";
}

std::string edited(Random& r, std::string s) {
    const size_t edits = 1 + r.below(8);
    for (size_t e = 0; e < edits; ++e) {
        const size_t at = r.below(s.size() + 1);
        const size_t n = r.below(200);
        switch (r.below(4)) {
            case 0: {  // insert
                std::string ins;
                for (size_t k = 0; k < n; ++k) {
                    ins.push_back(static_cast<char>(r.below(256)));
                }
                s.insert(at, ins);
                break;
            }
            case 1:  // delete
                s.erase(at, n);
                break;
            case 2:  // replace
                for (size_t k = at; k < s.size() && k < at + n; ++k) {
                    s[k] = static_cast<char>(r.below(256));
                }
                break;
            default: {  // a block moved
                const std::string block = s.substr(at, n);
                s.erase(at, n);
                s.insert(r.below(s.size() + 1), block);
                break;
            }
        }
    }
    return s;
}

void roundTrip(const std::string& base, const std::string& target) {
    const std::string delta = ByteDelta::encode(base, target);
    std::string back;
    ASSERT_TRUE(ByteDelta::apply(base, delta, back));
    ASSERT_EQ(back, target);
    EXPECT_EQ(ByteDelta::encode(base, target), delta) << "deterministic";
}
}  // namespace

TEST(ByteDeltaTest, roundTripsOverRandomEdits) {
    for (uint64_t seed = 1; seed <= 300; ++seed) {
        Random r(seed);
        const std::string base = xmlLike(r, 1 + r.below(300));
        const std::string target = edited(r, base);
        roundTrip(base, target);
        roundTrip(target, base);
    }
}

TEST(ByteDeltaTest, edgeCases) {
    Random r(7);
    const std::string text = xmlLike(r, 50);
    roundTrip("", "");
    roundTrip("", text);
    roundTrip(text, "");
    roundTrip(text, text);
    roundTrip("short", "shorter");
    roundTrip(std::string(10000, 'a'), std::string(10001, 'a'));  // (one block everywhere)
    roundTrip(std::string(5000, 'a') + "b", "b" + std::string(5000, 'a'));
    std::string binary;
    for (int i = 0; i < 70000; ++i) {
        binary.push_back(static_cast<char>(r.below(256)));
    }
    roundTrip(binary, binary.substr(1000) + binary.substr(0, 1000));
}

// One more stroke in a large document: the delta is a tiny part of it
TEST(ByteDeltaTest, aSmallChangeGivesASmallDelta) {
    Random r(11);
    const std::string base = xmlLike(r, 20000);  // (about 2 MB)
    std::string target = base;
    const size_t at = target.find("<stroke", target.size() / 2);
    target.insert(at, "<stroke tool=\"pen\" color=\"#ff0000ff\" width=\"2\">1 2 3 4 5 6</stroke>\n");
    const std::string delta = ByteDelta::encode(base, target);
    EXPECT_LT(delta.size(), 200u) << "of " << base.size();
    std::string back;
    ASSERT_TRUE(ByteDelta::apply(base, delta, back));
    EXPECT_EQ(back, target);
}

TEST(ByteDeltaTest, refusesDeltasThatDoNotFit) {
    Random r(3);
    const std::string base = xmlLike(r, 100);
    const std::string target = edited(r, base);
    const std::string delta = ByteDelta::encode(base, target);
    std::string out;
    EXPECT_FALSE(ByteDelta::apply(base + "x", delta, out)) << "another base";
    EXPECT_FALSE(ByteDelta::apply(base, "XQD2" + delta.substr(4), out)) << "not ours";
    for (size_t cut = 0; cut < delta.size(); cut += 1 + delta.size() / 50) {
        EXPECT_FALSE(ByteDelta::apply(base, delta.substr(0, cut), out)) << "cut at " << cut;
    }
    std::string damaged = delta;
    damaged[5] = '\x7f';  // (the target's size)
    EXPECT_FALSE(ByteDelta::apply(base, damaged, out));
    for (size_t k = 6; k < delta.size(); k += 7) {  // (anything, never out of bounds)
        std::string d = delta;
        d[k] = static_cast<char>(d[k] ^ 0x5a);
        ByteDelta::apply(base, d, out);
    }
}

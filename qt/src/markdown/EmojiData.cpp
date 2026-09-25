#include "EmojiData.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_set>
#include <utility>

namespace xqt::emoji {

namespace {

constexpr Emoji TABLE[] = {
#include "gemoji.inc"
};

constexpr std::array<const char*, 9> CATEGORIES = {"Smileys & Emotion", "People & Body", "Animals & Nature",
                                                   "Food & Drink",      "Travel & Places", "Activities",
                                                   "Objects",           "Symbols",         "Flags"};

/// The words of a space-separated list.
template <typename F>
void eachWord(const char* list, F&& f) {
    std::string_view s(list);
    while (!s.empty()) {
        const size_t space = s.find(' ');
        f(s.substr(0, space));
        if (space == std::string_view::npos) {
            break;
        }
        s.remove_prefix(space + 1);
    }
}

struct Name {
    std::string_view name;
    const Emoji* emoji;
};

/// Every shortcode, sorted (for finding one and the ones that begin with a prefix).
const std::vector<Name>& names() {
    static const std::vector<Name> sorted = [] {
        std::vector<Name> v;
        for (const Emoji& e: TABLE) {
            eachWord(e.aliases, [&](std::string_view a) { v.push_back({a, &e}); });
        }
        std::stable_sort(v.begin(), v.end(), [](const Name& a, const Name& b) { return a.name < b.name; });
        return v;
    }();
    return sorted;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c: out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

/// A word of `s` (after a "_" or " ") other than its first begins with `prefix`.
bool laterWordStartsWith(std::string_view s, std::string_view prefix) {
    for (size_t i = 0; i < s.size(); ++i) {
        if ((s[i] == '_' || s[i] == ' ') && startsWith(s.substr(i + 1), prefix)) {
            return true;
        }
    }
    return false;
}

bool isAlnum(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

}  // namespace

std::span<const Emoji> all() { return TABLE; }
std::span<const char* const> categories() { return CATEGORIES; }

bool isNameChar(char c) {
    return isAlnum(static_cast<unsigned char>(c)) || c == '_' || c == '+' || c == '-';
}

std::string_view byShortcode(std::string_view name) {
    const auto& v = names();
    auto it = std::lower_bound(v.begin(), v.end(), name, [](const Name& n, std::string_view k) { return n.name < k; });
    return it != v.end() && it->name == name ? std::string_view(it->emoji->emoji) : std::string_view();
}

std::string_view typedShortcode(std::string_view before) {
    size_t i = before.size();
    while (i > 0 && isNameChar(before[i - 1])) {
        --i;
    }
    if (i == 0 || before[i - 1] != ':' || before.size() - i < 2) {
        return {};
    }
    // The ":" starts a word: not after a letter, digit or another ":" ("10:30", "std::ve", "http://")
    if (i >= 2) {
        const auto c = static_cast<unsigned char>(before[i - 2]);
        if (isAlnum(c) || c == ':' || c == '_') {
            return {};
        }
    }
    return before.substr(i);
}

std::vector<Completion> complete(std::string_view prefix, size_t limit) {
    std::vector<Completion> out;
    if (prefix.empty() || limit == 0) {
        return out;
    }
    const std::string p = lower(prefix);
    std::unordered_set<const Emoji*> seen;
    auto add = [&](const Emoji* e, std::string_view name) {
        if (out.size() < limit && seen.insert(e).second) {
            out.push_back({e, name});
        }
    };
    // Shortcodes that begin with it: those of one word first (smile, smiley, smirk, then smile_cat), each
    // alphabetically
    const auto& v = names();
    auto from = std::lower_bound(v.begin(), v.end(), p, [](const Name& n, const std::string& k) { return n.name < k; });
    std::vector<const Name*> begins;
    for (auto it = from; it != v.end() && startsWith(it->name, p); ++it) {
        begins.push_back(&*it);
    }
    std::stable_partition(begins.begin(), begins.end(),
                          [](const Name* n) { return n->name.find('_') == std::string_view::npos; });
    for (const Name* n: begins) {
        add(n->emoji, n->name);
    }
    // A later word of a shortcode, then a tag
    for (const Emoji& e: TABLE) {
        eachWord(e.aliases, [&](std::string_view a) {
            if (laterWordStartsWith(a, p)) {
                add(&e, a);
            }
        });
    }
    for (const Emoji& e: TABLE) {
        eachWord(e.tags, [&](std::string_view t) {
            if (startsWith(t, p)) {
                std::string_view first;
                eachWord(e.aliases, [&](std::string_view a) {
                    if (first.empty()) {
                        first = a;
                    }
                });
                add(&e, first);
            }
        });
    }
    return out;
}

std::vector<const Emoji*> search(std::string_view query, size_t limit) {
    std::vector<const Emoji*> out;
    const std::string q = lower(query);
    std::vector<const Emoji*> later;
    for (const Emoji& e: TABLE) {
        if (q.empty()) {
            out.push_back(&e);
            continue;
        }
        bool first = false;
        bool any = false;
        eachWord(e.aliases, [&](std::string_view a) {
            first = first || startsWith(a, q);
            any = any || a.find(q) != std::string_view::npos;
        });
        any = any || std::string_view(e.tags).find(q) != std::string_view::npos ||
              std::string_view(e.description).find(q) != std::string_view::npos;
        if (first) {
            out.push_back(&e);
        } else if (any) {
            later.push_back(&e);
        }
    }
    out.insert(out.end(), later.begin(), later.end());
    if (out.size() > limit) {
        out.resize(limit);
    }
    return out;
}

std::vector<Shortcode> findShortcodes(std::string_view text) {
    std::vector<Shortcode> out;
    size_t i = text.find(':');
    while (i != std::string_view::npos && i + 1 < text.size()) {
        size_t j = i + 1;
        while (j < text.size() && isNameChar(text[j])) {
            ++j;
        }
        if (j == i + 1 || j >= text.size() || text[j] != ':') {
            // (no name, or no closing ":": the next ":" may begin one)
            i = text.find(':', j == i + 1 ? i + 1 : j);
            continue;
        }
        if (const std::string_view e = byShortcode(text.substr(i + 1, j - i - 1)); !e.empty()) {
            out.push_back({i, j + 1 - i, e});
            i = text.find(':', j + 1);
        } else {
            i = j;  // (the closing ":" may begin the next one: ":not:smile:")
        }
    }
    return out;
}

}  // namespace xqt::emoji

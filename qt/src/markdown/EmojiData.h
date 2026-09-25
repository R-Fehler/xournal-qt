/*
 * xournal-qt: emoji by name: shortcodes (":smile:" is 😄), their completion while typing, and the picker's search.
 *
 * The names are GitHub's (gemoji, qt/3rdparty/gemoji): 1870 emoji, each with one or more shortcodes, some words
 * (tags) and its Unicode name. Qt-free (the Markdown renderer uses it on its threads).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xqt::emoji {

struct Emoji {
    const char* emoji;        ///< UTF-8 (a sequence for some: ZWJ, flags, VS16)
    const char* aliases;      ///< its shortcodes, separated by spaces ("+1 thumbsup")
    const char* tags;         ///< words for searching, separated by spaces
    const char* description;  ///< its Unicode name ("grinning face with smiling eyes")
    unsigned char category;   ///< index into categories()
    bool skinTones;           ///< it takes a skin tone modifier
};

/// Every emoji, in gemoji's order (by category, as the emoji keyboards show them).
std::span<const Emoji> all();
/// The categories' names ("Smileys & Emotion", ...), in their order.
std::span<const char* const> categories();

/// The emoji of a shortcode's name ("smile" -> "😄"; without the colons, lower case); empty if there is none.
std::string_view byShortcode(std::string_view name);

/// Whether a byte can be in a shortcode's name: a–z, 0–9, "_", "+", "-" (upper case letters are taken as lower case).
bool isNameChar(char c);

/// The shortcode being typed at the end of `before` (the text before the cursor): the name after its ":" if it has
/// at least 2 characters and the ":" starts a word (not in "10:30" or "a::b"); empty otherwise. E.g. "Hi :smi" ->
/// "smi".
std::string_view typedShortcode(std::string_view before);

/// A suggestion for a shortcode being typed.
struct Completion {
    const Emoji* emoji = nullptr;
    std::string_view name;  ///< the shortcode that matched (without colons)
};
/// The emoji whose shortcode begins with `prefix` (those of one word first, each alphabetically: ":smi" -> smile,
/// smiley, smirk, smile_cat, ...), then those
/// with a word of their shortcode beginning with it (slightly_smiling_face), then those tagged with such a word. At
/// most `limit`, each emoji once. Case-insensitive.
std::vector<Completion> complete(std::string_view prefix, size_t limit = 8);

/// The picker's search: emoji whose shortcodes, tags or name contain `query` (case-insensitive), those whose
/// shortcode begins with it first. An empty query: every emoji.
std::vector<const Emoji*> search(std::string_view query, size_t limit = 5000);

/// A shortcode found in a text: bytes [at, at + length) are ":name:", shown as `emoji`.
struct Shortcode {
    size_t at = 0;
    size_t length = 0;
    std::string_view emoji;
};
/// The known shortcodes in a text, in order (unknown ":words:" are left alone).
std::vector<Shortcode> findShortcodes(std::string_view text);

}  // namespace xqt::emoji

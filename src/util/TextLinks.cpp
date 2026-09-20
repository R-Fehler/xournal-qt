#include "util/TextLinks.h"

#include <algorithm>
#include <cctype>

namespace xoj::util {

namespace {
bool startsWith(const std::string& text, size_t at, const char* prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    if (at + length > text.size()) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (std::tolower(static_cast<unsigned char>(text[at + i])) != prefix[i]) {
            return false;
        }
    }
    return true;
}

/// Where a link stops: a space, a line break, or punctuation that just ends the sentence.
size_t endOfLink(const std::string& text, size_t from) {
    size_t end = from;
    while (end < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[end]);
        if (std::isspace(c) || c == '<' || c == '>' || c == '"') {
            break;
        }
        ++end;
    }
    while (end > from) {  // ".", ",", ")" and friends at the very end belong to the sentence
        const char last = text[end - 1];
        if (last == '.' || last == ',' || last == ';' || last == ':' || last == '!' || last == '?' || last == ')' ||
            last == ']' || last == '\'') {
            --end;
        } else {
            break;
        }
    }
    return end;
}

/// The start of a word: a link is only found at one.
bool atWordStart(const std::string& text, size_t at) {
    if (at == 0) {
        return true;
    }
    const unsigned char before = static_cast<unsigned char>(text[at - 1]);
    return std::isspace(before) || before == '(' || before == '[' || before == '<';
}
}  // namespace

std::vector<TextLink> findLinks(const std::string& text) {
    std::vector<TextLink> links;
    for (size_t i = 0; i < text.size(); ++i) {
        if (!atWordStart(text, i)) {
            continue;
        }
        std::string scheme;
        if (startsWith(text, i, "https://") || startsWith(text, i, "http://") || startsWith(text, i, "mailto:") ||
            startsWith(text, i, "file://")) {
            scheme.clear();
        } else if (startsWith(text, i, "www.")) {
            scheme = "https://";
        } else {
            continue;
        }
        const size_t end = endOfLink(text, i);
        const size_t length = end - i;
        if (length < 8) {  // "www.x.de" is the shortest that makes sense
            continue;
        }
        links.push_back({i, length, scheme + text.substr(i, length)});
        i = end;
    }
    return links;
}

}  // namespace xoj::util

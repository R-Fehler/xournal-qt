/*
 * xournal-qt: lines of a Markdown source (internal helpers of qt/src/markdown).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace xqt::md::text {

inline size_t lineStart(std::string_view s, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    const size_t i = s.rfind('\n', std::min(pos, s.size()) - 1);
    return i == std::string_view::npos ? 0 : i + 1;
}
/// The end of the line `pos` is in (its line break, or the end of the text).
inline size_t lineEnd(std::string_view s, size_t pos) {
    const size_t i = s.find('\n', pos);
    return i == std::string_view::npos ? s.size() : i;
}
/// The start of the line after the one `pos` is in.
inline size_t nextLine(std::string_view s, size_t pos) {
    const size_t i = s.find('\n', pos);
    return i == std::string_view::npos ? s.size() : i + 1;
}
inline std::string_view lineAt(std::string_view s, size_t start) {
    const size_t end = s.find('\n', start);
    return s.substr(start, (end == std::string_view::npos ? s.size() : end) - start);
}
inline bool blank(std::string_view line) { return line.find_first_not_of(" \t\r") == std::string_view::npos; }
inline std::string_view afterIndent(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') {
        ++i;
    }
    return line.substr(i);
}
/// The fence of a fenced code block's opening line ("```", "~~~~"); empty if it is none.
inline std::string fenceOf(std::string_view line) {
    line = afterIndent(line);
    if (line.empty() || (line[0] != '`' && line[0] != '~')) {
        return {};
    }
    const size_t n = line.find_first_not_of(line[0]);
    const size_t count = n == std::string_view::npos ? line.size() : n;
    return count >= 3 ? std::string(count, line[0]) : std::string();
}
inline bool closesFence(std::string_view line, const std::string& fence) {
    line = afterIndent(line);
    if (fence.empty() || line.size() < fence.size() || line[0] != fence[0]) {
        return false;
    }
    const size_t n = line.find_first_not_of(fence[0]);
    return n == std::string_view::npos ? line.size() >= fence.size() : n >= fence.size() && blank(line.substr(n));
}
inline bool setextUnderline(std::string_view line) {
    line = afterIndent(line);
    if (line.empty() || (line[0] != '=' && line[0] != '-')) {
        return false;
    }
    const size_t n = line.find_first_not_of(line[0]);
    return n == std::string_view::npos || blank(line.substr(n));
}
/// Whether a line would start a block other than a paragraph (a paragraph must not continue with it on a page).
inline bool startsBlock(std::string_view line) {
    const std::string_view s = afterIndent(line);
    if (s.empty()) {
        return true;
    }
    const char c = s[0];
    if (c == '#' || c == '>' || c == '<' || c == '|' || c == '`' || c == '~' || c == '=' || c == '\t') {
        return true;
    }
    if ((c == '-' || c == '*' || c == '+') && (s.size() == 1 || s[1] == ' ' || s[1] == c)) {
        return true;
    }
    if (c >= '0' && c <= '9') {
        const size_t n = s.find_first_not_of("0123456789");
        return n != std::string_view::npos && n <= 9 && (s[n] == '.' || s[n] == ')');
    }
    return line.size() >= 4 && line.substr(0, 4) == "    ";  // (indented code)
}

}  // namespace xqt::md::text

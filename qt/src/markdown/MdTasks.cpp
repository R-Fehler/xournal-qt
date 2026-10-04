#include "MdTasks.h"

#include <cctype>

#include "MdDocument.h"

namespace xqt::md::tasks {

namespace {
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trimmed(std::string_view s) {
    while (!s.empty() && space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

void collect(const Block& b, std::string_view source, std::vector<Task>& out) {
    if (b.kind == BlockKind::ListItem && b.task && b.taskMark != NO_SOURCE && b.taskMark + 1 < source.size() &&
        source[b.taskMark + 1] == ']') {
        Task t;
        t.mark = b.taskMark;
        t.done = source[t.mark] != ' ';
        const size_t nl = t.mark == 0 ? std::string_view::npos : source.rfind('\n', t.mark - 1);
        t.lineBegin = nl == std::string_view::npos ? 0 : nl + 1;
        const size_t end = source.find('\n', t.mark);
        t.lineEnd = end == std::string_view::npos ? source.size() : end;
        t.text = std::string(trimmed(source.substr(t.mark + 2, t.lineEnd - (t.mark + 2))));
        out.push_back(std::move(t));
    }
    for (const Block& child: b.children) {
        collect(child, source, out);
    }
}

bool digits(std::string_view s, size_t at, size_t n) {
    if (at + n > s.size()) {
        return false;
    }
    for (size_t i = at; i < at + n; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
    }
    return true;
}

/// A date "YYYY-MM-DD" at `at` (a plausible one)
bool dateAt(std::string_view s, size_t at) {
    if (!digits(s, at, 4) || at + 10 > s.size() || s[at + 4] != '-' || !digits(s, at + 5, 2) || s[at + 7] != '-' ||
        !digits(s, at + 8, 2)) {
        return false;
    }
    if (at + 10 < s.size() && std::isdigit(static_cast<unsigned char>(s[at + 10]))) {
        return false;
    }
    const int month = std::stoi(std::string(s.substr(at + 5, 2)));
    const int day = std::stoi(std::string(s.substr(at + 8, 2)));
    return month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

constexpr std::string_view CALENDAR = "\xF0\x9F\x93\x85";  // 📅

/// Where the due date's token is: [begin, end) and the date's offset; false: none.
bool dueToken(std::string_view text, size_t& begin, size_t& end, size_t& date) {
    for (size_t i = 0; i < text.size(); ++i) {
        size_t after = std::string_view::npos;
        if (text.compare(i, CALENDAR.size(), CALENDAR) == 0) {
            after = i + CALENDAR.size();
            if (after < text.size() && text.compare(after, 3, "\xEF\xB8\x8F") == 0) {
                after += 3;  // (the emoji presentation selector)
            }
        } else if (i + 4 <= text.size() && std::tolower(static_cast<unsigned char>(text[i])) == 'd' &&
                   std::tolower(static_cast<unsigned char>(text[i + 1])) == 'u' &&
                   std::tolower(static_cast<unsigned char>(text[i + 2])) == 'e' && text[i + 3] == ':' &&
                   (i == 0 || !std::isalnum(static_cast<unsigned char>(text[i - 1])))) {
            after = i + 4;
        }
        if (after == std::string_view::npos) {
            continue;
        }
        while (after < text.size() && (text[after] == ' ' || text[after] == '\t')) {
            ++after;
        }
        if (dateAt(text, after)) {
            begin = i;
            date = after;
            end = after + 10;
            return true;
        }
    }
    return false;
}
}  // namespace

std::vector<Task> find(std::string_view source) {
    std::vector<Task> out;
    // (a quick test before parsing: most texts have no task)
    if (source.find("[ ]") == std::string_view::npos && source.find("[x]") == std::string_view::npos &&
        source.find("[X]") == std::string_view::npos) {
        return out;
    }
    const Document doc = parse(source);
    if (doc.plain) {
        return out;
    }
    collect(doc.root, source, out);
    int line = 0;
    size_t at = 0;
    for (Task& t: out) {  // (in the order of the text)
        for (; at < t.lineBegin; ++at) {
            line += source[at] == '\n' ? 1 : 0;
        }
        t.line = line;
    }
    return out;
}

std::string dueDate(std::string_view text) {
    size_t begin = 0, end = 0, date = 0;
    return dueToken(text, begin, end, date) ? std::string(text.substr(date, 10)) : std::string();
}

std::string withoutDueDate(std::string_view text) {
    size_t begin = 0, end = 0, date = 0;
    if (!dueToken(text, begin, end, date)) {
        return std::string(text);
    }
    std::string head(trimmed(text.substr(0, begin)));
    const std::string_view tail = trimmed(text.substr(end));
    if (!head.empty() && !tail.empty()) {
        head += ' ';
    }
    return head + std::string(tail);
}

bool isStamp(std::string_view source) {
    const std::string_view s = trimmed(source);
    return s.size() == 5 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ' && s[2] == '[' &&
           (s[3] == ' ' || s[3] == 'x' || s[3] == 'X') && s[4] == ']';
}

std::string withTask(std::string_view source, size_t mark, bool done) {
    std::string out(source);
    if (mark < out.size() && (out[mark] == ' ' || out[mark] == 'x' || out[mark] == 'X')) {
        out[mark] = done ? 'x' : ' ';
    }
    return out;
}

}  // namespace xqt::md::tasks

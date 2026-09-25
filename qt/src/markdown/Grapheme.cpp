#include "Grapheme.h"

#include <algorithm>
#include <vector>

#include <pango/pango.h>

namespace xqt::text {

size_t graphemeStep(std::string_view t, size_t pos, bool forward) {
    pos = std::min(pos, t.size());
    if (forward ? pos >= t.size() : pos == 0) {
        return pos;
    }
    if (t[forward ? pos : pos - 1] == '\n') {
        return forward ? pos + 1 : pos - 1;
    }
    const size_t nl = t.rfind('\n', forward ? pos : pos - 1);
    const size_t begin = nl == std::string_view::npos ? 0 : nl + 1;
    const size_t end = std::min(t.find('\n', pos), t.size());
    const std::string_view line = t.substr(begin, end - begin);
    const glong chars = g_utf8_strlen(line.data(), static_cast<gssize>(line.size()));
    std::vector<PangoLogAttr> attrs(static_cast<size_t>(chars) + 1);
    pango_get_log_attrs(line.data(), static_cast<int>(line.size()), -1, pango_language_get_default(), attrs.data(),
                        static_cast<int>(attrs.size()));
    // The characters' byte offsets in the line, and the one of `pos`
    std::vector<size_t> offsets;
    offsets.reserve(attrs.size());
    for (const char* p = line.data(); p < line.data() + line.size(); p = g_utf8_next_char(p)) {
        offsets.push_back(static_cast<size_t>(p - line.data()));
    }
    offsets.push_back(line.size());
    const size_t local = pos - begin;
    size_t i = static_cast<size_t>(std::lower_bound(offsets.begin(), offsets.end(), local) - offsets.begin());
    if (forward) {
        for (++i; i < offsets.size() && !attrs[i].is_cursor_position; ++i) {}
        return begin + (i < offsets.size() ? offsets[i] : line.size());
    }
    while (i > 0) {
        --i;
        if (attrs[i].is_cursor_position) {
            break;
        }
    }
    return begin + offsets[i];
}

}  // namespace xqt::text

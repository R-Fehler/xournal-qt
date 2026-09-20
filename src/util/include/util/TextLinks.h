/*
 * xournal-qt: web addresses inside a text element.
 *
 * Text that was pasted or typed often contains a link. Those parts are drawn underlined and in a link colour
 * (TextView) and can be tapped (the Qt canvas opens them). The text itself stays plain text, so nothing changes
 * about the file format.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

namespace xoj::util {

struct TextLink {
    size_t start = 0;  ///< byte offset in the text
    size_t length = 0;
    std::string uri;   ///< ready to open ("www.x.org" becomes "https://www.x.org")
};

/// The links in a text, in the order they appear.
std::vector<TextLink> findLinks(const std::string& text);

}  // namespace xoj::util

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
    std::string uri;   ///< ready to open ("www.x.org" becomes "https://www.x.org"); empty for a page link
    int page = 0;      ///< a page of this document (1-based; 0: not a page link)
};

/// The links in a text, in the order they appear: web addresses and "#Page:12" (a page of the document).
std::vector<TextLink> findLinks(const std::string& text);

/// Give the page links other numbers: newPage[oldPage - 1], where 0 leaves a link as it is. Returns true if the
/// text changed (the pages of a document moved, so the links follow them).
bool renumberPageLinks(std::string& text, const std::vector<int>& newPage);

/// How a link to a page is written.
std::string pageLinkText(int page);

}  // namespace xoj::util

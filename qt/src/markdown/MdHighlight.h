/*
 * xournal-qt: syntax highlighting of code blocks (KSyntaxHighlighting, Kate's highlighter: some 300 languages).
 *
 * Optional: without KSyntaxHighlighting at build time (XQT_HAVE_KSYNTAXHIGHLIGHTING), code has no colors.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <vector>

#include "util/Color.h"

namespace xqt::md {

/// Formatting of a part of the code: bytes [start, start + length) of the UTF-8 text.
struct CodeSpan {
    int start = 0;
    int length = 0;
    Color color;
    bool bold = false;
    bool italic = false;
};

/// The highlighting of a code block. `language`: the fence's info ("python", "cpp", "js", "C++", "bash", ...);
/// nothing if it is unknown, empty or highlighting is not built in.
std::vector<CodeSpan> highlight(const std::string& code, const std::string& language);

/// Whether highlighting is built in.
bool highlightingAvailable();

}  // namespace xqt::md

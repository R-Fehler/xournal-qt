/*
 * xournal-qt: Markdown texts outside the page's Markdown layer, marked in their data (qt/docs/features/plugins.md,
 * "Shapes"). A group lies in one layer, so the Markdown boxes of a group with ink (the function plotter's axis names,
 * numbers and formulas) stay in the ink's layer; the element data `{"xqt:markdown": true}` (the attribute xqt-data;
 * a key no plugin id can be) makes them Markdown texts there: drawn formatted and as big as drawn (the classifier of
 * model/MarkdownText.h, installed with the sticky notes'). Xournal++ shows their source, as it does for every box.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>

class Text;

namespace xqt::md {

inline constexpr const char* INLINE_KEY = "xqt:markdown";

/// The text's data marks it as a Markdown text
bool isInlineMarkdown(const Text& text);
/// Element data with the mark added (`data`: a JSON object, or "")
std::string withInlineMark(const std::string& data);

}  // namespace xqt::md

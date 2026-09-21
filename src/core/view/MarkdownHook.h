/*
 * Xournal++
 *
 * xournal-qt: Markdown boxes. The texts of a layer named "Markdown" hold Markdown sources. A frontend that
 * registers a renderer has them drawn formatted (the Qt frontend: qt/src/markdown). Without one, as in upstream
 * Xournal++, they are ordinary texts that show the source.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>
#include <string_view>

#include <cairo.h>  // for cairo_t

class Text;

namespace xoj::view {

constexpr std::string_view MARKDOWN_LAYER_NAME = "Markdown";

/// Draws a Markdown text (in page coordinates, like TextView::draw).
using MarkdownTextRenderer = void (*)(const Text& text, cairo_t* cr);

/// Set once by the frontend (drawing happens on several threads). nullptr: texts are drawn as they are.
inline std::atomic<MarkdownTextRenderer> markdownTextRenderer{nullptr};

}  // namespace xoj::view

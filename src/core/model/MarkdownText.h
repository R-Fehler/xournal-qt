/*
 * Xournal++
 *
 * xournal-qt: Markdown texts. The texts of a layer named "Markdown" hold Markdown sources (the file format does not
 * change: they are ordinary texts in a layer with that name). A text knows it is one while it is in such a layer
 * (Text::isMarkdown, kept by Layer). A frontend that registers a renderer has them drawn formatted, and as big as
 * they are drawn (their bounding box); the Qt frontend: qt/src/markdown. Without one, as in upstream Xournal++,
 * they are ordinary texts that show the source.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>
#include <string>
#include <string_view>

#include <cairo.h>  // for cairo_t

class Text;

namespace xoj::markdown {

constexpr std::string_view LAYER_NAME = "Markdown";

inline bool isMarkdownLayerName(const std::string& name) { return name == LAYER_NAME; }

/// Draws a Markdown text (in page coordinates, like TextView::draw).
using Renderer = void (*)(const Text& text, cairo_t* cr);
/// The size a Markdown text is drawn at (from its top left, the origin of the text).
struct Size {
    double width = 0;
    double height = 0;
};
using Sizer = Size (*)(const Text& text);

/// Set once by the frontend (drawing and sizes are asked from several threads). nullptr: texts are texts.
inline std::atomic<Renderer> renderer{nullptr};
inline std::atomic<Sizer> sizer{nullptr};

}  // namespace xoj::markdown

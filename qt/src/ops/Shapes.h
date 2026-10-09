/*
 * xournal-qt: elements described as plain data ("shapes"), as operations take them and plugins give them
 * (qt/docs/features/plugins.md, "Shapes"). The same shapes are inserted (element.insert) and drawn as a preview, so a
 * preview looks as the inserted elements will.
 *
 *   {type: "stroke", points: [x0, y0, x1, y1, …], color: "#rrggbb", width: 1, style: "plain|dash|dot|dashdot",
 *    closed: false, fill: -1 (or 0…255: filled with that opacity), cap: "round|butt|square"}
 *   {type: "text", x, y, text, size: 12, font: "Sans", color, anchor: "top-left"}     a plain Xournal++ text
 *   {type: "markdown", x, y, text, size: 12, font: "Sans", color, anchor: "top-left"}  a Markdown box ($…$ is math)
 *
 * Page coordinates (points). `anchor` names the point of the text's box that lands at (x, y): "top-left", "top",
 * "top-right", "left", "center", "right", "bottom-left", "bottom", "bottom-right". A Markdown box is made as wide as
 * its text (one line unless it has line breaks).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <memory>
#include <optional>

#include <QString>
#include <QVariantMap>

#include "util/Color.h"

class Element;

namespace xqt::ops {

struct MadeElement {
    std::unique_ptr<Element> element;
    bool markdown = false;  ///< belongs into the page's Markdown layer
};

/// The element of a shape (Error::Invalid for a bad one). Markdown boxes need the Markdown renderer installed
/// (md::installRenderer) to be measured as drawn.
MadeElement makeElement(const QVariantMap& shape);
/// "#rrggbb" (or "rrggbb")
std::optional<Color> parseColor(const QString& text);
QString colorName(Color c);
/// The operation that inserts a shape of this type ("stroke.insert", "text.insert", "markdown.insert")
QString insertOperationOf(const QVariantMap& shape);

}  // namespace xqt::ops

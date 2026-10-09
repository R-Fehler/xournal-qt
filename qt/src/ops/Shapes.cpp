#include "Shapes.h"

#include <cmath>
#include <vector>

#include <pango/pangocairo.h>

#include "model/Font.h"
#include "model/LineStyle.h"
#include "model/Point.h"
#include "model/Stroke.h"
#include "model/StrokeStyle.h"
#include "model/Text.h"
#include "util/Matrix.h"

#include "MdBox.h"
#include "MdLayout.h"
#include "Operations.h"

namespace xqt::ops {

namespace {
constexpr int MAX_POINTS = 200000;  ///< points of one stroke
constexpr int MAX_TEXT = 20000;     ///< characters of one text

[[noreturn]] void invalid(const QString& what) { throw Error(Error::Kind::Invalid, what); }

Color colorOf(const QVariantMap& shape) {
    const QString c = shape.value("color", "#000000").toString();
    const auto parsed = parseColor(c);
    if (!parsed) {
        invalid(QStringLiteral("\"%1\" is no color (#rrggbb)").arg(c));
    }
    return *parsed;
}

std::vector<double> coordinates(const QVariant& v) {
    std::vector<double> out;
    const QVariantList list = v.toList();
    out.reserve(static_cast<size_t>(list.size()) * 2);
    for (const QVariant& item: list) {
        if (item.typeId() == QMetaType::QVariantList) {  // [[x, y], …]
            const QVariantList xy = item.toList();
            if (xy.size() < 2) {
                invalid(QStringLiteral("a point needs x and y"));
            }
            out.push_back(xy[0].toDouble());
            out.push_back(xy[1].toDouble());
        } else {
            bool ok = false;
            out.push_back(item.toDouble(&ok));
            if (!ok) {
                invalid(QStringLiteral("points must be numbers"));
            }
        }
    }
    if (out.size() % 2 != 0) {
        invalid(QStringLiteral("points come in pairs (x, y)"));
    }
    for (double d: out) {
        if (!std::isfinite(d) || std::abs(d) > 1e6) {
            invalid(QStringLiteral("a point is not on any page"));
        }
    }
    return out;
}

/// The fraction of the box's width and height that lies left of and above the anchor
std::pair<double, double> anchorOf(const QString& a) {
    double ax = 0, ay = 0;
    if (a.isEmpty() || a == QLatin1String("top-left")) {
        return {0, 0};
    }
    if (a == QLatin1String("center")) {
        return {0.5, 0.5};
    }
    const QStringList parts = a.split('-');
    for (const QString& p: parts) {
        if (p == QLatin1String("top")) {
            ay = 0;
        } else if (p == QLatin1String("bottom")) {
            ay = 1;
        } else if (p == QLatin1String("left")) {
            ax = 0;
        } else if (p == QLatin1String("right")) {
            ax = 1;
        } else {
            invalid(QStringLiteral("no anchor \"%1\"").arg(a));
        }
    }
    const bool horizontal = a.contains(QLatin1String("left")) || a.contains(QLatin1String("right"));
    const bool vertical = a.contains(QLatin1String("top")) || a.contains(QLatin1String("bottom"));
    if (!horizontal) {
        ax = 0.5;
    }
    if (!vertical) {
        ay = 0.5;
    }
    return {ax, ay};
}

/// How big a Markdown source is drawn when nothing wraps it: the widest line and the height
std::pair<double, double> naturalSize(const std::string& source, md::Style style) {
    style.width = 4000;
    const md::Layout& layout = md::cachedLayout(source, style);
    double width = 0;
    for (const md::Item& item: layout.items) {
        double w = item.width;
        if (item.kind == md::Item::Kind::Text && item.layout) {
            PangoRectangle ink, logical;
            pango_layout_get_extents(item.layout.get(), &ink, &logical);
            w = static_cast<double>(logical.x + logical.width) / PANGO_SCALE;
        }
        width = std::max(width, item.x + w);
    }
    return {width, layout.height};
}

std::unique_ptr<Text> makeText(const QVariantMap& shape, bool markdown) {
    const QString content = shape.value("text").toString();
    if (content.size() > MAX_TEXT) {
        invalid(QStringLiteral("a text is too long"));
    }
    const double size = number(shape, "size", 12.0);
    if (size < 1 || size > 500) {
        invalid(QStringLiteral("\"size\" must be 1 to 500 points"));
    }
    const double x = number(shape, "x");
    const double y = number(shape, "y");
    const auto [ax, ay] = anchorOf(shape.value("anchor").toString());
    auto text = std::make_unique<Text>();
    text->setText(content.toStdString());
    text->setFont(XojFont(shape.value("font", "Sans").toString().toStdString(), size));
    text->setColor(colorOf(shape));
    double w = 0, h = 0;
    if (markdown) {
        text->setMarkdown(true);  // (as its layer will make it: drawn formatted, also as a preview)
        md::Style style = md::styleOf(*text);
        const auto natural = naturalSize(content.toStdString(), style);
        w = natural.first;
        h = natural.second;
        text->setWrap(std::ceil(w) + 1);  // (as wide as its text: a label, not a column)
    } else {
        const auto& box = text->getBoundingBox();
        w = box.width;
        h = box.height;
    }
    text->setTransformation(xoj::util::Matrix::TRANSLATION(x - ax * w, y - ay * h));
    return text;
}
}  // namespace

std::optional<Color> parseColor(const QString& text) {
    QString t = text.trimmed();
    if (t.startsWith('#')) {
        t = t.mid(1);
    }
    if (t.size() != 6 && t.size() != 8) {
        return std::nullopt;
    }
    bool ok = false;
    const uint rgb = t.left(6).toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }
    return Color(static_cast<uint8_t>((rgb >> 16) & 0xff), static_cast<uint8_t>((rgb >> 8) & 0xff),
                 static_cast<uint8_t>(rgb & 0xff));
}

QString colorName(Color c) {
    return QStringLiteral("#%1%2%3")
            .arg(c.red, 2, 16, QLatin1Char('0'))
            .arg(c.green, 2, 16, QLatin1Char('0'))
            .arg(c.blue, 2, 16, QLatin1Char('0'));
}

QString insertOperationOf(const QVariantMap& shape) {
    const QString type = shape.value("type").toString();
    if (type == QLatin1String("stroke") || type == QLatin1String("text") || type == QLatin1String("markdown")) {
        return type + QStringLiteral(".insert");
    }
    invalid(QStringLiteral("no shape type \"%1\" (stroke, text, markdown)").arg(type));
}

MadeElement makeElement(const QVariantMap& shape) {
    const QString type = shape.value("type").toString();
    if (type == QLatin1String("stroke")) {
        std::vector<double> xy = coordinates(shape.value("points"));
        if (xy.size() < 4) {
            invalid(QStringLiteral("a stroke needs two points"));
        }
        if (xy.size() / 2 > MAX_POINTS) {
            invalid(QStringLiteral("a stroke has too many points"));
        }
        if (shape.value("closed").toBool()) {
            xy.push_back(xy[0]);
            xy.push_back(xy[1]);
        }
        const double width = number(shape, "width", 1.0);
        if (width < 0.05 || width > 150) {
            invalid(QStringLiteral("\"width\" must be 0.05 to 150 points"));
        }
        auto stroke = std::make_unique<Stroke>();
        stroke->setToolType(StrokeTool::PEN);
        stroke->setColor(colorOf(shape));
        stroke->setWidth(width);
        const QString style = shape.value("style", "plain").toString();
        if (style != QLatin1String("plain")) {
            if (style != QLatin1String("dash") && style != QLatin1String("dot") &&
                style != QLatin1String("dashdot")) {
                invalid(QStringLiteral("no line style \"%1\" (plain, dash, dot, dashdot)").arg(style));
            }
            stroke->setLineStyle(StrokeStyle::parseStyle(style.toStdString()));
        }
        const int fill = shape.contains("fill") ? integer(shape, "fill") : -1;
        stroke->setFill(std::clamp(fill, -1, 255));
        const QString cap = shape.value("cap", "round").toString();
        stroke->setStrokeCapStyle(cap == QLatin1String("butt")     ? StrokeCapStyle::BUTT
                                  : cap == QLatin1String("square") ? StrokeCapStyle::SQUARE
                                                                   : StrokeCapStyle::ROUND);
        std::vector<Point> points;
        points.reserve(xy.size() / 2);
        for (size_t i = 0; i + 1 < xy.size(); i += 2) {
            points.emplace_back(xy[i], xy[i + 1]);
        }
        stroke->setPointVector(std::move(points));
        return {std::move(stroke), false};
    }
    if (type == QLatin1String("text")) {
        return {makeText(shape, false), false};
    }
    if (type == QLatin1String("markdown")) {
        return {makeText(shape, true), true};
    }
    invalid(QStringLiteral("no shape type \"%1\" (stroke, text, markdown)").arg(type));
}

}  // namespace xqt::ops

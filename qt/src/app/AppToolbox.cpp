/*
 * xournal-qt: the toolbox of the window (qt/toolbox, qt/docs/toolbox.md): taking one of the user's own tools
 * (ToolboxModel) gives the tool in hand everything that entry holds: the tool, its drawing type (a shape), the color
 * (its palette role's in the chosen palette, else its own), the width, the line style and the filling, the eraser's
 * kind, the font of a text box. A sticky note entry puts a note of its color on the page.
 *
 * @license GNU GPLv2 or later
 */
#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "model/StrokeStyle.h"
#include "session/AppContext.h"
#include "session/PenFill.h"
#include "shell/ColorPalettes.h"
#include "shell/SettingsModel.h"
#include "shell/ToolboxModel.h"

#include "AppController.h"
#include "Snip.h"

using namespace xqt;

namespace {
Color toColorKeeping(const QColor& c, uint8_t alpha) {
    Color color(static_cast<uint8_t>(c.red()), static_cast<uint8_t>(c.green()), static_cast<uint8_t>(c.blue()));
    color.alpha = alpha;
    return color;
}
QColor toQColor(Color c) { return QColor(c.red, c.green, c.blue); }

/// The tool's width now (its own width while that is chosen, else its size's)
double widthOf(ToolHandler& th, ToolType type) {
    const Tool& t = th.getTool(type);
    if (th.isCustomThicknessActive(type) && th.getCustomThickness(type) > 0) {
        return th.getCustomThickness(type);
    }
    const double* sizes = th.getToolThickness(type);
    return sizes ? sizes[std::clamp(static_cast<int>(t.getSize()), 0, 4)] : 0;
}
}  // namespace

QObject* AppController::toolboxObject() const { return toolbox; }

bool AppController::toolboxMode() const { return SettingsModel::toolbarMode(*app->getSettings()) == "toolbox"; }

QString AppController::migratedToolbox() const {
    // The tools of before: the pen's color and width, the highlighter's, the eraser's kind and width, the font, the
    // shape last used (only what differs from a first start is carried over)
    ToolHandler* th = app->getToolHandler();
    QVariantList entries = ToolboxModel::defaultEntries();
    const QMap<QString, QString> roles = colorRoles();
    std::string variants;
    app->getSettings()->getCustomElement("xournalQt").getString("toolVariants", variants);
    QString shape;
    for (const QString& part: QString::fromStdString(variants).split(';', Qt::SkipEmptyParts)) {
        if (part.startsWith("shape=")) {
            shape = part.mid(6);
        }
    }
    bool firstPen = true;
    bool firstHighlighter = true;
    for (QVariant& v: entries) {
        QVariantMap e = v.toMap();
        const QString type = e.value("type").toString();
        if (type == "pen" && firstPen) {
            firstPen = false;
            e["width"] = widthOf(*th, TOOL_PEN);
            const ColorRef ref = ColorRef::parse(roles.value("pen"));
            if (ref.valid()) {
                e["role"] = ref.role;
            } else if (const QColor c = toQColor(th->getTool(TOOL_PEN).getColor()); c != QColor(Qt::black)) {
                e["color"] = c.name();  // (a color of one's own: no role)
                e["role"] = QString();
            }
        } else if (type == "highlighter" && firstHighlighter) {
            firstHighlighter = false;
            e["width"] = widthOf(*th, TOOL_HIGHLIGHTER);
            const ColorRef ref = ColorRef::parse(roles.value("highlighter"));
            if (ref.valid()) {
                e["role"] = ref.role;
            }
        } else if (type == "eraser") {
            e["variant"] = QString::fromUtf8(eraserTypeToString(th->getEraserType()).data());
            e["width"] = widthOf(*th, TOOL_ERASER);
        } else if (type == "text") {
            e["font"] = QVariantMap{{"family", fontFamily()}, {"size", markdownFontSize()}};
        } else if (type == "shape" && !shape.isEmpty()) {
            e["variant"] = shape;
        }
        v = ToolboxModel::normalized(e);
    }
    QJsonObject root;
    root["version"] = 1;
    root["entries"] = QJsonArray::fromVariantList(entries);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

QColor AppController::toolEntryColor(const QVariantMap& entry) const {
    return ToolboxModel::colorIn(entry, colorPalette());
}

bool AppController::entryInHand(const QVariantMap& e) const {
    const QString type = e.value("type").toString();
    const QString t = tool();
    const QString drawing = drawingType();
    if (type == "pen" || type == "highlighter") {
        return t == type && (drawing == "default" || drawing == "dontChange");
    }
    if (type == "shape") {
        return t == (e.value("base") == "highlighter" ? "highlighter" : "pen") && drawing == e.value("variant");
    }
    if (type == "eraser") {
        return t == "eraser" &&
               QString::fromUtf8(eraserTypeToString(app->getToolHandler()->getEraserType()).data()) ==
                       e.value("variant");
    }
    if (type == "laser") {
        return t == (e.value("base") == "highlighter" ? "laserPointerHighlighter" : "laserPointerPen");
    }
    if (type == "text") {
        return t == "text";
    }
    if (type == "snip") {
        return snipShape() == e.value("variant").toString();  // (armed with its shape)
    }
    return false;
}

bool AppController::applyToolEntry(const QString& id) {
    if (!toolbox) {
        return false;
    }
    const QVariantMap e = toolbox->entry(id);
    const QString type = e.value("type").toString();
    if (e.isEmpty()) {
        return false;
    }
    if (type == "sticky") {
        return insertStickyNote(QColor(e.value("color").toString()));
    }
    // A snip (qt/docs/snip.md): one picture of a rectangle or a lasso, then the tool in hand before comes back; it is
    // never the entry in hand for long, so the active entry stays the one it gives back to
    if (type == "snip") {
        startSnip(e.value("variant").toString());
        return true;
    }
    if (snip::isArmed()) {
        endSnip(false);  // (another tool taken: the snip ends)
    }
    ToolHandler* th = app->getToolHandler();
    Settings& settings = *app->getSettings();
    const QColor color = ToolboxModel::colorIn(e, colorPalette());
    const double width = e.value("width").toDouble();
    auto takeColor = [&](ToolType t) {
        if (color.isValid()) {
            th->setColor(toColorKeeping(color, th->getTool(t).getColor().alpha), false);
        }
    };
    if (type == "pen" || type == "highlighter" || type == "shape") {
        const ToolType t = ToolboxModel::highlights(e) ? TOOL_HIGHLIGHTER : TOOL_PEN;
        th->selectTool(t);
        th->setDrawingType(type == "shape" ? drawingTypeFromString(e.value("variant").toString().toStdString())
                                           : DRAWING_TYPE_DEFAULT);
        takeColor(t);
        th->setCustomThickness(t, width, true);
        if (th->hasCapability(TOOL_CAP_LINE_STYLE, SelectedTool::toolbar)) {
            th->setLineStyle(StrokeStyle::parseStyle(e.value("lineStyle", "plain").toString().toStdString()));
        }
        const QVariantMap fill = e.value("fill").toMap();
        if (th->hasCapability(TOOL_CAP_FILL, SelectedTool::toolbar)) {
            th->setFillEnabled(fill.value("on").toBool());
            const int alpha = std::clamp(fill.value("alpha", 128).toInt(), 1, 255);
            if (t == TOOL_HIGHLIGHTER) {
                th->setHighlighterFill(alpha);
            } else {
                th->setPenFill(alpha);
            }
            if (penfill::hasOwnColor(t)) {
                const QColor own(fill.value("color").toString());
                const std::optional<Color> wanted = own.isValid() ? std::optional<Color>(toColorKeeping(own, 255))
                                                                  : std::nullopt;
                if (penfill::color(settings, t) != wanted) {
                    penfill::setColor(settings, t, wanted);  // (written only when it changes)
                }
            }
        }
    } else if (type == "eraser") {
        const EraserType kind = eraserTypeFromString(e.value("variant").toString().toStdString());
        if (kind != ERASER_TYPE_NONE) {
            th->setEraserType(kind);
        }
        th->selectTool(TOOL_ERASER);
        th->setCustomThickness(TOOL_ERASER, width, true);
    } else if (type == "laser") {
        const ToolType t = ToolboxModel::highlights(e) ? TOOL_LASER_POINTER_HIGHLIGHTER : TOOL_LASER_POINTER_PEN;
        th->selectTool(t);
        takeColor(t);
        // (the laser tools have the five sizes, no width of their own: the size nearest to the entry's)
        if (const double* sizes = th->getToolThickness(t)) {
            int best = 0;
            for (int s = 1; s < 5; ++s) {
                if (std::abs(sizes[s] - width) < std::abs(sizes[best] - width)) {
                    best = s;
                }
            }
            th->setSize(static_cast<ToolSize>(best));
        }
    } else if (type == "text") {
        setTextMarkdown(true);  // (the toolbox's text box is a Markdown text box, as the tool bar's)
        th->selectTool(TOOL_TEXT);
        takeColor(TOOL_TEXT);
        const QVariantMap font = e.value("font").toMap();
        const QString family = font.value("family").toString();
        if (!family.isEmpty() && family != fontFamily()) {
            setFontFamily(family);
        }
        const double size = font.value("size").toDouble();
        if (size > 0 && std::abs(size - markdownFontSize()) > 0.01) {
            setMarkdownFontSize(size);
        }
    } else {
        return false;
    }
    th->fireToolChanged();
    appliedEntryColor = AppController::color();
    toolbox->setActive(id);
    Q_EMIT toolChanged();
    return true;
}

void AppController::takeToolOfType(const QString& type) {
    const QString id = toolbox ? toolbox->recentOfType(type) : QString();
    if (!id.isEmpty() && applyToolEntry(id)) {
        return;
    }
    if (type == "text") {
        setTextMarkdown(true);
    }
    selectTool(type);
}

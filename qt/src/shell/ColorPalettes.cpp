/*
 * xournal-qt: the color palettes of the color chooser (see ColorPalettes.h).
 *
 * @license GNU GPLv2 or later
 */
#include "ColorPalettes.h"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

namespace xqt {

const PaletteRole* ColorPalette::role(const QString& key) const {
    for (const PaletteRole& r: roles) {
        if (r.key == key) {
            return &r;
        }
    }
    return nullptr;
}

ColorRef ColorRef::parse(const QString& text) {
    const int colon = text.indexOf(':');
    if (colon <= 0 || colon == text.size() - 1 || text.indexOf(':', colon + 1) >= 0) {
        return {};
    }
    return {text.left(colon).trimmed(), text.mid(colon + 1).trimmed()};
}

const ColorPalettes& ColorPalettes::builtIn() {
    static const ColorPalettes palettes = [] {
        ColorPalettes p;
        QFile file(":/xqt-palettes/palettes.json");
        QString error;
        if (!file.open(QIODevice::ReadOnly) || !p.load(file.readAll(), &error)) {
            qWarning("xournal-qt: the color palettes could not be read: %s", qPrintable(error));
        }
        return p;
    }();
    return palettes;
}

bool ColorPalettes::load(const QByteArray& json, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError) {
        return fail(parse.errorString());
    }
    const QJsonObject system = doc.object().value("paletteSystem").toObject();
    QStringList keys;
    for (const QJsonValue& v: system.value("roles").toArray()) {
        if (v.isString() && !v.toString().isEmpty() && !keys.contains(v.toString())) {
            keys << v.toString();
        }
    }
    if (keys.isEmpty()) {
        return fail("no roles");
    }
    QStringList rules;
    for (const QJsonValue& v: system.value("rules").toArray()) {
        rules << v.toString();
    }
    std::vector<ColorPalette> read;
    for (const QJsonValue& v: system.value("palettes").toArray()) {
        const QJsonObject o = v.toObject();
        ColorPalette p;
        p.id = o.value("id").toString();
        p.name = o.value("name").toString(p.id);
        p.source = o.value("source").toString();
        p.background = QColor(o.value("background").toString("#ffffff"));
        p.dark = o.value("mode").toString() == "dark";
        if (p.id.isEmpty() || p.id.contains(':')) {
            return fail(QString("a palette without an id (or with ':' in it): %1").arg(p.name));
        }
        if (std::any_of(read.begin(), read.end(), [&](const ColorPalette& q) { return q.id == p.id; })) {
            return fail("two palettes with the id " + p.id);
        }
        if (!p.background.isValid()) {
            return fail(p.id + ": the background is not a color");
        }
        const QJsonObject colors = o.value("colors").toObject();
        for (const QString& key: keys) {  // (in the spec's order, whatever the file's)
            if (!colors.contains(key)) {
                continue;
            }
            const QJsonObject c = colors.value(key).toObject();
            PaletteRole r{key, QColor(c.value("ink").toString()), QColor(c.value("highlight").toString())};
            if (!r.ink.isValid() || !r.highlight.isValid()) {
                return fail(QString("%1: %2 has no ink or no highlight color").arg(p.id, key));
            }
            p.roles.push_back(r);
        }
        if (p.roles.empty()) {
            return fail(p.id + ": no colors");
        }
        read.push_back(std::move(p));
    }
    if (read.empty()) {
        return fail("no palettes");
    }
    roles = keys;
    ruleTexts = rules;
    list = std::move(read);
    return true;
}

const ColorPalette* ColorPalettes::palette(const QString& id) const {
    for (const ColorPalette& p: list) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

QString ColorPalettes::roleName(const QString& key) {
    static const std::pair<const char*, const char*> names[] = {
            {"body", QT_TRANSLATE_NOOP("ColorPalettes", "Body")},
            {"warnings", QT_TRANSLATE_NOOP("ColorPalettes", "Warnings")},
            {"keyTerms", QT_TRANSLATE_NOOP("ColorPalettes", "Key terms")},
            {"examples", QT_TRANSLATE_NOOP("ColorPalettes", "Examples")},
            {"definitions", QT_TRANSLATE_NOOP("ColorPalettes", "Definitions")},
            {"headings", QT_TRANSLATE_NOOP("ColorPalettes", "Headings")},
            {"questions", QT_TRANSLATE_NOOP("ColorPalettes", "Questions")},
            {"ideas", QT_TRANSLATE_NOOP("ColorPalettes", "Ideas")},
    };
    for (const auto& [k, name]: names) {
        if (key == QLatin1String(k)) {
            return QCoreApplication::translate("ColorPalettes", name);
        }
    }
    return key;
}

std::optional<QColor> ColorPalettes::color(const QString& paletteId, const QString& role, Kind kind) const {
    const ColorPalette* p = palette(paletteId);
    const PaletteRole* r = p ? p->role(role) : nullptr;
    if (!r) {
        return std::nullopt;
    }
    return kind == Kind::Ink ? r->ink : r->highlight;
}

QVariantList ColorPalettes::toVariant() const {
    QVariantList out;
    for (const ColorPalette& p: list) {
        QVariantList rs;
        for (const PaletteRole& r: p.roles) {
            rs.append(QVariantMap{{"key", r.key}, {"name", roleName(r.key)}, {"ink", r.ink}, {"highlight", r.highlight}});
        }
        out.append(QVariantMap{{"id", p.id},
                               {"name", p.name},
                               {"source", p.source},
                               {"background", p.background},
                               {"dark", p.dark},
                               {"roles", rs}});
    }
    return out;
}

namespace {
double linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
}  // namespace

bool ColorPalettes::isDarkPaper(const QColor& paper) {
    if (!paper.isValid()) {
        return false;
    }
    const QColor c = paper.toRgb();
    const double l = 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
    // Black text has (l + 0.05) / 0.05 contrast, white text 1.05 / (l + 0.05): the same at l = sqrt(0.0525) - 0.05
    return l < std::sqrt(1.05 * 0.05) - 0.05;
}

double ColorPalettes::highlighterOpacity(const QColor& paper) {
    return isDarkPaper(paper) ? DARK_PAPER_OPACITY : LIGHT_PAPER_OPACITY;
}

std::vector<ColorPalettes::DarkPair> ColorPalettes::darkPairs(const QString& darkId) const {
    std::vector<DarkPair> out;
    const ColorPalette* dark = palette(darkId);
    if (!dark) {
        return out;
    }
    const auto add = [&](const QColor& light, const QColor& to, double opacity) {
        if (!light.isValid() || !to.isValid() ||
            std::any_of(out.begin(), out.end(), [&](const DarkPair& p) { return p.light.rgb() == light.rgb(); })) {
            return;
        }
        out.push_back({light, to, opacity});
    };
    for (const ColorPalette& p: list) {
        if (p.dark) {
            continue;
        }
        for (const PaletteRole& r: p.roles) {
            if (const PaletteRole* d = dark->role(r.key)) {
                add(r.ink, d->ink, 1.0);
                add(r.highlight, d->highlight, DARK_PAPER_OPACITY / UPSTREAM_HIGHLIGHTER_OPACITY);
            }
        }
    }
    return out;
}

}  // namespace xqt

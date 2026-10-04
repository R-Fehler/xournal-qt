/*
 * xournal-qt: the color palettes of the color chooser (qt/resources/palettes/palettes.json, the author's spec).
 *
 * A palette gives each *role* (body, warnings, key terms, examples, definitions, headings, questions, ideas) an ink
 * color (pen, text) and a highlight color (highlighter). Every palette uses the same role keys, so a role keeps its
 * meaning when the palette changes; a palette may leave roles out (Colorblind-safe (6)). A color picked from a palette
 * remembers its role as a ColorRef ("marker:warnings"), so it can follow when another palette is chosen.
 *
 * The highlighter's opacity follows the paper it is on: 0.5 on light paper, 0.8 on dark paper. Dark is judged from the
 * page's background color (its relative luminance), not from the palette's mode.
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <optional>
#include <vector>

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace xqt {

/// A role's two colors in a palette
struct PaletteRole {
    QString key;  ///< "body", "warnings", "keyTerms", …
    QColor ink;
    QColor highlight;
};

struct ColorPalette {
    QString id;      ///< "classic", "marker", …
    QString name;    ///< "Classic", "Marker", …
    QString source;  ///< where its colors come from (credits; empty: the author's own)
    QColor background;
    bool dark = false;  ///< meant for dark paper ("mode": "dark")
    std::vector<PaletteRole> roles;  ///< in the order of the spec's roles; only those it defines

    const PaletteRole* role(const QString& key) const;
};

/// A color picked from a palette: the palette and the role ("marker:warnings" as text)
struct ColorRef {
    QString palette;
    QString role;

    bool valid() const { return !palette.isEmpty() && !role.isEmpty(); }
    QString toString() const { return valid() ? palette + ':' + role : QString(); }
    /// "marker:warnings" (anything else: an invalid ref)
    static ColorRef parse(const QString& text);
    bool operator==(const ColorRef&) const = default;
};

class ColorPalettes {
public:
    enum class Kind { Ink, Highlight };

    /// Highlighter opacity on light and on dark paper (the spec's rule)
    static constexpr double LIGHT_PAPER_OPACITY = 0.5;
    static constexpr double DARK_PAPER_OPACITY = 0.8;

    /// The app's palettes (palettes.json, compiled in as :/xqt-palettes/palettes.json)
    static const ColorPalettes& builtIn();

    /// Reads a spec ({"paletteSystem": {"roles": [...], "palettes": [...]}}); false (and why in `error`) when it is
    /// not one: then nothing changes. Roles a palette has that the spec does not list are left out.
    bool load(const QByteArray& json, QString* error = nullptr);

    const std::vector<ColorPalette>& palettes() const { return list; }
    const ColorPalette* palette(const QString& id) const;
    /// The palette used when none is chosen (or the chosen one is gone): the first
    QString defaultId() const { return list.empty() ? QString() : list.front().id; }
    /// Every role key, in the spec's order
    const QStringList& roleKeys() const { return roles; }
    /// The spec's rules, as written there
    const QStringList& rules() const { return ruleTexts; }
    /// A role's name for people: "Body", "Warnings", "Key terms", … (the key itself for one this does not know)
    static QString roleName(const QString& key);

    /// The color of `role` in `paletteId` (none: no such palette, or it leaves the role out)
    std::optional<QColor> color(const QString& paletteId, const QString& role, Kind kind) const;
    std::optional<QColor> color(const ColorRef& ref, Kind kind) const { return color(ref.palette, ref.role, kind); }
    /// What a color picked as `ref` becomes when `paletteId` is chosen: its role's color there, if it has the role
    std::optional<QColor> follow(const ColorRef& ref, const QString& paletteId, Kind kind) const {
        return color(paletteId, ref.role, kind);
    }

    /// The palettes for QML: [{ id, name, source, background, dark, roles: [{ key, name, ink, highlight }] }]
    QVariantList toVariant() const;

    /// Paper this dark wants light ink: its relative luminance (WCAG) is below the point where white text has more
    /// contrast on it than black text (about 0.18)
    static bool isDarkPaper(const QColor& paper);
    /// The highlighter's opacity on this paper: 0.5 on light, 0.8 on dark
    static double highlighterOpacity(const QColor& paper);

private:
    QStringList roles;
    QStringList ruleTexts;
    std::vector<ColorPalette> list;
};

}  // namespace xqt

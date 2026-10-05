/*
 * xournal-qt: the toolbox's tools (qt/docs/toolbox.md): the user's own ordered tools ("my tools"), each a tool with
 * its settings, like pens taken from a sorted toolbox on a table (Drawboard). A pen in the body color, a red pen, a
 * yellow highlighter, a dashed arrow, a whiteout eraser, a text box in a font, a sticky note in a color, the laser
 * pointer, a snip (a picture of a rectangle or a lasso to the clipboard): each is one entry. Dividers group them into sections (which fold into stacks when the rail is short).
 *
 * The model is plain data: an ordered list of entries (QVariantMap for QML), the active entry, the order in which
 * entries were used (the keys P / H / E / T take the most recent one of their type). Applying an entry to the tool in
 * hand is AppController's (AppToolbox.cpp). Stored as JSON in the settings (`toolbox` in the xournalQt part), per
 * device; a change is written after a short pause (dragging a width slider writes once).
 *
 * The JSON (version 1):
 *   {"version":1,"active":"e3","recent":["e3","e1"],"entries":[
 *     {"id":"e1","type":"pen","color":"#2b2b2b","role":"body","width":1.41,"lineStyle":"plain",
 *      "fill":{"on":false,"color":"","alpha":128}},
 *     {"id":"d1","divider":true},
 *     {"id":"e4","type":"highlighter","role":"keyTerms","color":"#ffe066","width":8.5,"fill":{…}},
 *     {"id":"e6","type":"shape","variant":"arrow","base":"pen",…},
 *     {"id":"e5","type":"eraser","variant":"whiteout","width":8.5},
 *     {"id":"e7","type":"text","font":{"family":"Sans","size":12},"color":"#2b2b2b","role":"body"},
 *     {"id":"e8","type":"sticky","color":"#fff59d"},
 *     {"id":"e9","type":"laser","base":"pen","color":"#ff0000","width":2.4},
 *     {"id":"e10","type":"snip","variant":"lasso"}]}
 * `role` is a role of the color palettes (ColorPalettes.h): the entry takes that role's color in the palette chosen
 * now (ink; the highlight color for a highlighter), so it follows a palette switch; `color` is used when the role is
 * empty or the palette leaves it out. Widths are points (0.1 to 150).
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>

#include <QColor>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace xqt {

class ToolboxModel final: public QObject {
    Q_OBJECT
    /// The entries and dividers in their order: [{id, type, …} | {id, divider: true}]
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    /// Counts every change (bindings that call the invokables read it)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// The entry last taken ("": none)
    Q_PROPERTY(QString active READ active WRITE setActive NOTIFY activeChanged)
public:
    using Load = std::function<QString()>;
    using Store = std::function<void(const QString&)>;
    /// `load` gives the stored JSON ("" or broken: the default tools); `store` writes it (debounced, see flush)
    ToolboxModel(Load load, Store store, QObject* parent = nullptr);
    ~ToolboxModel() override;

    /// The kinds of entries, in the order "+" offers them
    static QStringList types();
    /// The variants of a type ("shape": line, rectangle, …; "eraser": default, whiteout, deleteStroke; "laser" and
    /// "shape" also have a base: pen, highlighter); empty: none
    static QStringList variantsOf(const QString& type);
    /// The entries of a first start: three pens (body, key terms, warnings) | two highlighters (key terms,
    /// definitions) | the eraser | a line, a text box, a sticky note | the laser pointer
    static QVariantList defaultEntries();
    /// An entry with every field of its type (missing ones from the type's defaults; widths within 0.1–150 pt;
    /// unknown fields dropped). An unknown type: an empty map.
    static QVariantMap normalized(const QVariantMap& entry);
    /// A new entry of `type` with the type's defaults
    static QVariantMap defaultOf(const QString& type);
    /// The color `entry` draws with in `paletteId`: its role's color there (the highlight color for a highlighter, or
    /// a shape or laser drawn with it), else its own `color`
    static QColor colorIn(const QVariantMap& entry, const QString& paletteId);
    /// It draws with the highlighter (a highlighter, or a shape or the laser on the highlighter)
    static bool highlights(const QVariantMap& entry);

    QVariantList entries() const { return list; }
    int revision() const { return rev; }
    QString active() const { return activeId; }
    void setActive(const QString& id);

    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    /// normalized() for QML (a draft of a new tool)
    Q_INVOKABLE QVariantMap normalize(const QVariantMap& e) const { return normalized(e); }
    /// Its index among entries and dividers (-1: none)
    Q_INVOKABLE int indexOf(const QString& id) const;
    /// Only the entries (no dividers)
    Q_INVOKABLE QVariantList tools() const;
    /// The entries in sections (split at the dividers): [[entry, …], …]
    Q_INVOKABLE QVariantList sections() const;
    /// Adds an entry (normalized; a new id) at `at` among entries and dividers (-1: at the end); its id ("" if the
    /// type is unknown)
    Q_INVOKABLE QString add(const QVariantMap& entry, int at = -1);
    /// Adds an entry after `id` (in its section)
    Q_INVOKABLE QString addAfter(const QString& id, const QVariantMap& entry);
    /// Changes fields of an entry (a nested `fill` or `font` merged); false: no such entry
    Q_INVOKABLE bool update(const QString& id, const QVariantMap& fields);
    /// Puts another tool in the entry's place (keeps the id: it stays where it is and stays active)
    Q_INVOKABLE bool replace(const QString& id, const QVariantMap& entry);
    /// A copy right after it; the copy's id
    Q_INVOKABLE QString duplicate(const QString& id);
    /// The last eraser cannot go (there must be a way to erase)
    Q_INVOKABLE bool canRemove(const QString& id) const;
    Q_INVOKABLE bool remove(const QString& id);
    /// Moves an entry (or divider) to `to` (an index among entries and dividers, before the move)
    Q_INVOKABLE bool move(const QString& id, int to);
    /// One place earlier (-1) or later (+1): past a divider it goes into the next section
    Q_INVOKABLE bool moveBy(const QString& id, int delta);
    Q_INVOKABLE bool canMoveBy(const QString& id, int delta) const;
    Q_INVOKABLE bool hasDividerAfter(const QString& id) const;
    /// A divider after the entry, or none
    Q_INVOKABLE void setDividerAfter(const QString& id, bool on);
    /// What "+" prefills for a type: the last entry of that type (a new id is given on add), else its defaults
    Q_INVOKABLE QVariantMap prefill(const QString& type) const;
    /// The entry of this type used most recently ("": none of that type); "pen" also finds a shape on the pen
    Q_INVOKABLE QString recentOfType(const QString& type) const;
    /// Of these entries, the one used most recently (none used: the first) — what a folded section shows
    Q_INVOKABLE QString recentAmong(const QStringList& ids) const;
    /// Back to the default tools
    Q_INVOKABLE void reset();
    /// The settings were written elsewhere (another window): read them again
    Q_INVOKABLE void reload();

    QString toJson() const;
    /// Reads the JSON; false (and nothing changes) when it is not a toolbox
    bool fromJson(const QString& json);
    /// Writes a pending change now
    void flush();

Q_SIGNALS:
    void changed();
    void activeChanged();

private:
    QString newId(const QString& prefix) const;
    void changedNow();
    /// No two dividers in a row, none at the start or the end, at least one eraser, a valid active entry
    void tidy();

    Load load;
    Store store;
    QVariantList list;
    QString activeId;
    QStringList recent;
    int rev = 0;
    QTimer writeTimer;
    bool pending = false;  ///< a change not written yet
};

}  // namespace xqt

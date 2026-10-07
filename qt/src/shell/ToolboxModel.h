/*
 * xournal-qt: the arrangement of the tools (qt/docs/toolbox.md): the user's own tools ("my tools"), each a tool with its
 * settings, like pens taken from a sorted toolbox on a table (Drawboard), and where everything sits on the two bars:
 * the rail (the toolbox beside the page) and the top bar. A pen in the body color, a red pen, a yellow highlighter, a
 * dashed arrow, a whiteout eraser, a text box in a font, a sticky note in a color, the laser pointer, a snip (a picture
 * of a rectangle or a lasso to the clipboard): each is one entry. The app's own tools and commands (hand, select, snip,
 * mark PDF text; open, save, record, …) are items too, by name, with one home each: the rail, the top bar or nowhere
 * ("not placed": the "+" catalog offers them). Dividers split a bar into sections; a group (made by the user: a tool
 * carried onto another) holds several items in one button that shows the member used last.
 *
 * The model is plain data: two ordered lists of items (QVariantMap for QML), the active entry, the order in which
 * entries were used (the keys P / H / E / T take the most recent one of their type). Applying an entry to the tool in
 * hand is AppController's (AppToolbox.cpp). Stored as JSON in the settings (`toolbox` in the xournalQt part), per
 * device; a change is written after a short pause (dragging a width slider writes once).
 *
 * The JSON (version 2; another version gives the first layout):
 *   {"version":2,"active":"e3","recent":["e3","e1"],
 *    "rail":[
 *     {"id":"e1","type":"pen","color":"#2b2b2b","role":"body","width":1.41,"lineStyle":"plain",
 *      "fill":{"on":false,"color":"","alpha":128}},
 *     {"id":"d1","divider":true},
 *     {"id":"g1","group":true,"last":"e5","members":[
 *       {"id":"e4","type":"highlighter","role":"keyTerms","color":"#ffe066","width":8.5,"fill":{…}},
 *       {"id":"e5","type":"eraser","variant":"whiteout","width":8.5}]},
 *     {"id":"e6","type":"shape","variant":"arrow","base":"pen",…},
 *     {"id":"e7","type":"text","font":{"family":"Sans","size":12},"color":"#2b2b2b","role":"body"},
 *     {"id":"e8","type":"sticky","color":"#fff59d"},
 *     {"id":"e9","type":"laser","base":"pen","color":"#ff0000","width":2.4},
 *     {"id":"e10","type":"snip","variant":"lasso"},
 *     {"id":"d2","divider":true},
 *     {"id":"a1","app":"hand"},{"id":"a2","app":"select"},{"id":"a3","app":"snip"},{"id":"a4","app":"pdfText"}],
 *    "top":[{"id":"a5","app":"open"},{"id":"a6","app":"save"},{"id":"d3","divider":true},…]}
 * `role` is a role of the color palettes (ColorPalettes.h): the entry takes that role's color in the palette chosen
 * now (ink; the highlight color for a highlighter), so it follows a palette switch; `color` is used when the role is
 * empty or the palette leaves it out. Widths are points (0.1 to 150). A group holds tool entries and app items (no
 * dividers, no groups); a group of one is that item again.
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
    /// The rail's items in their order: [{id, type, …} | {id, app} | {id, divider: true} | {id, group: true, members,
    /// last}]
    Q_PROPERTY(QVariantList rail READ entries NOTIFY changed)
    /// The top bar's items (the same kinds)
    Q_PROPERTY(QVariantList top READ topItems NOTIFY changed)
    /// Counts every change (bindings that call the invokables read it)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    /// The entry last taken ("": none)
    Q_PROPERTY(QString active READ active WRITE setActive NOTIFY activeChanged)
public:
    using Load = std::function<QString()>;
    using Store = std::function<void(const QString&)>;
    /// `load` gives the stored JSON ("" or broken: the first layout); `store` writes it (debounced, see flush)
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
    /// The app's own items by name (the window's buttons: tools and commands); an item of another name is dropped
    static QStringList appItemNames();
    /// The app's tools among them (in hand on the page: hand, select, …); the rest are commands (open, save, …)
    static bool isAppTool(const QString& name);
    /// The app items the rail has after the user's tools at a first start: hand, select, snip, mark PDF text
    static QStringList defaultRailApps();
    /// The top bar's first layout ("|": a divider): open, save, milestone, share, print | image, stickers, add page,
    /// write on the page | setsquare, finger draws, record | search, read, replay, present, full screen, Zen | tags,
    /// favourite, bookmark | settings
    static QStringList defaultTopLayout();
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

    QVariantList entries() const { return railList; }
    QVariantList topItems() const { return topList; }
    int revision() const { return rev; }
    QString active() const { return activeId; }
    void setActive(const QString& id);

    /// An item by its id, wherever it is (a member of a group too); empty: none
    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    /// normalized() for QML (a draft of a new tool)
    Q_INVOKABLE QVariantMap normalize(const QVariantMap& e) const { return normalized(e); }
    /// A bar's items: "rail" or "top"
    Q_INVOKABLE QVariantList items(const QString& bar) const;
    /// What an item is: "tool", "app", "divider", "group" ("": no such item)
    Q_INVOKABLE QString kindOf(const QString& id) const;
    /// The bar it is on ("rail", "top"; a member of a group: its group's), "" none
    Q_INVOKABLE QString barOf(const QString& id) const;
    /// Its index among its bar's items (a member of a group: the group's index; -1: none)
    Q_INVOKABLE int indexOf(const QString& id) const;
    /// Only the tool entries, wherever they are (the rail's first, members of groups in their place, then the top
    /// bar's)
    Q_INVOKABLE QVariantList tools() const;
    /// The rail's items in sections (split at the dividers): [[item, …], …]
    Q_INVOKABLE QVariantList sections() const;
    /// Adds an entry (normalized; a new id) at `at` among the rail's items (-1: after the user's last tool, before the
    /// app's items that end the rail); its id ("" if the type is unknown)
    Q_INVOKABLE QString add(const QVariantMap& entry, int at = -1);
    /// Adds an entry after the item `id` on its bar (after the group that holds it)
    Q_INVOKABLE QString addAfter(const QString& id, const QVariantMap& entry);
    /// Changes fields of an entry (a nested `fill` or `font` merged); false: no such entry
    Q_INVOKABLE bool update(const QString& id, const QVariantMap& fields);
    /// Puts another tool in the entry's place (keeps the id: it stays where it is and stays active)
    Q_INVOKABLE bool replace(const QString& id, const QVariantMap& entry);
    /// A copy right after it (in its group, if it is in one); the copy's id
    Q_INVOKABLE QString duplicate(const QString& id);
    /// The last eraser cannot go (there must be a way to erase)
    Q_INVOKABLE bool canRemove(const QString& id) const;
    /// Removes an item: a tool entry is gone, an app item is not placed any more (the catalog has it), a group goes
    /// with its members (the last eraser stays in its place)
    Q_INVOKABLE bool remove(const QString& id);
    /// Moves an item (or divider) to `to` on its bar (an index among the bar's items, before the move); a member of a
    /// group comes out of it
    Q_INVOKABLE bool move(const QString& id, int to);
    /// Moves an item to `to` among the items of `bar` ("rail", "top"; an index before the move): from the other bar, or
    /// out of its group
    Q_INVOKABLE bool moveTo(const QString& id, const QString& bar, int to);
    /// One place earlier (-1) or later (+1) on its bar, or in its group: past a divider it goes into the next section
    Q_INVOKABLE bool moveBy(const QString& id, int delta);
    Q_INVOKABLE bool canMoveBy(const QString& id, int delta) const;
    Q_INVOKABLE bool hasDividerAfter(const QString& id) const;
    /// A divider after the item (after its group), or none
    Q_INVOKABLE void setDividerAfter(const QString& id, bool on);

    // --- groups -------------------------------------------------------------------------------------------------------
    /// Groups the item `id` with `onto` (a tool entry, an app item or a group; a member: its group): a group in the
    /// place of `onto`, `id` its last member (a group carried onto another gives it its members). The group's id ("":
    /// not possible: the same item, a divider)
    Q_INVOKABLE QString group(const QString& id, const QString& onto);
    /// The members of a group back in its place, in their order
    Q_INVOKABLE bool ungroup(const QString& groupId);
    /// The group that holds an item ("": none)
    Q_INVOKABLE QString groupOf(const QString& id) const;
    /// A group's members in their order
    Q_INVOKABLE QVariantList members(const QString& groupId) const;
    /// The member a group shows: the one used last (none yet: the first)
    Q_INVOKABLE QString shownOf(const QString& groupId) const;
    /// An item was used: its group shows it from now on (taking a tool entry does this itself, see setActive)
    Q_INVOKABLE void use(const QString& id);

    // --- the app's items ----------------------------------------------------------------------------------------------
    /// The id of the app item `name` on either bar ("": not placed)
    Q_INVOKABLE QString idOfApp(const QString& name) const;
    /// The app items placed nowhere, in the order of appItemNames (what the catalog offers)
    Q_INVOKABLE QStringList unplaced() const;
    /// Places an app item that is not placed at `to` among the items of `bar` (-1: at the end); its id ("": unknown,
    /// or placed already)
    Q_INVOKABLE QString place(const QString& name, const QString& bar, int to = -1);

    /// What "+" prefills for a type: the last entry of that type (a new id is given on add), else its defaults
    Q_INVOKABLE QVariantMap prefill(const QString& type) const;
    /// The entry of this type used most recently ("": none of that type); "pen" also finds a shape on the pen
    Q_INVOKABLE QString recentOfType(const QString& type) const;
    /// Of these entries, the one used most recently (none used: the first)
    Q_INVOKABLE QString recentAmong(const QStringList& ids) const;
    /// Back to the first tools and the first layout of both bars
    Q_INVOKABLE void reset();
    /// Back to the first layout of both bars; the user's tools stay (on the rail, in their order, out of their groups)
    Q_INVOKABLE void resetLayout();
    /// The settings were written elsewhere (another window): read them again
    Q_INVOKABLE void reload();
    /// The whole arrangement as it is now, and back to one of those (an undo: "Grouped · Undo")
    Q_INVOKABLE QString snapshot() const { return toJson(); }
    Q_INVOKABLE bool restore(const QString& json);

    QString toJson() const;
    /// Reads the JSON (version 2, or 1 upgraded); false (and nothing changes) when it is not a toolbox
    bool fromJson(const QString& json);
    /// Writes a pending change now
    void flush();

Q_SIGNALS:
    void changed();
    void activeChanged();

private:
    /// Where an item is: its bar, its index there, its index in the group there (-1: not a member)
    struct Place {
        QString bar;
        int index = -1;
        int member = -1;
        bool valid() const { return index >= 0; }
    };
    Place locate(const QString& id) const;
    QVariantList& listOf(const QString& bar) { return bar == "top" ? topList : railList; }
    const QVariantList& listOf(const QString& bar) const { return bar == "top" ? topList : railList; }
    /// Takes an item out of where it is (a group of one left: that item again)
    QVariant takeOut(const Place& at);
    /// Every item, members of groups too
    QVariantList allItems() const;
    QString newId(const QString& prefix) const;
    void changedNow();
    /// Groups of at least two (members: tools and app items), one home per app item, no two dividers in a row and none
    /// at a bar's ends, at least one eraser, ids unique, a valid active entry
    void tidy();

    Load load;
    Store store;
    QVariantList railList;
    QVariantList topList;
    QString activeId;
    QStringList recent;
    int rev = 0;
    QTimer writeTimer;
    bool pending = false;  ///< a change not written yet
};

}  // namespace xqt

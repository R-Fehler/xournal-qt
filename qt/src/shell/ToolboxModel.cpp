/*
 * xournal-qt: the toolbox's tools (ToolboxModel.h).
 *
 * @license GNU GPLv2 or later
 */
#include "ToolboxModel.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ColorPalettes.h"

namespace xqt {

namespace {
constexpr int VERSION = 2;
constexpr double MIN_WIDTH = 0.1;
constexpr double MAX_WIDTH = 150;
/// A pause before a change is written (a slider dragged writes once)
constexpr int WRITE_DELAY_MS = 400;
constexpr int MAX_RECENT = 32;

QVariantMap fillOf(const QVariant& v) {
    const QVariantMap in = v.toMap();
    QVariantMap fill;
    fill["on"] = in.value("on", false).toBool();
    const QColor c(in.value("color").toString());
    fill["color"] = c.isValid() ? c.name() : QString();  // ("": the stroke's color)
    fill["alpha"] = std::clamp(in.value("alpha", 128).toInt(), 1, 255);
    return fill;
}

double widthOf(const QVariant& v, double fallback) {
    bool ok = false;
    const double w = v.toDouble(&ok);
    return ok && std::isfinite(w) && w > 0 ? std::clamp(w, MIN_WIDTH, MAX_WIDTH) : fallback;
}

QString colorOf(const QVariant& v, const QString& fallback) {
    const QColor c(v.toString());
    return c.isValid() ? c.name() : fallback;
}

QString oneOf(const QVariant& v, const QStringList& allowed) {
    const QString s = v.toString();
    return allowed.contains(s) ? s : allowed.value(0);
}

QString roleOf(const QVariant& v) {
    const QString role = v.toString();
    return ColorPalettes::builtIn().roleKeys().contains(role) ? role : QString();
}

const QStringList LINE_STYLES{"plain", "dash", "dashdot", "dot"};
const QStringList SHAPES{"line", "rectangle", "ellipse", "arrow", "doubleArrow", "drawCoordinateSystem",
                         "strokeRecognizer"};
const QStringList ERASERS{"default", "whiteout", "deleteStroke"};
const QStringList BASES{"pen", "highlighter"};
const QStringList SNIPS{"rect", "lasso"};

QVariantMap divider(const QString& id) { return QVariantMap{{"id", id}, {"divider", true}}; }
QVariantMap appItem(const QString& id, const QString& name) { return QVariantMap{{"id", id}, {"app", name}}; }
bool isDivider(const QVariant& v) { return v.toMap().value("divider").toBool(); }
bool isGroup(const QVariant& v) { return v.toMap().value("group").toBool(); }
bool isApp(const QVariant& v) { return v.toMap().contains("app"); }
bool isTool(const QVariant& v) { return !isDivider(v) && !isGroup(v) && !isApp(v); }
QString idOf(const QVariant& v) { return v.toMap().value("id").toString(); }
QVariantList membersOf(const QVariant& v) { return v.toMap().value("members").toList(); }
/// A bar's items from names ("|": a divider), ids from `n` on
QVariantList layoutOf(const QStringList& names, int& n) {
    QVariantList l;
    for (const QString& name: names) {
        l.append(name == "|" ? divider(QString("d%1").arg(++n)) : appItem(QString("a%1").arg(++n), name));
    }
    return l;
}
}  // namespace

ToolboxModel::ToolboxModel(Load l, Store s, QObject* parent): QObject(parent), load(std::move(l)), store(std::move(s)) {
    writeTimer.setSingleShot(true);
    writeTimer.setInterval(WRITE_DELAY_MS);
    connect(&writeTimer, &QTimer::timeout, this, &ToolboxModel::flush);
    if (!load || !fromJson(load())) {
        reset();
        rev = 0;
        pending = false;
        writeTimer.stop();
    }
}

ToolboxModel::~ToolboxModel() { flush(); }

QStringList ToolboxModel::types() {
    return {"pen", "highlighter", "shape", "eraser", "text", "sticky", "laser", "snip"};
}

QStringList ToolboxModel::variantsOf(const QString& type) {
    if (type == "shape") {
        return SHAPES;
    }
    if (type == "eraser") {
        return ERASERS;
    }
    if (type == "snip") {
        return SNIPS;
    }
    return {};
}

QVariantMap ToolboxModel::defaultOf(const QString& type) {
    QVariantMap e{{"type", type}};
    const QVariantMap noFill{{"on", false}, {"color", QString()}, {"alpha", 128}};
    if (type == "pen") {
        e = {{"type", type}, {"color", "#2b2b2b"}, {"role", "body"}, {"width", 1.41}, {"lineStyle", "plain"},
             {"fill", noFill}};
    } else if (type == "highlighter") {
        e = {{"type", type}, {"color", "#ffe066"}, {"role", "keyTerms"}, {"width", 8.5}, {"fill", noFill}};
    } else if (type == "shape") {
        e = {{"type", type}, {"variant", "line"}, {"base", "pen"}, {"color", "#2b2b2b"}, {"role", "body"},
             {"width", 1.41}, {"lineStyle", "plain"}, {"fill", noFill}};
    } else if (type == "eraser") {
        e = {{"type", type}, {"variant", "default"}, {"width", 8.5}};
    } else if (type == "text") {
        e = {{"type", type}, {"font", QVariantMap{{"family", "Sans"}, {"size", 12.0}}}, {"color", "#2b2b2b"},
             {"role", "body"}};
    } else if (type == "sticky") {
        e = {{"type", type}, {"color", "#fff59d"}};
    } else if (type == "laser") {
        e = {{"type", type}, {"base", "pen"}, {"color", "#ff0000"}, {"role", QString()}, {"width", 2.4}};
    } else if (type == "snip") {
        e = {{"type", type}, {"variant", "rect"}};  // (a picture of a rectangle or a lasso to the clipboard)
    } else {
        return {};
    }
    return e;
}

QVariantMap ToolboxModel::normalized(const QVariantMap& in) {
    const QString type = in.value("type").toString();
    const QVariantMap d = defaultOf(type);
    if (d.isEmpty()) {
        return {};
    }
    QVariantMap e;
    e["type"] = type;
    if (in.contains("id")) {
        e["id"] = in.value("id").toString();
    }
    for (auto it = d.begin(); it != d.end(); ++it) {
        const QString& key = it.key();
        const QVariant v = in.value(key, it.value());
        if (key == "type") {
            continue;
        } else if (key == "color") {
            e[key] = colorOf(v, it.value().toString());
        } else if (key == "role") {
            // (a color of one's own without a role: none, not the type's)
            e[key] = in.contains("color") && !in.contains("role") ? QString() : roleOf(v);
        } else if (key == "width") {
            e[key] = widthOf(v, it.value().toDouble());
        } else if (key == "lineStyle") {
            e[key] = oneOf(v, LINE_STYLES);
        } else if (key == "fill") {
            e[key] = fillOf(v);
        } else if (key == "variant") {
            e[key] = oneOf(v, variantsOf(type));
        } else if (key == "base") {
            e[key] = oneOf(v, BASES);
        } else if (key == "font") {
            const QVariantMap f = v.toMap();
            const QVariantMap df = it.value().toMap();
            const QString family = f.value("family").toString().trimmed();
            e[key] = QVariantMap{{"family", family.isEmpty() ? df.value("family") : family},
                                 {"size", std::clamp(widthOf(f.value("size"), 12), 4.0, 200.0)}};
        }
    }
    return e;
}

bool ToolboxModel::highlights(const QVariantMap& entry) {
    const QString type = entry.value("type").toString();
    return type == "highlighter" || ((type == "shape" || type == "laser") && entry.value("base") == "highlighter");
}

QColor ToolboxModel::colorIn(const QVariantMap& entry, const QString& paletteId) {
    const QString role = entry.value("role").toString();
    if (!role.isEmpty()) {
        if (const auto c = ColorPalettes::builtIn().color(
                    paletteId, role, highlights(entry) ? ColorPalettes::Kind::Highlight : ColorPalettes::Kind::Ink)) {
            return *c;
        }
    }
    return QColor(entry.value("color").toString());
}

QVariantList ToolboxModel::defaultEntries() {
    QVariantList l;
    int n = 0;
    auto tool = [&](const QString& type, const QVariantMap& fields = {}) {
        QVariantMap e = defaultOf(type);
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            e[it.key()] = it.value();
        }
        e["id"] = QString("e%1").arg(++n);
        l.append(normalized(e));
    };
    auto cut = [&]() { l.append(divider(QString("d%1").arg(++n))); };
    tool("pen", {{"role", "body"}, {"color", "#2b2b2b"}});
    tool("pen", {{"role", "keyTerms"}, {"color", "#d96b00"}});
    tool("pen", {{"role", "warnings"}, {"color", "#d6342c"}});
    cut();
    tool("highlighter", {{"role", "keyTerms"}, {"color", "#ffe066"}});
    tool("highlighter", {{"role", "definitions"}, {"color", "#8ce5e5"}});
    cut();
    tool("eraser");
    cut();
    tool("shape", {{"variant", "line"}});
    tool("text");
    tool("sticky");
    cut();
    tool("laser");
    return l;
}

QStringList ToolboxModel::appItemNames() {
    return {// the app's tools (the rail's at a first start, then those of the top bar)
            "hand", "select", "snip", "pdfText", "write", "geometry", "touchDrawing",
            // its commands
            "record", "open", "save", "milestone", "share", "print", "image", "sticker", "addPage", "search", "read",
            "replay", "present", "fullScreen", "zen", "tags", "favourite", "bookmark", "settings", "new"};
}

bool ToolboxModel::isAppTool(const QString& name) {
    static const QStringList TOOLS{"hand", "select", "snip", "pdfText", "write", "geometry", "touchDrawing"};
    return TOOLS.contains(name);
}

QStringList ToolboxModel::defaultRailApps() { return {"hand", "select", "snip", "pdfText"}; }

QStringList ToolboxModel::defaultTopLayout() {
    return {"open",   "save",   "milestone", "share",      "print", "|",   "image",    "sticker", "addPage",
            "write",  "|",      "geometry",  "touchDrawing", "record", "|", "search",   "read",    "replay",
            "present", "fullScreen", "zen",  "|",          "tags",  "favourite", "bookmark", "|", "settings"};
}

void ToolboxModel::setActive(const QString& id) {
    const QVariantMap e = entry(id);
    if (e.isEmpty() || !isTool(e) || e.value("type") == "sticky") {
        return;  // (a sticky note is put on the page, it is not a tool in hand)
    }
    use(id);  // (its group shows it)
    if (activeId == id && !recent.isEmpty() && recent.first() == id) {
        return;  // (taken again: nothing to write)
    }
    recent.removeAll(id);
    recent.prepend(id);
    while (recent.size() > MAX_RECENT) {
        recent.removeLast();
    }
    if (activeId != id) {
        activeId = id;
        Q_EMIT activeChanged();
    }
    pending = true;
    writeTimer.start();
}

ToolboxModel::Place ToolboxModel::locate(const QString& id) const {
    if (id.isEmpty()) {
        return {};
    }
    for (const QString& bar: {QStringLiteral("rail"), QStringLiteral("top")}) {
        const QVariantList& l = listOf(bar);
        for (int i = 0; i < l.size(); ++i) {
            if (idOf(l[i]) == id) {
                return {bar, i, -1};
            }
            if (isGroup(l[i])) {
                const QVariantList m = membersOf(l[i]);
                for (int j = 0; j < m.size(); ++j) {
                    if (idOf(m[j]) == id) {
                        return {bar, i, j};
                    }
                }
            }
        }
    }
    return {};
}

QVariantList ToolboxModel::allItems() const {
    QVariantList all;
    for (const QVariantList* l: {&railList, &topList}) {
        for (const QVariant& v: *l) {
            all.append(v);
            if (isGroup(v)) {
                all.append(membersOf(v));
            }
        }
    }
    return all;
}

QVariantMap ToolboxModel::entry(const QString& id) const {
    const Place at = locate(id);
    if (!at.valid()) {
        return {};
    }
    const QVariant& v = listOf(at.bar)[at.index];
    return at.member < 0 ? v.toMap() : membersOf(v)[at.member].toMap();
}

QVariantList ToolboxModel::items(const QString& bar) const { return listOf(bar); }

QString ToolboxModel::kindOf(const QString& id) const {
    const QVariantMap e = entry(id);
    if (e.isEmpty()) {
        return {};
    }
    return isDivider(e) ? "divider" : isGroup(e) ? "group" : isApp(e) ? "app" : "tool";
}

QString ToolboxModel::barOf(const QString& id) const { return locate(id).bar; }

int ToolboxModel::indexOf(const QString& id) const { return locate(id).index; }

QVariantList ToolboxModel::tools() const {
    QVariantList l;
    for (const QVariant& v: allItems()) {
        if (isTool(v)) {
            l.append(v);
        }
    }
    return l;
}

QVariantList ToolboxModel::sections() const {
    QVariantList all;
    QVariantList section;
    for (const QVariant& v: railList) {
        if (isDivider(v)) {
            if (!section.isEmpty()) {
                all.append(QVariant(section));
            }
            section.clear();
        } else {
            section.append(v);
        }
    }
    if (!section.isEmpty()) {
        all.append(QVariant(section));
    }
    return all;
}

QString ToolboxModel::newId(const QString& prefix) const {
    int n = 0;
    for (const QVariant& v: allItems()) {
        bool ok = false;
        const int k = idOf(v).mid(1).toInt(&ok);
        if (ok) {
            n = std::max(n, k);
        }
    }
    return prefix + QString::number(n + 1);
}

QString ToolboxModel::add(const QVariantMap& in, int at) {
    QVariantMap e = normalized(in);
    if (e.isEmpty()) {
        return {};
    }
    const QString id = newId("e");
    e["id"] = id;
    if (at < 0 || at > railList.size()) {
        // (after the user's last tool: before the app's items that end the rail)
        at = railList.size();
        for (int i = railList.size() - 1; i >= 0; --i) {
            if (isTool(railList[i]) || isGroup(railList[i])) {
                at = i + 1;
                break;
            }
        }
    }
    railList.insert(at, e);
    changedNow();
    return id;
}

QString ToolboxModel::addAfter(const QString& id, const QVariantMap& in) {
    const Place at = locate(id);
    if (!at.valid() || at.bar == "rail") {
        return add(in, at.valid() ? at.index + 1 : -1);
    }
    QVariantMap e = normalized(in);
    if (e.isEmpty()) {
        return {};
    }
    const QString fresh = newId("e");
    e["id"] = fresh;
    topList.insert(at.index + 1, e);
    changedNow();
    return fresh;
}

namespace {
/// Calls `change` on the item at `at` (in its group, if it is a member) and puts it back
template <typename F>
void changeAt(QVariantList& l, int index, int member, F change) {
    if (member < 0) {
        QVariantMap e = l[index].toMap();
        change(e);
        l[index] = e;
        return;
    }
    QVariantMap g = l[index].toMap();
    QVariantList m = g.value("members").toList();
    QVariantMap e = m[member].toMap();
    change(e);
    m[member] = e;
    g["members"] = m;
    l[index] = g;
}
}  // namespace

bool ToolboxModel::update(const QString& id, const QVariantMap& fields) {
    const Place at = locate(id);
    QVariantMap e = entry(id);
    if (!at.valid() || !isTool(e)) {
        return false;
    }
    const QVariantMap before = e;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (it.key() == "id" || it.key() == "type") {
            continue;
        }
        if ((it.key() == "fill" || it.key() == "font") && it.value().canConvert<QVariantMap>()) {
            QVariantMap merged = e.value(it.key()).toMap();
            const QVariantMap part = it.value().toMap();
            for (auto p = part.begin(); p != part.end(); ++p) {
                merged[p.key()] = p.value();
            }
            e[it.key()] = merged;
        } else {
            e[it.key()] = it.value();
        }
    }
    // A color of one's own: no role (unless a role comes with it)
    if (fields.contains("color") && !fields.contains("role")) {
        e["role"] = QString();
    }
    e = normalized(e);
    if (e == before) {
        return true;
    }
    changeAt(listOf(at.bar), at.index, at.member, [&e](QVariantMap& m) { m = e; });
    changedNow();
    return true;
}

bool ToolboxModel::replace(const QString& id, const QVariantMap& in) {
    const Place at = locate(id);
    QVariantMap e = normalized(in);
    const QVariantMap old = entry(id);
    if (!at.valid() || !isTool(old) || e.isEmpty()) {
        return false;
    }
    if (old.value("type") == "eraser" && e.value("type") != "eraser" && !canRemove(id)) {
        return false;  // (the last eraser stays)
    }
    e["id"] = id;
    changeAt(listOf(at.bar), at.index, at.member, [&e](QVariantMap& m) { m = e; });
    if (e.value("type") == "sticky" && activeId == id) {
        activeId.clear();
        Q_EMIT activeChanged();
    }
    changedNow();
    return true;
}

QString ToolboxModel::duplicate(const QString& id) {
    const Place at = locate(id);
    QVariantMap e = entry(id);
    if (!at.valid() || !isTool(e)) {
        return {};
    }
    const QString copy = newId("e");
    e["id"] = copy;
    QVariantList& l = listOf(at.bar);
    if (at.member < 0) {
        l.insert(at.index + 1, e);
    } else {
        QVariantMap g = l[at.index].toMap();
        QVariantList m = g.value("members").toList();
        m.insert(at.member + 1, e);
        g["members"] = m;
        l[at.index] = g;
    }
    changedNow();
    return copy;
}

bool ToolboxModel::canRemove(const QString& id) const {
    const QVariantMap e = entry(id);
    if (e.isEmpty()) {
        return false;
    }
    if (!isTool(e) || e.value("type") != "eraser") {
        return true;  // (a group holding the last eraser leaves it in its place)
    }
    int erasers = 0;
    for (const QVariant& v: allItems()) {
        erasers += isTool(v) && v.toMap().value("type") == "eraser" ? 1 : 0;
    }
    return erasers > 1;
}

QVariant ToolboxModel::takeOut(const Place& at) {
    QVariantList& l = listOf(at.bar);
    if (at.member < 0) {
        return l.takeAt(at.index);
    }
    QVariantMap g = l[at.index].toMap();
    QVariantList m = g.value("members").toList();
    const QVariant v = m.takeAt(at.member);
    if (m.size() == 1) {
        l[at.index] = m.first();  // (a group of one: that item again)
    } else {
        g["members"] = m;
        if (g.value("last") == idOf(v)) {
            g["last"] = idOf(m.first());
        }
        l[at.index] = g;
    }
    return v;
}

bool ToolboxModel::remove(const QString& id) {
    if (!canRemove(id)) {
        return false;
    }
    const Place at = locate(id);
    const QVariant v = takeOut(at);
    if (isGroup(v)) {
        // Its members go (an app item into the catalog); the last eraser stays in the group's place
        const QVariantList left = allItems();
        bool erases = std::any_of(left.begin(), left.end(),
                                  [](const QVariant& o) { return isTool(o) && o.toMap().value("type") == "eraser"; });
        for (const QVariant& m: membersOf(v)) {
            if (!erases && isTool(m) && m.toMap().value("type") == "eraser") {
                listOf(at.bar).insert(at.index, m);
                erases = true;
            } else {
                recent.removeAll(idOf(m));
            }
        }
    }
    recent.removeAll(id);
    changedNow();
    return true;
}

bool ToolboxModel::move(const QString& id, int to) {
    const Place at = locate(id);
    return at.valid() && moveTo(id, at.bar, to);
}

bool ToolboxModel::moveTo(const QString& id, const QString& bar, int to) {
    const Place from = locate(id);
    if (!from.valid() || (bar != "rail" && bar != "top")) {
        return false;
    }
    QVariantList& dst = listOf(bar);
    to = std::clamp(to, 0, static_cast<int>(dst.size()));
    if (from.member < 0 && from.bar == bar && (to == from.index || to == from.index + 1)) {
        return false;  // (its own place)
    }
    // A hole where it goes, then it is taken out (a group of one left is that item again: the hole stays put)
    const QVariantMap hole{{"hole", true}};
    dst.insert(to, hole);
    const QVariant v = takeOut(locate(id));
    for (int i = 0; i < dst.size(); ++i) {
        if (dst[i].toMap().value("hole").toBool()) {
            dst[i] = v;
            break;
        }
    }
    changedNow();
    return true;
}

bool ToolboxModel::canMoveBy(const QString& id, int delta) const {
    const Place at = locate(id);
    if (!at.valid() || delta == 0) {
        return false;
    }
    const int size = at.member < 0 ? listOf(at.bar).size() : membersOf(listOf(at.bar)[at.index]).size();
    const int i = at.member < 0 ? at.index : at.member;
    const int j = i + (delta < 0 ? -1 : 1);
    return j >= 0 && j < size;
}

bool ToolboxModel::moveBy(const QString& id, int delta) {
    if (!canMoveBy(id, delta)) {
        return false;
    }
    const Place at = locate(id);
    QVariantList& l = listOf(at.bar);
    const int step = delta < 0 ? -1 : 1;
    if (at.member < 0) {
        l.swapItemsAt(at.index, at.index + step);  // (past a divider: into the next section)
    } else {
        QVariantMap g = l[at.index].toMap();
        QVariantList m = g.value("members").toList();
        m.swapItemsAt(at.member, at.member + step);
        g["members"] = m;
        l[at.index] = g;
    }
    changedNow();
    return true;
}

bool ToolboxModel::hasDividerAfter(const QString& id) const {
    const Place at = locate(id);
    const QVariantList& l = listOf(at.bar);
    return at.valid() && at.index + 1 < l.size() && isDivider(l[at.index + 1]);
}

void ToolboxModel::setDividerAfter(const QString& id, bool on) {
    const Place at = locate(id);
    if (!at.valid() || hasDividerAfter(id) == on) {
        return;
    }
    QVariantList& l = listOf(at.bar);
    if (on) {
        if (at.index + 1 >= l.size()) {
            return;  // (nothing after it to divide from)
        }
        l.insert(at.index + 1, divider(newId("d")));
    } else {
        l.removeAt(at.index + 1);
    }
    changedNow();
}

// --- groups ------------------------------------------------------------------------------------------------------------

QString ToolboxModel::group(const QString& id, const QString& onto) {
    const Place a = locate(id);
    Place b = locate(onto);
    if (!a.valid() || !b.valid() || id == onto) {
        return {};
    }
    const QVariant carried = entry(id);
    if (isDivider(carried) || isDivider(entry(onto))) {
        return {};
    }
    if (a.bar == b.bar && a.index == b.index && a.member >= 0 && b.member >= 0) {
        return {};  // (two members of one group)
    }
    if (a.member < 0 && isGroup(carried) && a.bar == b.bar && a.index == b.index) {
        return {};  // (a group onto one of its own members)
    }
    const QString fresh = newId("g");
    // The target: the group that holds `onto`, or `onto` (made a group of one, for now)
    const QString targetId = b.member >= 0 ? idOf(listOf(b.bar)[b.index]) : onto;
    const QVariant v = takeOut(a);
    b = locate(targetId);
    if (!b.valid()) {
        return {};  // (cannot happen: the target is not the carried one)
    }
    QVariantList& l = listOf(b.bar);
    QVariantMap g;
    QVariantList members;
    if (isGroup(l[b.index])) {
        g = l[b.index].toMap();
        members = g.value("members").toList();
    } else {
        members.append(l[b.index]);
        g = QVariantMap{{"id", fresh}, {"group", true}, {"last", targetId}};
    }
    // (a group carried onto another gives it its members)
    const QVariantList adding = isGroup(v) ? membersOf(v) : QVariantList{v};
    members.append(adding);
    g["members"] = members;
    g["last"] = isGroup(v) ? v.toMap().value("last") : QVariant(idOf(v));
    l[b.index] = g;
    changedNow();
    return g.value("id").toString();
}

bool ToolboxModel::ungroup(const QString& groupId) {
    const Place at = locate(groupId);
    if (!at.valid() || at.member >= 0 || !isGroup(listOf(at.bar)[at.index])) {
        return false;
    }
    QVariantList& l = listOf(at.bar);
    const QVariantList m = membersOf(l.takeAt(at.index));
    for (int i = m.size() - 1; i >= 0; --i) {
        l.insert(at.index, m[i]);
    }
    changedNow();
    return true;
}

QString ToolboxModel::groupOf(const QString& id) const {
    const Place at = locate(id);
    return at.valid() && at.member >= 0 ? idOf(listOf(at.bar)[at.index]) : QString();
}

QVariantList ToolboxModel::members(const QString& groupId) const {
    const QVariantMap g = entry(groupId);
    return isGroup(g) ? g.value("members").toList() : QVariantList();
}

QString ToolboxModel::shownOf(const QString& groupId) const {
    const QVariantMap g = entry(groupId);
    if (!isGroup(g)) {
        return {};
    }
    const QString last = g.value("last").toString();
    const QVariantList m = g.value("members").toList();
    for (const QVariant& v: m) {
        if (idOf(v) == last) {
            return last;
        }
    }
    return m.isEmpty() ? QString() : idOf(m.first());
}

void ToolboxModel::use(const QString& id) {
    const Place at = locate(id);
    if (!at.valid() || at.member < 0) {
        return;
    }
    QVariantList& l = listOf(at.bar);
    QVariantMap g = l[at.index].toMap();
    if (g.value("last") == id) {
        return;
    }
    g["last"] = id;
    l[at.index] = g;
    ++rev;
    Q_EMIT changed();
    pending = true;
    writeTimer.start();
}

// --- the app's items ---------------------------------------------------------------------------------------------------

QString ToolboxModel::idOfApp(const QString& name) const {
    for (const QVariant& v: allItems()) {
        if (isApp(v) && v.toMap().value("app") == name) {
            return idOf(v);
        }
    }
    return {};
}

QStringList ToolboxModel::unplaced() const {
    QStringList out;
    for (const QString& name: appItemNames() + pluginItems) {
        if (idOfApp(name).isEmpty()) {
            out << name;
        }
    }
    return out;
}

void ToolboxModel::setPluginItems(const QStringList& names) {
    if (pluginItems == names) {
        return;
    }
    pluginItems = names;
    ++rev;
    Q_EMIT changed();
}

QString ToolboxModel::place(const QString& name, const QString& bar, int to) {
    if ((!appItemNames().contains(name) && !pluginItems.contains(name)) || !idOfApp(name).isEmpty() ||
        (bar != "rail" && bar != "top")) {
        return {};
    }
    QVariantList& l = listOf(bar);
    const QString id = newId("a");
    l.insert(to < 0 || to > l.size() ? l.size() : to, appItem(id, name));
    changedNow();
    return id;
}

QVariantMap ToolboxModel::prefill(const QString& type) const {
    for (const QString& id: recent) {
        const QVariantMap e = entry(id);
        if (e.value("type") == type) {
            QVariantMap copy = e;
            copy.remove("id");
            return copy;
        }
    }
    const QVariantList all = tools();
    for (int i = all.size() - 1; i >= 0; --i) {
        const QVariantMap e = all[i].toMap();
        if (e.value("type") == type) {
            QVariantMap copy = e;
            copy.remove("id");
            return copy;
        }
    }
    return defaultOf(type);
}

QString ToolboxModel::recentOfType(const QString& type) const {
    auto matches = [&type](const QVariantMap& e) { return e.value("type") == type; };
    for (const QString& id: recent) {
        if (matches(entry(id))) {
            return id;
        }
    }
    for (const QVariant& v: tools()) {
        if (matches(v.toMap())) {
            return idOf(v);
        }
    }
    return {};
}

void ToolboxModel::reset() {
    railList = defaultEntries();
    topList.clear();
    int n = 0;
    for (const QVariant& v: railList) {
        n = std::max(n, idOf(v).mid(1).toInt());
    }
    railList.append(divider(QString("d%1").arg(++n)));
    railList.append(layoutOf(defaultRailApps(), n));
    topList = layoutOf(defaultTopLayout(), n);
    recent.clear();
    activeId.clear();
    tidy();
    changedNow();
    Q_EMIT activeChanged();
}

void ToolboxModel::resetLayout() {
    // The user's tools in their order (the rail's dividers between them kept), out of their groups; then the app tools
    QVariantList mine;
    for (const QVariantList* l: {&railList, &topList}) {
        for (const QVariant& v: *l) {
            if (isDivider(v)) {
                if (l == &railList) {
                    mine.append(v);
                }
            } else if (isGroup(v)) {
                for (const QVariant& m: membersOf(v)) {
                    if (isTool(m)) {
                        mine.append(m);
                    }
                }
            } else if (isTool(v)) {
                mine.append(v);
            }
        }
    }
    railList = mine;
    topList.clear();
    tidy();  // (the dividers that divided only app items go)
    int n = 0;
    for (const QVariant& v: allItems()) {
        n = std::max(n, idOf(v).mid(1).toInt());
    }
    railList.append(divider(QString("d%1").arg(++n)));
    railList.append(layoutOf(defaultRailApps(), n));
    topList = layoutOf(defaultTopLayout(), n);
    changedNow();
}

void ToolboxModel::reload() {
    if (load && fromJson(load())) {
        ++rev;
        Q_EMIT changed();
        Q_EMIT activeChanged();
    }
}

bool ToolboxModel::restore(const QString& json) {
    if (!fromJson(json)) {
        return false;
    }
    changedNow();
    Q_EMIT activeChanged();
    return true;
}

void ToolboxModel::tidy() {
    QStringList apps;  // (one home per app item: the first place it has)
    const QStringList known = appItemNames();
    auto keepItem = [&](const QVariant& v) {
        if (!isApp(v)) {
            return true;
        }
        const QString name = v.toMap().value("app").toString();
        // (a plugin's command stays placed while its plugin is off or gone: the bars skip what has no button)
        if ((!known.contains(name) && !name.startsWith(QLatin1String("plugin:"))) || apps.contains(name)) {
            return false;
        }
        apps << name;
        return true;
    };
    for (QVariantList* bar: {&railList, &topList}) {
        QVariantList l;
        for (const QVariant& v: *bar) {
            if (isGroup(v)) {
                // Members: tools and app items (a group in a group gives its members), at least two
                QVariantList m;
                for (const QVariant& x: membersOf(v)) {
                    for (const QVariant& y: isGroup(x) ? membersOf(x) : QVariantList{x}) {
                        if (!isDivider(y) && !isGroup(y) && !y.toMap().value("hole").toBool() && keepItem(y)) {
                            m.append(y);
                        }
                    }
                }
                if (m.size() == 1) {
                    l.append(m.first());
                } else if (m.size() > 1) {
                    QVariantMap g = v.toMap();
                    g["members"] = m;
                    l.append(g);
                }
                continue;
            }
            // Dividers: none first or last, never two in a row
            if (isDivider(v) && (l.isEmpty() || isDivider(l.last()))) {
                continue;
            }
            if (v.toMap().value("hole").toBool() || !keepItem(v)) {
                continue;
            }
            l.append(v);
        }
        while (!l.isEmpty() && isDivider(l.last())) {
            l.removeLast();
        }
        *bar = l;
    }
    // A way to erase: after the last of the user's tools on the rail
    const QVariantList all = allItems();
    if (std::none_of(all.begin(), all.end(),
                     [](const QVariant& v) { return isTool(v) && v.toMap().value("type") == "eraser"; })) {
        QVariantMap e = defaultOf("eraser");
        e["id"] = newId("e");
        int at = 0;
        for (int i = 0; i < railList.size(); ++i) {
            if (isTool(railList[i]) || isGroup(railList[i])) {
                at = i + 1;
            }
        }
        railList.insert(at, e);
    }
    // Ids: unique and present (members of groups too); a group's member shown is one of its members
    QStringList seen;
    auto fresh = [&](QVariant& v) {
        QVariantMap e = v.toMap();
        const QString id = e.value("id").toString();
        if (id.isEmpty() || seen.contains(id)) {
            e["id"] = newId(isDivider(v) ? "d" : isGroup(v) ? "g" : isApp(v) ? "a" : "e");
            // (newId looks at the lists as they are: the new id must be in them before the next one is made)
            v = e;
        }
        seen << e.value("id").toString();
    };
    for (QVariantList* bar: {&railList, &topList}) {
        for (int i = 0; i < bar->size(); ++i) {
            QVariant v = (*bar)[i];
            fresh(v);
            (*bar)[i] = v;
            if (isGroup(v)) {
                QVariantMap g = v.toMap();
                QVariantList m = g.value("members").toList();
                QStringList ids;
                for (int j = 0; j < m.size(); ++j) {
                    fresh(m[j]);
                    g["members"] = m;
                    (*bar)[i] = g;
                    ids << idOf(m[j]);
                }
                if (!ids.contains(g.value("last").toString())) {
                    g["last"] = ids.value(0);
                }
                (*bar)[i] = g;
            }
        }
    }
    recent.erase(std::remove_if(recent.begin(), recent.end(),
                                [this](const QString& id) { return !isTool(entry(id)) || entry(id).isEmpty(); }),
                 recent.end());
    if (!activeId.isEmpty() && (!isTool(entry(activeId)) || entry(activeId).isEmpty() ||
                                entry(activeId).value("type") == "sticky")) {
        activeId.clear();
    }
    if (activeId.isEmpty()) {
        for (const QVariant& v: tools()) {
            if (v.toMap().value("type") != "sticky") {
                activeId = idOf(v);
                break;
            }
        }
    }
}

void ToolboxModel::changedNow() {
    tidy();
    ++rev;
    Q_EMIT changed();
    pending = true;
    writeTimer.start();
}

QString ToolboxModel::toJson() const {
    QJsonObject root;
    root["version"] = VERSION;
    root["active"] = activeId;
    root["recent"] = QJsonArray::fromStringList(recent);
    root["rail"] = QJsonArray::fromVariantList(railList);
    root["top"] = QJsonArray::fromVariantList(topList);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

namespace {
/// An item read from the JSON (a tool normalized, an app item, a divider, a group with its members); null: dropped
QVariant itemFrom(const QVariantMap& m) {
    if (m.value("divider").toBool()) {
        return divider(m.value("id").toString());
    }
    if (m.value("group").toBool()) {
        QVariantList members;
        for (const QVariant& x: m.value("members").toList()) {
            const QVariant y = itemFrom(x.toMap());
            if (y.isValid() && !isDivider(y)) {
                members.append(y);
            }
        }
        return QVariantMap{{"id", m.value("id").toString()}, {"group", true}, {"members", members},
                           {"last", m.value("last").toString()}};
    }
    if (m.contains("app")) {
        return appItem(m.value("id").toString(), m.value("app").toString());
    }
    const QVariantMap e = ToolboxModel::normalized(m);
    return e.isEmpty() ? QVariant() : QVariant(e);
}
}  // namespace

bool ToolboxModel::fromJson(const QString& json) {
    if (json.trimmed().isEmpty()) {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value("version").toInt() != VERSION || !root.value("rail").isArray()) {
        return false;
    }
    auto read = [](const QJsonArray& a) {
        QVariantList l;
        for (const QJsonValue& v: a) {
            if (const QVariant item = itemFrom(v.toObject().toVariantMap()); item.isValid()) {
                l.append(item);
            }
        }
        return l;
    };
    const QVariantList rail = read(root.value("rail").toArray());
    const QVariantList top = read(root.value("top").toArray());
    auto hasTool = [](const QVariantList& l) {
        return std::any_of(l.begin(), l.end(), [](const QVariant& v) {
            if (isGroup(v)) {
                const QVariantList m = membersOf(v);
                return std::any_of(m.begin(), m.end(), isTool);
            }
            return isTool(v);
        });
    };
    if (!hasTool(rail) && !hasTool(top)) {
        return false;  // (no tools at all: the first layout instead)
    }
    railList = rail;
    topList = top;
    recent.clear();
    for (const QJsonValue& v: root.value("recent").toArray()) {
        recent << v.toString();
    }
    activeId = root.value("active").toString();
    tidy();
    return true;
}

void ToolboxModel::flush() {
    if (!pending) {
        return;
    }
    pending = false;
    writeTimer.stop();
    if (store) {
        store(toJson());
    }
}

}  // namespace xqt

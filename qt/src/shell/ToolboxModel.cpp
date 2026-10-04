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
constexpr int VERSION = 1;
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

QVariantMap divider(const QString& id) { return QVariantMap{{"id", id}, {"divider", true}}; }
bool isDivider(const QVariant& v) { return v.toMap().value("divider").toBool(); }
QString idOf(const QVariant& v) { return v.toMap().value("id").toString(); }
}  // namespace

ToolboxModel::ToolboxModel(Load l, Store s, QObject* parent): QObject(parent), load(std::move(l)), store(std::move(s)) {
    writeTimer.setSingleShot(true);
    writeTimer.setInterval(WRITE_DELAY_MS);
    connect(&writeTimer, &QTimer::timeout, this, &ToolboxModel::flush);
    if (!load || !fromJson(load())) {
        list = defaultEntries();
        tidy();
    }
}

ToolboxModel::~ToolboxModel() { flush(); }

QStringList ToolboxModel::types() {
    return {"pen", "highlighter", "shape", "eraser", "text", "sticky", "laser"};
}

QStringList ToolboxModel::variantsOf(const QString& type) {
    if (type == "shape") {
        return SHAPES;
    }
    if (type == "eraser") {
        return ERASERS;
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

void ToolboxModel::setActive(const QString& id) {
    const QVariantMap e = entry(id);
    if (e.isEmpty() || e.value("type") == "sticky") {
        return;  // (a sticky note is put on the page, it is not a tool in hand)
    }
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

QVariantMap ToolboxModel::entry(const QString& id) const {
    const int i = indexOf(id);
    return i >= 0 ? list[i].toMap() : QVariantMap();
}

int ToolboxModel::indexOf(const QString& id) const {
    if (id.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < list.size(); ++i) {
        if (idOf(list[i]) == id) {
            return i;
        }
    }
    return -1;
}

QVariantList ToolboxModel::tools() const {
    QVariantList l;
    for (const QVariant& v: list) {
        if (!isDivider(v)) {
            l.append(v);
        }
    }
    return l;
}

QVariantList ToolboxModel::sections() const {
    QVariantList all;
    QVariantList section;
    for (const QVariant& v: list) {
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
    for (const QVariant& v: list) {
        const QString id = idOf(v);
        bool ok = false;
        const int k = id.mid(1).toInt(&ok);
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
    list.insert(at < 0 || at > list.size() ? list.size() : at, e);
    changedNow();
    return id;
}

QString ToolboxModel::addAfter(const QString& id, const QVariantMap& e) {
    const int i = indexOf(id);
    return add(e, i < 0 ? -1 : i + 1);
}

bool ToolboxModel::update(const QString& id, const QVariantMap& fields) {
    const int i = indexOf(id);
    if (i < 0 || isDivider(list[i])) {
        return false;
    }
    QVariantMap e = list[i].toMap();
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
    if (e == list[i].toMap()) {
        return true;
    }
    list[i] = e;
    changedNow();
    return true;
}

bool ToolboxModel::replace(const QString& id, const QVariantMap& in) {
    const int i = indexOf(id);
    QVariantMap e = normalized(in);
    if (i < 0 || isDivider(list[i]) || e.isEmpty()) {
        return false;
    }
    const QString oldType = list[i].toMap().value("type").toString();
    if (oldType == "eraser" && e.value("type") != "eraser" && !canRemove(id)) {
        return false;  // (the last eraser stays)
    }
    e["id"] = id;
    list[i] = e;
    if (e.value("type") == "sticky" && activeId == id) {
        activeId.clear();
        Q_EMIT activeChanged();
    }
    changedNow();
    return true;
}

QString ToolboxModel::duplicate(const QString& id) {
    const QVariantMap e = entry(id);
    if (e.isEmpty() || isDivider(e)) {
        return {};
    }
    return add(e, indexOf(id) + 1);
}

bool ToolboxModel::canRemove(const QString& id) const {
    const QVariantMap e = entry(id);
    if (e.isEmpty()) {
        return false;
    }
    if (e.value("type") != "eraser") {
        return true;
    }
    int erasers = 0;
    for (const QVariant& v: list) {
        erasers += v.toMap().value("type") == "eraser" ? 1 : 0;
    }
    return erasers > 1;
}

bool ToolboxModel::remove(const QString& id) {
    if (!canRemove(id)) {
        return false;
    }
    list.removeAt(indexOf(id));
    recent.removeAll(id);
    changedNow();
    return true;
}

bool ToolboxModel::move(const QString& id, int to) {
    const int from = indexOf(id);
    if (from < 0) {
        return false;
    }
    to = std::clamp(to, 0, static_cast<int>(list.size()));
    // `to` counts the places before the move: past itself, one less once it is taken out
    const int target = to > from ? to - 1 : to;
    if (target == from) {
        return false;
    }
    const QVariant v = list.takeAt(from);
    list.insert(std::clamp(target, 0, static_cast<int>(list.size())), v);
    changedNow();
    return true;
}

bool ToolboxModel::canMoveBy(const QString& id, int delta) const {
    const int i = indexOf(id);
    const int j = i + (delta < 0 ? -1 : 1);
    return i >= 0 && delta != 0 && j >= 0 && j < list.size();
}

bool ToolboxModel::moveBy(const QString& id, int delta) {
    if (!canMoveBy(id, delta)) {
        return false;
    }
    const int i = indexOf(id);
    list.swapItemsAt(i, i + (delta < 0 ? -1 : 1));  // (past a divider: into the next section)
    changedNow();
    return true;
}

bool ToolboxModel::hasDividerAfter(const QString& id) const {
    const int i = indexOf(id);
    return i >= 0 && i + 1 < list.size() && isDivider(list[i + 1]);
}

void ToolboxModel::setDividerAfter(const QString& id, bool on) {
    const int i = indexOf(id);
    if (i < 0 || hasDividerAfter(id) == on) {
        return;
    }
    if (on) {
        if (i + 1 >= list.size()) {
            return;  // (nothing after it to divide from)
        }
        list.insert(i + 1, divider(newId("d")));
    } else {
        list.removeAt(i + 1);
    }
    changedNow();
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
    for (int i = list.size() - 1; i >= 0; --i) {
        const QVariantMap e = list[i].toMap();
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
    for (const QVariant& v: list) {
        if (matches(v.toMap())) {
            return idOf(v);
        }
    }
    return {};
}

QString ToolboxModel::recentAmong(const QStringList& ids) const {
    if (ids.contains(activeId)) {
        return activeId;
    }
    for (const QString& id: recent) {
        if (ids.contains(id)) {
            return id;
        }
    }
    return ids.value(0);
}

void ToolboxModel::reset() {
    list = defaultEntries();
    recent.clear();
    activeId.clear();
    tidy();
    changedNow();
    Q_EMIT activeChanged();
}

void ToolboxModel::reload() {
    if (load && fromJson(load())) {
        ++rev;
        Q_EMIT changed();
        Q_EMIT activeChanged();
    }
}

void ToolboxModel::tidy() {
    // Dividers: none first or last, never two in a row
    QVariantList l;
    for (const QVariant& v: list) {
        if (isDivider(v) && (l.isEmpty() || isDivider(l.last()))) {
            continue;
        }
        l.append(v);
    }
    while (!l.isEmpty() && isDivider(l.last())) {
        l.removeLast();
    }
    list = l;
    // A way to erase
    if (std::none_of(list.begin(), list.end(), [](const QVariant& v) { return v.toMap().value("type") == "eraser"; })) {
        QVariantMap e = defaultOf("eraser");
        e["id"] = newId("e");
        list.append(e);
    }
    // Ids: unique and present
    QStringList seen;
    for (QVariant& v: list) {
        QVariantMap e = v.toMap();
        if (e.value("id").toString().isEmpty() || seen.contains(e.value("id").toString())) {
            e["id"] = newId(isDivider(v) ? "d" : "e");
            v = e;
        }
        seen << e.value("id").toString();
    }
    recent.erase(std::remove_if(recent.begin(), recent.end(), [this](const QString& id) { return indexOf(id) < 0; }),
                 recent.end());
    if (!activeId.isEmpty() && (indexOf(activeId) < 0 || entry(activeId).value("type") == "sticky")) {
        activeId.clear();
    }
    if (activeId.isEmpty()) {
        for (const QVariant& v: list) {
            const QString type = v.toMap().value("type").toString();
            if (!isDivider(v) && type != "sticky") {
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
    root["entries"] = QJsonArray::fromVariantList(list);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

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
    if (root.value("version").toInt() != VERSION || !root.value("entries").isArray()) {
        return false;
    }
    QVariantList l;
    for (const QJsonValue& v: root.value("entries").toArray()) {
        const QVariantMap m = v.toObject().toVariantMap();
        if (m.value("divider").toBool()) {
            l.append(divider(m.value("id").toString()));
        } else if (QVariantMap e = normalized(m); !e.isEmpty()) {
            l.append(e);
        }
    }
    if (std::none_of(l.begin(), l.end(), [](const QVariant& v) { return !isDivider(v); })) {
        return false;  // (no tools at all: the defaults instead)
    }
    list = l;
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

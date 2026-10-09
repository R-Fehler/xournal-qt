#include "Operations.h"

#include <algorithm>
#include <cmath>
#include <shared_mutex>

#include <QCoreApplication>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/XojPage.h"
#include "session/DocumentSession.h"
#include "session/SequenceUndoAction.h"

#include "DocumentOps.h"

namespace xqt::ops {

bool Authority::ask(const Principal&, const QString&, const QString&) { return false; }

namespace {
struct ClassInfo {
    const char* name;
    QStringList patterns;
    const char* asked;  ///< how the user is asked (empty: never asked)
};
const std::vector<ClassInfo>& classes() {
    static const std::vector<ClassInfo> all = {
            {"read",
             {"document.read", "page.read", "layer.list", "element.list", "selection.read", "selection.clear", "tool.read",
              "view.read"},
             ""},
            {"view", {"view.*"}, ""},
            {"ui", {"ui.*"}, ""},
            {"edit",
             {"element.*", "stroke.*", "text.*", "markdown.*", "image.*"},
             QT_TRANSLATE_NOOP("ops", "change what is on the pages (add, change and remove ink and text)")},
            {"pages",
             {"page.insert", "page.delete", "page.move", "page.size", "background.*", "layer.add", "layer.delete",
              "layer.rename", "layer.visible", "layer.select"},
             QT_TRANSLATE_NOOP("ops", "add, remove and change pages and layers")},
            {"tools", {"tool.select", "tool.color", "tool.width"}, QT_TRANSLATE_NOOP("ops", "change the tool in hand")},
            {"files", {"file.*"}, QT_TRANSLATE_NOOP("ops", "open and save files you choose")},
    };
    return all;
}
}  // namespace

bool Patterns::matches(const QString& op) const {
    return std::any_of(list.begin(), list.end(), [&op](const QString& p) {
        if (p == QLatin1String("*") || p == op) {
            return true;
        }
        return p.endsWith(QLatin1String(".*")) && op.startsWith(p.left(p.size() - 1));
    });
}

Patterns patternsOfClass(const QString& opClass) {
    for (const ClassInfo& c: classes()) {
        if (opClass == QLatin1String(c.name)) {
            return Patterns(c.patterns);
        }
    }
    return {};
}

QStringList grantableClasses() {
    QStringList out;
    for (const ClassInfo& c: classes()) {
        if (*c.asked) {
            out << QString::fromLatin1(c.name);
        }
    }
    return out;
}

QString describeClass(const QString& opClass) {
    for (const ClassInfo& c: classes()) {
        if (opClass == QLatin1String(c.name)) {
            return QCoreApplication::translate("ops", c.asked);
        }
    }
    return opClass;
}

bool isFreeClass(const QString& opClass) {
    return opClass == QLatin1String("read") || opClass == QLatin1String("view") || opClass == QLatin1String("ui");
}

QString Error::name() const {
    switch (kind) {
        case Kind::Denied:
            return QStringLiteral("PermissionError");
        case Kind::Invalid:
            return QStringLiteral("TypeError");
        case Kind::Stale:
            return QStringLiteral("StaleReferenceError");
        case Kind::ReadOnly:
            return QStringLiteral("ReadOnlyError");
        case Kind::Unknown:
            return QStringLiteral("ReferenceError");
        case Kind::Failed:
            break;
    }
    return QStringLiteral("Error");
}

// --- references ---------------------------------------------------------------------------------------------------

QString ElementRefs::add(const PageRef& page, Layer* layer, const Element* element) {
    for (const auto& [key, t]: refs) {
        if (t.element == element && t.layer == layer && t.page == page) {
            return key;
        }
    }
    const QString key = QStringLiteral("e%1").arg(next++);
    refs[key] = Target{page, layer, element, 0};
    return key;
}

ElementRefs::Target ElementRefs::resolve(const QString& ref, const DocumentSession& session) const {
    const auto it = refs.find(ref);
    if (it == refs.end()) {
        throw Error(Error::Kind::Stale, QStringLiteral("no element \"%1\"").arg(ref));
    }
    Target t = it->second;
    Document* doc = session.getDocument();
    t.pageIndex = doc->indexOf(t.page);
    if (t.pageIndex == npos) {
        throw Error(Error::Kind::Stale, QStringLiteral("the page of element \"%1\" is gone").arg(ref));
    }
    const auto layers = t.page->getLayersView();
    if (std::find(layers.begin(), layers.end(), t.layer) == layers.end() ||
        t.layer->indexOf(t.element) == Element::InvalidIndex) {
        throw Error(Error::Kind::Stale, QStringLiteral("element \"%1\" is gone").arg(ref));
    }
    return t;
}

// --- the context --------------------------------------------------------------------------------------------------

Context::Context(Operations& ops, Principal who, Authority& authority, DocumentSession* document):
        operations(ops), principal(std::move(who)), authority(authority), document(document) {}

Context::~Context() {
    if (transaction) {
        transaction->rollback();
    }
}

void Context::check(const QString& op, const QString& opClass) {
    switch (authority.decide(principal, op, opClass)) {
        case Decision::Allow:
            return;
        case Decision::Ask:
            if (authority.ask(principal, op, opClass)) {
                return;
            }
            throw Error(Error::Kind::Denied, QStringLiteral("%1 was not allowed to %2 (%3)")
                                                     .arg(principal.name, describeClass(opClass), op));
        case Decision::Deny:
            break;
    }
    throw Error(Error::Kind::Denied,
                QStringLiteral("%1 may not %2 (%3)").arg(principal.name, describeClass(opClass), op));
}

QVariant Context::apply(const QString& name, const QVariantMap& args) {
    const Operations::Info* info = operations.find(name);
    if (!info) {
        throw Error(Error::Kind::Unknown, QStringLiteral("no operation \"%1\"").arg(name));
    }
    check(info->name, info->opClass);
    if (!info->writes) {
        return info->handler(*this, args);
    }
    writable();  // (Error::ReadOnly before anything is done)
    if (inTransaction()) {
        return info->handler(*this, args);
    }
    // One of its own: one undo step, or nothing
    begin(name);
    try {
        QVariant result = info->handler(*this, args);
        commit();
        return result;
    } catch (...) {
        rollback();
        throw;
    }
}

void Context::begin(const QString& title) {
    if (transaction || !document) {
        return;
    }
    transaction = std::make_unique<UndoGathering>(*document, title.toStdString());
    if (!transaction->active()) {
        transaction.reset();
        throw Error(Error::Kind::Failed, QStringLiteral("another change is being gathered"));
    }
}

bool Context::inTransaction() const { return transaction != nullptr; }

bool Context::changed() const { return transaction && transaction->count() > 0; }

bool Context::commit() {
    if (!transaction) {
        return false;
    }
    const bool pushed = transaction->commit();
    transaction.reset();
    return pushed;
}

void Context::rollback() {
    if (!transaction) {
        return;
    }
    transaction->rollback();
    transaction.reset();
}

DocumentSession& Context::readable() const {
    if (!document) {
        throw Error(Error::Kind::ReadOnly, QStringLiteral("no document is open"));
    }
    return *document;
}

DocumentSession& Context::writable() const {
    DocumentSession& s = readable();
    if (s.isReadOnly() || s.isReplaying()) {
        throw Error(Error::Kind::ReadOnly, QStringLiteral("the document cannot be changed now (read only)"));
    }
    return s;
}

// --- the table ----------------------------------------------------------------------------------------------------

Operations::Operations() { addDocumentOperations(*this); }

void Operations::add(const QString& name, const QString& opClass, bool writes, Handler handler) {
    table[name] = Info{name, opClass, writes, std::move(handler)};
}

const Operations::Info* Operations::find(const QString& name) const {
    const auto it = table.find(name);
    return it == table.end() ? nullptr : &it->second;
}

QStringList Operations::names() const {
    QStringList out;
    for (const auto& [name, info]: table) {
        out << name;
    }
    return out;
}

// --- helpers ------------------------------------------------------------------------------------------------------

double number(const QVariantMap& args, const QString& key, std::optional<double> fallback) {
    const QVariant v = args.value(key);
    bool ok = false;
    const double d = v.toDouble(&ok);
    if (v.isValid() && !v.isNull() && ok && std::isfinite(d)) {
        return d;
    }
    if (fallback && (!v.isValid() || v.isNull())) {
        return *fallback;
    }
    throw Error(Error::Kind::Invalid, QStringLiteral("\"%1\" must be a number").arg(key));
}

int integer(const QVariantMap& args, const QString& key, std::optional<int> fallback) {
    const double d = number(args, key, fallback ? std::optional<double>(*fallback) : std::nullopt);
    if (d != std::floor(d)) {
        throw Error(Error::Kind::Invalid, QStringLiteral("\"%1\" must be a whole number").arg(key));
    }
    return static_cast<int>(d);
}

QString string(const QVariantMap& args, const QString& key, std::optional<QString> fallback) {
    const QVariant v = args.value(key);
    if (v.isValid() && !v.isNull()) {
        return v.toString();
    }
    if (fallback) {
        return *fallback;
    }
    throw Error(Error::Kind::Invalid, QStringLiteral("\"%1\" is missing").arg(key));
}

size_t pageIndex(const Context& context, const QVariantMap& args, const QString& key) {
    const DocumentSession& s = context.readable();
    const int p = integer(args, key, static_cast<int>(s.getCurrentPageNo()));
    std::shared_lock lock(*s.getDocument());
    if (p < 0 || static_cast<size_t>(p) >= s.getDocument()->getPageCount()) {
        throw Error(Error::Kind::Invalid, QStringLiteral("no page %1").arg(p));
    }
    return static_cast<size_t>(p);
}

}  // namespace xqt::ops
